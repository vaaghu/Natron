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
const int kTickMs = 15;              // transitions and frame hand-over to the mixer
const int kReopenDelayMs = 5000;     // the NDI source could not be opened: try again this often
const int kConnectionsMs = 1000;     // receivers are counted this often
const int kPreviewMonitorWidth = 960; // Preview is composited this small when not on air
const int kDefaultWidth = 1920;
const int kDefaultHeight = 1080;
const double kDefaultFps = 30;

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
// output frame rate.
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
        m_connections(-1)
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
      QList<Layer> program, preview;
      double mix, black, fps;
      int width, height;
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
      }

      // Open the output; if it cannot be opened (NDI runtime missing...),
      // keep mixing for the dashboard's monitors and try again later.
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
        if (layer.width == width && layer.height == height && layer.placement.isDefault())
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
    if (channel && channel->status().state != PlayoutChannel::eStopped &&
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
  if (which == ePreview)
  {
    cueLayers(unique);
  }
  else
  {
    stopTransition();
    playLayers(unique);
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
  if (source.isEmpty() || m_program == (Layers() << source))
  {
    return;
  }
  if (m_preview == (Layers() << source))
  {
    cut();
    return;
  }
  setLayers(eProgram, Layers() << source);
}

void ProgramOutput::cut()
{
  if (m_preview.isEmpty())
  {
    return;
  }
  stopTransition();
  playLayers(m_preview);
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
  playLayers(m_preview);
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
    playLayers(m_preview);
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

int ProgramOutput::connections() const
{
  return m_mixer->connections();
}

void ProgramOutput::cueLayers(const Layers &layers)
{
  for (int i = 0; i < layers.size(); ++i)
  {
    PlayoutChannel *channel = m_ndi->channel(layers.at(i).sceneId, layers.at(i).item);
    if (channel && channel->status().state == PlayoutChannel::eStopped)
    {
      channel->seek(0);
    }
  }
}

void ProgramOutput::playLayers(const Layers &layers)
{
  for (int i = 0; i < layers.size(); ++i)
  {
    PlayoutChannel *channel = m_ndi->channel(layers.at(i).sceneId, layers.at(i).item);
    if (!channel)
    {
      continue;
    }
    // Stopped, cued, held or at its end: play (from the start at the end).
    const PlayoutChannel::State state = channel->status().state;
    if (state == PlayoutChannel::eStopped || state == PlayoutChannel::ePaused)
    {
      channel->play();
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

void ProgramOutput::swap()
{
  const Layers program = m_program;
  m_program = m_preview;
  m_preview = program;
  const QHash<QString, LayerPlacement> placements = m_placements[eProgram];
  m_placements[eProgram] = m_placements[ePreview];
  m_placements[ePreview] = placements;
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
