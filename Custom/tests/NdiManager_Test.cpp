#include "TestUtil.h"

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/PlayoutChannel.h"
#include "Custom/scene/SceneRenderer.h"
#include "Custom/scene/SceneStore.h"
#include "Custom/state/Json.h"
#include "Custom/state/StateStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
class NdiManagerTest : public ::testing::Test
{
protected:
  void SetUp()
  {
    ASSERT_TRUE(m_dir.isValid());
#ifdef Q_OS_UNIX
    // A decoder that never ends: channels keep playing.
    m_ffmpeg = path(QString::fromUtf8("ffmpeg"));
    ASSERT_TRUE(writeScript(m_ffmpeg, "exec cat /dev/zero"));
#endif
    m_project = path(QString::fromUtf8("intro.ntp"));
    ASSERT_TRUE(writeProject(m_project, QList<Node>() << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("player"))
                                                      << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("[Project]/out.png")),
                             25));
    m_multi = path(QString::fromUtf8("multi.ntp"));
    ASSERT_TRUE(writeProject(m_multi, QList<Node>() << writeNode(QString::fromUtf8("WriteA"), QString::fromUtf8("[Project]/a.png"))
                                                    << writeNode(QString::fromUtf8("WriteB"), QString())));
    m_item = SceneItem::make(m_project, QString::fromUtf8("Write1"));
    m_sceneId = m_scenes.createScene(QString::fromUtf8("Show"));
    m_scenes.addItems(m_sceneId, QStringList() << m_item);
  }

  void TearDown()
  {
    m_manager.reset(); // channels stop before the store goes
    m_renderer.reset();
  }

  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }

  NdiManager *manager(bool withRenderer = false)
  {
    if (withRenderer)
    {
      m_renderer.reset(new SceneRenderer(nullptr, &m_scenes, path(QString::fromUtf8("missing/NatronRenderer")), QString(),
                                         QString(), QString()));
    }
    m_manager.reset(new NdiManager(&m_scenes, m_renderer.get(), m_ffmpeg, QString(), createFakeSink));
    return m_manager.get();
  }

  // The item's render (at the scene's output: next to out.png, in "Show").
  void render()
  {
    ASSERT_TRUE(writeImage(path(QString::fromUtf8("Show/out.png")), 8, 4));
  }

  int http(const QByteArray &method, const QByteArray &body, QVariantMap *json)
  {
    int status = 0;
    QByteArray response;
    EXPECT_TRUE(m_manager->handleHttpRequest(method, "/ndi", body, &status, &response));
    *json = Json::parse(response, nullptr).toMap();
    return status;
  }

  QVariantMap source(const QVariantMap &json, int i = 0) const
  {
    return json.value(QString::fromUtf8("sources")).toList().value(i).toMap();
  }

  QTemporaryDir m_dir;
  SceneStore m_scenes;
  std::unique_ptr<SceneRenderer> m_renderer;
  std::unique_ptr<NdiManager> m_manager;
  QString m_ffmpeg;
  QString m_project;
  QString m_multi;
  QString m_item;
  QString m_sceneId;
};
}

TEST_F(NdiManagerTest, SourceNames)
{
  Scene scene;
  scene.name = QString::fromUtf8("Show");
  EXPECT_EQ(QString::fromUtf8("Show - intro"), NdiManager::sourceName(scene, m_item));
  EXPECT_EQ(QString::fromUtf8("Show - multi - WriteB"), NdiManager::sourceName(scene, SceneItem::make(m_multi, QString::fromUtf8("WriteB"))));
}

TEST_F(NdiManagerTest, ChannelsFollowTheScenes)
{
  NdiManager *ndi = manager();
  QStringList changed;
  QObject::connect(ndi, &NdiManager::channelsChanged, [&](const QString &scene) { changed << scene; });

  EXPECT_EQ(nullptr, ndi->channel(m_sceneId, m_item)); // NDI off

  m_scenes.setNdiEnabled(m_sceneId, true);
  PlayoutChannel *channel = ndi->channel(m_sceneId, m_item);
  ASSERT_NE(nullptr, channel);
  EXPECT_EQ(QString::fromUtf8("Show - intro"), channel->sourceName());
  EXPECT_FALSE(channel->alpha());
  EXPECT_EQ(QStringList() << m_sceneId, changed);
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(QString::fromUtf8("Show - intro")).opened >= 1; }));

  // Alpha or a new name: a new source.
  m_scenes.setNdiAlpha(m_sceneId, true);
  ASSERT_NE(nullptr, ndi->channel(m_sceneId, m_item));
  EXPECT_TRUE(ndi->channel(m_sceneId, m_item)->alpha());
  m_scenes.renameScene(m_sceneId, QString::fromUtf8("Live Show"));
  EXPECT_EQ(QString::fromUtf8("Live Show - intro"), ndi->channel(m_sceneId, m_item)->sourceName());

  // Items added and removed.
  const QString a = SceneItem::make(m_multi, QString::fromUtf8("WriteA"));
  m_scenes.addItems(m_sceneId, QStringList() << a);
  ASSERT_NE(nullptr, ndi->channel(m_sceneId, a));
  EXPECT_EQ(QString::fromUtf8("Live Show - multi - WriteA"), ndi->channel(m_sceneId, a)->sourceName());
  m_scenes.removeItems(m_sceneId, QStringList() << a);
  EXPECT_EQ(nullptr, ndi->channel(m_sceneId, a));

  m_scenes.setNdiEnabled(m_sceneId, false);
  EXPECT_EQ(nullptr, ndi->channel(m_sceneId, m_item));
}

TEST_F(NdiManagerTest, ChannelsGetTheSceneSettings)
{
  m_scenes.setNdiEnabled(m_sceneId, true);
  m_scenes.setNdiCuePoints(m_sceneId, m_item, QList<double>() << 2 << 1);
  m_scenes.setNdiLoop(m_sceneId, m_item, true);
  NdiManager *ndi = manager(); // existing scenes get channels at once
  PlayoutChannel *channel = ndi->channel(m_sceneId, m_item);
  ASSERT_NE(nullptr, channel);
  EXPECT_EQ(QList<double>() << 1 << 2, channel->status().pausePoints);
  EXPECT_TRUE(channel->status().loop);

  m_scenes.setNdiPauseAt(m_sceneId, m_item, 0.5);
  m_scenes.setNdiLoop(m_sceneId, m_item, false);
  EXPECT_EQ(QList<double>() << 0.5, channel->status().pausePoints);
  EXPECT_FALSE(channel->status().loop);
}

TEST_F(NdiManagerTest, MediaOfTheRender)
{
  NdiManager *ndi = manager();
  m_scenes.setNdiEnabled(m_sceneId, true);
  PlayoutChannel *channel = ndi->channel(m_sceneId, m_item);
  ASSERT_NE(nullptr, channel);
  EXPECT_EQ(QString::fromUtf8("not rendered yet (out.png not found)"), channel->status().message);

  // A finished render is what plays now.
  render();
  RenderRecord done;
  done.status = RenderRecord::eDone;
  m_scenes.setRenderRecord(m_sceneId, m_item, done);
  EXPECT_TRUE(channel->status().message.isEmpty());
  EXPECT_EQ(1, channel->status().frameCount);
  EXPECT_DOUBLE_EQ(25.0, channel->status().fps); // the project's
}

TEST_F(NdiManagerTest, WriteNodeWithoutOutput)
{
  const QString b = SceneItem::make(m_multi, QString::fromUtf8("WriteB"));
  m_scenes.addItems(m_sceneId, QStringList() << b);
  m_scenes.setNdiEnabled(m_sceneId, true);
  NdiManager *ndi = manager();
  ASSERT_NE(nullptr, ndi->channel(m_sceneId, b));
  EXPECT_EQ(QString::fromUtf8("the Write node has no output file"), ndi->channel(m_sceneId, b)->status().message);
}

TEST_F(NdiManagerTest, ChannelStatusIsForwarded)
{
  NdiManager *ndi = manager();
  QStringList changed;
  QObject::connect(ndi, &NdiManager::channelStatusChanged,
                   [&](const QString &scene, const QString &item) { changed << scene + QLatin1Char('|') + item; });
  m_scenes.setNdiEnabled(m_sceneId, true);
  ASSERT_TRUE(waitFor([&]() { return !changed.isEmpty(); }));
  EXPECT_EQ(m_sceneId + QLatin1Char('|') + m_item, changed.first());
}

TEST_F(NdiManagerTest, HttpList)
{
  NdiManager *ndi = manager();
  QVariantMap json;
  EXPECT_EQ(200, http("GET", QByteArray(), &json));
  EXPECT_TRUE(json.value(QString::fromUtf8("sources")).toList().isEmpty());

  m_scenes.setNdiEnabled(m_sceneId, true);
  m_scenes.setNdiCuePoints(m_sceneId, m_item, QList<double>() << 1.5);
  EXPECT_EQ(200, http("GET", QByteArray(), &json));
  const QVariantMap s = source(json);
  EXPECT_EQ(QString::fromUtf8("Show - intro"), s.value(QString::fromUtf8("source")).toString());
  EXPECT_EQ(QString::fromUtf8("Show"), s.value(QString::fromUtf8("scene")).toString());
  EXPECT_EQ(QString::fromUtf8("intro"), s.value(QString::fromUtf8("project")).toString());
  EXPECT_EQ(QString::fromUtf8("Write1"), s.value(QString::fromUtf8("writer")).toString());
  EXPECT_EQ(QString::fromUtf8("stopped"), s.value(QString::fromUtf8("state")).toString());
  EXPECT_DOUBLE_EQ(1.5, s.value(QString::fromUtf8("pauseAt")).toDouble());
  EXPECT_EQ(1, s.value(QString::fromUtf8("cuePoints")).toList().size());
  EXPECT_TRUE(s.contains(QString::fromUtf8("message"))); // not rendered yet

  int status = 0;
  QByteArray response;
  EXPECT_FALSE(ndi->handleHttpRequest("GET", "/other", QByteArray(), &status, &response));
}

TEST_F(NdiManagerTest, HttpRejections)
{
  manager();
  QVariantMap json;
  EXPECT_EQ(405, http("DELETE", QByteArray(), &json));
  EXPECT_EQ(404, http("POST", "{\"scene\":\"Nope\"}", &json));
  EXPECT_EQ(404, http("POST", "not json", &json));
  EXPECT_EQ(400, http("POST", "{\"scene\":\"Show\",\"action\":\"play\"}", &json)); // NDI off
  EXPECT_EQ(QString::fromUtf8("NDI output is not enabled for this scene"), json.value(QString::fromUtf8("error")).toString());

  m_scenes.setNdiEnabled(m_sceneId, true);
  EXPECT_EQ(404, http("POST", "{\"scene\":\"Show\",\"project\":\"other\"}", &json));
  EXPECT_EQ(404, http("POST", "{\"scene\":\"Show\",\"writer\":\"Write9\"}", &json));
  EXPECT_EQ(400, http("POST", "{\"scene\":\"Show\",\"action\":\"dance\"}", &json));
}

TEST_F(NdiManagerTest, HttpSettings)
{
  manager();
  m_scenes.setNdiEnabled(m_sceneId, true);
  QVariantMap json;
  // By scene id, project by name.
  EXPECT_EQ(200, http("POST", "{\"scene\":\"" + m_sceneId.toUtf8() + "\",\"project\":\"intro\",\"cuePoints\":[3,1],\"loop\":true}", &json));
  Scene scene;
  ASSERT_TRUE(m_scenes.scene(m_sceneId, &scene));
  EXPECT_EQ(QList<double>() << 1 << 3, scene.cuePoints(m_item));
  EXPECT_TRUE(scene.ndiLoop.value(m_item));
  EXPECT_EQ(QString::fromUtf8("Show"), json.value(QString::fromUtf8("scene")).toString());
  EXPECT_TRUE(source(json).value(QString::fromUtf8("loop")).toBool());

  // Project by path; pauseAt null: none.
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"project\":\"" + m_project.toUtf8() + "\",\"pauseAt\":null}", &json));
  ASSERT_TRUE(m_scenes.scene(m_sceneId, &scene));
  EXPECT_TRUE(scene.cuePoints(m_item).isEmpty());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"writer\":\"Write1\",\"pauseAt\":2}", &json));
  ASSERT_TRUE(m_scenes.scene(m_sceneId, &scene));
  EXPECT_EQ(QList<double>() << 2, scene.cuePoints(m_item));
}

#ifdef Q_OS_UNIX
TEST_F(NdiManagerTest, HttpAndSceneTransport)
{
  render();
  m_scenes.setNdiEnabled(m_sceneId, true);
  NdiManager *ndi = manager();
  PlayoutChannel *channel = ndi->channel(m_sceneId, m_item);
  ASSERT_NE(nullptr, channel);
  ASSERT_EQ(1, channel->status().frameCount);

  QVariantMap json;
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"play\"}", &json));
  EXPECT_EQ(QString::fromUtf8("playing"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"pause\"}", &json));
  EXPECT_EQ(QString::fromUtf8("paused"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"continue\"}", &json));
  EXPECT_EQ(QString::fromUtf8("playing"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"stop\"}", &json));
  EXPECT_EQ(QString::fromUtf8("stopped"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"continue\"}", &json)); // only paused ones
  EXPECT_EQ(QString::fromUtf8("stopped"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"replay\"}", &json));
  EXPECT_EQ(QString::fromUtf8("playing"), source(json).value(QString::fromUtf8("state")).toString());
  EXPECT_EQ(200, http("POST", "{\"scene\":\"Show\",\"action\":\"cue\"}", &json));
  EXPECT_EQ(QString::fromUtf8("playing"), source(json).value(QString::fromUtf8("state")).toString());

  ndi->stopAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::eStopped, channel->status().state);
  ndi->playAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::ePlaying, channel->status().state);
  ndi->pauseAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::ePaused, channel->status().state);
  ndi->continueAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::ePlaying, channel->status().state);
  ndi->stopAll(m_sceneId);
  ndi->continueAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::eStopped, channel->status().state);
  ndi->replayAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::ePlaying, channel->status().state);
  ndi->cueAll(m_sceneId);
  EXPECT_EQ(PlayoutChannel::ePlaying, channel->status().state);
  ndi->pauseAll(QString::fromUtf8("unknown")); // no channels: nothing
  EXPECT_EQ(PlayoutChannel::ePlaying, channel->status().state);
}
#endif

TEST_F(NdiManagerTest, LiveScenesRerenderWhenTheirValuesChange)
{
  m_scenes.setNdiEnabled(m_sceneId, true);
  m_scenes.setNdiLive(m_sceneId, true);
  NdiManager *ndi = manager(true);
  StateStore state;
  ndi->setStateStore(&state);

  // A key no project uses: nothing.
  state.set(QString::fromUtf8("unused"), QString::fromUtf8("x"));
  processEventsFor(600);
  EXPECT_EQ(RenderRecord::eNone, m_scenes.renderRecord(m_sceneId, m_item).status);

  // A key the project binds: re-rendered (the renderer is missing: it fails).
  state.set(QString::fromUtf8("player"), QString::fromUtf8("COPPER"));
  ASSERT_TRUE(waitFor([&]() { return m_scenes.renderRecord(m_sceneId, m_item).status == RenderRecord::eFailed; }));
  EXPECT_TRUE(m_scenes.renderRecord(m_sceneId, m_item).message.startsWith(QString::fromUtf8("NatronRenderer not found")));

  // Renaming the key to another store key changes the values used too.
  m_scenes.setRenderRecord(m_sceneId, m_item, RenderRecord());
  m_scenes.setKeyMapping(m_sceneId, QString::fromUtf8("player"), QString::fromUtf8("player2"));
  EXPECT_EQ(RenderRecord::eFailed, m_scenes.renderRecord(m_sceneId, m_item).status);

  // The renamed key is the one followed now.
  m_scenes.setRenderRecord(m_sceneId, m_item, RenderRecord());
  state.set(QString::fromUtf8("player"), QString::fromUtf8("other"));
  processEventsFor(600);
  EXPECT_EQ(RenderRecord::eNone, m_scenes.renderRecord(m_sceneId, m_item).status);
  state.set(QString::fromUtf8("player2"), QString::fromUtf8("x"));
  ASSERT_TRUE(waitFor([&]() { return m_scenes.renderRecord(m_sceneId, m_item).status == RenderRecord::eFailed; }));

  // Detached: changes are not followed.
  ndi->setStateStore(nullptr);
  m_scenes.setRenderRecord(m_sceneId, m_item, RenderRecord());
  state.set(QString::fromUtf8("player2"), QString::fromUtf8("y"));
  processEventsFor(600);
  EXPECT_EQ(RenderRecord::eNone, m_scenes.renderRecord(m_sceneId, m_item).status);
}

TEST_F(NdiManagerTest, PlayoutScenesDoNotRerender)
{
  m_scenes.setNdiEnabled(m_sceneId, true); // not live
  NdiManager *ndi = manager(true);
  StateStore state;
  ndi->setStateStore(&state);
  state.set(QString::fromUtf8("player"), QString::fromUtf8("COPPER"));
  processEventsFor(600);
  EXPECT_EQ(RenderRecord::eNone, m_scenes.renderRecord(m_sceneId, m_item).status);
  ndi->setStateStore(nullptr);
}
