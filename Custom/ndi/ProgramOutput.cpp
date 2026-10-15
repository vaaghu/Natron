#include "ProgramOutput.h"

#include "NdiManager.h"
#include "PlayoutChannel.h"
#include "../scene/SceneStore.h"
#include "../sdi/SdiOutput.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace
{
const int kTickMs = 15;              // transitions and frame hand-over to the mixer
const int kReopenDelayMs = 5000;     // the NDI source or SDI port could not be opened: try again this often
const int kConnectionsMs = 1000;     // receivers are counted this often
const int kPreviewMonitorWidth = 960; // Preview is composited this small when not on air
const int kDefaultWidth = 1920;
const int kDefaultHeight = 1080;
const double kDefaultFps = 30;
// Bus clocks: how far a playing layer may be from the clock and still lead
// it, how far it may drift before being sought back, and how long after a
// seek it is left alone (its decoder starting).
const double kFollowWindow = 1.0;
const double kResyncSeconds = 0.5;
const qint64 kResyncGraceMs = 1000;

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

// Frame rate as a fraction (29.97 -> 30000/1001).
void fpsFraction(double fps, int *num, int *den)
{
  const double ntsc = fps * 1.001;
  if (std::fabs(ntsc - qRound(ntsc)) < 0.01 && std::fabs(fps - qRound(fps)) > 0.01)
  {
    *num = qRound(ntsc) * 1000;
    *den = 1001;
  }
  else
  {
    *num = qRound(fps * 1000);
    *den = 1000;
  }
}

QString trProgram(const char *text)
{
  return QCoreApplication::translate("ProgramOutput", text);
}
}

// Composites the layers of Program and Preview handed over by
// ProgramOutput, mixes them, and sends the result to the NDI source at the
// output frame rate (unless NDI is turned off), and to the SDI port if one
// is chosen. One clock paces the frames: the SDI card's if it plays on its
// clock (NDI then sends without waiting), else NDI's, else a timer.
class ProgramMixer : public QThread
{
public:
  // A layer's frame (BGRA, with its transparency).
  struct Layer
  {
    QByteArray frame;
    int width;
    int height;
    LayerPlacement placement;

    Layer()
        : width(0),
          height(0)
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
        m_width(kDefaultWidth),
        m_height(kDefaultHeight),
        m_quit(false),
        m_outputWidth(0),
        m_outputHeight(0),
        m_previewWidth(0),
        m_previewHeight(0),
        m_connections(-1),
        m_ndiEnabled(true),
        m_sdiMode(0),
        m_sdiCardClock(false),
        m_sdiConfig(0)
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

  void setFormat(int width, int height, double fps)
  {
    QMutexLocker locker(&m_mutex);
    m_width = width;
    m_height = height;
    m_fps = fps > 0 ? fps : kDefaultFps;
  }

  // The frames of the next output frames (mix: 0 Program .. 1 Preview).
  void setInputs(const QList<Layer> &program, const QList<Layer> &preview, double mix, double black)
  {
    QMutexLocker locker(&m_mutex);
    m_program = program;
    m_preview = preview;
    m_mix = mix;
    m_black = black;
  }

  bool output(bool preview, QByteArray *frame, int *width, int *height) const
  {
    QMutexLocker locker(&m_mutex);
    *frame = preview ? m_previewOutput : m_output;
    *width = preview ? m_previewWidth : m_outputWidth;
    *height = preview ? m_previewHeight : m_outputHeight;
    return *width > 0 && *height > 0;
  }

  // Off: no NDI source (it is closed, receivers lose it).
  void setNdiEnabled(bool enabled)
  {
    QMutexLocker locker(&m_mutex);
    m_ndiEnabled = enabled;
  }

  // SDI port (empty: none), mode (0: the output format's) and clock.
  void setSdi(const QString &port, quint32 mode, bool cardClock)
  {
    QMutexLocker locker(&m_mutex);
    m_sdiPort = port;
    m_sdiMode = mode;
    m_sdiCardClock = cardClock;
    ++m_sdiConfig;
    m_sdiError.clear();
    m_sdiModeName.clear();
  }

  void sdiStatus(QString *modeName, QString *error) const
  {
    QMutexLocker locker(&m_mutex);
    *modeName = m_sdiModeName;
    *error = m_sdiError;
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
    Sdi::Output sdi;
    int sdiConfig = -1;    // m_sdiConfig sdi was opened (or tried) with
    int sdiWidth = 0, sdiHeight = 0;
    double sdiFps = 0;     // format it was opened for
    QElapsedTimer sdiReopen;
    QElapsedTimer connectionsTimer;
    QElapsedTimer clock;   // pacing when the sink does not clock itself
    qint64 nextFrameMs = 0;
    connectionsTimer.start();
    clock.start();

    for (;;)
    {
      QList<Layer> program, preview;
      double mix, black, fps;
      int width, height;
      bool ndiEnabled;
      QString sdiPort;
      quint32 sdiMode;
      bool sdiCardClock;
      int config;
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
        width = m_width;
        height = m_height;
        ndiEnabled = m_ndiEnabled;
        sdiPort = m_sdiPort;
        sdiMode = m_sdiMode;
        sdiCardClock = m_sdiCardClock;
        config = m_sdiConfig;
      }

      // SDI: (re)open on a new choice, or a new format if its mode follows
      // the format; a port that cannot be opened is tried again later.
      const bool sdiFormatChanged = sdiMode == 0 && (width != sdiWidth || height != sdiHeight || fps != sdiFps);
      if (config != sdiConfig || (sdi.isOpen() && sdiFormatChanged) ||
          (!sdiPort.isEmpty() && !sdi.isOpen() && sdiReopen.isValid() && sdiReopen.elapsed() > kReopenDelayMs))
      {
        sdiConfig = config;
        sdiWidth = width;
        sdiHeight = height;
        sdiFps = fps;
        sdi.close();
        QString error;
        if (!sdiPort.isEmpty() && !sdi.open(sdiPort, sdiMode, width, height, fps, sdiCardClock, &error))
        {
          sdiReopen.start();
          error += trProgram(" (retrying)");
        }
        QMutexLocker locker(&m_mutex);
        if (m_sdiConfig == config)
        {
          m_sdiError = error;
          m_sdiModeName = sdi.modeName();
        }
      }

      // Open the output; if it cannot be opened (NDI runtime missing...),
      // keep mixing for the dashboard's monitors and try again later.
      if (!ndiEnabled)
      {
        delete sink;
        sink = nullptr;
        reopen.invalidate(); // turned on again: at once
        QMutexLocker locker(&m_mutex);
        m_error.clear();
        m_connections = -1;
      }
      // NDI keeps its own time unless the SDI card paces the frames.
      const bool ndiClocked = !sdi.isClocked();
      if (sink && sink->isClocked() != ndiClocked)
      {
        delete sink;
        sink = nullptr;
        reopen.invalidate();
      }
      if (ndiEnabled && !sink && (!reopen.isValid() || reopen.elapsed() > kReopenDelayMs))
      {
        QString error;
        sink = m_sinkFactory ? m_sinkFactory() : nullptr;
        if (sink)
        {
          sink->setClocked(ndiClocked);
        }
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

      // Preview: at the output size while it is mixed in, else monitor size.
      const bool previewOnAir = mix > 0;
      const int previewWidth = previewOnAir ? width : qMin(width, kPreviewMonitorWidth);
      const int previewHeight = previewOnAir ? height : qMax(1, height * previewWidth / width);
      const QByteArray previewFrame = composite(preview, previewWidth, previewHeight);

      QByteArray frame = (mix >= 1) ? previewFrame : composite(program, width, height);
      if (mix > 0 && mix < 1)
      {
        blend(&frame, previewFrame, int(mix * 256 + 0.5));
      }
      if (black > 0)
      {
        darken(&frame, int(black * 256 + 0.5));
      }
      {
        QMutexLocker locker(&m_mutex);
        m_output = frame;
        m_outputWidth = width;
        m_outputHeight = height;
        m_previewOutput = previewFrame;
        m_previewWidth = previewWidth;
        m_previewHeight = previewHeight;
      }

      if (sdi.isOpen())
      {
        sdi.sendFrame(frame, width, height);
      }

      if (sink)
      {
        int num, den;
        fpsFraction(fps, &num, &den);
        sink->sendFrame(frame, width, height, num, den);
        if (connectionsTimer.elapsed() > kConnectionsMs)
        {
          connectionsTimer.restart();
          const int connections = sink->connectionCount();
          QMutexLocker locker(&m_mutex);
          m_connections = connections;
        }
      }

      // Pace (NDI senders clock themselves, and so does an SDI card on its
      // clock: sendFrame() waited).
      if (!sdi.isClocked() && (!sink || !sink->isClocked()))
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
  // The layers over black, each at its placement in width x height (by
  // default fitted, aspect ratio kept, and centred), with their
  // transparency. Opaque BGRA.
  static QByteArray composite(const QList<Layer> &layers, int width, int height)
  {
    QImage canvas(width, height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(0xff000000);
    {
      QPainter painter(&canvas);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      for (int i = 0; i < layers.size(); ++i)
      {
        const Layer &layer = layers.at(i);
        if (!layer.isValid())
        {
          continue;
        }
        // BGRA bytes = ARGB32 on little-endian machines.
        const QImage image(reinterpret_cast<const uchar *>(layer.frame.constData()), layer.width, layer.height,
                           layer.width * 4, QImage::Format_ARGB32);
        if (layer.width == width && layer.height == height && layer.placement.isDefaultFrame())
        {
          painter.drawImage(0, 0, image);
        }
        else
        {
          QSize size = image.size();
          size.scale(width, height, Qt::KeepAspectRatio); // QSize::scaled() is Qt 5 only
          const double w = size.width() * layer.placement.scale;
          const double h = size.height() * layer.placement.scale;
          painter.drawImage(QRectF(layer.placement.x * width - w / 2, layer.placement.y * height - h / 2, w, h), image);
        }
      }
    }
    // Opaque: premultiplied and straight alpha are the same bytes.
    return QByteArray(reinterpret_cast<const char *>(canvas.constBits()), width * height * 4);
  }

  const QString m_name;
  const FrameSinkFactory m_sinkFactory;

  mutable QMutex m_mutex;
  // shared state (m_mutex)
  QList<Layer> m_program;
  QList<Layer> m_preview;
  double m_mix;
  double m_black;
  double m_fps;
  int m_width;
  int m_height;
  bool m_quit;
  QByteArray m_output;
  int m_outputWidth;
  int m_outputHeight;
  QByteArray m_previewOutput;
  int m_previewWidth;
  int m_previewHeight;
  QString m_error;
  int m_connections;
  bool m_ndiEnabled;
  QString m_sdiPort;
  quint32 m_sdiMode;
  bool m_sdiCardClock;
  int m_sdiConfig; // bumped by setSdi()
  QString m_sdiError;
  QString m_sdiModeName;
};

namespace
{
// The frames the layers' channels send now. Layers without a channel, or
// stopped (sending a clear frame), are left out.
QList<ProgramMixer::Layer> framesOf(NdiManager *ndi, const ProgramOutput &output, ProgramOutput::Bus bus)
{
  const ProgramOutput::Layers layers = output.layers(bus);
  QList<ProgramMixer::Layer> frames;
  for (int i = 0; i < layers.size(); ++i)
  {
    PlayoutChannel *channel = ndi->channel(layers.at(i).sceneId, layers.at(i).item);
    ProgramMixer::Layer layer;
    // Only the layers within their block on the bus's timeline.
    if (channel && output.isActive(bus, layers.at(i)) && channel->status().state != PlayoutChannel::eStopped &&
        channel->currentFrame(&layer.frame, &layer.width, &layer.height))
    {
      layer.placement = output.placement(bus, layers.at(i));
      frames << layer;
    }
  }
  return frames;
}

QImage imageOf(const QByteArray &frame, int width, int height, int maxWidth)
{
  if (width <= 0 || height <= 0 || frame.size() < width * height * 4)
  {
    return QImage();
  }
  const QImage image(reinterpret_cast<const uchar *>(frame.constData()), width, height, width * 4, QImage::Format_ARGB32);
  return (width > maxWidth) ? image.scaledToWidth(maxWidth, Qt::FastTransformation) : image.copy();
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
      m_ndiEnabled(true),
      m_sdiMode(0),
      m_sdiCardClock(false),
      m_width(kDefaultWidth),
      m_height(kDefaultHeight),
      m_fps(kDefaultFps),
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

void ProgramOutput::setFormat(int width, int height, double fps)
{
  if (width <= 0 || height <= 0)
  {
    return;
  }
  m_width = width;
  m_height = height;
  m_fps = fps > 0 ? fps : kDefaultFps;
  m_mixer->setFormat(m_width, m_height, m_fps);
}

int ProgramOutput::width() const
{
  return m_width;
}

int ProgramOutput::height() const
{
  return m_height;
}

double ProgramOutput::fps() const
{
  return m_fps;
}

ProgramOutput::Layers &ProgramOutput::bus(Bus which)
{
  return (which == ePreview) ? m_preview : m_program;
}

ProgramOutput::Layers ProgramOutput::layers(Bus which) const
{
  return (which == ePreview) ? m_preview : m_program;
}

bool ProgramOutput::contains(Bus which, const Source &source) const
{
  return layers(which).contains(source);
}

void ProgramOutput::setLayers(Bus which, const Layers &layers)
{
  // A bus shows one scene: the layers of the scene of the last one given.
  const QString sceneId = layers.isEmpty() ? QString() : layers.last().sceneId;
  Layers unique;
  for (int i = 0; i < layers.size(); ++i)
  {
    if (!layers.at(i).isEmpty() && layers.at(i).sceneId == sceneId && !unique.contains(layers.at(i)))
    {
      unique << layers.at(i);
    }
  }
  if (unique == bus(which))
  {
    return;
  }
  const bool wasEmpty = bus(which).isEmpty();
  if (which == eProgram)
  {
    stopTransition();
  }
  // Placements: kept for the layers staying; new ones get the scene's (on
  // Program, the one they have on Preview, if they are there).
  QHash<QString, LayerPlacement> placements;
  for (int i = 0; i < unique.size(); ++i)
  {
    const QString key = placementKey(unique.at(i));
    if (m_placements[which].contains(key))
    {
      placements.insert(key, m_placements[which].value(key));
    }
    else if (which == eProgram && m_placements[ePreview].contains(key))
    {
      placements.insert(key, m_placements[ePreview].value(key));
    }
    else
    {
      placements.insert(key, savedPlacement(unique.at(i)));
    }
  }
  m_placements[which] = placements;
  bus(which) = unique;
  // Clock: Program plays its new layers from the start; an empty Preview
  // getting layers holds their first frames.
  if (unique.isEmpty())
  {
    m_clocks[which] = Clock();
  }
  else if (which == eProgram)
  {
    m_clocks[which] = Clock();
    play(eProgram);
  }
  else if (wasEmpty)
  {
    m_clocks[which] = Clock();
    m_clocks[which].stopped = false;
  }
  Q_EMIT busChanged();
}

void ProgramOutput::addLayer(Bus which, const Source &source)
{
  if (source.isEmpty() || bus(which).contains(source))
  {
    return;
  }
  // From another scene: it starts a new stack (setLayers keeps its scene only).
  setLayers(which, bus(which) + (Layers() << source));
}

void ProgramOutput::removeLayer(Bus which, const Source &source)
{
  Layers layers = bus(which);
  if (layers.removeAll(source) > 0)
  {
    setLayers(which, layers);
  }
}

void ProgramOutput::moveLayer(Bus which, const Source &source, int steps)
{
  Layers layers = bus(which);
  const int from = layers.indexOf(source);
  if (from < 0)
  {
    return;
  }
  const int to = qBound(0, from + steps, layers.size() - 1);
  if (to != from)
  {
    layers.move(from, to);
    bus(which) = layers;
    Q_EMIT busChanged();
  }
}

QString ProgramOutput::placementKey(const Source &source)
{
  return source.sceneId + QLatin1Char('|') + source.item;
}

LayerPlacement ProgramOutput::savedPlacement(const Source &source) const
{
  Scene scene;
  return m_scenes->scene(source.sceneId, &scene) ? scene.layerPlacements.value(source.item) : LayerPlacement();
}

LayerPlacement ProgramOutput::placement(Bus which, const Source &source) const
{
  return m_placements[which].value(placementKey(source));
}

void ProgramOutput::setPlacement(Bus which, const Source &source, const LayerPlacement &placement)
{
  if (layers(which).contains(source) && placement.scale > 0)
  {
    m_placements[which].insert(placementKey(source), placement);
  }
}

void ProgramOutput::setPreview(const Source &source)
{
  setLayers(ePreview, source.isEmpty() ? Layers() : (Layers() << source));
}

void ProgramOutput::cutTo(const Source &source)
{
  if (!source.isEmpty())
  {
    cutTo(Layers() << source);
  }
}

void ProgramOutput::cutTo(const Layers &layers)
{
  if (layers.isEmpty() || m_program == layers)
  {
    return;
  }
  if (m_preview == layers)
  {
    cut();
    return;
  }
  setLayers(eProgram, layers);
}

ProgramOutput::Layers ProgramOutput::sceneLayers(const Scene &scene)
{
  Layers layers;
  for (int i = 0; i < scene.items.size(); ++i)
  {
    layers << Source(scene.id, scene.items.at(i));
  }
  return layers;
}

void ProgramOutput::cut()
{
  if (m_preview.isEmpty())
  {
    return;
  }
  stopTransition();
  play(ePreview);
  take();
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
  play(ePreview);
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
    play(ePreview);
  }
  if (position >= 1)
  {
    m_position = 0;
    take();
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
  return m_mixer->output(false, &frame, &width, &height) ? imageOf(frame, width, height, maxWidth) : QImage();
}

QImage ProgramOutput::previewImage(int maxWidth) const
{
  QByteArray frame;
  int width, height;
  return m_mixer->output(true, &frame, &width, &height) ? imageOf(frame, width, height, maxWidth) : QImage();
}

QString ProgramOutput::outputError() const
{
  return m_mixer->error();
}

void ProgramOutput::setNdiEnabled(bool enabled)
{
  m_ndiEnabled = enabled;
  m_mixer->setNdiEnabled(enabled);
}

bool ProgramOutput::ndiEnabled() const
{
  return m_ndiEnabled;
}

void ProgramOutput::setSdiOutput(const QString &port, quint32 mode, bool cardClock)
{
  m_sdiPort = port;
  m_sdiMode = mode;
  m_sdiCardClock = cardClock;
  m_mixer->setSdi(port, mode, cardClock);
}

bool ProgramOutput::sdiCardClock() const
{
  return m_sdiCardClock;
}

QString ProgramOutput::sdiPort() const
{
  return m_sdiPort;
}

quint32 ProgramOutput::sdiMode() const
{
  return m_sdiMode;
}

QString ProgramOutput::sdiStatus(bool *ok) const
{
  QString modeName, error;
  m_mixer->sdiStatus(&modeName, &error);
  if (ok)
  {
    *ok = !m_sdiPort.isEmpty() && error.isEmpty();
  }
  if (m_sdiPort.isEmpty())
  {
    return QString();
  }
  if (!error.isEmpty())
  {
    return error;
  }
  const QString clock = m_sdiCardClock ? QString::fromUtf8("card clock") : QString::fromUtf8("system clock");
  return modeName.isEmpty() ? m_sdiPort
                            : QString::fromUtf8("%1  \xC2\xB7  %2  \xC2\xB7  %3").arg(m_sdiPort).arg(modeName).arg(clock);
}

int ProgramOutput::connections() const
{
  return m_mixer->connections();
}

// ----- Bus clocks -----

double ProgramOutput::layerDuration(const Source &source) const
{
  PlayoutChannel *channel = m_ndi->channel(source.sceneId, source.item);
  return channel ? channel->status().duration() : 0.0;
}

double ProgramOutput::duration(Bus which) const
{
  double end = 0;
  const Layers list = layers(which);
  for (int i = 0; i < list.size(); ++i)
  {
    end = qMax(end, placement(which, list.at(i)).start + layerDuration(list.at(i)));
  }
  return end;
}

double ProgramOutput::position(Bus which) const
{
  return m_clocks[which].position;
}

bool ProgramOutput::isPlaying(Bus which) const
{
  return m_clocks[which].playing;
}

bool ProgramOutput::loops(Bus which) const
{
  return m_clocks[which].loop;
}

void ProgramOutput::setLoop(Bus which, bool loop)
{
  m_clocks[which].loop = loop;
}

bool ProgramOutput::isActive(Bus which, const Source &source) const
{
  const Clock &clock = m_clocks[which];
  const double local = clock.position - placement(which, source).start;
  return !clock.stopped && local >= 0 && local < layerDuration(source);
}

QList<double> ProgramOutput::cuePoints(Bus which) const
{
  QList<double> points;
  const Layers list = layers(which);
  for (int i = 0; i < list.size(); ++i)
  {
    Scene scene;
    if (!m_scenes->scene(list.at(i).sceneId, &scene))
    {
      continue;
    }
    const double start = placement(which, list.at(i)).start;
    const QList<double> own = scene.cuePoints(list.at(i).item);
    for (int p = 0; p < own.size(); ++p)
    {
      bool known = false;
      for (int k = 0; k < points.size() && !known; ++k)
      {
        known = std::fabs(points.at(k) - (start + own.at(p))) < 0.001;
      }
      if (!known)
      {
        points << start + own.at(p);
      }
    }
  }
  std::sort(points.begin(), points.end());
  return points;
}

double ProgramOutput::nextCuePoint(Bus which, double after) const
{
  const QList<double> points = cuePoints(which);
  for (int i = 0; i < points.size(); ++i)
  {
    if (points.at(i) > after + 0.001)
    {
      return points.at(i);
    }
  }
  return -1;
}

void ProgramOutput::advance(Bus which)
{
  Clock &clock = m_clocks[which];
  if (!clock.playing)
  {
    return;
  }
  double t = clock.from + clock.timer.elapsed() / 1000.0;
  // Follow the first layer playing within its block, as a player follows
  // its decoder: the clock waits while a decoder starts and never runs
  // ahead of the pictures. Between blocks (no layer playing) the timer
  // goes on from there.
  const Layers list = layers(which);
  for (int i = 0; i < list.size(); ++i)
  {
    PlayoutChannel *channel = m_ndi->channel(list.at(i).sceneId, list.at(i).item);
    if (!channel)
    {
      continue;
    }
    const PlayoutChannel::Status status = channel->status();
    const double local = status.seconds();
    const double master = placement(which, list.at(i)).start + local;
    if (status.state != PlayoutChannel::ePlaying || local >= status.duration() || std::fabs(master - t) > kFollowWindow)
    {
      continue; // not playing, or somewhere else (left by a seek in progress, looping on its own)
    }
    t = master;
    clock.from = t;
    clock.timer.restart();
    break;
  }
  const double end = duration(which);
  if (clock.stopAt >= 0 && t >= clock.stopAt)
  {
    clock.position = clock.stopAt; // a cue point
    clock.playing = false;
  }
  else if (end > 0 && t >= end)
  {
    if (clock.loop)
    {
      clock.position = 0;
      clock.from = 0;
      clock.timer.start();
      clock.stopAt = nextCuePoint(which, -1);
    }
    else
    {
      // Hold the last frame.
      clock.position = qMax(0.0, end - 0.5 / m_fps);
      clock.playing = false;
    }
  }
  else
  {
    clock.position = t;
  }
}

void ProgramOutput::play(Bus which)
{
  Clock &clock = m_clocks[which];
  if (clock.playing || layers(which).isEmpty())
  {
    return;
  }
  const double end = duration(which);
  if (clock.stopped || (end > 0 && clock.position >= end - 1.0 / m_fps))
  {
    clock.position = 0; // from the start
  }
  clock.stopped = false;
  clock.playing = true;
  clock.from = clock.position;
  clock.timer.start();
  clock.stopAt = nextCuePoint(which, clock.position);
}

void ProgramOutput::pause(Bus which)
{
  advance(which);
  m_clocks[which].playing = false;
}

void ProgramOutput::stop(Bus which)
{
  Clock &clock = m_clocks[which];
  clock.playing = false;
  clock.stopped = true;
  clock.position = 0;
  const Layers list = layers(which);
  for (int i = 0; i < list.size(); ++i)
  {
    PlayoutChannel *channel = m_ndi->channel(list.at(i).sceneId, list.at(i).item);
    if (channel)
    {
      channel->stop();
    }
  }
}

void ProgramOutput::restart(Bus which)
{
  m_clocks[which].playing = false;
  m_clocks[which].stopped = true;
  play(which); // from the start, stopping at the first cue point
}

void ProgramOutput::seek(Bus which, double seconds)
{
  advance(which);
  Clock &clock = m_clocks[which];
  const double end = duration(which);
  clock.position = qBound(0.0, seconds, end > 0 ? end : 0.0);
  clock.stopped = false;
  if (clock.playing)
  {
    clock.from = clock.position;
    clock.timer.start();
    clock.stopAt = nextCuePoint(which, clock.position);
  }
}

void ProgramOutput::drive(Bus which, const QStringList &skip)
{
  const Clock &clock = m_clocks[which];
  const Layers list = layers(which);
  for (int i = 0; i < list.size(); ++i)
  {
    PlayoutChannel *channel = m_ndi->channel(list.at(i).sceneId, list.at(i).item);
    if (!channel || clock.stopped || skip.contains(placementKey(list.at(i))))
    {
      continue;
    }
    const PlayoutChannel::Status status = channel->status();
    if (status.state == PlayoutChannel::eLive)
    {
      continue; // sending the frames it renders
    }
    const QString key = placementKey(list.at(i));
    const double local = clock.position - placement(which, list.at(i)).start;
    if (local < 0 || local >= status.duration())
    {
      // Outside its block: not shown, held.
      if (status.state == PlayoutChannel::ePlaying)
      {
        channel->pause();
      }
      continue;
    }
    // In whole frames of the layer, within its render.
    const double fps = status.fps > 0 ? status.fps : 25;
    const int wanted = qBound(0, int(std::floor(local * fps + 0.5)), qMax(0, status.frameCount - 1));
    const int off = qAbs(status.position - wanted);
    if (clock.playing)
    {
      if (status.state != PlayoutChannel::ePlaying)
      {
        // Seek only when elsewhere: a layer held at its own cue point, or
        // just paused, goes on without restarting its decoder.
        if (off > 2 || status.state == PlayoutChannel::eStopped)
        {
          channel->seek(wanted / fps);
          m_seeks[key].start();
        }
        channel->play();
      }
      else if (off > int(kResyncSeconds * fps) && (!m_seeks.contains(key) || m_seeks[key].elapsed() > kResyncGraceMs))
      {
        // Drifted away (a slow decoder): back in step, then left alone a
        // while (its decoder restarts).
        channel->seek(wanted / fps);
        m_seeks[key].start();
      }
    }
    else
    {
      if (status.state == PlayoutChannel::ePlaying)
      {
        channel->pause();
      }
      if (off > 0 || status.state == PlayoutChannel::eStopped)
      {
        channel->seek(wanted / fps); // holds that frame
      }
    }
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

void ProgramOutput::take()
{
  m_program = m_preview;
  m_placements[eProgram] = m_placements[ePreview];
  m_clocks[eProgram] = m_clocks[ePreview];
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
      // Done: Preview is on air (the frames below already follow the take).
      m_fadeMs = 0;
      m_position = 0;
      take();
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

  // Clocks, then the channels follow them (Program first: a layer on both
  // buses plays as on air).
  advance(eProgram);
  advance(ePreview);
  drive(eProgram, QStringList());
  QStringList onAir;
  for (int i = 0; i < m_program.size(); ++i)
  {
    onAir << placementKey(m_program.at(i));
  }
  drive(ePreview, onAir);

  m_mixer->setInputs(framesOf(m_ndi, *this, eProgram), framesOf(m_ndi, *this, ePreview), m_position, m_black);

  if (changed)
  {
    Q_EMIT transitionChanged();
  }
}

void ProgramOutput::onScenesChanged()
{
  // Layers whose scene or Write node was removed go.
  bool changed = false;
  Layers *buses[] = { &m_preview, &m_program };
  for (int b = 0; b < 2; ++b)
  {
    for (int i = buses[b]->size() - 1; i >= 0; --i)
    {
      Scene scene;
      const Source &source = buses[b]->at(i);
      if (!m_scenes->scene(source.sceneId, &scene) || !scene.items.contains(source.item))
      {
        buses[b]->removeAt(i);
        changed = true;
      }
    }
  }
  if (changed)
  {
    if (m_preview.isEmpty())
    {
      stopTransition();
    }
    Q_EMIT busChanged();
  }
}
