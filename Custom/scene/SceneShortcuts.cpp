#include "SceneShortcuts.h"

#include "SceneStore.h"

#include <QKeySequence>
#include <QSet>
#include <QSettings>

namespace
{
const char *const kSettingsKey = "dashboard/sceneShortcuts";
}

namespace SceneShortcuts
{
QString defaultShortcut(int index)
{
  if (index < 0 || index >= kDefaults)
  {
    return QString();
  }
  return QKeySequence(Qt::CTRL + Qt::Key_1 + index).toString(QKeySequence::PortableText);
}

QHash<QString, QString> shortcuts(const QList<Scene> &scenes, const QVariantMap &saved)
{
  QHash<QString, QString> out;
  QSet<QString> used;
  for (int i = 0; i < scenes.size(); ++i)
  {
    const QString keys = saved.value(scenes.at(i).id).toString();
    if (!keys.isEmpty())
    {
      out.insert(scenes.at(i).id, keys);
      used.insert(keys);
    }
  }
  // Scenes not saved (added since): the default of their place, if free.
  for (int i = 0; i < scenes.size(); ++i)
  {
    const QString keys = defaultShortcut(i);
    if (!saved.contains(scenes.at(i).id) && !keys.isEmpty() && !used.contains(keys))
    {
      out.insert(scenes.at(i).id, keys);
      used.insert(keys);
    }
  }
  return out;
}

QHash<QString, QString> load(const QList<Scene> &scenes)
{
  return shortcuts(scenes, saved());
}

QVariantMap saved()
{
  return QSettings().value(QString::fromUtf8(kSettingsKey)).toMap();
}

void save(const QVariantMap &shortcuts)
{
  QSettings().setValue(QString::fromUtf8(kSettingsKey), shortcuts);
}

QList<Conflict> conflicts(const QList<QString> &keys, const QHash<QString, QString> &taken)
{
  QList<Conflict> out;
  QHash<QString, int> rows; // keys -> first row using them
  for (int row = 0; row < keys.size(); ++row)
  {
    const QString &k = keys.at(row);
    if (k.isEmpty())
    {
      continue;
    }
    if (rows.contains(k))
    {
      Conflict c;
      c.row = row;
      c.otherRow = rows.value(k);
      c.keys = k;
      out << c;
    }
    else
    {
      rows.insert(k, row);
    }
    if (taken.contains(k))
    {
      Conflict c;
      c.row = row;
      c.otherRow = -1;
      c.keys = k;
      c.taken = taken.value(k);
      out << c;
    }
  }
  return out;
}
}
