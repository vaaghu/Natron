#include "TestUtil.h"

#include "Custom/scene/SceneStore.h"
#include "Custom/state/Json.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
// Records the store's signals while it exists.
struct Signals
{
  QObject context;
  int scenesChanged;
  QStringList renderRecordChanged; // "<scene>|<item>"
  QStringList ndiSettingsChanged;

  explicit Signals(SceneStore *store)
      : scenesChanged(0)
  {
    QObject::connect(store, &SceneStore::scenesChanged, &context, [this]() { ++scenesChanged; });
    QObject::connect(store, &SceneStore::renderRecordChanged, &context,
                     [this](const QString &scene, const QString &item) { renderRecordChanged << scene + QLatin1Char('|') + item; });
    QObject::connect(store, &SceneStore::ndiSettingsChanged, &context,
                     [this](const QString &scene, const QString &item) { ndiSettingsChanged << scene + QLatin1Char('|') + item; });
  }
};

Scene sceneOf(const SceneStore &store, const QString &id)
{
  Scene scene;
  EXPECT_TRUE(store.scene(id, &scene));
  return scene;
}

RenderRecord record(RenderRecord::Status status, const QString &message = QString())
{
  RenderRecord r;
  r.status = status;
  r.message = message;
  r.time = QDateTime(QDate(2026, 10, 1), QTime(12, 30, 0));
  r.output = QString::fromUtf8("/out/a.mov");
  r.thumbnail = QString::fromUtf8("/thumbs/a.png");
  return r;
}

class SceneStoreTest : public ::testing::Test
{
protected:
  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }

  // A project with two Write nodes.
  QString twoWriterProject()
  {
    const QString project = path(QString::fromUtf8("two.ntp"));
    EXPECT_TRUE(writeProject(project, QList<Node>() << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("a.mov"))
                                                    << writeNode(QString::fromUtf8("Write2"), QString::fromUtf8("b.mov"))));
    return project;
  }

  QTemporaryDir m_dir;
};
}

TEST(SceneItem, MakeAndSplit)
{
  const QString item = SceneItem::make(QString::fromUtf8("/a#b/p.ntp"), QString::fromUtf8("Group1.Write1"));
  EXPECT_EQ(QString::fromUtf8("/a#b/p.ntp#Group1.Write1"), item);
  EXPECT_EQ(QString::fromUtf8("/a#b/p.ntp"), SceneItem::project(item));
  EXPECT_EQ(QString::fromUtf8("Group1.Write1"), SceneItem::writer(item));

  EXPECT_EQ(QString::fromUtf8("/p.ntp"), SceneItem::project(QString::fromUtf8("/p.ntp")));
  EXPECT_TRUE(SceneItem::writer(QString::fromUtf8("/p.ntp")).isEmpty());
  EXPECT_EQ(QString::fromUtf8("/p.ntp"), SceneItem::project(SceneItem::make(QString::fromUtf8("/p.ntp"), QString())));
  EXPECT_TRUE(SceneItem::writer(SceneItem::make(QString::fromUtf8("/p.ntp"), QString())).isEmpty());
}

TEST_F(SceneStoreTest, AllOfListsEveryWriteNode)
{
  const QString project = twoWriterProject();
  EXPECT_EQ(QStringList() << SceneItem::make(project, QString::fromUtf8("Write1")) << SceneItem::make(project, QString::fromUtf8("Write2")),
            SceneItem::allOf(project));

  const QString none = path(QString::fromUtf8("none.ntp"));
  ASSERT_TRUE(writeProject(none, QList<Node>() << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("a"))));
  EXPECT_EQ(QStringList() << SceneItem::make(none, QString()), SceneItem::allOf(none));
}

TEST(LayerPlacement, DefaultsAndComparison)
{
  LayerPlacement p;
  EXPECT_TRUE(p.isDefault());
  EXPECT_TRUE(p.isDefaultFrame());
  p.start = 2;
  EXPECT_FALSE(p.isDefault());
  EXPECT_TRUE(p.isDefaultFrame());
  LayerPlacement q;
  EXPECT_TRUE(p != q);
  q.start = 2;
  EXPECT_TRUE(p == q);
  q.scale = 0.5;
  EXPECT_FALSE(q.isDefaultFrame());
}

TEST(SceneStruct, Helpers)
{
  Scene scene;
  scene.items << QString::fromUtf8("/a.ntp#W1") << QString::fromUtf8("/b.ntp#W1") << QString::fromUtf8("/a.ntp#W2");
  EXPECT_EQ(QStringList() << QString::fromUtf8("/a.ntp") << QString::fromUtf8("/b.ntp"), scene.projects());

  EXPECT_DOUBLE_EQ(-1.0, scene.pauseAt(QString::fromUtf8("/a.ntp#W1")));
  scene.ndiCuePoints.insert(QString::fromUtf8("/a.ntp#W1"), QList<double>() << 1.5 << 3);
  EXPECT_DOUBLE_EQ(1.5, scene.pauseAt(QString::fromUtf8("/a.ntp#W1")));
  EXPECT_EQ(QList<double>() << 1.5 << 3, scene.cuePoints(QString::fromUtf8("/a.ntp#W1")));

  scene.keyMap.insert(QString::fromUtf8("Player1"), QString::fromUtf8("Player2"));
  EXPECT_EQ(QString::fromUtf8("Player2"), scene.mappedKey(QString::fromUtf8("Player1")));
  EXPECT_EQ(QString::fromUtf8("Other"), scene.mappedKey(QString::fromUtf8("Other")));
}

TEST_F(SceneStoreTest, CreateRenameRemove)
{
  SceneStore store;
  Signals s(&store);
  const QString id = store.createScene(QString::fromUtf8("Intro"));
  EXPECT_FALSE(id.isEmpty());
  EXPECT_EQ(1, s.scenesChanged);
  EXPECT_EQ(QString::fromUtf8("Intro"), sceneOf(store, id).name);
  EXPECT_FALSE(sceneOf(store, id).ndiEnabled);

  const QString other = store.createScene(QString::fromUtf8("Outro"));
  EXPECT_NE(id, other);
  ASSERT_EQ(2, store.scenes().size());
  EXPECT_EQ(id, store.scenes().at(0).id);

  store.renameScene(id, QString::fromUtf8("Intro")); // same name: nothing
  EXPECT_EQ(2, s.scenesChanged);
  store.renameScene(id, QString::fromUtf8("Opening"));
  EXPECT_EQ(3, s.scenesChanged);
  EXPECT_EQ(QString::fromUtf8("Opening"), sceneOf(store, id).name);
  store.renameScene(QString::fromUtf8("unknown"), QString::fromUtf8("x"));
  EXPECT_EQ(3, s.scenesChanged);

  store.setRenderRecord(id, QString::fromUtf8("/p.ntp#W1"), record(RenderRecord::eDone));
  store.setRenderRecord(other, QString::fromUtf8("/p.ntp#W1"), record(RenderRecord::eDone));
  store.removeScene(id);
  EXPECT_EQ(4, s.scenesChanged);
  Scene gone;
  EXPECT_FALSE(store.scene(id, &gone));
  EXPECT_EQ(RenderRecord::eNone, store.renderRecord(id, QString::fromUtf8("/p.ntp#W1")).status);
  EXPECT_EQ(RenderRecord::eDone, store.renderRecord(other, QString::fromUtf8("/p.ntp#W1")).status);
  store.removeScene(id);
  EXPECT_EQ(4, s.scenesChanged);
}

TEST_F(SceneStoreTest, AddAndRemoveItems)
{
  SceneStore store;
  const QString id = store.createScene(QString::fromUtf8("S"));
  Signals s(&store);

  const QString a = SceneItem::make(path(QString::fromUtf8("a.ntp")), QString::fromUtf8("Write1"));
  const QString b = SceneItem::make(path(QString::fromUtf8("b.ntp")), QString::fromUtf8("Write1"));
  store.addItems(id, QStringList() << a << b << a);
  EXPECT_EQ(QStringList() << a << b, sceneOf(store, id).items);
  EXPECT_EQ(1, s.scenesChanged);

  store.addItems(id, QStringList() << b); // already there
  EXPECT_EQ(1, s.scenesChanged);

  // Relative projects become absolute.
  store.addItems(id, QStringList() << QString::fromUtf8("rel/c.ntp#W"));
  EXPECT_EQ(SceneItem::make(QDir::current().absoluteFilePath(QString::fromUtf8("rel/c.ntp")), QString::fromUtf8("W")),
            sceneOf(store, id).items.last());

  store.removeItems(id, QStringList() << a << QString::fromUtf8("missing"));
  EXPECT_FALSE(sceneOf(store, id).items.contains(a));
  EXPECT_EQ(3, s.scenesChanged);
  store.removeItems(id, QStringList() << QString::fromUtf8("missing"));
  EXPECT_EQ(3, s.scenesChanged);

  store.addItems(QString::fromUtf8("unknown"), QStringList() << a);
  store.removeItems(QString::fromUtf8("unknown"), QStringList() << a);
  EXPECT_EQ(3, s.scenesChanged);
}

TEST_F(SceneStoreTest, KeyMappingAndOutputDir)
{
  SceneStore store;
  const QString id = store.createScene(QString::fromUtf8("S"));
  Signals s(&store);

  store.setKeyMapping(id, QString::fromUtf8("Player1"), QString::fromUtf8(" Player2 "));
  EXPECT_EQ(QString::fromUtf8("Player2"), sceneOf(store, id).keyMap.value(QString::fromUtf8("Player1")));
  EXPECT_EQ(1, s.scenesChanged);
  store.setKeyMapping(id, QString::fromUtf8("Player1"), QString::fromUtf8("Player2")); // unchanged
  EXPECT_EQ(1, s.scenesChanged);
  store.setKeyMapping(id, QString::fromUtf8("Player1"), QString::fromUtf8("Player1")); // same key: removed
  EXPECT_TRUE(sceneOf(store, id).keyMap.isEmpty());
  EXPECT_EQ(2, s.scenesChanged);
  store.setKeyMapping(id, QString::fromUtf8("Player1"), QString()); // nothing to remove
  store.setKeyMapping(id, QString(), QString::fromUtf8("x"));      // no key
  EXPECT_EQ(2, s.scenesChanged);

  store.setOutputDir(id, QString::fromUtf8("/renders"));
  EXPECT_EQ(QString::fromUtf8("/renders"), sceneOf(store, id).outputDir);
  store.setOutputDir(id, QString::fromUtf8("/renders"));
  EXPECT_EQ(3, s.scenesChanged);
}

TEST_F(SceneStoreTest, NdiSceneOptions)
{
  SceneStore store;
  const QString id = store.createScene(QString::fromUtf8("S"));
  Signals s(&store);

  store.setNdiEnabled(id, true);
  store.setNdiAlpha(id, true);
  store.setNdiLive(id, true);
  EXPECT_EQ(3, s.scenesChanged);
  const Scene scene = sceneOf(store, id);
  EXPECT_TRUE(scene.ndiEnabled);
  EXPECT_TRUE(scene.ndiAlpha);
  EXPECT_TRUE(scene.ndiLive);

  store.setNdiEnabled(id, true);
  store.setNdiAlpha(id, true);
  store.setNdiLive(id, true);
  EXPECT_EQ(3, s.scenesChanged);
}

TEST_F(SceneStoreTest, CuePointsAndLoop)
{
  SceneStore store;
  const QString id = store.createScene(QString::fromUtf8("S"));
  const QString item = QString::fromUtf8("/p.ntp#W1");
  Signals s(&store);

  store.setNdiCuePoints(id, item, QList<double>() << 3 << -1 << 1.5 << 3);
  EXPECT_EQ(QList<double>() << 1.5 << 3, sceneOf(store, id).cuePoints(item));
  EXPECT_EQ(QStringList() << id + QString::fromUtf8("|") + item, s.ndiSettingsChanged);
  EXPECT_EQ(0, s.scenesChanged); // views need no rebuilding

  store.setNdiCuePoints(id, item, QList<double>() << 1.5 << 3); // unchanged
  EXPECT_EQ(1, s.ndiSettingsChanged.size());

  store.setNdiPauseAt(id, item, 2);
  EXPECT_EQ(QList<double>() << 2, sceneOf(store, id).cuePoints(item));
  store.setNdiPauseAt(id, item, -1);
  EXPECT_FALSE(sceneOf(store, id).ndiCuePoints.contains(item));
  EXPECT_EQ(3, s.ndiSettingsChanged.size());

  store.setNdiLoop(id, item, false); // already off
  EXPECT_EQ(3, s.ndiSettingsChanged.size());
  store.setNdiLoop(id, item, true);
  EXPECT_TRUE(sceneOf(store, id).ndiLoop.value(item));
  EXPECT_EQ(4, s.ndiSettingsChanged.size());

  store.setNdiCuePoints(QString::fromUtf8("unknown"), item, QList<double>() << 1);
  store.setNdiLoop(QString::fromUtf8("unknown"), item, true);
  EXPECT_EQ(4, s.ndiSettingsChanged.size());
}

TEST_F(SceneStoreTest, LayerPlacements)
{
  SceneStore store;
  const QString id = store.createScene(QString::fromUtf8("S"));
  const QString item = QString::fromUtf8("/p.ntp#W1");
  Signals s(&store);

  LayerPlacement p;
  p.x = 0.25;
  p.scale = 0.5;
  store.setLayerPlacement(id, item, p);
  EXPECT_TRUE(sceneOf(store, id).layerPlacements.value(item) == p);
  EXPECT_EQ(0, s.scenesChanged); // saved, no signal

  store.setLayerPlacement(id, item, LayerPlacement());
  EXPECT_FALSE(sceneOf(store, id).layerPlacements.contains(item));
}

TEST_F(SceneStoreTest, RenderRecords)
{
  SceneStore store;
  Signals s(&store);
  EXPECT_EQ(RenderRecord::eNone, store.renderRecord(QString::fromUtf8("s"), QString::fromUtf8("i")).status);
  store.setRenderRecord(QString::fromUtf8("s"), QString::fromUtf8("i"), record(RenderRecord::eFailed, QString::fromUtf8("boom")));
  EXPECT_EQ(RenderRecord::eFailed, store.renderRecord(QString::fromUtf8("s"), QString::fromUtf8("i")).status);
  EXPECT_EQ(QString::fromUtf8("boom"), store.renderRecord(QString::fromUtf8("s"), QString::fromUtf8("i")).message);
  EXPECT_EQ(QStringList() << QString::fromUtf8("s|i"), s.renderRecordChanged);
}

TEST_F(SceneStoreTest, SnapshotAndRestore)
{
  SceneStore store;
  const QString first = store.createScene(QString::fromUtf8("First"));
  const QString id = store.createScene(QString::fromUtf8("S"));
  store.createScene(QString::fromUtf8("Last"));
  store.addItems(id, QStringList() << QString::fromUtf8("/p.ntp#W1"));
  store.setRenderRecord(id, QString::fromUtf8("/p.ntp#W1"), record(RenderRecord::eDone));
  store.setRenderRecord(first, QString::fromUtf8("/p.ntp#W1"), record(RenderRecord::eFailed));

  const SceneSnapshot snap = store.snapshot(id);
  EXPECT_TRUE(snap.valid);
  EXPECT_EQ(1, snap.index);
  EXPECT_EQ(1, snap.records.size());

  // Changed, then put back.
  store.renameScene(id, QString::fromUtf8("Renamed"));
  store.addItems(id, QStringList() << QString::fromUtf8("/q.ntp#W1"));
  store.setRenderRecord(id, QString::fromUtf8("/q.ntp#W1"), record(RenderRecord::eFailed));
  store.restore(snap);
  EXPECT_EQ(QString::fromUtf8("S"), sceneOf(store, id).name);
  EXPECT_EQ(QStringList() << QString::fromUtf8("/p.ntp#W1"), sceneOf(store, id).items);
  EXPECT_EQ(RenderRecord::eNone, store.renderRecord(id, QString::fromUtf8("/q.ntp#W1")).status);
  EXPECT_EQ(RenderRecord::eFailed, store.renderRecord(first, QString::fromUtf8("/p.ntp#W1")).status);

  // Removed, then re-created at its place with its records.
  store.removeScene(id);
  store.restore(snap);
  ASSERT_EQ(3, store.scenes().size());
  EXPECT_EQ(id, store.scenes().at(1).id);
  EXPECT_EQ(RenderRecord::eDone, store.renderRecord(id, QString::fromUtf8("/p.ntp#W1")).status);

  // Before it existed: restoring removes it.
  const SceneSnapshot none = store.snapshot(QString::fromUtf8("later"));
  EXPECT_FALSE(none.valid);
  EXPECT_EQ(-1, none.index);
  const SceneSnapshot before = store.snapshot(id);
  store.removeScene(id);
  SceneSnapshot missing = store.snapshot(id);
  store.restore(before);
  EXPECT_EQ(3, store.scenes().size());
  store.restore(missing);
  EXPECT_EQ(2, store.scenes().size());
}

TEST_F(SceneStoreTest, SavesAndLoadsEverything)
{
  const QString file = path(QString::fromUtf8("sub/scenes.json"));
  QString id;
  {
    SceneStore store;
    ASSERT_TRUE(store.loadAndAutoSave(file));
    id = store.createScene(QString::fromUtf8("Show"));
    store.addItems(id, QStringList() << QString::fromUtf8("/p.ntp#W1") << QString::fromUtf8("/p.ntp#W2"));
    store.setKeyMapping(id, QString::fromUtf8("k"), QString::fromUtf8("k2"));
    store.setOutputDir(id, QString::fromUtf8("/renders"));
    store.setNdiEnabled(id, true);
    store.setNdiAlpha(id, true);
    store.setNdiCuePoints(id, QString::fromUtf8("/p.ntp#W1"), QList<double>() << 1 << 2.5);
    store.setNdiLoop(id, QString::fromUtf8("/p.ntp#W2"), true);
    LayerPlacement p;
    p.x = 0.1;
    p.y = 0.2;
    p.scale = 0.3;
    p.start = 4;
    store.setLayerPlacement(id, QString::fromUtf8("/p.ntp#W1"), p);
    store.setRenderRecord(id, QString::fromUtf8("/p.ntp#W1"), record(RenderRecord::eDone, QString::fromUtf8("ok")));
    store.setRenderRecord(id, QString::fromUtf8("/p.ntp#W2"), record(RenderRecord::eRendering)); // not saved
    ASSERT_TRUE(store.saveNow());
    EXPECT_FALSE(QFile::exists(file + QString::fromUtf8(".tmp")));
  }

  SceneStore loaded;
  Signals s(&loaded);
  ASSERT_TRUE(loaded.loadAndAutoSave(file));
  EXPECT_EQ(1, s.scenesChanged);
  ASSERT_EQ(1, loaded.scenes().size());
  const Scene scene = loaded.scenes().first();
  EXPECT_EQ(id, scene.id);
  EXPECT_EQ(QString::fromUtf8("Show"), scene.name);
  EXPECT_EQ(QStringList() << QString::fromUtf8("/p.ntp#W1") << QString::fromUtf8("/p.ntp#W2"), scene.items);
  EXPECT_EQ(QString::fromUtf8("k2"), scene.keyMap.value(QString::fromUtf8("k")));
  EXPECT_EQ(QString::fromUtf8("/renders"), scene.outputDir);
  EXPECT_TRUE(scene.ndiEnabled);
  EXPECT_TRUE(scene.ndiAlpha);
  EXPECT_FALSE(scene.ndiLive);
  EXPECT_EQ(QList<double>() << 1 << 2.5, scene.cuePoints(QString::fromUtf8("/p.ntp#W1")));
  EXPECT_TRUE(scene.ndiLoop.value(QString::fromUtf8("/p.ntp#W2")));
  const LayerPlacement p = scene.layerPlacements.value(QString::fromUtf8("/p.ntp#W1"));
  EXPECT_DOUBLE_EQ(0.1, p.x);
  EXPECT_DOUBLE_EQ(0.2, p.y);
  EXPECT_DOUBLE_EQ(0.3, p.scale);
  EXPECT_DOUBLE_EQ(4, p.start);

  const RenderRecord done = loaded.renderRecord(id, QString::fromUtf8("/p.ntp#W1"));
  EXPECT_EQ(RenderRecord::eDone, done.status);
  EXPECT_EQ(QString::fromUtf8("ok"), done.message);
  EXPECT_EQ(QDateTime(QDate(2026, 10, 1), QTime(12, 30, 0)), done.time);
  EXPECT_EQ(QString::fromUtf8("/out/a.mov"), done.output);
  EXPECT_EQ(QString::fromUtf8("/thumbs/a.png"), done.thumbnail);
  EXPECT_EQ(RenderRecord::eNone, loaded.renderRecord(id, QString::fromUtf8("/p.ntp#W2")).status);
}

TEST_F(SceneStoreTest, AutoSavesAndSavesOnDestruction)
{
  const QString file = path(QString::fromUtf8("scenes.json"));
  {
    SceneStore store;
    ASSERT_TRUE(store.loadAndAutoSave(file));
    store.createScene(QString::fromUtf8("A"));
    EXPECT_FALSE(QFile::exists(file));
    ASSERT_TRUE(waitFor([&]() { return QFile::exists(file); }));
    store.createScene(QString::fromUtf8("B"));
  }
  const QVariantList scenes = Json::parse(readFile(file), nullptr).toMap().value(QString::fromUtf8("scenes")).toList();
  EXPECT_EQ(2, scenes.size());
}

TEST_F(SceneStoreTest, LoadMissingAndInvalidFiles)
{
  SceneStore store;
  EXPECT_TRUE(store.loadAndAutoSave(path(QString::fromUtf8("missing.json"))));
  EXPECT_TRUE(store.scenes().isEmpty());

  ASSERT_TRUE(writeFile(path(QString::fromUtf8("bad.json")), "{\"scenes\": ["));
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("list.json")), "[]"));
  SceneStore other;
  EXPECT_FALSE(other.loadAndAutoSave(path(QString::fromUtf8("bad.json"))));
  EXPECT_FALSE(other.loadAndAutoSave(path(QString::fromUtf8("list.json"))));
}

TEST_F(SceneStoreTest, LoadSanitizesValues)
{
  const QByteArray json =
      "{\"scenes\":["
      "{\"name\":\"no id: dropped\"},"
      "{\"id\":\"s1\",\"name\":\"S\",\"items\":[\"/p.ntp#W1\"],"
      " \"ndi\":{\"pauseAt\":{\"/p.ntp#W1\":[3,-1,1,3]}},"
      " \"layerPlacement\":{\"/p.ntp#W1\":{\"x\":0.2,\"scale\":0},\"/p.ntp#W2\":{\"start\":-5}}}],"
      "\"renders\":{\"old-without-scene\":{\"status\":\"done\"},\"s1|/p.ntp#W1\":{\"status\":\"weird\"}}}";
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("scenes.json")), json));

  SceneStore store;
  ASSERT_TRUE(store.loadAndAutoSave(path(QString::fromUtf8("scenes.json"))));
  ASSERT_EQ(1, store.scenes().size());
  const Scene scene = store.scenes().first();
  EXPECT_EQ(QList<double>() << 1 << 3, scene.cuePoints(QString::fromUtf8("/p.ntp#W1")));
  EXPECT_FALSE(scene.layerPlacements.contains(QString::fromUtf8("/p.ntp#W1"))); // scale 0
  EXPECT_DOUBLE_EQ(0, scene.layerPlacements.value(QString::fromUtf8("/p.ntp#W2")).start);
  EXPECT_DOUBLE_EQ(0.5, scene.layerPlacements.value(QString::fromUtf8("/p.ntp#W2")).x);
  EXPECT_EQ(RenderRecord::eNone, store.renderRecord(QString::fromUtf8("s1"), QString::fromUtf8("/p.ntp#W1")).status);
}

TEST_F(SceneStoreTest, MigratesProjectScenes)
{
  // Before scenes held Write nodes, they listed projects.
  const QString project = twoWriterProject();
  const QString item1 = SceneItem::make(project, QString::fromUtf8("Write1"));
  const QString item2 = SceneItem::make(project, QString::fromUtf8("Write2"));
  QVariantMap pauseAt;
  pauseAt.insert(project, 1.5); // one number per project
  QVariantMap loop;
  loop.insert(project, true);
  QVariantMap ndi;
  ndi.insert(QString::fromUtf8("enabled"), true);
  ndi.insert(QString::fromUtf8("pauseAt"), pauseAt);
  ndi.insert(QString::fromUtf8("loop"), loop);
  QVariantMap scene;
  scene.insert(QString::fromUtf8("id"), QString::fromUtf8("s1"));
  scene.insert(QString::fromUtf8("name"), QString::fromUtf8("Old"));
  scene.insert(QString::fromUtf8("projects"), QVariantList() << project);
  scene.insert(QString::fromUtf8("ndi"), ndi);
  QVariantMap done;
  done.insert(QString::fromUtf8("status"), QString::fromUtf8("done"));
  done.insert(QString::fromUtf8("output"), QString::fromUtf8("/out/a.mov"));
  QVariantMap renders;
  renders.insert(QString::fromUtf8("s1|") + project, done);
  QVariantMap root;
  root.insert(QString::fromUtf8("scenes"), QVariantList() << scene);
  root.insert(QString::fromUtf8("renders"), renders);
  const QString file = path(QString::fromUtf8("scenes.json"));
  ASSERT_TRUE(writeFile(file, Json::serialize(root)));

  {
    SceneStore store;
    ASSERT_TRUE(store.loadAndAutoSave(file));
    const Scene migrated = store.scenes().first();
    EXPECT_EQ(QStringList() << item1 << item2, migrated.items);
    EXPECT_EQ(QList<double>() << 1.5, migrated.cuePoints(item1));
    EXPECT_EQ(QList<double>() << 1.5, migrated.cuePoints(item2));
    EXPECT_TRUE(migrated.ndiLoop.value(item1));
    EXPECT_TRUE(migrated.ndiLoop.value(item2));
    // The project's record goes to its first Write node.
    EXPECT_EQ(RenderRecord::eDone, store.renderRecord(QString::fromUtf8("s1"), item1).status);
    EXPECT_EQ(QString::fromUtf8("/out/a.mov"), store.renderRecord(QString::fromUtf8("s1"), item1).output);
    EXPECT_EQ(RenderRecord::eNone, store.renderRecord(QString::fromUtf8("s1"), project).status);
    // Migrated: saved in the new form.
    ASSERT_TRUE(waitFor([&]() { return Json::parse(readFile(file), nullptr).toMap().value(QString::fromUtf8("scenes"))
                                           .toList().value(0).toMap().contains(QString::fromUtf8("items")); }));
  }
  SceneStore reloaded;
  ASSERT_TRUE(reloaded.loadAndAutoSave(file));
  EXPECT_EQ(QStringList() << item1 << item2, reloaded.scenes().first().items);
}
