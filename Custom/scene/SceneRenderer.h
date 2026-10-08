#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class SceneStore;
class StateStore;

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

  bool isBusy() const;
  QString currentProject() const;
  int queuedCount() const;

  QString rendererPath() const;

  // Writes the Python script applied by NatronRenderer before rendering.
  static bool writeApplyScript(const QString &path);

Q_SIGNALS:
  // Queue or current project changed.
  void statusChanged();

private Q_SLOTS:
  void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onError(QProcess::ProcessError error);
  void onOutput();

private:
  void startNext();
  void finishCurrent(bool ok, const QString &message);
  QString makeThumbnail(const QString &project, const QString &output);

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
  bool m_stopping;
};
