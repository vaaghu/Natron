#pragma once

#include "FrameSink.h"
#include "../server/HttpServer.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class PlayoutChannel;
class SceneRenderer;
class StateStore;
class SceneStore;
struct Scene;

// NDI outputs of the scenes: every project of a scene with NDI enabled is
// an NDI source "<scene> - <project>", played by a PlayoutChannel.
// - Playout (default): the channel plays the project's last render.
// - Live (scene option): frames are sent while the scene renders (needs an
//   image-sequence output), then the render can be played normally.
// Also serves HTTP control on /ndi (see handleHttpRequest).
class NdiManager : public QObject, public HttpRouteHandler
{
  Q_OBJECT

public:
  NdiManager(SceneStore *scenes,
             SceneRenderer *renderer,
             const QString &ffmpegPath,
             const QString &ffprobePath,
             FrameSinkFactory sinkFactory,
             QObject *parent = nullptr);
  ~NdiManager();

  // Channel of a project of a scene (null if the scene has no NDI output).
  PlayoutChannel *channel(const QString &sceneId, const QString &project) const;

  // NDI source name of a project of a scene.
  static QString sourceName(const Scene &scene, const QString &project);

  // Scene-wide transport.
  void playAll(const QString &sceneId);
  void pauseAll(const QString &sceneId);
  void stopAll(const QString &sceneId);
  void replayAll(const QString &sceneId);
  void cueAll(const QString &sceneId);      // from the start, each stops at its pause point
  void continueAll(const QString &sceneId); // resume the paused ones

  // Re-reads the rendered output of a channel (e.g. after a render).
  void reloadMedia(const QString &sceneId, const QString &project);

  // Scenes in Live mode re-render (restarting a running render) when a
  // value they use changes in this store.
  void setStateStore(StateStore *state);

  // HttpRouteHandler: /ndi
  //   GET  /ndi   -> all NDI sources with their state
  //   POST /ndi   {"scene": "<name or id>", "project": "<name>" (optional:
  //                all projects), "action": "play|pause|stop|replay|cue|
  //                continue", "pauseAt": seconds, "loop": true|false}
  bool handleHttpRequest(const QByteArray &method, const QByteArray &path, const QByteArray &body,
                         int *status, QByteArray *response);

Q_SIGNALS:
  // Channels of a scene were created/removed.
  void channelsChanged(const QString &sceneId);
  // A channel's state/position changed.
  void channelStatusChanged(const QString &sceneId, const QString &project);

private Q_SLOTS:
  void syncChannels();
  void onRenderRecordChanged(const QString &sceneId, const QString &project);
  void onRenderProgressChanged(const QString &sceneId, const QString &project);
  void onRendererStatusChanged();
  void onNdiSettingsChanged(const QString &sceneId, const QString &project);
  void onChannelStatusChanged();
  void onStateValueChanged(const QString &key);
  void onDataChangeTimer();

private:
  struct ChannelInfo
  {
    PlayoutChannel *channel;
    QString sceneId;
    QString project;
    QString name;
    bool alpha;
  };

  static QString channelKey(const QString &sceneId, const QString &project);
  QList<PlayoutChannel *> sceneChannels(const QString &sceneId) const;
  void applySettings(const ChannelInfo &info, const Scene &scene);
  QString findScene(const QString &nameOrId) const;
  QVariantMap channelState(const ChannelInfo &info) const;

  SceneStore *m_scenes;
  SceneRenderer *m_renderer;
  QString m_ffmpegPath;
  QString m_ffprobePath;
  FrameSinkFactory m_sinkFactory;
  QHash<QString, ChannelInfo> m_channels; // key: "<scene id>|<project>"
  StateStore *m_state;
  QStringList m_changedKeys; // store keys changed since the last re-render
  QTimer m_dataChangeTimer;  // batches bursts of changes (one API request)
  QString m_liveKey; // channel currently fed live frames
  int m_liveLastFrame; // last frame sent live (renderer frame number)
};
