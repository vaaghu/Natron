#include "ProgramOutput.h"

#include "NdiManager.h"
#include "PlayoutChannel.h"
#include "../scene/SceneStore.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QThread>

#include <cmath>

namespace
{
const int kTickMs = 15;           // transitions and frame hand-over to the mixer
const int kReopenDelayMs = 5000;  // the NDI source could not be opened: try again this often
const int kConnectionsMs = 1000;  // receivers are counted this often
const int kDefaultWidth = 1920;   // output size before any input had a frame
const int kDefaultHeight = 1080;
const double kDefaultFps = 30;

// Opaque black BGRA frame.
QByteArray blackFrame(int width, int height)
{
  QByteArray frame(width * height * 4, '\0');
  char *p = frame.data();
  for (int i = 3; i < frame.size(); i += 4)
  {
    p[i] = char(0xff);
  }
  return frame;
}

// Composites a frame with transparency over black: the output is opaque.
void flatten(QByteArray *frame)
{
  uchar *p = reinterpret_cast<uchar *>(frame->data());
  const int size = frame->size() - 3;
  for (int i = 0; i < size; i += 4)
  {
    const int alpha = p[i + 3];
    if (alpha != 255)
    {
      p[i] = uchar(p[i] * alpha / 255);
      p[i + 1] = uchar(p[i + 1] * alpha / 255);
      p[i + 2] = uchar(p[i + 2] * alpha / 255);
      p[i + 3] = 255;
    }
  }
}

// frame (width x height) fitted into outWidth x outHeight, keeping its
// aspect ratio, with black bars.
QByteArray fit(const QByteArray &frame, int width, int height, int outWidth, int outHeight)
{
  // BGRA bytes = ARGB32 on little-endian machines.
  const QImage source(reinterpret_cast<const uchar *>(frame.constData()), width, height, width * 4, QImage::Format_ARGB32);
  QImage out(outWidth, outHeight, QImage::Format_ARGB32);
  out.fill(0xff000000);
  {
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QSize size = source.size();
    size.scale(outWidth, outHeight, Qt::KeepAspectRatio); // QSize::scaled() is Qt 5 only
    painter.drawImage(QRect((outWidth - size.width()) / 2, (outHeight - size.height()) / 2, size.width(), size.height()), source);
  }
  return QByteArray(reinterpret_cast<const char *>(out.constBits()), outWidth * outHeight * 4);
}

// a = a * (1 - k) + b * k, k in 0..256 (same sizes).
void blend(QByteArray *a, const QByteArray &b, int k)
{
  uchar *p = reinterpret_cast<uchar *>(a->data());
  const uchar *q = reinterpret_cast<const uchar *>(b.constData());
  const int size = qMin(a->size(), b.size());
  for (int i = 0; i < size; ++i)
  {
    p[i] = uchar((p[i] * (256 - k) + q[i] * k) >> 8);
  }
}

// Colors towards black, k in 0..256 (256: black). Alpha is kept.
void darken(QByteArray *frame, int k)
{
  uchar *p = reinterpret_cast<uchar *>(frame->data());
  const int size = frame->size() - 3;
  for (int i = 0; i < size; i += 4)
  {
    p[i] = uchar((p[i] * (256 - k)) >> 8);
    p[i + 1] = uchar((p[i + 1] * (256 - k)) >> 8);
    p[i + 2] = uchar((p[i + 2] * (256 - k)) >> 8);
  }
}

QString trProgram(const char *text)
{
  return QCoreApplication::translate("ProgramOutput", text);
}
}

// Mixes the Program and Preview frames handed over by ProgramOutput and
// sends the result to the NDI source, at the Program input's frame rate.
class ProgramMixer : public QThread
{
public:
  struct Input
  {
    QByteArray frame; // BGRA
    int width;
    int height;
    bool alpha;       // composite over black

    Input()
        : width(0),
          height(0),
          alpha(false)
    {
    }

    bool isValid() const { return width > 0 && height > 0 && frame.size() >= width * height * 4; }
  };

  ProgramMixer(const QString &name, FrameSinkFactory sinkFactory)
      : m_name(name),
        m_sinkFactory(sinkFactory),
        m_mix(0),
        m_black(0),
        m_fps(kDefaultFps),
        m_quit(false),
        m_outputWidth(0),
        m_outputHeight(0),
        m_connections(-1),
        m_lastWidth(kDefaultWidth),
        m_lastHeight(kDefaultHeight)
  {
  }

  ~ProgramMixer()
  {
    {
      QMutexLocker locker(&m_mutex);
      m_quit = true;
    }
    wait();
  }

  // The frames of the next output frames (mix: 0 Program .. 1 Preview).
  void setInputs(const Input &program, const Input &preview, double mix, double black, double fps)
  {
    QMutexLocker locker(&m_mutex);
    m_program = program;
    m_preview = preview;
    m_mix = mix;
    m_black = black;
    m_fps = fps > 0 ? fps : kDefaultFps;
  }

  bool output(QByteArray *frame, int *width, int *height) const
  {
    QMutexLocker locker(&m_mutex);
    if (m_outputWidth <= 0 || m_outputHeight <= 0)
    {
      return false;
    }
    *frame = m_output;
    *width = m_outputWidth;
    *height = m_outputHeight;
    return true;
  }

  QString error() const
  {
    QMutexLocker locker(&m_mutex);
    return m_error;
  }

  int connections() const
  {
    QMutexLocker locker(&m_mutex);
    return m_connections;
  }

protected:
  void run()
  {
    FrameSink *sink = nullptr;
    QElapsedTimer reopen;  // since the last failed open
    QElapsedTimer connectionsTimer;
    QElapsedTimer clock;   // pacing when the sink does not clock itself
    qint64 nextFrameMs = 0;
    connectionsTimer.start();
    clock.start();

    for (;;)
    {
      Input program, preview;
      double mix, black, fps;
      {
        QMutexLocker locker(&m_mutex);
        if (m_quit)
        {
          break;
        }
        program = m_program;
        preview = m_preview;
        mix = m_mix;
        black = m_black;
        fps = m_fps;
      }

      // Open the output; if it cannot be opened (NDI runtime missing...),
      // keep mixing for the dashboard's monitor and try again later.
      if (!sink && (!reopen.isValid() || reopen.elapsed() > kReopenDelayMs))
      {
        QString error;
        sink = m_sinkFactory ? m_sinkFactory() : nullptr;
        if (sink && sink->open(m_name, false, &error))
        {
          QMutexLocker locker(&m_mutex);
          m_error.clear();
        }
        else
        {
          delete sink;
          sink = nullptr;
          reopen.start();
          QMutexLocker locker(&m_mutex);
          m_error = (error.isEmpty() ? trProgram("no output") : error) + trProgram(" (retrying)");
          m_connections = -1;
        }
      }

      int width, height;
      const QByteArray frame = compose(program, preview, mix, black, &width, &height);
      {
        QMutexLocker locker(&m_mutex);
        m_output = frame;
        m_outputWidth = width;
        m_outputHeight = height;
      }

      if (sink)
      {
        sink->sendFrame(frame, width, height, qRound(fps * 1000), 1000);
        if (connectionsTimer.elapsed() > kConnectionsMs)
        {
          connectionsTimer.restart();
          const int connections = sink->connectionCount();
          QMutexLocker locker(&m_mutex);
          m_connections = connections;
        }
      }

      // Pace (NDI senders clock themselves).
      if (!sink || !sink->isClocked())
      {
        nextFrameMs += qint64(1000.0 / fps);
        const qint64 wait = nextFrameMs - clock.elapsed();
        if (wait > 0)
        {
          msleep(wait);
        }
        else if (wait < -1000)
        {
          nextFrameMs = clock.elapsed(); // far behind: do not catch up
        }
      }
    }

    delete sink;
  }

private:
  // An input at the output size, opaque; black if it has no frame.
  QByteArray prepare(const Input &input, int width, int height)
  {
    if (!input.isValid())
    {
      if (m_blackFrame.size() != width * height * 4)
      {
        m_blackFrame = blackFrame(width, height);
      }
      return m_blackFrame;
    }
    QByteArray frame = input.frame;
    if (input.alpha)
    {
      flatten(&frame);
    }
    if (input.width != width || input.height != height)
    {
      frame = fit(frame, input.width, input.height, width, height);
    }
    return frame;
  }

  QByteArray compose(const Input &program, const Input &preview, double mix, double black, int *width, int *height)
  {
    // Output size: the Program input's, else the Preview input's while it
    // shows, else the last one.
    if (program.isValid())
    {
      m_lastWidth = program.width;
      m_lastHeight = program.height;
    }
    else if (preview.isValid() && mix > 0)
    {
      m_lastWidth = preview.width;
      m_lastHeight = preview.height;
    }
    *width = m_lastWidth;
    *height = m_lastHeight;

    QByteArray out = prepare(program, m_lastWidth, m_lastHeight);
    if (mix >= 1)
    {
      out = prepare(preview, m_lastWidth, m_lastHeight);
    }
    else if (mix > 0)
    {
      blend(&out, prepare(preview, m_lastWidth, m_lastHeight), int(mix * 256 + 0.5));
    }
    if (black > 0)
    {
      darken(&out, int(black * 256 + 0.5));
    }
    return out;
  }

  const QString m_name;
  const FrameSinkFactory m_sinkFactory;

  mutable QMutex m_mutex;
  // shared state (m_mutex)
  Input m_program;
  Input m_preview;
  double m_mix;
  double m_black;
  double m_fps;
  bool m_quit;
  QByteArray m_output;
  int m_outputWidth;
  int m_outputHeight;
  QString m_error;
  int m_connections;

  // mixer thread
  int m_lastWidth;
  int m_lastHeight;
  QByteArray m_blackFrame;
};

namespace
{
// The frame an input's channel sends now (invalid without a channel or frame).
ProgramMixer::Input inputOf(NdiManager *ndi, const ProgramOutput::Source &source, double *fps)
{
  ProgramMixer::Input input;
  PlayoutChannel *channel = source.isEmpty() ? nullptr : ndi->channel(source.sceneId, source.item);
  if (channel && channel->currentFrame(&input.frame, &input.width, &input.height))
  {
    input.alpha = channel->alpha();
    if (fps && *fps <= 0)
    {
      *fps = channel->status().fps;
    }
  }
  return input;
}
}

ProgramOutput::ProgramOutput(NdiManager *ndi,
                             SceneStore *scenes,
                             const QString &sourceName,
                             FrameSinkFactory sinkFactory,
                             QObject *parent)
    : QObject(parent),
      m_ndi(ndi),
      m_scenes(scenes),
      m_name(sourceName),
      m_mixer(new ProgramMixer(sourceName, sinkFactory)),
      m_position(0),
      m_fadeMs(0),
      m_fadeFrom(0),
      m_blackTarget(false),
      m_black(0),
      m_blackMs(0),
      m_blackFrom(0)
{
  connect(m_scenes, SIGNAL(scenesChanged()), this, SLOT(onScenesChanged()));
  connect(&m_tick, SIGNAL(timeout()), this, SLOT(onTick()));
  m_tick.setInterval(kTickMs);
  m_tick.start();
  m_mixer->start();
}

ProgramOutput::~ProgramOutput()
{
  delete m_mixer; // stops its thread
}

QString ProgramOutput::sourceName() const
{
  return m_name;
}

ProgramOutput::Source ProgramOutput::preview() const
{
  return m_preview;
}

ProgramOutput::Source ProgramOutput::program() const
{
  return m_program;
}

void ProgramOutput::setPreview(const Source &source)
{
  if (source == m_preview)
  {
    return;
  }
  m_preview = source;
  Q_EMIT busChanged();
}

void ProgramOutput::cutTo(const Source &source)
{
  if (source.isEmpty() || source == m_program)
  {
    return;
  }
  if (source == m_preview)
  {
    cut();
    return;
  }
  stopTransition();
  startPlaying(source);
  m_program = source;
  Q_EMIT busChanged();
}

void ProgramOutput::cut()
{
  if (m_preview.isEmpty())
  {
    return;
  }
  stopTransition();
  startPlaying(m_preview);
  swap();
}

void ProgramOutput::fade(int durationMs)
{
  if (m_preview.isEmpty() || m_fadeMs > 0)
  {
    return;
  }
  if (durationMs <= 0)
  {
    cut();
    return;
  }
  startPlaying(m_preview);
  // From where the T-bar is, in the time left.
  m_fadeFrom = m_position;
  m_fadeMs = qMax(1, int(durationMs * (1.0 - m_fadeFrom)));
  m_fadeClock.start();
  Q_EMIT transitionChanged();
}

void ProgramOutput::setManualPosition(double position)
{
  position = qBound(0.0, position, 1.0);
  if (m_preview.isEmpty())
  {
    return;
  }
  m_fadeMs = 0; // the T-bar takes over
  if (m_position <= 0 && position > 0)
  {
    startPlaying(m_preview);
  }
  if (position >= 1)
  {
    m_position = 0;
    swap();
  }
  else
  {
    m_position = position;
  }
  Q_EMIT transitionChanged();
}

bool ProgramOutput::inTransition() const
{
  return m_position > 0;
}

bool ProgramOutput::inAutoTransition() const
{
  return m_fadeMs > 0;
}

double ProgramOutput::transitionPosition() const
{
  return m_position;
}

void ProgramOutput::fadeToBlack(int durationMs)
{
  m_blackTarget = !m_blackTarget;
  const double target = m_blackTarget ? 1.0 : 0.0;
  if (durationMs <= 0)
  {
    m_blackMs = 0;
    m_black = target;
  }
  else
  {
    m_blackFrom = m_black;
    m_blackMs = qMax(1, int(durationMs * std::fabs(target - m_black)));
    m_blackClock.start();
  }
  Q_EMIT transitionChanged();
}

bool ProgramOutput::isBlack() const
{
  return m_blackTarget;
}

double ProgramOutput::blackLevel() const
{
  return m_black;
}

QImage ProgramOutput::outputPreview(int maxWidth) const
{
  QByteArray frame;
  int width, height;
  if (!m_mixer->output(&frame, &width, &height))
  {
    return QImage();
  }
  const QImage image(reinterpret_cast<const uchar *>(frame.constData()), width, height, width * 4, QImage::Format_ARGB32);
  return (width > maxWidth) ? image.scaledToWidth(maxWidth, Qt::FastTransformation) : image.copy();
}

QString ProgramOutput::outputError() const
{
  return m_mixer->error();
}

int ProgramOutput::connections() const
{
  return m_mixer->connections();
}

void ProgramOutput::startPlaying(const Source &source)
{
  PlayoutChannel *channel = source.isEmpty() ? nullptr : m_ndi->channel(source.sceneId, source.item);
  if (!channel)
  {
    return;
  }
  // Stopped, or holding its last frame: play (from the start).
  const PlayoutChannel::Status status = channel->status();
  const bool atEnd = status.state == PlayoutChannel::ePaused && status.frameCount > 0 && status.position >= status.frameCount;
  if (status.state == PlayoutChannel::eStopped || atEnd)
  {
    channel->play();
  }
}

void ProgramOutput::stopTransition()
{
  const bool was = m_position > 0 || m_fadeMs > 0;
  m_position = 0;
  m_fadeMs = 0;
  if (was)
  {
    Q_EMIT transitionChanged();
  }
}

void ProgramOutput::swap()
{
  const Source program = m_program;
  m_program = m_preview;
  m_preview = program;
  Q_EMIT busChanged();
}

void ProgramOutput::onTick()
{
  bool changed = false;

  if (m_fadeMs > 0)
  {
    const double t = qMin(1.0, double(m_fadeClock.elapsed()) / m_fadeMs);
    m_position = m_fadeFrom + (1.0 - m_fadeFrom) * t;
    changed = true;
    if (t >= 1.0)
    {
      // Done: Preview is on air (the frames below already follow the swap).
      m_fadeMs = 0;
      m_position = 0;
      swap();
    }
  }
  if (m_blackMs > 0)
  {
    const double target = m_blackTarget ? 1.0 : 0.0;
    const double t = qMin(1.0, double(m_blackClock.elapsed()) / m_blackMs);
    m_black = m_blackFrom + (target - m_blackFrom) * t;
    changed = true;
    if (t >= 1.0)
    {
      m_blackMs = 0;
      m_black = target;
    }
  }

  double fps = 0;
  const ProgramMixer::Input program = inputOf(m_ndi, m_program, &fps);
  const ProgramMixer::Input preview = (m_position > 0) ? inputOf(m_ndi, m_preview, &fps) : ProgramMixer::Input();
  m_mixer->setInputs(program, preview, m_position, m_black, fps);

  if (changed)
  {
    Q_EMIT transitionChanged();
  }
}

void ProgramOutput::onScenesChanged()
{
  // Inputs whose scene or Write node was removed are cleared.
  Source *sources[] = { &m_preview, &m_program };
  bool changed = false;
  for (int i = 0; i < 2; ++i)
  {
    Scene scene;
    if (!sources[i]->isEmpty() && (!m_scenes->scene(sources[i]->sceneId, &scene) || !scene.items.contains(sources[i]->item)))
    {
      *sources[i] = Source();
      changed = true;
    }
  }
  if (changed)
  {
    stopTransition();
    Q_EMIT busChanged();
  }
}
