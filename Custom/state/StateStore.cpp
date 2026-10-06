#include "StateStore.h"

void StateStore::set(
    const QString &key,
    const QVariant &value)
{
  m_store.insert(key, value);
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
  return m_store.remove(key) > 0;
}

void StateStore::clear()
{
  m_store.clear();
}

int StateStore::size() const
{
  return m_store.size();
}
