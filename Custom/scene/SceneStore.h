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

  // Key renaming for this scene: key bound in the projects -> store key
  // used instead when rendering (e.g. PlayerName1 -> PlayerName2).
  QHash<QString, QString> keyMap;

  // Folder where this scene's renders are written (same file names as the
  // projects' Write nodes). Empty: use the projects' own output paths.
  QString outputDir;

  // NDI output: one source per project of the scene.
  bool ndiEnabled;
  bool ndiAlpha;  // send with transparency (overlays)
  bool ndiLive;   // send frames while rendering instead of playing the render
  QHash<QString, double> ndiPauseAt; // project -> pause point in seconds
  QHash<QString, bool> ndiLoop;      // project -> loop

  Scene()
      : ndiEnabled(false),
        ndiAlpha(false),
        ndiLive(false)
  {
  }

  double pauseAt(const QString &project) const
  {
    return ndiPauseAt.value(project, -1.0);
  }

  // Store key used for a key bound in the projects.
  QString mappedKey(const QString &key) const
  {
    return keyMap.value(key, key);
  }
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
  QString frameRange; // frames rendered, e.g. "1-250"

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

  // usedKey empty or equal to key: no renaming.
  void setKeyMapping(const QString &id, const QString &key, const QString &usedKey);
  void setOutputDir(const QString &id, const QString &dir);

  void setNdiEnabled(const QString &id, bool enabled);
  void setNdiAlpha(const QString &id, bool alpha);
  void setNdiLive(const QString &id, bool live);
  void setNdiPauseAt(const QString &id, const QString &project, double seconds); // < 0: none
  void setNdiLoop(const QString &id, const QString &project, bool loop);

  // Renders are per scene: the same project renders differently (other
  // keys, other output folder) in another scene.
  RenderRecord renderRecord(const QString &sceneId, const QString &project) const;
  void setRenderRecord(const QString &sceneId, const QString &project, const RenderRecord &record);

Q_SIGNALS:
  void scenesChanged();
  void renderRecordChanged(const QString &sceneId, const QString &project);
  // A project's NDI pause point / loop changed (no scenesChanged: the
  // scene views do not need rebuilding).
  void ndiSettingsChanged(const QString &sceneId, const QString &project);

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
