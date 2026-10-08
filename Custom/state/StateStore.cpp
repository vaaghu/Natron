#include "StateStore.h"

#include "Json.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

namespace
{
// Batch bursts of changes (e.g. one POST with many keys) into one write.
const int kSaveDelayMs = 300;
}

StateStore::StateStore(QObject *parent)
    : QObject(parent),
      m_dirty(false)
{
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(kSaveDelayMs);
  connect(&m_saveTimer, SIGNAL(timeout()), this, SLOT(onSaveTimer()));
}

StateStore::~StateStore()
{
  if (m_dirty)
  {
    saveNow();
  }
}

void StateStore::set(
    const QString &key,
    const QVariant &value)
{
  const bool isNewKey = !m_store.contains(key);

  m_store.insert(key, value);
  scheduleSave();

  Q_EMIT valueChanged(key);

  if (isNewKey)
  {
    Q_EMIT keysChanged();
  }
}

QVariant StateStore::get(
    const QString &key,
    const QVariant &defaultValue) const
{
  return m_store.value(key, defaultValue);
}

bool StateStore::has(const QString &key) const
{
  return m_store.contains(key);
}

QStringList StateStore::keys() const
{
  return m_store.keys();
}

bool StateStore::remove(const QString &key)
{
  if (m_store.remove(key) == 0)
  {
    return false;
  }

  scheduleSave();

  Q_EMIT valueChanged(key);
  Q_EMIT keysChanged();

  return true;
}

void StateStore::clear()
{
  const QStringList removed = m_store.keys();

  m_store.clear();

  if (!removed.isEmpty())
  {
    scheduleSave();
  }

  for (const QString &key : removed)
  {
    Q_EMIT valueChanged(key);
  }

  if (!removed.isEmpty())
  {
    Q_EMIT keysChanged();
  }
}

int StateStore::size() const
{
  return m_store.size();
}

QString StateStore::toText(const QVariant &value)
{
  const QVariant::Type type = value.type();

  if (type == QVariant::List || type == QVariant::Map)
  {
    return QString::fromUtf8(Json::serialize(value));
  }

  return value.toString();
}

QString StateStore::filePath() const
{
  return m_path;
}

bool StateStore::loadAndAutoSave(const QString &path)
{
  m_path = path;

  QFile file(path);
  if (!file.exists())
  {
    return true;
  }

  if (!file.open(QIODevice::ReadOnly))
  {
    qWarning() << "StateStore: cannot read" << path << ":" << file.errorString();
    return false;
  }

  bool ok = false;
  QString error;
  const QVariant doc = Json::parse(file.readAll(), &ok, &error);

  if (!ok || doc.type() != QVariant::Map)
  {
    qWarning() << "StateStore: ignoring invalid file" << path << ":" << error;
    return false;
  }

  const QVariantMap map = doc.toMap();
  for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
  {
    m_store.insert(it.key(), it.value());
    Q_EMIT valueChanged(it.key());
  }

  if (!map.isEmpty())
  {
    Q_EMIT keysChanged();
  }

  return true;
}

void StateStore::scheduleSave()
{
  if (m_path.isEmpty())
  {
    return;
  }

  m_dirty = true;
  m_saveTimer.start();
}

void StateStore::onSaveTimer()
{
  saveNow();
}

bool StateStore::saveNow()
{
  m_saveTimer.stop();

  if (m_path.isEmpty())
  {
    return true;
  }

  QVariantMap map;
  for (QHash<QString, QVariant>::const_iterator it = m_store.constBegin(); it != m_store.constEnd(); ++it)
  {
    map.insert(it.key(), it.value());
  }

  QDir().mkpath(QFileInfo(m_path).absolutePath());

  // Write a temporary file and swap it in, so a crash never leaves a
  // half-written store.
  const QString tmpPath = m_path + QString::fromUtf8(".tmp");
  QFile tmp(tmpPath);
  if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      tmp.write(Json::serialize(map)) < 0)
  {
    qWarning() << "StateStore: cannot write" << tmpPath << ":" << tmp.errorString();
    return false;
  }
  tmp.close();

  QFile::remove(m_path);
  if (!QFile::rename(tmpPath, m_path))
  {
    qWarning() << "StateStore: cannot replace" << m_path;
    return false;
  }

  m_dirty = false;
  return true;
}
