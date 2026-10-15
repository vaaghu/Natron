#include "TestUtil.h"

#include "Custom/scene/SceneShortcuts.h"
#include "Custom/scene/SceneStore.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include "gtest/gtest.h"

namespace
{
QString keys(const char *text)
{
  return QKeySequence(QString::fromUtf8(text)).toString(QKeySequence::PortableText);
}

QList<Scene> scenes(int count)
{
  QList<Scene> out;
  for (int i = 0; i < count; ++i)
  {
    Scene scene;
    scene.id = QStringLiteral("s%1").arg(i + 1);
    scene.name = QStringLiteral("Scene %1").arg(i + 1);
    out << scene;
  }
  return out;
}

// The application's QSettings in a temporary folder while a test runs.
class SceneShortcutsSettingsTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_dir.isValid());
    m_organization = QCoreApplication::organizationName();
    m_application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("NatronCustomTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SceneShortcuts"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
  }

  void TearDown() override
  {
    QSettings::setDefaultFormat(QSettings::NativeFormat);
    QCoreApplication::setOrganizationName(m_organization);
    QCoreApplication::setApplicationName(m_application);
  }

  QTemporaryDir m_dir;
  QString m_organization;
  QString m_application;
};
}

TEST(SceneShortcuts, DefaultsAreCtrl1ToCtrl9)
{
  EXPECT_EQ(keys("Ctrl+1"), SceneShortcuts::defaultShortcut(0));
  EXPECT_EQ(keys("Ctrl+7"), SceneShortcuts::defaultShortcut(6));
  EXPECT_EQ(keys("Ctrl+9"), SceneShortcuts::defaultShortcut(8));
  EXPECT_TRUE(SceneShortcuts::defaultShortcut(9).isEmpty());
  EXPECT_TRUE(SceneShortcuts::defaultShortcut(-1).isEmpty());
}

TEST(SceneShortcuts, NothingSavedGivesTheFirstNineScenesTheirPlace)
{
  const QHash<QString, QString> out = SceneShortcuts::shortcuts(scenes(11), QVariantMap());

  EXPECT_EQ(9, out.size());
  EXPECT_EQ(keys("Ctrl+1"), out.value(QStringLiteral("s1")));
  EXPECT_EQ(keys("Ctrl+7"), out.value(QStringLiteral("s7")));
  EXPECT_EQ(keys("Ctrl+9"), out.value(QStringLiteral("s9")));
  EXPECT_FALSE(out.contains(QStringLiteral("s10")));
  EXPECT_FALSE(out.contains(QStringLiteral("s11")));
}

TEST(SceneShortcuts, NoScenesNoShortcuts)
{
  EXPECT_TRUE(SceneShortcuts::shortcuts(QList<Scene>(), QVariantMap()).isEmpty());
}

TEST(SceneShortcuts, SavedShortcutReplacesTheDefault)
{
  QVariantMap saved;
  saved.insert(QStringLiteral("s2"), keys("Ctrl+Alt+F2"));

  const QHash<QString, QString> out = SceneShortcuts::shortcuts(scenes(3), saved);

  EXPECT_EQ(keys("Ctrl+1"), out.value(QStringLiteral("s1")));
  EXPECT_EQ(keys("Ctrl+Alt+F2"), out.value(QStringLiteral("s2")));
  EXPECT_EQ(keys("Ctrl+3"), out.value(QStringLiteral("s3")));
}

TEST(SceneShortcuts, SavedEmptyMeansNone)
{
  QVariantMap saved;
  saved.insert(QStringLiteral("s1"), QString());

  const QHash<QString, QString> out = SceneShortcuts::shortcuts(scenes(2), saved);

  EXPECT_FALSE(out.contains(QStringLiteral("s1")));
  EXPECT_EQ(keys("Ctrl+2"), out.value(QStringLiteral("s2")));
}

TEST(SceneShortcuts, DefaultSkippedWhenAnotherSceneSavedIt)
{
  // Scene 7 was given Ctrl+3; scene 3, added since, does not get it too.
  QVariantMap saved;
  saved.insert(QStringLiteral("s7"), keys("Ctrl+3"));

  const QHash<QString, QString> out = SceneShortcuts::shortcuts(scenes(7), saved);

  EXPECT_EQ(keys("Ctrl+3"), out.value(QStringLiteral("s7")));
  EXPECT_FALSE(out.contains(QStringLiteral("s3")));
  EXPECT_EQ(keys("Ctrl+4"), out.value(QStringLiteral("s4")));
}

TEST(SceneShortcuts, SavedShortcutFollowsTheSceneWhenReordered)
{
  QList<Scene> list = scenes(3);
  QVariantMap saved;
  for (int i = 0; i < list.size(); ++i)
  {
    saved.insert(list.at(i).id, SceneShortcuts::defaultShortcut(i));
  }
  list.move(0, 2); // s2, s3, s1

  const QHash<QString, QString> out = SceneShortcuts::shortcuts(list, saved);

  EXPECT_EQ(keys("Ctrl+1"), out.value(QStringLiteral("s1")));
  EXPECT_EQ(keys("Ctrl+2"), out.value(QStringLiteral("s2")));
  EXPECT_EQ(keys("Ctrl+3"), out.value(QStringLiteral("s3")));
}

TEST(SceneShortcuts, SavedForRemovedScenesIgnored)
{
  QVariantMap saved;
  saved.insert(QStringLiteral("gone"), keys("Ctrl+1"));

  const QHash<QString, QString> out = SceneShortcuts::shortcuts(scenes(1), saved);

  EXPECT_FALSE(out.contains(QStringLiteral("gone")));
  EXPECT_EQ(keys("Ctrl+1"), out.value(QStringLiteral("s1")));
}

TEST(SceneShortcuts, NoConflicts)
{
  const QList<QString> rows = QList<QString>() << keys("Ctrl+1") << QString() << keys("Ctrl+2") << QString();

  EXPECT_TRUE(SceneShortcuts::conflicts(rows, QHash<QString, QString>()).isEmpty());
}

TEST(SceneShortcuts, SameKeysOnTwoRows)
{
  const QList<QString> rows = QList<QString>() << keys("Ctrl+7") << keys("Ctrl+1") << keys("Ctrl+7") << keys("Ctrl+7");

  const QList<SceneShortcuts::Conflict> out = SceneShortcuts::conflicts(rows, QHash<QString, QString>());

  ASSERT_EQ(2, out.size());
  EXPECT_EQ(2, out.at(0).row);
  EXPECT_EQ(0, out.at(0).otherRow);
  EXPECT_EQ(keys("Ctrl+7"), out.at(0).keys);
  EXPECT_EQ(3, out.at(1).row);
  EXPECT_EQ(0, out.at(1).otherRow);
}

TEST(SceneShortcuts, KeysTakenByAnotherAction)
{
  QHash<QString, QString> taken;
  taken.insert(keys("Ctrl+N"), QStringLiteral("\"New Scene\""));
  const QList<QString> rows = QList<QString>() << keys("Ctrl+1") << keys("Ctrl+N");

  const QList<SceneShortcuts::Conflict> out = SceneShortcuts::conflicts(rows, taken);

  ASSERT_EQ(1, out.size());
  EXPECT_EQ(1, out.at(0).row);
  EXPECT_EQ(-1, out.at(0).otherRow);
  EXPECT_EQ(keys("Ctrl+N"), out.at(0).keys);
  EXPECT_EQ(QString::fromUtf8("\"New Scene\""), out.at(0).taken);
}

TEST(SceneShortcuts, DuplicateAndTakenBothReported)
{
  QHash<QString, QString> taken;
  taken.insert(keys("Ctrl+R"), QStringLiteral("\"Render\""));
  const QList<QString> rows = QList<QString>() << keys("Ctrl+R") << keys("Ctrl+R");

  const QList<SceneShortcuts::Conflict> out = SceneShortcuts::conflicts(rows, taken);

  // Row 0: taken; row 1: same as row 0, and taken.
  ASSERT_EQ(3, out.size());
  EXPECT_EQ(0, out.at(0).row);
  EXPECT_EQ(-1, out.at(0).otherRow);
  EXPECT_EQ(1, out.at(1).row);
  EXPECT_EQ(0, out.at(1).otherRow);
  EXPECT_EQ(1, out.at(2).row);
  EXPECT_EQ(-1, out.at(2).otherRow);
}

TEST(SceneShortcuts, PerInputCutKeysDoNotClashWithTheDefaults)
{
  // The scene panel's Cut to Input 1-9 moved to Ctrl+Alt+1-9.
  QHash<QString, QString> taken;
  for (int i = 0; i < SceneShortcuts::kDefaults; ++i)
  {
    taken.insert(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_1 + i).toString(QKeySequence::PortableText), QStringLiteral("Cut to Input"));
    taken.insert(QKeySequence(Qt::Key_1 + i).toString(QKeySequence::PortableText), QStringLiteral("Preview Input"));
    taken.insert(QKeySequence(Qt::SHIFT + Qt::Key_1 + i).toString(QKeySequence::PortableText), QStringLiteral("Layer Input"));
  }
  QList<QString> rows;
  for (int i = 0; i < SceneShortcuts::kDefaults; ++i)
  {
    rows << SceneShortcuts::defaultShortcut(i);
  }

  EXPECT_TRUE(SceneShortcuts::conflicts(rows, taken).isEmpty());
}

TEST_F(SceneShortcutsSettingsTest, NothingSaved)
{
  EXPECT_TRUE(SceneShortcuts::saved().isEmpty());
  EXPECT_EQ(keys("Ctrl+1"), SceneShortcuts::load(scenes(1)).value(QStringLiteral("s1")));
}

TEST_F(SceneShortcutsSettingsTest, SaveAndLoad)
{
  QVariantMap shortcuts;
  shortcuts.insert(QStringLiteral("s1"), QString());
  shortcuts.insert(QStringLiteral("s2"), keys("Ctrl+7"));
  SceneShortcuts::save(shortcuts);

  EXPECT_EQ(shortcuts, SceneShortcuts::saved());
  const QHash<QString, QString> out = SceneShortcuts::load(scenes(3));
  EXPECT_FALSE(out.contains(QStringLiteral("s1")));
  EXPECT_EQ(keys("Ctrl+7"), out.value(QStringLiteral("s2")));
  EXPECT_EQ(keys("Ctrl+3"), out.value(QStringLiteral("s3")));
}

TEST_F(SceneShortcutsSettingsTest, SaveReplacesEverything)
{
  QVariantMap first;
  first.insert(QStringLiteral("gone"), keys("Ctrl+5"));
  SceneShortcuts::save(first);
  QVariantMap second;
  second.insert(QStringLiteral("s1"), keys("Ctrl+2"));
  SceneShortcuts::save(second);

  EXPECT_EQ(second, SceneShortcuts::saved());
}
