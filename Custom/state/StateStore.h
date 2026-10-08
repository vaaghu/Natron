#pragma once

#include <QHash>
#include <QObject>
#include <QVariant>
#include <QStringList>
#include <QTimer>

class StateStore : public QObject
{
  Q_OBJECT

public:
  explicit StateStore(QObject *parent = nullptr);
  ~StateStore();

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

  // Value as display text: scalars via QVariant::toString(),
  // lists/maps as compact JSON.
  static QString toText(const QVariant &value);

  // Persistence: loads the JSON object in path (replacing the current
  // content), then saves every later change back to it (batched).
  // Returns false if the file exists but cannot be read/parsed.
  bool loadAndAutoSave(const QString &path);

  // Writes pending changes now. Returns false on write error.
  bool saveNow();

  QString filePath() const;

Q_SIGNALS:
  // A key was set (added or overwritten) or removed.
  void valueChanged(const QString &key);

  // The set of keys changed (a key was added or removed).
  void keysChanged();

private Q_SLOTS:
  void onSaveTimer();

private:
  void scheduleSave();

  QHash<QString, QVariant> m_store;
  QString m_path;
  QTimer m_saveTimer;
  bool m_dirty;
};
