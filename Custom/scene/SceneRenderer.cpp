#include "SceneRenderer.h"

#include "ProjectInfo.h"
#include "SceneStore.h"
#include "../state/Json.h"
#include "../state/StateStore.h"
#include "../util/BundledTool.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcessEnvironment>
#include <QRegExp>
#include <QThread>
#include <QVariantMap>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

namespace
{
// Keep only the end of the renderer output for error messages.
const int kOutputTailBytes = 4000;
const int kThumbnailWidth = 320;
const int kFfmpegTimeoutMs = 20000;

// The renderer's own error lines (ERROR: ..., Python tracebacks), or the
// last lines of its output if it printed none.
QString errorLines(const QByteArray &output, int fallbackCount)
{
  const QStringList all = QString::fromUtf8(output).split(QLatin1Char('\n'));
  QStringList errors;
  bool traceback = false;
  for (int i = 0; i < all.size(); ++i)
  {
    const QString line = all.at(i).trimmed();
    if (line.startsWith(QString::fromUtf8("Traceback")))
    {
      traceback = true;
    }
    if (traceback || line.contains(QString::fromUtf8("ERROR")) || line.startsWith(QString::fromUtf8("Error")))
    {
      if (!line.isEmpty())
      {
        errors << line;
      }
    }
  }
  if (errors.isEmpty())
  {
    QStringList lines;
    for (int i = 0; i < all.size(); ++i)
    {
      if (!all.at(i).trimmed().isEmpty())
      {
        lines << all.at(i);
      }
    }
    while (lines.size() > fallbackCount)
    {
      lines.removeFirst();
    }
    return lines.join(QString::fromUtf8("\n")).trimmed();
  }
  while (errors.size() > 12)
  {
    errors.removeFirst();
  }
  return errors.join(QString::fromUtf8("\n"));
}

// File name of an output path written on any OS (C:\\a\\b.mov -> b.mov).
QString outputFileName(const QString &path)
{
  QString p = path;
  p.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return p.section(QLatin1Char('/'), -1);
}
}

SceneRenderer::SceneRenderer(StateStore *state,
                             SceneStore *scenes,
                             const QString &rendererPath,
                             const QString &applyScriptPath,
                             const QString &ffmpegPath,
                             const QString &thumbnailDir,
                             QObject *parent)
    : QObject(parent),
      m_state(state),
      m_scenes(scenes),
      m_rendererPath(rendererPath),
      m_applyScriptPath(applyScriptPath),
      m_ffmpegPath(ffmpegPath),
      m_thumbnailDir(thumbnailDir),
      m_maxParallel(QThread::idealThreadCount() >= 4 ? 2 : 1)
{
}

SceneRenderer::~SceneRenderer()
{
  m_queue.clear();
  for (int i = 0; i < m_runs.size(); ++i)
  {
    Run *run = m_runs.at(i);
    if (run->process)
    {
      run->process->disconnect(this);
      run->process->kill();
      run->process->waitForFinished(3000);
      delete run->process;
    }
    delete run;
  }
  m_runs.clear();
}

QString SceneRenderer::rendererPath() const
{
  return m_rendererPath;
}

int SceneRenderer::maxParallel() const
{
  return m_maxParallel;
}

void SceneRenderer::setMaxParallel(int count)
{
  m_maxParallel = qMax(1, count);
  startPending();
}

bool SceneRenderer::isBusy() const
{
  return !m_runs.isEmpty();
}

QList<QPair<QString, QString> > SceneRenderer::runningItems() const
{
  QList<QPair<QString, QString> > items;
  for (int i = 0; i < m_runs.size(); ++i)
  {
    items << qMakePair(m_runs.at(i)->job.sceneId, m_runs.at(i)->job.item);
  }
  return items;
}

int SceneRenderer::queuedCount() const
{
  return m_queue.size();
}

bool SceneRenderer::isQueued(const QString &sceneId, const QString &item) const
{
  Job job;
  job.sceneId = sceneId;
  job.item = item;
  return m_queue.contains(job);
}

SceneRenderer::Run *SceneRenderer::findRun(const QString &sceneId, const QString &item) const
{
  for (int i = 0; i < m_runs.size(); ++i)
  {
    if (m_runs.at(i)->job.sceneId == sceneId && m_runs.at(i)->job.item == item)
    {
      return m_runs.at(i);
    }
  }
  return nullptr;
}

SceneRenderer::Run *SceneRenderer::runOf(QObject *process) const
{
  for (int i = 0; i < m_runs.size(); ++i)
  {
    if (process && m_runs.at(i)->process == process)
    {
      return m_runs.at(i);
    }
  }
  return nullptr;
}

bool SceneRenderer::isCurrent(const QString &sceneId, const QString &item) const
{
  return findRun(sceneId, item) != nullptr;
}

RenderProgress SceneRenderer::progress(const QString &sceneId, const QString &item) const
{
  const Run *run = findRun(sceneId, item);
  return run ? run->progress : RenderProgress();
}

void SceneRenderer::setRecordStatus(const Job &job, int status, const QString &message)
{
  RenderRecord record = m_scenes->renderRecord(job.sceneId, job.item);
  record.status = static_cast<RenderRecord::Status>(status);
  record.message = message;
  m_scenes->setRenderRecord(job.sceneId, job.item, record);
}

void SceneRenderer::enqueue(const QString &sceneId, const QStringList &items)
{
  for (int i = 0; i < items.size(); ++i)
  {
    Job job;
    job.sceneId = sceneId;
    job.item = items.at(i);
    if (isCurrent(job.sceneId, job.item) || m_queue.contains(job))
    {
      continue;
    }
    m_queue << job;
    setRecordStatus(job, RenderRecord::eQueued, QString());
  }

  Q_EMIT statusChanged();
  startPending();
}

void SceneRenderer::rerender(const QString &sceneId, const QStringList &items)
{
  for (int i = 0; i < items.size(); ++i)
  {
    Job job;
    job.sceneId = sceneId;
    job.item = items.at(i);

    Run *run = findRun(job.sceneId, job.item);
    if (run && run->process)
    {
      // Restart: kill it (its end starts the queue) and queue it again.
      run->stopping = true;
      run->process->kill();
    }
    else if (!m_queue.contains(job))
    {
      setRecordStatus(job, RenderRecord::eQueued, QString());
    }
    if (!m_queue.contains(job))
    {
      m_queue << job;
    }
  }

  Q_EMIT statusChanged();
  startPending();
}

void SceneRenderer::stop()
{
  const QList<Job> cancelled = m_queue;
  m_queue.clear();

  for (int i = 0; i < cancelled.size(); ++i)
  {
    setRecordStatus(cancelled.at(i), RenderRecord::eNone, tr("Cancelled"));
  }

  for (int i = 0; i < m_runs.size(); ++i)
  {
    if (m_runs.at(i)->process)
    {
      m_runs.at(i)->stopping = true;
      m_runs.at(i)->process->kill();
    }
  }

  Q_EMIT statusChanged();
}

void SceneRenderer::cancel(const QString &sceneId, const QString &item)
{
  Job job;
  job.sceneId = sceneId;
  job.item = item;

  if (m_queue.removeAll(job) > 0)
  {
    setRecordStatus(job, RenderRecord::eNone, tr("Cancelled"));
    Q_EMIT statusChanged();
  }

  Run *run = findRun(sceneId, item);
  if (run && run->process)
  {
    run->stopping = true;
    run->process->kill(); // SIGKILL also ends a stopped (paused) process
  }
}

bool SceneRenderer::canPause() const
{
#ifdef Q_OS_UNIX
  return true;
#else
  return false;
#endif
}

bool SceneRenderer::signalProcess(Run *run, int sig)
{
#ifdef Q_OS_UNIX
  if (!run || !run->process)
  {
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
  const qint64 pid = run->process->processId();
#else
  const qint64 pid = run->process->pid();
#endif
  return pid > 0 && ::kill(static_cast<pid_t>(pid), sig) == 0;
#else
  Q_UNUSED(run);
  Q_UNUSED(sig);
  return false;
#endif
}

void SceneRenderer::pause(const QString &sceneId, const QString &item)
{
#ifdef Q_OS_UNIX
  Run *run = findRun(sceneId, item);
  if (run && !run->progress.paused && signalProcess(run, SIGSTOP))
  {
    run->progress.paused = true;
    Q_EMIT progressChanged(sceneId, item);
    Q_EMIT statusChanged();
  }
#else
  Q_UNUSED(sceneId);
  Q_UNUSED(item);
#endif
}

void SceneRenderer::resume(const QString &sceneId, const QString &item)
{
#ifdef Q_OS_UNIX
  Run *run = findRun(sceneId, item);
  if (run && run->progress.paused && signalProcess(run, SIGCONT))
  {
    run->progress.paused = false;
    Q_EMIT progressChanged(sceneId, item);
    Q_EMIT statusChanged();
  }
#else
  Q_UNUSED(sceneId);
  Q_UNUSED(item);
#endif
}

QString SceneRenderer::itemOutput(const QString &outputDir, const QString &item)
{
  const QString output = readProjectInfo(SceneItem::project(item)).writerOutputs.value(SceneItem::writer(item));

  if (output.isEmpty() || outputDir.isEmpty())
  {
    return output;
  }
  return QDir(outputDir).absoluteFilePath(outputFileName(output));
}

void SceneRenderer::startPending()
{
  // Start queued jobs while there is room. A job writing the same file as a
  // running one waits for it (two scenes without an output folder render a
  // project's Write node to the same file).
  bool started = true;
  while (started && m_runs.size() < m_maxParallel)
  {
    started = false;
    for (int i = 0; i < m_queue.size(); ++i)
    {
      const Job job = m_queue.at(i);
      m_queue.removeAt(i);
      if (startJob(job))
      {
        started = true;
        break;
      }
      m_queue.insert(i, job);
    }
  }

  if (m_runs.isEmpty() && m_queue.isEmpty())
  {
    Q_EMIT statusChanged();
    Q_EMIT allFinished();
  }
}

bool SceneRenderer::startJob(const Job &job)
{
  Run *run = new Run;
  run->job = job;
  run->progress.active = true;

  Scene scene;
  const bool sceneExists = m_scenes->scene(job.sceneId, &scene);
  const QString project = SceneItem::project(job.item);
  const QString writer = SceneItem::writer(job.item);
  const QString output = sceneExists ? itemOutput(scene.outputDir, job.item) : QString();
  const QString finalOutput = output.isEmpty() ? QString() : QFileInfo(project).absoluteDir().absoluteFilePath(output);

  for (int i = 0; i < m_runs.size(); ++i)
  {
    if (!finalOutput.isEmpty() && m_runs.at(i)->finalOutput == finalOutput)
    {
      delete run;
      return false; // starts when that one is done
    }
  }

  m_runs << run;

  if (!sceneExists)
  {
    finishRun(run, false, tr("Scene was deleted"), false);
    return true;
  }
  if (!QFile::exists(project))
  {
    finishRun(run, false, tr("Project file not found"), false);
    return true;
  }
  if (!QFile::exists(m_rendererPath))
  {
    finishRun(run, false, tr("NatronRenderer not found at %1").arg(m_rendererPath), false);
    return true;
  }
  const ProjectInfo info = readProjectInfo(project);
  if (writer.isEmpty())
  {
    finishRun(run, false, tr("The project has no Write node: open it in the editor, add a Write node "
                             "with an output file, save, and render again."), false);
    return true;
  }
  if (!info.writers.contains(writer))
  {
    finishRun(run, false, tr("The project has no Write node named %1 any more (renamed or deleted?): "
                             "remove it from the scene and add the project's Write nodes again.").arg(writer), false);
    return true;
  }
  if (output.isEmpty())
  {
    finishRun(run, false, tr("%1 has no output file: set one in the editor, save, and render again.").arg(writer), false);
    return true;
  }
  if (!scene.outputDir.isEmpty() && !QDir().mkpath(scene.outputDir))
  {
    finishRun(run, false, tr("Cannot create the output folder %1").arg(scene.outputDir), false);
    return true;
  }

  // Movies: render next to the final file (same disk: the move is a rename),
  // in a folder of this job's own (renders run in parallel).
  // Image sequences are written frame by frame (live output reads them), so
  // they still go straight to their final files.
  run->finalOutput = finalOutput;
  if (!isSequencePattern(output))
  {
    const QByteArray id = (job.sceneId + QLatin1Char('|') + job.item).toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Md5).toHex().left(8));
    run->stagingDir = QFileInfo(run->finalOutput).absoluteDir().absoluteFilePath(QString::fromUtf8(".natron-render-") + hash);
    removeStagingDir(run->stagingDir);
    if (!QDir().mkpath(run->stagingDir))
    {
      run->stagingDir.clear(); // render in place
    }
  }

  // The renderer reads the values from the saved store.
  if (m_state)
  {
    m_state->saveNow();
  }

  setRecordStatus(job, RenderRecord::eRendering,
                  job.attempt > 1 ? tr("Retrying after a failed render") : QString());

  QProcess *process = new QProcess(this);
  run->process = process;
  process->setProcessChannelMode(QProcess::MergedChannels);
  process->setWorkingDirectory(QFileInfo(project).absolutePath());

  QVariantMap keyMap;
  for (QHash<QString, QString>::const_iterator it = scene.keyMap.constBegin(); it != scene.keyMap.constEnd(); ++it)
  {
    keyMap.insert(it.key(), it.value());
  }

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (m_state)
  {
    env.insert(QString::fromUtf8("NATRON_STATE_JSON"), m_state->filePath());
  }
  env.insert(QString::fromUtf8("NATRON_STATE_KEYMAP"), QString::fromUtf8(Json::serialize(keyMap)));
  env.insert(QString::fromUtf8("NATRON_OUTPUT_DIR"), run->stagingDir.isEmpty() ? scene.outputDir : run->stagingDir);
  process->setProcessEnvironment(env);

  connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onFinished(int,QProcess::ExitStatus)));
  connect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(onError(QProcess::ProcessError)));
  connect(process, SIGNAL(readyRead()), this, SLOT(onOutput()));

  QStringList args;
  if (QFile::exists(m_applyScriptPath))
  {
    args << QString::fromUtf8("-l") << m_applyScriptPath;
  }
  args << QString::fromUtf8("-w") << writer << project;

  Q_EMIT statusChanged();
  process->start(m_rendererPath, args);
  return true;
}

void SceneRenderer::onOutput()
{
  Run *run = runOf(sender());
  if (!run)
  {
    return;
  }

  const QByteArray data = run->process->readAll();

  run->outputTail.append(data);
  if (run->outputTail.size() > kOutputTailBytes)
  {
    run->outputTail = run->outputTail.right(kOutputTailBytes);
  }

  // Progress is reported line by line; keep an incomplete last line.
  run->partialLine.append(data);
  int newline;
  while ((newline = run->partialLine.indexOf('\n')) >= 0)
  {
    parseOutputLine(run, QString::fromUtf8(run->partialLine.left(newline)).trimmed());
    run->partialLine.remove(0, newline + 1);
  }
}

void SceneRenderer::parseOutputLine(Run *run, const QString &line)
{
  // "Write1 ==> Frame: 12, Progress: 60.0%, 14.2 Fps, Time Remaining: 3 seconds"
  static const QRegExp frameLine(QString::fromUtf8(
      "^(\\S+) ==> Frame: (-?\\d+), Progress: ([0-9.]+)%, ([0-9.]+) Fps, Time Remaining: (.*)$"));
  // "Write1 ==> Rendering started" / "Write1 ==> Rendering finished"
  static const QRegExp stateLine(QString::fromUtf8("^(\\S+) ==> Rendering (started|finished)$"));

  QRegExp frame(frameLine);
  QRegExp state(stateLine);
  RenderProgress &progress = run->progress;

  if (frame.exactMatch(line))
  {
    progress.currentFrame = frame.cap(2).toInt();
    progress.node = frame.cap(1);
    progress.percent = frame.cap(3).toDouble();
    progress.fps = frame.cap(4).toDouble();
    progress.timeRemaining = frame.cap(5).trimmed();
    Q_EMIT progressChanged(run->job.sceneId, run->job.item);
  }
  else if (state.exactMatch(line))
  {
    progress.node = state.cap(1);
    if (state.cap(2) == QString::fromUtf8("started"))
    {
      progress.percent = 0;
      progress.timeRemaining.clear();
    }
    else
    {
      progress.percent = 100;
      progress.timeRemaining = tr("Done");
    }
    Q_EMIT progressChanged(run->job.sceneId, run->job.item);
  }
}

void SceneRenderer::onError(QProcess::ProcessError error)
{
  // Crashes and kills also end in finished(); only handle failure to start.
  Run *run = runOf(sender());
  if (error == QProcess::FailedToStart && run)
  {
    const QString message = tr("Could not start %1: %2").arg(m_rendererPath).arg(run->process->errorString());
    run->process->deleteLater();
    run->process = nullptr;
    finishRun(run, false, message, false);
  }
}

void SceneRenderer::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  Run *run = runOf(sender());
  if (!run)
  {
    return;
  }

  const QByteArray rest = run->process->readAll();
  run->outputTail.append(rest);
  run->partialLine.append(rest);
  const QStringList lines = QString::fromUtf8(run->partialLine).split(QLatin1Char('\n'));
  for (int i = 0; i < lines.size(); ++i)
  {
    parseOutputLine(run, lines.at(i).trimmed());
  }
  run->partialLine.clear();
  run->process->deleteLater();
  run->process = nullptr;

  if (run->stopping)
  {
    finishRun(run, false, tr("Cancelled"), true);
  }
  else if (exitStatus != QProcess::NormalExit)
  {
    finishRun(run, false, tr("Renderer crashed.\n%1").arg(errorLines(run->outputTail, 8)), true);
  }
  else if (exitCode != 0)
  {
    // First line: the reason (shown in the status column); then details.
    const QString errors = errorLines(run->outputTail, 8);
    const QString reason = errors.section(QLatin1Char('\n'), 0, 0).remove(QString::fromUtf8("ERROR: ")).remove(QString::fromUtf8("Natron: ")).trimmed();
    finishRun(run, false, tr("%1\n(renderer exit code %2)\n%3").arg(reason).arg(exitCode).arg(errors), true);
  }
  else
  {
    finishRun(run, true, QString(), true);
  }
}

void SceneRenderer::finishRun(Run *run, bool ok, const QString &message, bool ran)
{
  const Job job = run->job;
  const bool stopping = run->stopping;
  m_runs.removeAll(run);

  RenderRecord record = m_scenes->renderRecord(job.sceneId, job.item);
  record.time = QDateTime::currentDateTime();
  record.message = message;

  QString moveError;
  if (ok && !run->stagingDir.isEmpty() && !moveStagedOutputs(run, &moveError))
  {
    ok = false;
    record.message = moveError;
  }
  removeStagingDir(run->stagingDir);
  delete run;

  // A render that failed once it ran (crash, renderer error) may be a
  // passing problem: try it once more, first in the queue.
  const int kMaxAttempts = 2;
  if (!ok && !stopping && ran && job.attempt < kMaxAttempts)
  {
    Job retry = job;
    ++retry.attempt;
    m_queue.prepend(retry);
    record.status = RenderRecord::eQueued;
    record.message = tr("Failed, retrying: %1").arg(message.section(QLatin1Char('\n'), 0, 0));
    m_scenes->setRenderRecord(job.sceneId, job.item, record);
    startPending();
    return;
  }

  if (ok)
  {
    record.status = RenderRecord::eDone;
    record.output.clear();
    record.thumbnail.clear();

    Scene scene;
    m_scenes->scene(job.sceneId, &scene);
    const QString output = itemOutput(scene.outputDir, job.item);
    record.output = findExistingOutputFile(output);
    if (record.output.isEmpty())
    {
      record.message = output.isEmpty() ? tr("Rendered, but the Write node has no output file.")
                                        : tr("Rendered, but no output file was found at %1").arg(output);
    }
    else
    {
      record.thumbnail = makeThumbnail(job, record.output);
    }
  }
  else
  {
    record.status = stopping ? RenderRecord::eNone : RenderRecord::eFailed;
  }

  m_scenes->setRenderRecord(job.sceneId, job.item, record);
  if (!stopping)
  {
    Q_EMIT jobFinished(job.sceneId, job.item, ok, record.message);
  }

  Q_EMIT statusChanged();
  startPending();
}

bool SceneRenderer::moveStagedOutputs(Run *run, QString *error)
{
  const QString target = run->finalOutput;
  const QString staged = QDir(run->stagingDir).absoluteFilePath(outputFileName(target));
  if (!QFile::exists(staged))
  {
    return true; // reported as "no output file was found"
  }
#ifdef Q_OS_UNIX
  // Atomic replace: a reader of the old file keeps reading it.
  const bool moved = ::rename(QFile::encodeName(staged).constData(), QFile::encodeName(target).constData()) == 0;
#else
  QFile::remove(target);
  const bool moved = QFile::rename(staged, target);
#endif
  // Another disk: copy instead.
  if (!moved && !((!QFile::exists(target) || QFile::remove(target)) && QFile::copy(staged, target)))
  {
    *error = tr("Rendered, but could not replace %1").arg(target);
    return false;
  }
  return true;
}

void SceneRenderer::removeStagingDir(const QString &path)
{
  if (path.isEmpty())
  {
    return;
  }
  QDir dir(path);
  if (!dir.exists())
  {
    return;
  }
  const QStringList files = dir.entryList(QDir::Files | QDir::Hidden | QDir::System);
  for (int i = 0; i < files.size(); ++i)
  {
    dir.remove(files.at(i));
  }
  QDir().rmdir(path);
}

QString SceneRenderer::makeThumbnail(const Job &job, const QString &output)
{
  if (m_thumbnailDir.isEmpty())
  {
    return QString();
  }

  QDir().mkpath(m_thumbnailDir);
  const QByteArray id = (job.sceneId + QLatin1Char('|') + job.item).toUtf8();
  const QString hash = QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Md5).toHex());
  const QString thumbPath = QDir(m_thumbnailDir).absoluteFilePath(hash + QString::fromUtf8(".png"));
  QFile::remove(thumbPath);

  // Still images Qt can read directly.
  QImage image(output);
  if (!image.isNull())
  {
    image = image.scaledToWidth(kThumbnailWidth, Qt::SmoothTransformation);

    // Renders often have alpha (e.g. white text on transparent): show them
    // over a dark background rather than as an invisible white-on-white.
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(QColor(32, 32, 32).rgb());
    QPainter painter(&flat);
    painter.drawImage(0, 0, image);
    painter.end();

    return flat.save(thumbPath) ? thumbPath : QString();
  }

  // Videos and other formats (EXR...): first frame through ffmpeg.
  if (m_ffmpegPath.isEmpty() || !QFile::exists(m_ffmpegPath))
  {
    return QString();
  }

  QStringList args;
  args << QString::fromUtf8("-y") << QString::fromUtf8("-loglevel") << QString::fromUtf8("error")
       << QString::fromUtf8("-i") << output
       << QString::fromUtf8("-frames:v") << QString::fromUtf8("1")
       << QString::fromUtf8("-vf") << QString::fromUtf8("scale=%1:-2").arg(kThumbnailWidth)
       << thumbPath;

  QProcess ffmpeg;
  prepareBundledTool(&ffmpeg, m_ffmpegPath);
  ffmpeg.start(m_ffmpegPath, args);
  if (!ffmpeg.waitForFinished(kFfmpegTimeoutMs))
  {
    ffmpeg.kill();
    ffmpeg.waitForFinished(1000);
    return QString();
  }

  return QFile::exists(thumbPath) ? thumbPath : QString();
}

bool SceneRenderer::writeApplyScript(const QString &path)
{
  // Runs inside NatronRenderer (Python 2 or 3) after the project is loaded.
  static const char script[] =
      "# Generated by Natron (dashboard scenes). Before NatronRenderer renders:\n"
      "# - writes the state store values into the nodes bound to a key\n"
      "#   (Text -> text, Read -> file), using the scene's key renaming;\n"
      "# - moves the Write node outputs to the scene's output folder.\n"
      "import io\n"
      "import json\n"
      "import os\n"
      "\n"
      "# plugin -> (parameter receiving the value, value type it needs)\n"
      "_NATRON_STATE_TARGETS = {\n"
      "    'net.fxarena.openfx.Text': ('text', 'text'),\n"
      "    'fr.inria.built-in.Read': ('filename', 'image'),\n"
      "}\n"
      "_NATRON_STATE_WRITE = 'fr.inria.built-in.Write'\n"
      "\n"
      "\n"
      "def _natron_state_text(value):\n"
      "    # Text node: the text of a text, number or on/off value (None otherwise).\n"
      "    t = value.get('type')\n"
      "    v = value.get('value')\n"
      "    if t == 'text':\n"
      "        return u'%s' % (v if v is not None else u'',)\n"
      "    if t == 'number':\n"
      "        return u'%.15g' % (float(v),)\n"
      "    if t == 'bool':\n"
      "        return u'true' if v else u'false'\n"
      "    return None\n"
      "\n"
      "\n"
      "def _natron_state_linear(hexcolor):\n"
      "    # '#rrggbb' / '#rrggbbaa' (sRGB) -> linear RGBA, as color parameters store it.\n"
      "    h = (hexcolor or u'').strip().lstrip(u'#')\n"
      "    if len(h) not in (6, 8):\n"
      "        return None\n"
      "    try:\n"
      "        c = [int(h[i:i + 2], 16) / 255.0 for i in range(0, len(h), 2)]\n"
      "    except ValueError:\n"
      "        return None\n"
      "    if len(c) == 3:\n"
      "        c.append(1.0)\n"
      "    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c[:3]] + [c[3]]\n"
      "\n"
      "\n"
      "def _natron_state_set(node, target_name, wanted, value):\n"
      "    # Applies value to the node; returns the parameter set, or None if the\n"
      "    # value does not fit (Read: images; Text: text, numbers, on/off as its\n"
      "    # text, colors as its text color).\n"
      "    target = node.getParam(target_name)\n"
      "    if isinstance(value, dict) and 'type' in value:\n"
      "        t = value['type']\n"
      "        if wanted == 'image':\n"
      "            if t != 'image':\n"
      "                return None\n"
      "            target.setValue(u'%s' % (value.get('path') or u'',))\n"
      "            return target_name\n"
      "        if t == 'color':\n"
      "            rgba = _natron_state_linear(value.get('value'))\n"
      "            color = node.getParam('color')\n"
      "            if rgba is None or color is None:\n"
      "                return None\n"
      "            for i in range(color.getNumDimensions()):\n"
      "                color.setValue(rgba[i], i)\n"
      "            return 'color'\n"
      "        text = _natron_state_text(value)\n"
      "        if text is None:\n"
      "            return None\n"
      "        target.setValue(text)\n"
      "        return target_name\n"
      "    # Older plain values.\n"
      "    if value is None:\n"
      "        text = u''\n"
      "    elif isinstance(value, bool):\n"
      "        text = u'true' if value else u'false'\n"
      "    elif isinstance(value, (dict, list)):\n"
      "        text = json.dumps(value, separators=(',', ':'), sort_keys=True, ensure_ascii=False)\n"
      "    else:\n"
      "        text = u'%s' % (value,)\n"
      "    target.setValue(text)\n"
      "    return target_name\n"
      "\n"
      "\n"
      "def _natron_state_apply(group, values, key_map, output_dir):\n"
      "    for node in group.getChildren():\n"
      "        plugin = node.getPluginID()\n"
      "        target_info = _NATRON_STATE_TARGETS.get(plugin)\n"
      "        key_param = node.getParam('" kProjectInfoStateKeyParam "')\n"
      "        if key_param is not None and target_info:\n"
      "            target_name, wanted = target_info\n"
      "            bound = key_param.getValue()\n"
      "            key = key_map.get(bound, bound)\n"
      "            if bound and node.getParam(target_name) is not None:\n"
      "                if key not in values:\n"
      "                    print('natron-state: %s: key %s not in store, unchanged' % (node.getScriptName(), key))\n"
      "                else:\n"
      "                    applied = _natron_state_set(node, target_name, wanted, values[key])\n"
      "                    if applied is None:\n"
      "                        print('natron-state: %s: key %s does not fit a %s binding, unchanged' % (node.getScriptName(), key, wanted))\n"
      "                    else:\n"
      "                        print('natron-state: %s.%s <- %s' % (node.getScriptName(), applied, key))\n"
      "        if output_dir and plugin == _NATRON_STATE_WRITE:\n"
      "            out = node.getParam('filename')\n"
      "            if out is not None and out.getValue():\n"
      "                name = out.getValue().replace('\\\\', '/').split('/')[-1]\n"
      "                out.setValue(os.path.join(output_dir, name))\n"
      "                print('natron-state: %s output -> %s' % (node.getScriptName(), out.getValue()))\n"
      "        _natron_state_apply(node, values, key_map, output_dir)\n"
      "\n"
      "\n"
      "def _natron_state_main():\n"
      "    values = {}\n"
      "    path = os.environ.get('NATRON_STATE_JSON')\n"
      "    if path and os.path.exists(path):\n"
      "        with io.open(path, encoding='utf-8') as f:\n"
      "            values = json.load(f)\n"
      "    else:\n"
      "        print('natron-state: no state file')\n"
      "    key_map = json.loads(os.environ.get('NATRON_STATE_KEYMAP') or '{}')\n"
      "    output_dir = os.environ.get('NATRON_OUTPUT_DIR') or ''\n"
      "    _natron_state_apply(app, values, key_map, output_dir)\n"
      "\n"
      "\n"
      "_natron_state_main()\n";

  QDir().mkpath(QFileInfo(path).absolutePath());

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    return false;
  }
  return file.write(script) >= 0;
}
