#pragma once

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

// Renders projects one after the other with NatronRenderer:
//   NatronRenderer -l <applyScript> <project.ntp>
// with NATRON_STATE_JSON pointing at the saved state store, so the script
// can write the current values into the bound Text nodes before rendering.
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

  // Adds projects to the queue (already queued/rendering ones are skipped).
  void enqueue(const QStringList &projects);

  // Kills the current render and clears the queue.
  void stop();

  // Cancels one project: removes it from the queue, or kills it if it is
  // the one rendering (the queue then continues).
  void cancel(const QString &project);

  // Pausing suspends the renderer process (POSIX only, see canPause()).
  bool canPause() const;
  void pause();
  void resume();
  bool isPaused() const;

  bool isBusy() const;
  bool isQueued(const QString &project) const;
  QString currentProject() const;
  int queuedCount() const;

  // Live progress of project (inactive unless it is rendering).
  RenderProgress progress(const QString &project) const;

  QString rendererPath() const;

  // Writes the Python script applied by NatronRenderer before rendering.
  static bool writeApplyScript(const QString &path);

Q_SIGNALS:
  // Queue, current project or pause state changed.
  void statusChanged();

  // New progress line for the project being rendered.
  void progressChanged(const QString &project);

private Q_SLOTS:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onError(QProcess::ProcessError error);
  void onOutput();

private:
  void startNext();
  void finishCurrent(bool ok, const QString &message);
  QString makeThumbnail(const QString &project, const QString &output);
  void parseOutputLine(const QString &line);
  bool signalProcess(int signal);

  StateStore *m_state;
  SceneStore *m_scenes;
  QString m_rendererPath;
  QString m_applyScriptPath;
  QString m_ffmpegPath;
  QString m_thumbnailDir;

  QStringList m_queue;
  QString m_current;
  QProcess *m_process;
  QByteArray m_outputTail;
  QByteArray m_partialLine;
  RenderProgress m_progress;
  bool m_stopping; // the current render is being cancelled
};
