#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class SceneStore;
class StateStore;

// Live state of the project being rendered, parsed from the renderer output
// ("Write1 ==> Frame: 12, Progress: 60.0%, 14.2 Fps, Time Remaining: 3 seconds").
struct RenderProgress
{
  bool active;           // this project is the one rendering
  bool paused;
  QString node;          // Write node currently rendering
  double percent;        // of the current Write node, 0-100
  double fps;
  QString timeRemaining; // as printed by the renderer
  int firstFrame;        // lowest / highest frame rendered so far (-1: none)
  int lastFrame;
  int framesDone;

  RenderProgress()
      : active(false),
        paused(false),
        percent(0),
        fps(0),
        firstFrame(-1),
        lastFrame(-1),
        framesDone(0)
  {
  }

  // "1-250", or empty before the first frame.
  QString frameRange() const;
};

// Renders scene projects one after the other with NatronRenderer:
//   NatronRenderer -l <applyScript> <project.ntp>
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

  // Adds projects of a scene to the queue (already queued/rendering ones
  // are skipped).
  void enqueue(const QString &sceneId, const QStringList &projects);

  // Kills the current render and clears the queue.
  void stop();

  // Cancels one job: removes it from the queue, or kills it if it is the
  // one rendering (the queue then continues).
  void cancel(const QString &sceneId, const QString &project);

  // Pausing suspends the renderer process (POSIX only, see canPause()).
  bool canPause() const;
  void pause();
  void resume();
  bool isPaused() const;

  bool isBusy() const;
  bool isQueued(const QString &sceneId, const QString &project) const;
  bool isCurrent(const QString &sceneId, const QString &project) const;
  QString currentSceneId() const;
  QString currentProject() const;
  int queuedCount() const;

  // Live progress (inactive unless this job is rendering).
  RenderProgress progress(const QString &sceneId, const QString &project) const;

  QString rendererPath() const;

  // Output files a project of the scene renders to (the Write node paths,
  // moved to the scene's output folder if it has one).
  static QStringList sceneOutputs(const QString &outputDir, const QString &project);

  // Writes the Python script applied by NatronRenderer before rendering.
  static bool writeApplyScript(const QString &path);

Q_SIGNALS:
  // Queue, current job or pause state changed.
  void statusChanged();

  // New progress line for the job being rendered.
  void progressChanged(const QString &sceneId, const QString &project);

private Q_SLOTS:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onError(QProcess::ProcessError error);
  void onOutput();

private:
  struct Job
  {
    QString sceneId;
    QString project;

    bool operator==(const Job &other) const
    {
      return sceneId == other.sceneId && project == other.project;
    }
  };

  void startNext();
  void finishCurrent(bool ok, const QString &message);
  QString makeThumbnail(const Job &job, const QString &output);
  void parseOutputLine(const QString &line);
  bool signalProcess(int signal);
  void setRecordStatus(const Job &job, int status, const QString &message);

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
};
