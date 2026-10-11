#include "PlayoutChannel.h"

#include "../util/BundledTool.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
const int kIdleSleepMs = 5;
// The output (NDI source) could not be opened: try again this often.
const int kReopenDelayMs = 5000;
const int kDecoderReadTimeoutMs = 5000;
const int kLiveQueueMax = 8; // drop old live frames if the output lags
}

PlayoutChannel::PlayoutChannel(const QString &sourceName,
                               bool alpha,
                               const QString &ffmpegPath,
                               FrameSinkFactory sinkFactory,
                               QObject *parent)
    : QThread(parent),
      m_name(sourceName),
      m_alpha(alpha),
      m_ffmpegPath(ffmpegPath),
      m_sinkFactory(sinkFactory),
      m_mediaChanged(false),
      m_state(eStopped),
      m_position(0),
      m_seekTo(-1),
      m_loop(false),
      m_pauseArmed(false),
      m_pauseFrame(-1),
      m_cueMode(false),
      m_clearRequested(false),
      m_quit(false),
      m_liveWidth(0),
      m_liveHeight(0),
      m_liveFps(25),
      m_connections(-1),
      m_sink(nullptr),
      m_decoder(nullptr),
      m_decoderFrame(-1),
      m_lastWidth(0),
      m_lastHeight(0),
      m_previewWidth(0),
      m_previewHeight(0)
{
}

PlayoutChannel::~PlayoutChannel()
{
  {
    QMutexLocker locker(&m_mutex);
    m_quit = true;
  }
  wait();
}

QString PlayoutChannel::sourceName() const
{
  return m_name;
}

bool PlayoutChannel::alpha() const
{
  return m_alpha;
}

QString PlayoutChannel::stateName(State state)
{
  switch (state)
  {
  case eStopped:
    return tr("Stopped");
  case ePlaying:
    return tr("Playing");
  case ePaused:
    return tr("Paused");
  case eLive:
    return tr("Live");
  case eError:
    return tr("Error");
  }
  return QString();
}

int PlayoutChannel::nextPauseFrameLocked(int frame) const
{
  for (int i = 0; i < m_pausePoints.size(); ++i)
  {
    const int pauseFrame = int(std::floor(m_pausePoints.at(i) * m_media.fps() + 0.5));
    if (pauseFrame > frame)
    {
      return pauseFrame;
    }
  }
  return -1;
}

void PlayoutChannel::setStateLocked(State state, const QString &message)
{
  m_state = state;
  m_message = message;
}

PlayoutChannel::Status PlayoutChannel::status() const
{
  QMutexLocker locker(&m_mutex);
  Status s;
  s.state = m_state;
  s.position = m_position;
  s.frameCount = (m_state == eLive) ? m_position : m_media.frameCount;
  s.fps = (m_state == eLive) ? m_liveFps : m_media.fps();
  s.pauseAt = m_pausePoints.isEmpty() ? -1.0 : m_pausePoints.first();
  s.pausePoints = m_pausePoints;
  s.loop = m_loop;
  s.connections = m_connections;
  // An output that cannot open does not stop the channel (its frames still
  // feed the switcher): it is only reported.
  s.message = m_outputError.isEmpty() ? m_message
              : m_message.isEmpty() ? m_outputError : m_outputError + QString::fromUtf8("; ") + m_message;
  return s;
}

void PlayoutChannel::setMedia(const MediaInfo &media)
{
  {
    QMutexLocker locker(&m_mutex);
    m_media = media;
    m_mediaChanged = true;
    if (m_state == eLive)
    {
      // keep sending live; the media is used once live ends
    }
    else if (!media.ok)
    {
      setStateLocked(m_state == eError ? eStopped : m_state, media.error);
      if (m_state != eStopped)
      {
        m_state = eStopped;
        m_clearRequested = true;
      }
      m_position = 0;
    }
    else
    {
      m_message.clear();
      // Same position in the new media (e.g. a re-render with new values
      // continues where the previous one was).
      m_position = qMin(m_position, qMax(0, media.frameCount - 1));
      m_seekTo = m_position;
    }
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::play()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive || !m_media.ok)
    {
      return;
    }
    if (m_position >= m_media.frameCount)
    {
      m_seekTo = 0;
      m_position = 0;
    }
    // Continuing past a pause point: stop at the next one.
    m_pauseFrame = nextPauseFrameLocked(m_position);
    m_pauseArmed = m_pauseFrame >= 0;
    setStateLocked(ePlaying);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::pause()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state != ePlaying)
    {
      return;
    }
    setStateLocked(ePaused);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::stop()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive)
    {
      return;
    }
    m_position = 0;
    m_seekTo = 0;
    m_pauseArmed = false;
    m_clearRequested = true;
    setStateLocked(eStopped, m_media.ok ? QString() : m_media.error);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::replay()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive || !m_media.ok)
    {
      return;
    }
    m_position = 0;
    m_seekTo = 0;
    m_pauseArmed = false; // replay plays straight through (also when looping)
    m_cueMode = false;
    setStateLocked(ePlaying);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::cue()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive || !m_media.ok)
    {
      return;
    }
    m_position = 0;
    m_seekTo = 0;
    m_pauseFrame = nextPauseFrameLocked(0);
    m_pauseArmed = m_pauseFrame >= 0;
    m_cueMode = true;
    setStateLocked(ePlaying);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::setLoop(bool loop)
{
  {
    QMutexLocker locker(&m_mutex);
    m_loop = loop;
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::setPauseAt(double seconds)
{
  setPausePoints(seconds < 0 ? QList<double>() : (QList<double>() << seconds));
}

void PlayoutChannel::setPausePoints(const QList<double> &seconds)
{
  {
    QMutexLocker locker(&m_mutex);
    m_pausePoints.clear();
    for (int i = 0; i < seconds.size(); ++i)
    {
      if (seconds.at(i) >= 0)
      {
        m_pausePoints << seconds.at(i);
      }
    }
    std::sort(m_pausePoints.begin(), m_pausePoints.end());
    m_pauseFrame = nextPauseFrameLocked(m_position);
    m_pauseArmed = (m_state == ePlaying) && m_pauseFrame >= 0;
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::seek(double seconds)
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive || !m_media.ok || m_media.frameCount <= 0)
    {
      return;
    }
    const int frame = qBound(0, int(std::floor(seconds * m_media.fps() + 0.5)), m_media.frameCount - 1);
    m_position = frame;
    m_seekTo = frame;
    if (m_pauseArmed)
    {
      m_pauseFrame = nextPauseFrameLocked(frame);
      m_pauseArmed = m_pauseFrame >= 0;
    }
    if (m_state != ePlaying)
    {
      setStateLocked(ePaused); // holding the frame sought to
    }
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::setMessage(const QString &message)
{
  {
    QMutexLocker locker(&m_mutex);
    m_message = message;
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::enterLive(int width, int height, double fps)
{
  {
    QMutexLocker locker(&m_mutex);
    m_liveFrames.clear();
    m_liveWidth = width;
    m_liveHeight = height;
    m_liveFps = fps > 0 ? fps : 25;
    m_position = 0;
    setStateLocked(eLive);
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::pushLiveFrame(const QByteArray &bgra, int width, int height)
{
  QMutexLocker locker(&m_mutex);
  if (m_state != eLive)
  {
    return;
  }
  m_liveWidth = width;
  m_liveHeight = height;
  m_liveFrames.append(bgra);
  while (m_liveFrames.size() > kLiveQueueMax)
  {
    m_liveFrames.removeFirst();
  }
}

void PlayoutChannel::leaveLive()
{
  {
    QMutexLocker locker(&m_mutex);
    if (m_state != eLive)
    {
      return;
    }
    // Hold the last live frame; Play/Replay then play the finished render.
    setStateLocked(ePaused);
    m_position = m_media.ok ? qMax(0, m_media.frameCount - 1) : 0;
    m_seekTo = m_position;
  }
  Q_EMIT statusChanged();
}

// ---------------- channel thread ----------------

void PlayoutChannel::sendFrame(const QByteArray &frame, int width, int height)
{
  int num = 25, den = 1;
  {
    QMutexLocker locker(&m_mutex);
    if (m_state == eLive)
    {
      fpsToFraction(m_liveFps, &num, &den);
    }
    else if (m_media.ok)
    {
      num = m_media.fpsNum;
      den = m_media.fpsDen;
    }
  }
  if (m_sink)
  {
    m_sink->sendFrame(frame, width, height, num, den);
  }
  m_lastFrame = frame;
  m_lastWidth = width;
  m_lastHeight = height;
  {
    QMutexLocker locker(&m_mutex);
    m_previewFrame = frame;
    m_previewWidth = width;
    m_previewHeight = height;
  }
}

QImage PlayoutChannel::preview(int maxWidth) const
{
  QByteArray frame;
  int width, height;
  {
    QMutexLocker locker(&m_mutex);
    frame = m_previewFrame;
    width = m_previewWidth;
    height = m_previewHeight;
  }
  if (width <= 0 || height <= 0 || frame.size() < width * height * 4)
  {
    return QImage();
  }
  // BGRA bytes = ARGB32 on little-endian machines.
  const QImage image(reinterpret_cast<const uchar *>(frame.constData()), width, height, width * 4, QImage::Format_ARGB32);
  return (width > maxWidth) ? image.scaledToWidth(maxWidth, Qt::FastTransformation) : image.copy();
}

void PlayoutChannel::openSink()
{
  QString error;
  m_sink = m_sinkFactory ? m_sinkFactory() : nullptr;
  const bool opened = m_sink && m_sink->open(m_name, m_alpha, &error);
  if (!opened)
  {
    delete m_sink;
    m_sink = nullptr;
  }
  {
    QMutexLocker locker(&m_mutex);
    if (opened)
    {
      m_outputError.clear();
    }
    else
    {
      m_outputError = tr("NDI output not available: %1 (retrying)").arg(error.isEmpty() ? tr("no output") : error);
    }
  }
  Q_EMIT statusChanged();
}

void PlayoutChannel::showFrame(int frame)
{
  int width, height;
  {
    QMutexLocker locker(&m_mutex);
    width = m_media.width;
    height = m_media.height;
  }
  QByteArray data;
  if (startDecoder(frame) && readFrame(&data))
  {
    sendFrame(data, width, height);
  }
  stopDecoder(); // playing restarts it at the position
}

bool PlayoutChannel::currentFrame(QByteArray *bgra, int *width, int *height) const
{
  QMutexLocker locker(&m_mutex);
  if (m_previewWidth <= 0 || m_previewHeight <= 0 || m_previewFrame.size() < m_previewWidth * m_previewHeight * 4)
  {
    return false;
  }
  *bgra = m_previewFrame;
  *width = m_previewWidth;
  *height = m_previewHeight;
  return true;
}

void PlayoutChannel::sendClearFrame()
{
  // Transparent (alpha) or black: receivers stop showing the last frame.
  if (m_lastWidth > 0 && m_lastHeight > 0)
  {
    QByteArray clear(m_lastWidth * m_lastHeight * 4, '\0');
    if (!m_alpha)
    {
      for (int i = 3; i < clear.size(); i += 4)
      {
        clear[i] = char(0xff);
      }
    }
    sendFrame(clear, m_lastWidth, m_lastHeight);
  }
}

bool PlayoutChannel::startDecoder(int frame)
{
  stopDecoder();

  MediaInfo media;
  {
    QMutexLocker locker(&m_mutex);
    media = m_media;
  }
  if (!media.ok)
  {
    return false;
  }

  QStringList args;
  args << QString::fromUtf8("-v") << QString::fromUtf8("error") << QString::fromUtf8("-nostdin");
  if (media.sequence)
  {
    // Exact frame: start the sequence at the wanted file.
    args << QString::fromUtf8("-framerate") << QString::fromUtf8("%1/%2").arg(media.fpsNum).arg(media.fpsDen)
         << QString::fromUtf8("-start_number") << QString::number(media.startNumber + frame);
  }
  else if (frame > 0)
  {
    args << QString::fromUtf8("-ss") << QString::number(frame / media.fps(), 'f', 6);
  }
  args << QString::fromUtf8("-i") << media.input
       << QString::fromUtf8("-f") << QString::fromUtf8("rawvideo")
       << QString::fromUtf8("-pix_fmt") << QString::fromUtf8("bgra")
       << QString::fromUtf8("-s") << QString::fromUtf8("%1x%2").arg(media.width).arg(media.height)
       << QString::fromUtf8("-");

  m_decoder = new QProcess;
  m_decoder->setReadChannel(QProcess::StandardOutput);
  prepareBundledTool(m_decoder, m_ffmpegPath);
  m_decoder->start(m_ffmpegPath, args, QIODevice::ReadOnly);
  if (!m_decoder->waitForStarted(5000))
  {
    QMutexLocker locker(&m_mutex);
    setStateLocked(eError, tr("cannot start ffmpeg (%1)").arg(m_ffmpegPath));
    delete m_decoder;
    m_decoder = nullptr;
    return false;
  }
  m_decoderFrame = frame;
  return true;
}

void PlayoutChannel::stopDecoder()
{
  if (m_decoder)
  {
    m_decoder->kill();
    m_decoder->waitForFinished(2000);
    delete m_decoder;
    m_decoder = nullptr;
  }
  m_decoderFrame = -1;
}

bool PlayoutChannel::readFrame(QByteArray *frame)
{
  int bytes;
  {
    QMutexLocker locker(&m_mutex);
    bytes = m_media.width * m_media.height * 4;
  }
  if (!m_decoder || bytes <= 0)
  {
    return false;
  }

  frame->clear();
  frame->reserve(bytes);
  while (frame->size() < bytes)
  {
    if (m_decoder->bytesAvailable() == 0 &&
        !m_decoder->waitForReadyRead(kDecoderReadTimeoutMs))
    {
      return false; // end of media (or decoder error)
    }
    frame->append(m_decoder->read(bytes - frame->size()));
  }
  ++m_decoderFrame;
  return true;
}

void PlayoutChannel::run()
{
  // The output (NDI source) is opened here and, if it cannot be (NDI runtime
  // missing, network down...), again every few seconds. Meanwhile the
  // channel plays as usual: its frames still feed the switcher.
  QElapsedTimer reopen;
  openSink();
  reopen.start();

  QElapsedTimer clock;   // pacing when the sink does not clock itself
  qint64 nextFrameMs = 0;
  QElapsedTimer connectionsTimer;
  connectionsTimer.start();

  for (;;)
  {
    State state;
    int seekTo;
    bool clear;
    bool mediaChanged;
    QList<QByteArray> live;
    int liveWidth, liveHeight;
    double fps;
    {
      QMutexLocker locker(&m_mutex);
      if (m_quit)
      {
        break;
      }
      state = m_state;
      seekTo = m_seekTo;
      m_seekTo = -1;
      clear = m_clearRequested;
      m_clearRequested = false;
      mediaChanged = m_mediaChanged;
      m_mediaChanged = false;
      live = m_liveFrames;
      m_liveFrames.clear();
      liveWidth = m_liveWidth;
      liveHeight = m_liveHeight;
      fps = m_media.fps();
    }

    if (!m_sink && reopen.elapsed() > kReopenDelayMs)
    {
      openSink();
      reopen.restart();
    }

    if (connectionsTimer.elapsed() > 1000)
    {
      connectionsTimer.restart();
      const int connections = m_sink ? m_sink->connectionCount() : -1;
      bool changed;
      {
        QMutexLocker locker(&m_mutex);
        changed = connections != m_connections;
        m_connections = connections;
      }
      if (changed)
      {
        Q_EMIT statusChanged();
      }
    }

    if (mediaChanged)
    {
      stopDecoder();
    }
    if (clear)
    {
      stopDecoder();
      sendClearFrame();
    }

    // Live frames (also the last ones queued just before leaveLive()).
    for (int i = 0; i < live.size(); ++i)
    {
      sendFrame(live.at(i), liveWidth, liveHeight);
      {
        QMutexLocker locker(&m_mutex);
        if (m_state == eLive)
        {
          ++m_position;
        }
      }
      Q_EMIT statusChanged();
    }
    if (state == eLive)
    {
      if (live.isEmpty())
      {
        msleep(kIdleSleepMs);
      }
      continue;
    }

    // Paused on a frame sought to (scrubbing): show it.
    if (state == ePaused && seekTo >= 0)
    {
      showFrame(seekTo);
      Q_EMIT statusChanged();
      continue;
    }

    if (state != ePlaying)
    {
      msleep(kIdleSleepMs);
      clock.invalidate();
      continue;
    }

    // ---- playing ----
    int position;
    {
      QMutexLocker locker(&m_mutex);
      position = m_position;
    }
    if (seekTo >= 0 || !m_decoder || m_decoderFrame != position)
    {
      if (!startDecoder(seekTo >= 0 ? seekTo : position))
      {
        QMutexLocker locker(&m_mutex);
        if (m_state != eError)
        {
          setStateLocked(eStopped, m_media.error);
        }
        continue;
      }
      clock.invalidate();
    }

    QByteArray frame;
    int width, height;
    {
      QMutexLocker locker(&m_mutex);
      width = m_media.width;
      height = m_media.height;
    }

    if (!readFrame(&frame))
    {
      // End of the media.
      stopDecoder();
      bool loop;
      {
        QMutexLocker locker(&m_mutex);
        loop = m_loop && m_state == ePlaying;
        if (loop)
        {
          m_position = 0;
          m_seekTo = 0;
          // Cue'd playback stops at the pause points on every pass.
          m_pauseFrame = nextPauseFrameLocked(0);
          m_pauseArmed = m_cueMode && m_pauseFrame >= 0;
        }
        else if (m_state == ePlaying)
        {
          m_position = m_media.frameCount; // at the end, holding the last frame
          setStateLocked(ePaused, tr("End"));
        }
      }
      Q_EMIT statusChanged();
      continue;
    }

    // Pace (NDI senders clock themselves).
    if ((!m_sink || !m_sink->isClocked()) && fps > 0)
    {
      if (!clock.isValid())
      {
        clock.start();
        nextFrameMs = 0;
      }
      const qint64 wait = nextFrameMs - clock.elapsed();
      if (wait > 0)
      {
        msleep(wait);
      }
      nextFrameMs += qint64(1000.0 / fps);
    }

    // Paused/stopped while decoding: do not send this frame.
    bool stillPlaying;
    {
      QMutexLocker locker(&m_mutex);
      stillPlaying = (m_state == ePlaying) && (m_seekTo < 0) && (m_position == position) && !m_mediaChanged;
    }
    if (!stillPlaying)
    {
      // The decoder is one frame ahead: re-seek when playing resumes.
      stopDecoder();
      continue;
    }

    sendFrame(frame, width, height);

    {
      QMutexLocker locker(&m_mutex);
      ++m_position;
      if (m_pauseArmed && m_pauseFrame >= 0 && m_position >= m_pauseFrame)
      {
        m_pauseArmed = false;
        setStateLocked(ePaused, tr("Pause point"));
      }
    }
    Q_EMIT statusChanged();
  }

  stopDecoder();
  delete m_sink;
  m_sink = nullptr;
}
