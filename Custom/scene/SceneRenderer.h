#pragma once

#include <QList>
#include <QPair>
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

// Renders scene items (Write nodes of projects, see SceneItem) with
// NatronRenderer, up to maxParallel() at once:
//   NatronRenderer -l <applyScript> -w <Write node> <project.ntp>
// The apply script (see writeApplyScript) runs after the project is loaded
// and, using the environment set here:
//   NATRON_STATE_JSON    saved state store: values of the bound keys
//   NATRON_STATE_KEYMAP  the scene's key renaming (JSON object)
//   NATRON_OUTPUT_DIR    the scene's output folder (may be empty)
// writes the store values into the bound Text (text) and Read (file) nodes
// and redirects the Write nodes to the output folder.
// A render that fails once it ran (crash, renderer error) is retried once.
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

  // Kills the running renders and clears the queue.
  void stop();

  // Cancels one job: removes it from the queue, or kills it if it is
  // rendering (the queue then continues).
  void cancel(const QString &sceneId, const QString &item);

  // Pausing suspends a renderer process (POSIX only, see canPause()).
  bool canPause() const;
  void pause(const QString &sceneId, const QString &item);
  void resume(const QString &sceneId, const QString &item);

  // How many renders run at once (default: 2 with 4 cores or more, else 1).
  int maxParallel() const;
  void setMaxParallel(int count);

  bool isBusy() const;
  bool isQueued(const QString &sceneId, const QString &item) const;
  bool isCurrent(const QString &sceneId, const QString &item) const; // rendering now
  QList<QPair<QString, QString> > runningItems() const; // (scene id, item)
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
  // Queue, running jobs or pause state changed.
  void statusChanged();

  // New progress line for a job being rendered.
  void progressChanged(const QString &sceneId, const QString &item);

  // A job ended for good (not cancelled, retries done).
  void jobFinished(const QString &sceneId, const QString &item, bool ok, const QString &message);

  // Nothing left to render.
  void allFinished();

private Q_SLOTS:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onError(QProcess::ProcessError error);
  void onOutput();

private:
  struct Job
  {
    QString sceneId;
    QString item;
    int attempt; // 1, then 2 for the retry

    Job()
        : attempt(1)
    {
    }

    bool operator==(const Job &other) const
    {
      return sceneId == other.sceneId && item == other.item;
    }
  };

  // One running render.
  struct Run
  {
    Job job;
    QProcess *process;
    QByteArray outputTail;
    QByteArray partialLine;
    RenderProgress progress;
    bool stopping; // being cancelled
    // Movie outputs are rendered into a staging folder and moved over their
    // final file once complete, so a channel playing the previous render
    // never reads a half-written file.
    QString stagingDir;  // empty: rendering straight to the final file
    QString finalOutput; // where the staged file goes

    Run()
        : process(nullptr),
          stopping(false)
    {
    }
  };

  void startPending();
  bool startJob(const Job &job); // false: not started now (output in use)
  void finishRun(Run *run, bool ok, const QString &message, bool ran);
  Run *findRun(const QString &sceneId, const QString &item) const;
  Run *runOf(QObject *process) const;
  QString makeThumbnail(const Job &job, const QString &output);
  void parseOutputLine(Run *run, const QString &line);
  bool signalProcess(Run *run, int signal);
  void setRecordStatus(const Job &job, int status, const QString &message);
  bool moveStagedOutputs(Run *run, QString *error);
  static void removeStagingDir(const QString &dir);

  StateStore *m_state;
  SceneStore *m_scenes;
  QString m_rendererPath;
  QString m_applyScriptPath;
  QString m_ffmpegPath;
  QString m_thumbnailDir;

  QList<Job> m_queue;
  QList<Run *> m_runs;
  int m_maxParallel;
};
