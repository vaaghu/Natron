#include "StateStore.h"

#include "Json.h"

StateStore::StateStore(QObject *parent)
    : QObject(parent)
{
}

void StateStore::set(
    const QString &key,
    const QVariant &value)
{
  const bool isNewKey = !m_store.contains(key);

  m_store.insert(key, value);

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

  Q_EMIT valueChanged(key);
  Q_EMIT keysChanged();

  return true;
}

void StateStore::clear()
{
  const QStringList removed = m_store.keys();

  m_store.clear();

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
