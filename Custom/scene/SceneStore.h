#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// A named group of Natron projects rendered together.
struct Scene
{
  QString id;
  QString name;
  QStringList projects; // absolute .ntp paths
};

// Result of the last render of a project.
struct RenderRecord
{
  enum Status
  {
    eNone,
    eQueued,
    eRendering,
    eDone,
    eFailed
  };

  Status status;
  QDateTime time;     // when the render finished
  QString message;    // error / summary
  QString output;     // existing output file found after the render
  QString thumbnail;  // png made from the output

  RenderRecord()
      : status(eNone)
  {
  }
};

// Scenes and per-project render records, saved to a JSON file.
class SceneStore : public QObject
{
  Q_OBJECT

public:
  explicit SceneStore(QObject *parent = nullptr);
  ~SceneStore();

  // Loads path (if it exists) and saves every later change to it.
  bool loadAndAutoSave(const QString &path);
  bool saveNow();

  QList<Scene> scenes() const;
  bool scene(const QString &id, Scene *out) const;

  // Returns the new scene id.
  QString createScene(const QString &name);
  void renameScene(const QString &id, const QString &name);
  void removeScene(const QString &id);
  void addProjects(const QString &id, const QStringList &projects);
  void removeProjects(const QString &id, const QStringList &projects);

  RenderRecord renderRecord(const QString &project) const;
  void setRenderRecord(const QString &project, const RenderRecord &record);

Q_SIGNALS:
  void scenesChanged();
  void renderRecordChanged(const QString &project);

private Q_SLOTS:
  void onSaveTimer();

private:
  int indexOf(const QString &id) const;
  void changed();

  QList<Scene> m_scenes;
  QHash<QString, RenderRecord> m_records;
  QString m_path;
  QTimer m_saveTimer;
  bool m_dirty;
};
