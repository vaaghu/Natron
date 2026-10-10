#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class SceneStore;
class StateStore;

// Live state of the item being rendered, parsed from the renderer output
// ("Write1 ==> Frame: 12, Progress: 60.0%, 14.2 Fps, Time Remaining: 3 seconds").
struct RenderProgress
{
  bool active;           // this item is the one rendering
  bool paused;
  QString node;          // Write node rendering
  double percent;        // 0-100
  double fps;
  QString timeRemaining; // as printed by the renderer
  int currentFrame;      // frame just rendered (-1: none yet)

  RenderProgress()
      : active(false),
        paused(false),
        percent(0),
        fps(0),
        currentFrame(-1)
  {
  }
};

// Renders scene items (Write nodes of projects, see SceneItem) one after the
// other with NatronRenderer:
//   NatronRenderer -l <applyScript> -w <Write node> <project.ntp>
// The apply script (see writeApplyScript) runs after the project is loaded
// and, using the environment set here:
//   NATRON_STATE_JSON    saved state store: values of the bound keys
//   NATRON_STATE_KEYMAP  the scene's key renaming (JSON object)
//   NATRON_OUTPUT_DIR    the scene's output folder (may be empty)
// writes the store values into the bound Text (text) and Read (file) nodes
// and redirects the Write nodes to the output folder.
// Results (and a thumbnail of the output) go to the SceneStore.
class SceneRenderer : public QObject
{
  Q_OBJECT

public:
  SceneRenderer(StateStore *state,
                SceneStore *scenes,
                const QString &rendererPath,
                const QString &applyScriptPath,
                const QString &ffmpegPath,
                const QString &thumbnailDir,
                QObject *parent = nullptr);
  ~SceneRenderer();

  // Adds items of a scene to the queue (already queued/rendering ones are
  // skipped).
  void enqueue(const QString &sceneId, const QStringList &items);

  // Renders items again with the current values: queued like enqueue(), and
  // an item that is rendering right now is stopped and restarted.
  void rerender(const QString &sceneId, const QStringList &items);

  // Kills the current render and clears the queue.
  void stop();

  // Cancels one job: removes it from the queue, or kills it if it is the
  // one rendering (the queue then continues).
  void cancel(const QString &sceneId, const QString &item);

  // Pausing suspends the renderer process (POSIX only, see canPause()).
  bool canPause() const;
  void pause();
  void resume();
  bool isPaused() const;

  bool isBusy() const;
  bool isQueued(const QString &sceneId, const QString &item) const;
  bool isCurrent(const QString &sceneId, const QString &item) const;
  QString currentSceneId() const;
  QString currentItem() const;
  int queuedCount() const;

  // Live progress (inactive unless this job is rendering).
  RenderProgress progress(const QString &sceneId, const QString &item) const;

  QString rendererPath() const;

  // Output an item of the scene renders to (its Write node path, moved to
  // the scene's output folder if it has one). Empty: no such Write node.
  static QString itemOutput(const QString &outputDir, const QString &item);

  // Writes the Python script applied by NatronRenderer before rendering.
  static bool writeApplyScript(const QString &path);

Q_SIGNALS:
  // Queue, current job or pause state changed.
  void statusChanged();

  // New progress line for the job being rendered.
  void progressChanged(const QString &sceneId, const QString &item);

private Q_SLOTS:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onError(QProcess::ProcessError error);
  void onOutput();

private:
  struct Job
  {
    QString sceneId;
    QString item;

    bool operator==(const Job &other) const
    {
      return sceneId == other.sceneId && item == other.item;
    }
  };

  void startNext();
  void finishCurrent(bool ok, const QString &message);
  QString makeThumbnail(const Job &job, const QString &output);
  void parseOutputLine(const QString &line);
  bool signalProcess(int signal);
  void setRecordStatus(const Job &job, int status, const QString &message);
  bool moveStagedOutputs(QString *error);
  void removeStagingDir();

  StateStore *m_state;
  SceneStore *m_scenes;
  QString m_rendererPath;
  QString m_applyScriptPath;
  QString m_ffmpegPath;
  QString m_thumbnailDir;

  QList<Job> m_queue;
  Job m_current;
  QProcess *m_process;
  QByteArray m_outputTail;
  QByteArray m_partialLine;
  RenderProgress m_progress;
  bool m_stopping; // the current render is being cancelled
  // Movie outputs are rendered into a staging folder and moved over their
  // final file once complete, so a channel playing the previous render never
  // reads a half-written file.
  QString m_stagingDir;  // empty: rendering straight to the final file
  QString m_finalOutput; // where the staged file goes
};
