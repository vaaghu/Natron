#include "TestUtil.h"

#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>

#include "gtest/gtest.h"

using TestUtil::waitFor;

namespace
{
// Records the store's signals while it exists.
struct Signals
{
  QObject context;
  QStringList valueChanged;
  int keysChanged;

  explicit Signals(StateStore *store)
      : keysChanged(0)
  {
    QObject::connect(store, &StateStore::valueChanged, &context, [this](const QString &key) { valueChanged << key; });
    QObject::connect(store, &StateStore::keysChanged, &context, [this]() { ++keysChanged; });
  }
};

QVariantMap savedObject(const QString &path)
{
  return Json::parse(TestUtil::readFile(path), nullptr).toMap();
}
}

TEST(StateStore, StartsEmpty)
{
  StateStore store;
  EXPECT_EQ(0, store.size());
  EXPECT_TRUE(store.keys().isEmpty());
  EXPECT_FALSE(store.has(QString::fromUtf8("a")));
  EXPECT_FALSE(store.get(QString::fromUtf8("a")).isValid());
  EXPECT_EQ(QVariant(7), store.get(QString::fromUtf8("a"), 7));
  EXPECT_TRUE(store.filePath().isEmpty());
}

TEST(StateStore, SetStoresTypedValues)
{
  StateStore store;
  store.set(QString::fromUtf8("name"), QString::fromUtf8("COPPER"));
  store.set(QString::fromUtf8("score"), Kv::makeNumber(3));
  store.set(QString::fromUtf8("plain"), 42);

  EXPECT_EQ(3, store.size());
  EXPECT_TRUE(store.has(QString::fromUtf8("name")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("COPPER")), store.get(QString::fromUtf8("name")));
  EXPECT_EQ(Kv::makeNumber(3), store.get(QString::fromUtf8("score")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("42")), store.get(QString::fromUtf8("plain")));
  QStringList keys = store.keys();
  keys.sort();
  EXPECT_EQ(QStringList() << QString::fromUtf8("name") << QString::fromUtf8("plain") << QString::fromUtf8("score"), keys);
}

TEST(StateStore, SignalsOnSetAndOverwrite)
{
  StateStore store;
  Signals s(&store);

  store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  EXPECT_EQ(QStringList() << QString::fromUtf8("a"), s.valueChanged);
  EXPECT_EQ(1, s.keysChanged);

  // Overwriting: the value changed, not the keys.
  store.set(QString::fromUtf8("a"), QString::fromUtf8("2"));
  EXPECT_EQ(2, s.valueChanged.size());
  EXPECT_EQ(1, s.keysChanged);
}

TEST(StateStore, Remove)
{
  StateStore store;
  store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  Signals s(&store);

  EXPECT_FALSE(store.remove(QString::fromUtf8("missing")));
  EXPECT_TRUE(s.valueChanged.isEmpty());
  EXPECT_EQ(0, s.keysChanged);

  EXPECT_TRUE(store.remove(QString::fromUtf8("a")));
  EXPECT_FALSE(store.has(QString::fromUtf8("a")));
  EXPECT_EQ(QStringList() << QString::fromUtf8("a"), s.valueChanged);
  EXPECT_EQ(1, s.keysChanged);
}

TEST(StateStore, Clear)
{
  StateStore store;
  {
    Signals s(&store);
    store.clear(); // empty: no signals
    EXPECT_TRUE(s.valueChanged.isEmpty());
    EXPECT_EQ(0, s.keysChanged);
  }

  store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  store.set(QString::fromUtf8("b"), QString::fromUtf8("2"));
  Signals s(&store);
  store.clear();
  EXPECT_EQ(0, store.size());
  QStringList changed = s.valueChanged;
  changed.sort();
  EXPECT_EQ(QStringList() << QString::fromUtf8("a") << QString::fromUtf8("b"), changed);
  EXPECT_EQ(1, s.keysChanged);
}

TEST(StateStore, ToText)
{
  EXPECT_EQ(QString::fromUtf8("On"), StateStore::toText(Kv::makeBool(true)));
  EXPECT_EQ(QString::fromUtf8("x"), StateStore::toText(Kv::makeText(QString::fromUtf8("x"))));
}

TEST(StateStore, LoadMissingFileSucceeds)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("sub/state.json"));
  StateStore store;
  EXPECT_TRUE(store.loadAndAutoSave(path));
  EXPECT_EQ(path, store.filePath());
  EXPECT_EQ(0, store.size());
}

TEST(StateStore, SaveNowWritesAtomically)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("sub/state.json"));
  StateStore store;
  ASSERT_TRUE(store.loadAndAutoSave(path));
  store.set(QString::fromUtf8("name"), QString::fromUtf8("COPPER"));
  store.set(QString::fromUtf8("score"), Kv::makeNumber(3));
  ASSERT_TRUE(store.saveNow());

  EXPECT_FALSE(QFile::exists(path + QString::fromUtf8(".tmp")));
  const QVariantMap saved = savedObject(path);
  EXPECT_EQ(2, saved.size());
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("COPPER")), saved.value(QString::fromUtf8("name")));
  EXPECT_EQ(QString::fromUtf8("number"), saved.value(QString::fromUtf8("score")).toMap().value(QString::fromUtf8("type")).toString());
}

TEST(StateStore, SaveNowWithoutFileSucceeds)
{
  StateStore store;
  store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  EXPECT_TRUE(store.saveNow());
}

TEST(StateStore, AutoSavesChangesInABatch)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("state.json"));
  StateStore store;
  ASSERT_TRUE(store.loadAndAutoSave(path));
  store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  store.set(QString::fromUtf8("b"), QString::fromUtf8("2"));
  EXPECT_FALSE(QFile::exists(path)); // not yet: batched

  ASSERT_TRUE(waitFor([&]() { return savedObject(path).size() == 2; }));

  store.remove(QString::fromUtf8("a"));
  ASSERT_TRUE(waitFor([&]() { return savedObject(path).size() == 1; }));
  EXPECT_TRUE(savedObject(path).contains(QString::fromUtf8("b")));
}

TEST(StateStore, DestructorSavesPendingChanges)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("state.json"));
  {
    StateStore store;
    ASSERT_TRUE(store.loadAndAutoSave(path));
    store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  }
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("1")), savedObject(path).value(QString::fromUtf8("a")));
}

TEST(StateStore, LoadsSavedAndOlderPlainValues)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("state.json"));
  ASSERT_TRUE(TestUtil::writeFile(path, "{\"typed\":{\"type\":\"number\",\"value\":3},"
                                        "\"plain\":\"COPPER\",\"count\":7,\"object\":{\"x\":1}}"));
  StateStore store;
  store.set(QString::fromUtf8("old"), QString::fromUtf8("replaced"));
  Signals s(&store);
  ASSERT_TRUE(store.loadAndAutoSave(path));

  EXPECT_EQ(Kv::makeNumber(3), store.get(QString::fromUtf8("typed")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("COPPER")), store.get(QString::fromUtf8("plain")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("7")), store.get(QString::fromUtf8("count")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("{\"x\":1}")), store.get(QString::fromUtf8("object")));
  EXPECT_EQ(4, s.valueChanged.size());
  EXPECT_EQ(1, s.keysChanged);
}

TEST(StateStore, RoundTripsThroughFile)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("state.json"));
  {
    StateStore store;
    ASSERT_TRUE(store.loadAndAutoSave(path));
    store.set(QString::fromUtf8("t"), QString::fromUtf8("text"));
    store.set(QString::fromUtf8("n"), Kv::makeNumber(2.5));
    store.set(QString::fromUtf8("b"), Kv::makeBool(true));
    store.set(QString::fromUtf8("c"), Kv::makeColor(QString::fromUtf8("#112233")));
    ASSERT_TRUE(store.saveNow());
  }
  StateStore loaded;
  ASSERT_TRUE(loaded.loadAndAutoSave(path));
  EXPECT_EQ(4, loaded.size());
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("text")), loaded.get(QString::fromUtf8("t")));
  EXPECT_EQ(Kv::makeNumber(2.5), loaded.get(QString::fromUtf8("n")));
  EXPECT_EQ(Kv::makeBool(true), loaded.get(QString::fromUtf8("b")));
  EXPECT_EQ(Kv::makeColor(QString::fromUtf8("#112233")), loaded.get(QString::fromUtf8("c")));
}

TEST(StateStore, RejectsInvalidFiles)
{
  QTemporaryDir dir;
  const QString broken = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("broken.json"));
  const QString array = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("array.json"));
  ASSERT_TRUE(TestUtil::writeFile(broken, "{\"a\":"));
  ASSERT_TRUE(TestUtil::writeFile(array, "[1,2]"));

  StateStore store;
  EXPECT_FALSE(store.loadAndAutoSave(broken));
  EXPECT_FALSE(store.loadAndAutoSave(array));
  EXPECT_EQ(0, store.size());
}

TEST(StateStore, RefreshesImagesWhenTheirFileChanges)
{
  QTemporaryDir dir;
  const QString image = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("player.png"));
  ASSERT_TRUE(TestUtil::writeImage(image, 4, 2));

  StateStore store;
  QVariantMap input;
  input.insert(QString::fromUtf8("type"), QString::fromUtf8("image"));
  input.insert(QString::fromUtf8("path"), image);
  store.set(QString::fromUtf8("photo"), input);
  store.set(QString::fromUtf8("photo2"), input);
  ASSERT_EQ(4, Kv::imageInfo(store.get(QString::fromUtf8("photo"))).width);

  Signals s(&store);
  ASSERT_TRUE(TestUtil::writeImage(image, 8, 8));
  ASSERT_TRUE(waitFor([&]() { return Kv::imageInfo(store.get(QString::fromUtf8("photo"))).width == 8; }));
  EXPECT_EQ(8, Kv::imageInfo(store.get(QString::fromUtf8("photo2"))).height);
  EXPECT_TRUE(s.valueChanged.contains(QString::fromUtf8("photo")));
  EXPECT_TRUE(s.valueChanged.contains(QString::fromUtf8("photo2")));
  EXPECT_EQ(0, s.keysChanged);
}
