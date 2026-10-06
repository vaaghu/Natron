#pragma once

#include <QHash>
#include <QVariant>
#include <QStringList>

class StateStore
{
public:
  StateStore() = default;
  ~StateStore() = default;

  // Set or overwrite a value.
  void set(const QString &key, const QVariant &value);

  // Get a value.
  // Returns defaultValue if the key doesn't exist.
  QVariant get(
      const QString &key,
      const QVariant &defaultValue = QVariant()) const;

  // Check whether a key exists.
  bool has(const QString &key) const;

  // Return all keys currently in the store.
  QStringList keys() const;

  // Remove a key.
  bool remove(const QString &key);

  // Remove everything.
  void clear();

  // Number of keys currently stored.
  int size() const;

private:
  QHash<QString, QVariant> m_store;
};
