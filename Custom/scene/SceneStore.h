#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// A scene renders Write nodes of projects ("items"). An item is
// "<absolute .ntp path>#<Write node script name>" (empty name: the project
// has no Write node). Render records, NDI sources and their settings are
// per item.
namespace SceneItem
{
QString make(const QString &project, const QString &writer);
QString project(const QString &item);
QString writer(const QString &item);
// Items for every Write node of a project (one item without a writer if it
// has none).
QStringList allOf(const QString &project);
}

// A named group of Write nodes of Natron projects, rendered together.
struct Scene
{
  QString id;
  QString name;
  QStringList items; // see SceneItem, in display order

  // Key renaming for this scene: key bound in the projects -> store key
  // used instead when rendering (e.g. PlayerName1 -> PlayerName2).
  QHash<QString, QString> keyMap;

  // Folder where this scene's renders are written (same file names as the
  // projects' Write nodes). Empty: use the projects' own output paths.
  QString outputDir;

  // NDI output: one source per item of the scene.
  bool ndiEnabled;
  bool ndiAlpha;  // send with transparency (overlays)
  bool ndiLive;   // send frames while rendering instead of playing the render
  QHash<QString, double> ndiPauseAt; // item -> pause point in seconds
  QHash<QString, bool> ndiLoop;      // item -> loop

  Scene()
      : ndiEnabled(false),
        ndiAlpha(false),
        ndiLive(false)
  {
  }

  double pauseAt(const QString &item) const
  {
    return ndiPauseAt.value(item, -1.0);
  }

  // Projects of the items, without duplicates, in order of appearance.
  QStringList projects() const
  {
    QStringList out;
    for (int i = 0; i < items.size(); ++i)
    {
      const QString project = SceneItem::project(items.at(i));
      if (!out.contains(project))
      {
        out << project;
      }
    }
    return out;
  }

  // Store key used for a key bound in the projects.
  QString mappedKey(const QString &key) const
  {
    return keyMap.value(key, key);
  }
};

// Result of the last render of an item.
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

// Scenes and per-item render records, saved to a JSON file.
// A scene and its render records at one moment, to put it back (undo).
// valid is false for "no such scene" (e.g. before it was created).
struct SceneSnapshot
{
  bool valid;
  QString id;
  int index; // position in the scene list
  Scene scene;
  QHash<QString, RenderRecord> records; // "<scene id>|<item>" -> record

  SceneSnapshot()
      : valid(false),
        index(-1)
  {
  }
};

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
  // Items already in the scene are skipped.
  void addItems(const QString &id, const QStringList &items);
  void removeItems(const QString &id, const QStringList &items);

  // usedKey empty or equal to key: no renaming.
  void setKeyMapping(const QString &id, const QString &key, const QString &usedKey);
  void setOutputDir(const QString &id, const QString &dir);

  void setNdiEnabled(const QString &id, bool enabled);
  void setNdiAlpha(const QString &id, bool alpha);
  void setNdiLive(const QString &id, bool live);
  void setNdiPauseAt(const QString &id, const QString &item, double seconds); // < 0: none
  void setNdiLoop(const QString &id, const QString &item, bool loop);

  // Renders are per scene: the same item renders differently (other keys,
  // other output folder) in another scene.
  // Undo support: the scene as it is now, and putting a snapshot back
  // (replacing the scene, re-creating it, or removing it if not valid).
  SceneSnapshot snapshot(const QString &id) const;
  void restore(const SceneSnapshot &snapshot);

  RenderRecord renderRecord(const QString &sceneId, const QString &item) const;
  void setRenderRecord(const QString &sceneId, const QString &item, const RenderRecord &record);

Q_SIGNALS:
  void scenesChanged();
  void renderRecordChanged(const QString &sceneId, const QString &item);
  // An item's NDI pause point / loop changed (no scenesChanged: the
  // scene views do not need rebuilding).
  void ndiSettingsChanged(const QString &sceneId, const QString &item);

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
