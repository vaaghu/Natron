#include "SceneStore.h"

#include "ProjectInfo.h"
#include "../state/Json.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

namespace
{
const int kSaveDelayMs = 300;

QString statusToString(RenderRecord::Status status)
{
  switch (status)
  {
  case RenderRecord::eDone:
    return QString::fromUtf8("done");
  case RenderRecord::eFailed:
    return QString::fromUtf8("failed");
  default:
    // Queued/rendering are not meaningful after a restart.
    return QString::fromUtf8("none");
  }
}

RenderRecord::Status statusFromString(const QString &status)
{
  if (status == QString::fromUtf8("done"))
  {
    return RenderRecord::eDone;
  }
  if (status == QString::fromUtf8("failed"))
  {
    return RenderRecord::eFailed;
  }
  return RenderRecord::eNone;
}

QString key(const char *k)
{
  return QString::fromUtf8(k);
}

// Render records are stored per "<scene id>|<item>".
QString recordKey(const QString &sceneId, const QString &item)
{
  return sceneId + QLatin1Char('|') + item;
}
}

namespace SceneItem
{
// Write node names are Python identifiers (dots for nodes in groups): the
// last '#' separates them from the project path.
QString make(const QString &project, const QString &writer)
{
  return project + QLatin1Char('#') + writer;
}

QString project(const QString &item)
{
  const int sep = item.lastIndexOf(QLatin1Char('#'));
  return sep < 0 ? item : item.left(sep);
}

QString writer(const QString &item)
{
  const int sep = item.lastIndexOf(QLatin1Char('#'));
  return sep < 0 ? QString() : item.mid(sep + 1);
}

QStringList allOf(const QString &project)
{
  const QString path = QFileInfo(project).absoluteFilePath();
  const QStringList writers = readProjectInfo(path).writers;
  QStringList items;
  for (int i = 0; i < writers.size(); ++i)
  {
    items << make(path, writers.at(i));
  }
  if (items.isEmpty())
  {
    items << make(path, QString());
  }
  return items;
}
}

SceneStore::SceneStore(QObject *parent)
    : QObject(parent),
      m_dirty(false)
{
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(kSaveDelayMs);
  connect(&m_saveTimer, SIGNAL(timeout()), this, SLOT(onSaveTimer()));
}

SceneStore::~SceneStore()
{
  if (m_dirty)
  {
    saveNow();
  }
}

bool SceneStore::loadAndAutoSave(const QString &path)
{
  m_path = path;

  QFile file(path);
  if (!file.exists())
  {
    return true;
  }
  if (!file.open(QIODevice::ReadOnly))
  {
    qWarning() << "SceneStore: cannot read" << path << ":" << file.errorString();
    return false;
  }

  bool ok = false;
  QString error;
  const QVariant doc = Json::parse(file.readAll(), &ok, &error);
  if (!ok || doc.type() != QVariant::Map)
  {
    qWarning() << "SceneStore: ignoring invalid file" << path << ":" << error;
    return false;
  }

  const QVariantMap root = doc.toMap();

  // Files from before scenes held Write nodes list projects: each becomes
  // all of its Write nodes, with its NDI settings and last render record.
  QList<QPair<QString, QStringList> > migrated; // record key prefix -> items

  const QVariantList scenes = root.value(key("scenes")).toList();
  for (int i = 0; i < scenes.size(); ++i)
  {
    const QVariantMap s = scenes.at(i).toMap();
    Scene scene;
    scene.id = s.value(key("id")).toString();
    scene.name = s.value(key("name")).toString();
    const QVariantList items = s.value(key("items")).toList();
    for (int j = 0; j < items.size(); ++j)
    {
      scene.items << items.at(j).toString();
    }
    QHash<QString, QStringList> projectItems; // legacy project -> its items
    const QVariantList projects = s.value(key("projects")).toList();
    for (int j = 0; j < projects.size(); ++j)
    {
      const QString project = projects.at(j).toString();
      const QStringList all = SceneItem::allOf(project);
      projectItems.insert(project, all);
      scene.items << all;
      migrated << qMakePair(recordKey(scene.id, project), QStringList(all.first()));
    }
    const QVariantMap keyMap = s.value(key("keyMap")).toMap();
    for (QVariantMap::const_iterator k = keyMap.constBegin(); k != keyMap.constEnd(); ++k)
    {
      scene.keyMap.insert(k.key(), k.value().toString());
    }
    scene.outputDir = s.value(key("outputDir")).toString();
    const QVariantMap ndi = s.value(key("ndi")).toMap();
    scene.ndiEnabled = ndi.value(key("enabled")).toBool();
    scene.ndiAlpha = ndi.value(key("alpha")).toBool();
    scene.ndiLive = ndi.value(key("live")).toBool();
    const QVariantMap pauseAt = ndi.value(key("pauseAt")).toMap();
    for (QVariantMap::const_iterator p = pauseAt.constBegin(); p != pauseAt.constEnd(); ++p)
    {
      const QStringList targets = projectItems.value(p.key(), QStringList(p.key()));
      for (int t = 0; t < targets.size(); ++t)
      {
        scene.ndiPauseAt.insert(targets.at(t), p.value().toDouble());
      }
    }
    const QVariantMap loop = ndi.value(key("loop")).toMap();
    for (QVariantMap::const_iterator l = loop.constBegin(); l != loop.constEnd(); ++l)
    {
      const QStringList targets = projectItems.value(l.key(), QStringList(l.key()));
      for (int t = 0; t < targets.size(); ++t)
      {
        scene.ndiLoop.insert(targets.at(t), l.value().toBool());
      }
    }
    if (!scene.id.isEmpty())
    {
      m_scenes << scene;
    }
  }

  const QVariantMap renders = root.value(key("renders")).toMap();
  for (QVariantMap::const_iterator it = renders.constBegin(); it != renders.constEnd(); ++it)
  {
    const QVariantMap r = it.value().toMap();
    RenderRecord record;
    record.status = statusFromString(r.value(key("status")).toString());
    record.time = QDateTime::fromString(r.value(key("time")).toString(), Qt::ISODate);
    record.message = r.value(key("message")).toString();
    record.output = r.value(key("output")).toString();
    record.thumbnail = r.value(key("thumbnail")).toString();
    // Records from before renders were per scene have no scene id: drop them.
    if (it.key().contains(QLatin1Char('|')))
    {
      m_records.insert(it.key(), record);
    }
  }

  // A project's record goes to its first Write node.
  for (int i = 0; i < migrated.size(); ++i)
  {
    const QString oldKey = migrated.at(i).first;
    if (m_records.contains(oldKey))
    {
      const QString sceneId = oldKey.section(QLatin1Char('|'), 0, 0);
      m_records.insert(recordKey(sceneId, migrated.at(i).second.first()), m_records.take(oldKey));
    }
  }
  if (!migrated.isEmpty())
  {
    changed();
  }

  Q_EMIT scenesChanged();
  return true;
}

bool SceneStore::saveNow()
{
  m_saveTimer.stop();

  if (m_path.isEmpty())
  {
    return true;
  }

  QVariantList scenes;
  for (int i = 0; i < m_scenes.size(); ++i)
  {
    QVariantMap s;
    s.insert(key("id"), m_scenes.at(i).id);
    s.insert(key("name"), m_scenes.at(i).name);
    QVariantList items;
    for (int j = 0; j < m_scenes.at(i).items.size(); ++j)
    {
      items << m_scenes.at(i).items.at(j);
    }
    s.insert(key("items"), items);
    QVariantMap keyMap;
    for (QHash<QString, QString>::const_iterator k = m_scenes.at(i).keyMap.constBegin(); k != m_scenes.at(i).keyMap.constEnd(); ++k)
    {
      keyMap.insert(k.key(), k.value());
    }
    s.insert(key("keyMap"), keyMap);
    s.insert(key("outputDir"), m_scenes.at(i).outputDir);
    QVariantMap ndi;
    ndi.insert(key("enabled"), m_scenes.at(i).ndiEnabled);
    ndi.insert(key("alpha"), m_scenes.at(i).ndiAlpha);
    ndi.insert(key("live"), m_scenes.at(i).ndiLive);
    QVariantMap pauseAt;
    for (QHash<QString, double>::const_iterator p = m_scenes.at(i).ndiPauseAt.constBegin(); p != m_scenes.at(i).ndiPauseAt.constEnd(); ++p)
    {
      pauseAt.insert(p.key(), p.value());
    }
    ndi.insert(key("pauseAt"), pauseAt);
    QVariantMap loop;
    for (QHash<QString, bool>::const_iterator l = m_scenes.at(i).ndiLoop.constBegin(); l != m_scenes.at(i).ndiLoop.constEnd(); ++l)
    {
      loop.insert(l.key(), l.value());
    }
    ndi.insert(key("loop"), loop);
    s.insert(key("ndi"), ndi);
    scenes << s;
  }

  QVariantMap renders;
  for (QHash<QString, RenderRecord>::const_iterator it = m_records.constBegin(); it != m_records.constEnd(); ++it)
  {
    const RenderRecord &record = it.value();
    if (record.status != RenderRecord::eDone && record.status != RenderRecord::eFailed)
    {
      continue;
    }
    QVariantMap r;
    r.insert(key("status"), statusToString(record.status));
    r.insert(key("time"), record.time.toString(Qt::ISODate));
    r.insert(key("message"), record.message);
    r.insert(key("output"), record.output);
    r.insert(key("thumbnail"), record.thumbnail);
    renders.insert(it.key(), r);
  }

  QVariantMap root;
  root.insert(key("scenes"), scenes);
  root.insert(key("renders"), renders);

  QDir().mkpath(QFileInfo(m_path).absolutePath());

  const QString tmpPath = m_path + QString::fromUtf8(".tmp");
  QFile tmp(tmpPath);
  if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      tmp.write(Json::serialize(root)) < 0)
  {
    qWarning() << "SceneStore: cannot write" << tmpPath << ":" << tmp.errorString();
    return false;
  }
  tmp.close();

  QFile::remove(m_path);
  if (!QFile::rename(tmpPath, m_path))
  {
    qWarning() << "SceneStore: cannot replace" << m_path;
    return false;
  }

  m_dirty = false;
  return true;
}

void SceneStore::onSaveTimer()
{
  saveNow();
}

void SceneStore::changed()
{
  if (!m_path.isEmpty())
  {
    m_dirty = true;
    m_saveTimer.start();
  }
}

int SceneStore::indexOf(const QString &id) const
{
  for (int i = 0; i < m_scenes.size(); ++i)
  {
    if (m_scenes.at(i).id == id)
    {
      return i;
    }
  }
  return -1;
}

QList<Scene> SceneStore::scenes() const
{
  return m_scenes;
}

bool SceneStore::scene(const QString &id, Scene *out) const
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return false;
  }
  *out = m_scenes.at(i);
  return true;
}

QString SceneStore::createScene(const QString &name)
{
  Scene scene;
  scene.id = QUuid::createUuid().toString();
  scene.name = name;
  m_scenes << scene;

  changed();
  Q_EMIT scenesChanged();
  return scene.id;
}

void SceneStore::renameScene(const QString &id, const QString &name)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).name == name)
  {
    return;
  }
  m_scenes[i].name = name;

  changed();
  Q_EMIT scenesChanged();
}

SceneSnapshot SceneStore::snapshot(const QString &id) const
{
  SceneSnapshot snap;
  snap.id = id;
  snap.index = indexOf(id);
  if (snap.index < 0)
  {
    return snap;
  }
  snap.valid = true;
  snap.scene = m_scenes.at(snap.index);
  const QString prefix = id + QLatin1Char('|');
  for (QHash<QString, RenderRecord>::const_iterator it = m_records.constBegin(); it != m_records.constEnd(); ++it)
  {
    if (it.key().startsWith(prefix))
    {
      snap.records.insert(it.key(), it.value());
    }
  }
  return snap;
}

void SceneStore::restore(const SceneSnapshot &snap)
{
  if (!snap.valid)
  {
    removeScene(snap.id);
    return;
  }

  const int i = indexOf(snap.id);
  if (i >= 0)
  {
    m_scenes[i] = snap.scene;
  }
  else
  {
    m_scenes.insert(qBound(0, snap.index, m_scenes.size()), snap.scene);
  }

  const QString prefix = snap.id + QLatin1Char('|');
  QHash<QString, RenderRecord>::iterator it = m_records.begin();
  while (it != m_records.end())
  {
    if (it.key().startsWith(prefix))
    {
      it = m_records.erase(it);
    }
    else
    {
      ++it;
    }
  }
  for (QHash<QString, RenderRecord>::const_iterator r = snap.records.constBegin(); r != snap.records.constEnd(); ++r)
  {
    m_records.insert(r.key(), r.value());
  }

  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::removeScene(const QString &id)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }
  m_scenes.removeAt(i);

  const QString prefix = id + QLatin1Char('|');
  QHash<QString, RenderRecord>::iterator it = m_records.begin();
  while (it != m_records.end())
  {
    if (it.key().startsWith(prefix))
    {
      it = m_records.erase(it);
    }
    else
    {
      ++it;
    }
  }

  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::addItems(const QString &id, const QStringList &items)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }

  bool added = false;
  for (int j = 0; j < items.size(); ++j)
  {
    const QString item = SceneItem::make(QFileInfo(SceneItem::project(items.at(j))).absoluteFilePath(),
                                         SceneItem::writer(items.at(j)));
    if (!m_scenes.at(i).items.contains(item))
    {
      m_scenes[i].items << item;
      added = true;
    }
  }

  if (added)
  {
    changed();
    Q_EMIT scenesChanged();
  }
}

void SceneStore::removeItems(const QString &id, const QStringList &items)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }

  int removed = 0;
  for (int j = 0; j < items.size(); ++j)
  {
    removed += m_scenes[i].items.removeAll(items.at(j));
  }

  if (removed > 0)
  {
    changed();
    Q_EMIT scenesChanged();
  }
}

void SceneStore::setKeyMapping(const QString &id, const QString &key, const QString &usedKey)
{
  const int i = indexOf(id);
  if (i < 0 || key.isEmpty())
  {
    return;
  }

  const QString used = usedKey.trimmed();
  if (used.isEmpty() || used == key)
  {
    if (m_scenes[i].keyMap.remove(key) == 0)
    {
      return;
    }
  }
  else
  {
    if (m_scenes.at(i).keyMap.value(key) == used)
    {
      return;
    }
    m_scenes[i].keyMap.insert(key, used);
  }

  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::setOutputDir(const QString &id, const QString &dir)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).outputDir == dir)
  {
    return;
  }
  m_scenes[i].outputDir = dir;

  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::setNdiEnabled(const QString &id, bool enabled)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).ndiEnabled == enabled)
  {
    return;
  }
  m_scenes[i].ndiEnabled = enabled;
  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::setNdiAlpha(const QString &id, bool alpha)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).ndiAlpha == alpha)
  {
    return;
  }
  m_scenes[i].ndiAlpha = alpha;
  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::setNdiLive(const QString &id, bool live)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).ndiLive == live)
  {
    return;
  }
  m_scenes[i].ndiLive = live;
  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::setNdiPauseAt(const QString &id, const QString &item, double seconds)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).pauseAt(item) == seconds)
  {
    return;
  }
  if (seconds < 0)
  {
    m_scenes[i].ndiPauseAt.remove(item);
  }
  else
  {
    m_scenes[i].ndiPauseAt.insert(item, seconds);
  }
  changed();
  Q_EMIT ndiSettingsChanged(id, item);
}

void SceneStore::setNdiLoop(const QString &id, const QString &item, bool loop)
{
  const int i = indexOf(id);
  if (i < 0 || m_scenes.at(i).ndiLoop.value(item, false) == loop)
  {
    return;
  }
  m_scenes[i].ndiLoop.insert(item, loop);
  changed();
  Q_EMIT ndiSettingsChanged(id, item);
}

RenderRecord SceneStore::renderRecord(const QString &sceneId, const QString &item) const
{
  return m_records.value(recordKey(sceneId, item));
}

void SceneStore::setRenderRecord(const QString &sceneId, const QString &item, const RenderRecord &record)
{
  m_records.insert(recordKey(sceneId, item), record);

  changed();
  Q_EMIT renderRecordChanged(sceneId, item);
}
