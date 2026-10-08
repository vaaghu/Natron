#include "SceneStore.h"

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

  const QVariantList scenes = root.value(key("scenes")).toList();
  for (int i = 0; i < scenes.size(); ++i)
  {
    const QVariantMap s = scenes.at(i).toMap();
    Scene scene;
    scene.id = s.value(key("id")).toString();
    scene.name = s.value(key("name")).toString();
    const QVariantList projects = s.value(key("projects")).toList();
    for (int j = 0; j < projects.size(); ++j)
    {
      scene.projects << projects.at(j).toString();
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
    m_records.insert(it.key(), record);
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
    QVariantList projects;
    for (int j = 0; j < m_scenes.at(i).projects.size(); ++j)
    {
      projects << m_scenes.at(i).projects.at(j);
    }
    s.insert(key("projects"), projects);
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

void SceneStore::removeScene(const QString &id)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }
  m_scenes.removeAt(i);

  changed();
  Q_EMIT scenesChanged();
}

void SceneStore::addProjects(const QString &id, const QStringList &projects)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }

  bool added = false;
  for (int j = 0; j < projects.size(); ++j)
  {
    const QString path = QFileInfo(projects.at(j)).absoluteFilePath();
    if (!m_scenes.at(i).projects.contains(path))
    {
      m_scenes[i].projects << path;
      added = true;
    }
  }

  if (added)
  {
    changed();
    Q_EMIT scenesChanged();
  }
}

void SceneStore::removeProjects(const QString &id, const QStringList &projects)
{
  const int i = indexOf(id);
  if (i < 0)
  {
    return;
  }

  int removed = 0;
  for (int j = 0; j < projects.size(); ++j)
  {
    removed += m_scenes[i].projects.removeAll(projects.at(j));
  }

  if (removed > 0)
  {
    changed();
    Q_EMIT scenesChanged();
  }
}

RenderRecord SceneStore::renderRecord(const QString &project) const
{
  return m_records.value(project);
}

void SceneStore::setRenderRecord(const QString &project, const RenderRecord &record)
{
  m_records.insert(project, record);

  changed();
  Q_EMIT renderRecordChanged(project);
}
