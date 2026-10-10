#include "NdiManager.h"

#include "MediaProbe.h"
#include "PlayoutChannel.h"
#include "../scene/ProjectInfo.h"
#include "../scene/SceneRenderer.h"
#include "../scene/SceneStore.h"
#include "../state/Json.h"
#include "../state/StateStore.h"

#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

QString projectName(const QString &project)
{
  QString name = QFileInfo(project).fileName();
  if (name.endsWith(s(".ntp")))
  {
    name.chop(4);
  }
  return name;
}
}

NdiManager::NdiManager(SceneStore *scenes,
                       SceneRenderer *renderer,
                       const QString &ffmpegPath,
                       const QString &ffprobePath,
                       FrameSinkFactory sinkFactory,
                       QObject *parent)
    : QObject(parent),
      m_scenes(scenes),
      m_renderer(renderer),
      m_ffmpegPath(ffmpegPath),
      m_ffprobePath(ffprobePath),
      m_sinkFactory(sinkFactory),
      m_state(nullptr),
      m_liveLastFrame(-1)
{
  m_dataChangeTimer.setSingleShot(true);
  m_dataChangeTimer.setInterval(300);
  connect(&m_dataChangeTimer, SIGNAL(timeout()), this, SLOT(onDataChangeTimer()));
  connect(m_scenes, SIGNAL(scenesChanged()), this, SLOT(syncChannels()));
  connect(m_scenes, SIGNAL(renderRecordChanged(QString,QString)), this, SLOT(onRenderRecordChanged(QString,QString)));
  connect(m_scenes, SIGNAL(ndiSettingsChanged(QString,QString)), this, SLOT(onNdiSettingsChanged(QString,QString)));
  if (m_renderer)
  {
    connect(m_renderer, SIGNAL(progressChanged(QString,QString)), this, SLOT(onRenderProgressChanged(QString,QString)));
    connect(m_renderer, SIGNAL(statusChanged()), this, SLOT(onRendererStatusChanged()));
  }
  syncChannels();
}

NdiManager::~NdiManager()
{
  for (QHash<QString, ChannelInfo>::iterator it = m_channels.begin(); it != m_channels.end(); ++it)
  {
    delete it.value().channel; // stops its thread
  }
}

QString NdiManager::channelKey(const QString &sceneId, const QString &item)
{
  return sceneId + QLatin1Char('|') + item;
}

QString NdiManager::sourceName(const Scene &scene, const QString &item)
{
  // A project with a single Write node keeps the plain "<scene> - <project>"
  // name; with several, each source names its Write node.
  const QString project = SceneItem::project(item);
  if (readProjectInfo(project).writers.size() <= 1)
  {
    return s("%1 - %2").arg(scene.name).arg(projectName(project));
  }
  return s("%1 - %2 - %3").arg(scene.name).arg(projectName(project)).arg(SceneItem::writer(item));
}

PlayoutChannel *NdiManager::channel(const QString &sceneId, const QString &item) const
{
  const QHash<QString, ChannelInfo>::const_iterator it = m_channels.find(channelKey(sceneId, item));
  return it == m_channels.end() ? nullptr : it.value().channel;
}

QList<PlayoutChannel *> NdiManager::sceneChannels(const QString &sceneId) const
{
  QList<PlayoutChannel *> channels;
  for (QHash<QString, ChannelInfo>::const_iterator it = m_channels.constBegin(); it != m_channels.constEnd(); ++it)
  {
    if (it.value().sceneId == sceneId)
    {
      channels << it.value().channel;
    }
  }
  return channels;
}

void NdiManager::applySettings(const ChannelInfo &info, const Scene &scene)
{
  info.channel->setPauseAt(scene.pauseAt(info.item));
  info.channel->setLoop(scene.ndiLoop.value(info.item, false));
}

void NdiManager::syncChannels()
{
  // Wanted channels: items of the NDI-enabled scenes.
  QHash<QString, ChannelInfo> wanted;
  const QList<Scene> scenes = m_scenes->scenes();
  for (int i = 0; i < scenes.size(); ++i)
  {
    const Scene &scene = scenes.at(i);
    if (!scene.ndiEnabled)
    {
      continue;
    }
    for (int p = 0; p < scene.items.size(); ++p)
    {
      ChannelInfo info;
      info.channel = nullptr;
      info.sceneId = scene.id;
      info.item = scene.items.at(p);
      info.name = sourceName(scene, info.item);
      info.alpha = scene.ndiAlpha;
      wanted.insert(channelKey(scene.id, info.item), info);
    }
  }

  QSet<QString> touchedScenes;

  // Remove channels no longer wanted, or whose name/alpha changed (an NDI
  // source cannot be renamed or switch alpha: recreate it).
  QList<QString> keys = m_channels.keys();
  for (int i = 0; i < keys.size(); ++i)
  {
    const ChannelInfo current = m_channels.value(keys.at(i));
    const QHash<QString, ChannelInfo>::const_iterator w = wanted.find(keys.at(i));
    if (w == wanted.end() || w.value().name != current.name || w.value().alpha != current.alpha)
    {
      touchedScenes.insert(current.sceneId);
      if (m_liveKey == keys.at(i))
      {
        m_liveKey.clear();
      }
      delete current.channel;
      m_channels.remove(keys.at(i));
    }
  }

  // Create the missing ones.
  for (QHash<QString, ChannelInfo>::iterator it = wanted.begin(); it != wanted.end(); ++it)
  {
    if (m_channels.contains(it.key()))
    {
      continue;
    }
    ChannelInfo info = it.value();
    info.channel = new PlayoutChannel(info.name, info.alpha, m_ffmpegPath, m_sinkFactory);
    info.channel->setProperty("sceneId", info.sceneId);
    info.channel->setProperty("item", info.item);
    connect(info.channel, SIGNAL(statusChanged()), this, SLOT(onChannelStatusChanged()), Qt::QueuedConnection);
    info.channel->start();
    m_channels.insert(it.key(), info);
    touchedScenes.insert(info.sceneId);

    Scene scene;
    m_scenes->scene(info.sceneId, &scene);
    applySettings(info, scene);
    reloadMedia(info.sceneId, info.item);
  }

  for (QSet<QString>::const_iterator it = touchedScenes.constBegin(); it != touchedScenes.constEnd(); ++it)
  {
    Q_EMIT channelsChanged(*it);
  }

  rerenderRemappedItems(scenes);
}

void NdiManager::rerenderRemappedItems(const QList<Scene> &scenes)
{
  // Live scenes: a key renamed to another store key changes the values the
  // items use, like a value change.
  QHash<QString, QHash<QString, QString> > keyMaps;
  for (int i = 0; i < scenes.size(); ++i)
  {
    const Scene &scene = scenes.at(i);
    keyMaps.insert(scene.id, scene.keyMap);
    if (!m_renderer || !scene.ndiEnabled || !scene.ndiLive || !m_keyMaps.contains(scene.id))
    {
      continue;
    }
    const QHash<QString, QString> previous = m_keyMaps.value(scene.id);
    if (previous == scene.keyMap)
    {
      continue;
    }
    QStringList affected;
    for (int p = 0; p < scene.items.size(); ++p)
    {
      const QStringList keys = readProjectInfo(SceneItem::project(scene.items.at(p))).stateKeys;
      for (int k = 0; k < keys.size(); ++k)
      {
        if (previous.value(keys.at(k), keys.at(k)) != scene.mappedKey(keys.at(k)))
        {
          affected << scene.items.at(p);
          break;
        }
      }
    }
    if (!affected.isEmpty())
    {
      m_renderer->rerender(scene.id, affected);
    }
  }
  m_keyMaps = keyMaps;
}

void NdiManager::onNdiSettingsChanged(const QString &sceneId, const QString &item)
{
  const QHash<QString, ChannelInfo>::const_iterator it = m_channels.find(channelKey(sceneId, item));
  if (it != m_channels.end())
  {
    Scene scene;
    m_scenes->scene(sceneId, &scene);
    applySettings(it.value(), scene);
  }
}

void NdiManager::reloadMedia(const QString &sceneId, const QString &item)
{
  PlayoutChannel *c = channel(sceneId, item);
  Scene scene;
  if (!c || !m_scenes->scene(sceneId, &scene))
  {
    return;
  }

  const QString output = SceneRenderer::itemOutput(scene.outputDir, item);
  if (output.isEmpty())
  {
    MediaInfo none;
    none.error = tr("the Write node has no output file");
    c->setMedia(none);
    return;
  }
  const double fps = readProjectInfo(SceneItem::project(item)).fps;
  c->setMedia(probeMedia(output, m_ffprobePath, fps));
}

void NdiManager::setStateStore(StateStore *state)
{
  if (m_state)
  {
    disconnect(m_state, nullptr, this, nullptr);
  }
  m_state = state;
  if (m_state)
  {
    connect(m_state, SIGNAL(valueChanged(QString)), this, SLOT(onStateValueChanged(QString)));
  }
}

void NdiManager::onStateValueChanged(const QString &key)
{
  if (!m_changedKeys.contains(key))
  {
    m_changedKeys << key;
  }
  m_dataChangeTimer.start();
}

void NdiManager::onDataChangeTimer()
{
  const QStringList changed = m_changedKeys;
  m_changedKeys.clear();
  if (!m_renderer || changed.isEmpty())
  {
    return;
  }

  // Live scenes: re-render the items whose project uses a changed value (through the
  // scene's key renaming).
  const QList<Scene> scenes = m_scenes->scenes();
  for (int i = 0; i < scenes.size(); ++i)
  {
    const Scene &scene = scenes.at(i);
    if (!scene.ndiEnabled || !scene.ndiLive)
    {
      continue;
    }
    QStringList affected;
    for (int p = 0; p < scene.items.size(); ++p)
    {
      const QStringList keys = readProjectInfo(SceneItem::project(scene.items.at(p))).stateKeys;
      for (int k = 0; k < keys.size(); ++k)
      {
        if (changed.contains(scene.mappedKey(keys.at(k))))
        {
          affected << scene.items.at(p);
          break;
        }
      }
    }
    if (!affected.isEmpty())
    {
      m_renderer->rerender(scene.id, affected);
    }
  }
}

void NdiManager::onRenderRecordChanged(const QString &sceneId, const QString &item)
{
  PlayoutChannel *c = channel(sceneId, item);
  if (!c)
  {
    return;
  }

  const RenderRecord record = m_scenes->renderRecord(sceneId, item);
  if (record.status == RenderRecord::eDone || record.status == RenderRecord::eFailed || record.status == RenderRecord::eNone)
  {
    if (m_liveKey == channelKey(sceneId, item))
    {
      m_liveKey.clear();
      reloadMedia(sceneId, item);
      c->leaveLive();
    }
    else if (record.status == RenderRecord::eDone)
    {
      reloadMedia(sceneId, item); // the new render is what plays now
    }
  }
}

void NdiManager::onRendererStatusChanged()
{
  // A live render that was cancelled before any frame: leave live mode.
  if (!m_liveKey.isEmpty() && (!m_renderer->isBusy() ||
                               channelKey(m_renderer->currentSceneId(), m_renderer->currentItem()) != m_liveKey))
  {
    const ChannelInfo info = m_channels.value(m_liveKey);
    m_liveKey.clear();
    if (info.channel)
    {
      reloadMedia(info.sceneId, info.item);
      info.channel->leaveLive();
    }
  }
}

void NdiManager::onRenderProgressChanged(const QString &sceneId, const QString &item)
{
  PlayoutChannel *c = channel(sceneId, item);
  Scene scene;
  if (!c || !m_scenes->scene(sceneId, &scene) || !scene.ndiLive)
  {
    return;
  }

  const RenderProgress progress = m_renderer->progress(sceneId, item);
  // Progress is also reported for "started"/"finished": send each frame once.
  if (progress.currentFrame < 0 ||
      (m_liveKey == channelKey(sceneId, item) && progress.currentFrame == m_liveLastFrame))
  {
    return;
  }

  // Live needs one file per frame to read as soon as it is written.
  const QString output = SceneRenderer::itemOutput(scene.outputDir, item);
  if (output.isEmpty() || !isSequencePattern(output))
  {
    c->setMessage(tr("Live needs an image-sequence output (e.g. frame_####.png); playing the render instead."));
    return;
  }

  const QString file = sequenceFrameFile(output, progress.currentFrame);
  QImage image(file);
  if (image.isNull())
  {
    return;
  }
  image = image.convertToFormat(QImage::Format_ARGB32); // BGRA bytes in memory (little endian)
  const QByteArray bgra(reinterpret_cast<const char *>(image.constBits()), image.bytesPerLine() * image.height());

  const QString key = channelKey(sceneId, item);
  if (m_liveKey != key)
  {
    m_liveKey = key;
    m_liveLastFrame = -1;
    c->enterLive(image.width(), image.height(), readProjectInfo(SceneItem::project(item)).fps);
  }
  c->pushLiveFrame(bgra, image.width(), image.height());
  m_liveLastFrame = progress.currentFrame;
}

void NdiManager::onChannelStatusChanged()
{
  QObject *source = sender();
  if (source)
  {
    Q_EMIT channelStatusChanged(source->property("sceneId").toString(), source->property("item").toString());
  }
}

void NdiManager::playAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    channels.at(i)->play();
  }
}

void NdiManager::pauseAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    channels.at(i)->pause();
  }
}

void NdiManager::stopAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    channels.at(i)->stop();
  }
}

void NdiManager::replayAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    channels.at(i)->replay();
  }
}

void NdiManager::cueAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    channels.at(i)->cue();
  }
}

void NdiManager::continueAll(const QString &sceneId)
{
  const QList<PlayoutChannel *> channels = sceneChannels(sceneId);
  for (int i = 0; i < channels.size(); ++i)
  {
    if (channels.at(i)->status().state == PlayoutChannel::ePaused)
    {
      channels.at(i)->play();
    }
  }
}

// ---------------- HTTP ----------------

QString NdiManager::findScene(const QString &nameOrId) const
{
  const QList<Scene> scenes = m_scenes->scenes();
  for (int i = 0; i < scenes.size(); ++i)
  {
    if (scenes.at(i).id == nameOrId || scenes.at(i).name == nameOrId)
    {
      return scenes.at(i).id;
    }
  }
  return QString();
}

QVariantMap NdiManager::channelState(const ChannelInfo &info) const
{
  const PlayoutChannel::Status st = info.channel->status();
  QVariantMap m;
  m.insert(s("source"), info.name);
  m.insert(s("project"), projectName(SceneItem::project(info.item)));
  m.insert(s("writer"), SceneItem::writer(info.item));
  m.insert(s("state"), PlayoutChannel::stateName(st.state).toLower());
  m.insert(s("position"), st.seconds());
  m.insert(s("duration"), st.duration());
  m.insert(s("pauseAt"), st.pauseAt);
  m.insert(s("loop"), st.loop);
  m.insert(s("receivers"), st.connections);
  if (!st.message.isEmpty())
  {
    m.insert(s("message"), st.message);
  }
  return m;
}

bool NdiManager::handleHttpRequest(const QByteArray &method, const QByteArray &path, const QByteArray &body,
                                   int *status, QByteArray *response)
{
  if (path != "/ndi")
  {
    return false;
  }

  QVariantMap result;

  if (method == "GET")
  {
    QVariantList sources;
    for (QHash<QString, ChannelInfo>::const_iterator it = m_channels.constBegin(); it != m_channels.constEnd(); ++it)
    {
      Scene scene;
      m_scenes->scene(it.value().sceneId, &scene);
      QVariantMap m = channelState(it.value());
      m.insert(s("scene"), scene.name);
      sources << m;
    }
    result.insert(s("sources"), sources);
    *status = 200;
    *response = Json::serialize(result);
    return true;
  }

  if (method != "POST")
  {
    *status = 405;
    *response = "{\"error\":\"use GET or POST\"}";
    return true;
  }

  bool ok = false;
  const QVariantMap request = Json::parse(body, &ok).toMap();
  const QString sceneId = findScene(request.value(s("scene")).toString());
  Scene scene;
  if (!ok || sceneId.isEmpty() || !m_scenes->scene(sceneId, &scene))
  {
    *status = 404;
    *response = "{\"error\":\"unknown scene (give its name or id in \\\"scene\\\")\"}";
    return true;
  }
  if (!scene.ndiEnabled)
  {
    *status = 400;
    *response = "{\"error\":\"NDI output is not enabled for this scene\"}";
    return true;
  }

  // Sources addressed: all of the scene, or those of one project (by name or
  // path), optionally only one of its Write nodes ("writer").
  QStringList items;
  const QString projectArg = request.value(s("project")).toString();
  const QString writerArg = request.value(s("writer")).toString();
  for (int i = 0; i < scene.items.size(); ++i)
  {
    const QString item = scene.items.at(i);
    const QString project = SceneItem::project(item);
    const bool projectMatches = projectArg.isEmpty() || projectArg == project || projectArg == projectName(project);
    if (projectMatches && (writerArg.isEmpty() || writerArg == SceneItem::writer(item)))
    {
      items << item;
    }
  }
  if (items.isEmpty())
  {
    *status = 404;
    *response = "{\"error\":\"unknown project or writer in this scene\"}";
    return true;
  }

  // Settings first, then the action.
  for (int i = 0; i < items.size(); ++i)
  {
    if (request.contains(s("pauseAt")))
    {
      const QVariant v = request.value(s("pauseAt"));
      m_scenes->setNdiPauseAt(sceneId, items.at(i), v.isValid() ? v.toDouble() : -1.0);
    }
    if (request.contains(s("loop")))
    {
      m_scenes->setNdiLoop(sceneId, items.at(i), request.value(s("loop")).toBool());
    }
  }

  const QString action = request.value(s("action")).toString();
  for (int i = 0; i < items.size(); ++i)
  {
    PlayoutChannel *c = channel(sceneId, items.at(i));
    if (!c || action.isEmpty())
    {
      continue;
    }
    if (action == s("play"))
    {
      c->play();
    }
    else if (action == s("pause"))
    {
      c->pause();
    }
    else if (action == s("stop"))
    {
      c->stop();
    }
    else if (action == s("replay"))
    {
      c->replay();
    }
    else if (action == s("cue"))
    {
      c->cue();
    }
    else if (action == s("continue"))
    {
      if (c->status().state == PlayoutChannel::ePaused)
      {
        c->play();
      }
    }
    else
    {
      *status = 400;
      *response = "{\"error\":\"unknown action (play, pause, stop, replay, cue, continue)\"}";
      return true;
    }
  }

  QVariantList sources;
  for (int i = 0; i < items.size(); ++i)
  {
    const QHash<QString, ChannelInfo>::const_iterator it = m_channels.find(channelKey(sceneId, items.at(i)));
    if (it != m_channels.end())
    {
      sources << channelState(it.value());
    }
  }
  result.insert(s("scene"), scene.name);
  result.insert(s("sources"), sources);
  *status = 200;
  *response = Json::serialize(result);
  return true;
}
