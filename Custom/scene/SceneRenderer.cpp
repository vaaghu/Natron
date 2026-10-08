#include "SceneRenderer.h"

#include "ProjectInfo.h"
#include "SceneStore.h"
#include "../state/StateStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QProcessEnvironment>
#include <QRegExp>

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

QString lastLines(const QByteArray &output, int count)
{
  QStringList lines;
  const QStringList all = QString::fromUtf8(output).split(QLatin1Char('\n'));
  for (int i = 0; i < all.size(); ++i)
  {
    if (!all.at(i).trimmed().isEmpty())
    {
      lines << all.at(i);
    }
  }
  while (lines.size() > count)
  {
    lines.removeFirst();
  }
  return lines.join(QString::fromUtf8("\n")).trimmed();
}
}

QString RenderProgress::frameRange() const
{
  if (firstFrame < 0)
  {
    return QString();
  }
  if (firstFrame == lastFrame)
  {
    return QString::number(firstFrame);
  }
  return QString::fromUtf8("%1-%2").arg(firstFrame).arg(lastFrame);
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
      m_process(nullptr),
      m_stopping(false)
{
}

SceneRenderer::~SceneRenderer()
{
  m_queue.clear();
  if (m_process)
  {
    m_stopping = true;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(3000);
    delete m_process;
  }
}

QString SceneRenderer::rendererPath() const
{
  return m_rendererPath;
}

bool SceneRenderer::isBusy() const
{
  return m_process != nullptr;
}

QString SceneRenderer::currentProject() const
{
  return m_current;
}

int SceneRenderer::queuedCount() const
{
  return m_queue.size();
}

bool SceneRenderer::isQueued(const QString &project) const
{
  return m_queue.contains(project);
}

RenderProgress SceneRenderer::progress(const QString &project) const
{
  if (m_process && project == m_current)
  {
    return m_progress;
  }
  return RenderProgress();
}

bool SceneRenderer::canPause() const
{
#ifdef Q_OS_UNIX
  return true;
#else
  return false;
#endif
}

bool SceneRenderer::isPaused() const
{
  return m_process && m_progress.paused;
}

bool SceneRenderer::signalProcess(int sig)
{
#ifdef Q_OS_UNIX
  if (!m_process)
  {
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
  const qint64 pid = m_process->processId();
#else
  const qint64 pid = m_process->pid();
#endif
  return pid > 0 && ::kill(static_cast<pid_t>(pid), sig) == 0;
#else
  Q_UNUSED(sig);
  return false;
#endif
}

void SceneRenderer::pause()
{
#ifdef Q_OS_UNIX
  if (m_process && !m_progress.paused && signalProcess(SIGSTOP))
  {
    m_progress.paused = true;
    Q_EMIT progressChanged(m_current);
    Q_EMIT statusChanged();
  }
#endif
}

void SceneRenderer::resume()
{
#ifdef Q_OS_UNIX
  if (m_process && m_progress.paused && signalProcess(SIGCONT))
  {
    m_progress.paused = false;
    Q_EMIT progressChanged(m_current);
    Q_EMIT statusChanged();
  }
#endif
}

void SceneRenderer::cancel(const QString &project)
{
  if (m_queue.removeAll(project) > 0)
  {
    RenderRecord record = m_scenes->renderRecord(project);
    record.status = RenderRecord::eNone;
    record.message = tr("Cancelled");
    m_scenes->setRenderRecord(project, record);
    Q_EMIT statusChanged();
  }

  if (m_process && project == m_current)
  {
    m_stopping = true;
    m_process->kill(); // SIGKILL also ends a stopped (paused) process
  }
}

void SceneRenderer::enqueue(const QStringList &projects)
{
  for (int i = 0; i < projects.size(); ++i)
  {
    const QString &project = projects.at(i);
    if (project == m_current || m_queue.contains(project))
    {
      continue;
    }
    m_queue << project;

    RenderRecord record = m_scenes->renderRecord(project);
    record.status = RenderRecord::eQueued;
    record.message.clear();
    m_scenes->setRenderRecord(project, record);
  }

  Q_EMIT statusChanged();

  if (!m_process)
  {
    startNext();
  }
}

void SceneRenderer::stop()
{
  const QStringList cancelled = m_queue;
  m_queue.clear();

  for (int i = 0; i < cancelled.size(); ++i)
  {
    RenderRecord record = m_scenes->renderRecord(cancelled.at(i));
    record.status = RenderRecord::eNone;
    record.message = tr("Cancelled");
    m_scenes->setRenderRecord(cancelled.at(i), record);
  }

  if (m_process)
  {
    m_stopping = true;
    m_process->kill();
  }

  Q_EMIT statusChanged();
}

void SceneRenderer::startNext()
{
  if (m_queue.isEmpty())
  {
    m_current.clear();
    Q_EMIT statusChanged();
    return;
  }

  m_current = m_queue.takeFirst();
  m_outputTail.clear();
  m_partialLine.clear();
  m_progress = RenderProgress();
  m_progress.active = true;
  m_stopping = false;

  if (!QFile::exists(m_current))
  {
    finishCurrent(false, tr("Project file not found"));
    return;
  }
  if (!QFile::exists(m_rendererPath))
  {
    finishCurrent(false, tr("NatronRenderer not found at %1").arg(m_rendererPath));
    return;
  }

  // The renderer reads the values from the saved store.
  if (m_state)
  {
    m_state->saveNow();
  }

  RenderRecord record = m_scenes->renderRecord(m_current);
  record.status = RenderRecord::eRendering;
  record.message.clear();
  m_scenes->setRenderRecord(m_current, record);

  m_process = new QProcess(this);
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  m_process->setWorkingDirectory(QFileInfo(m_current).absolutePath());

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (m_state)
  {
    env.insert(QString::fromUtf8("NATRON_STATE_JSON"), m_state->filePath());
  }
  m_process->setProcessEnvironment(env);

  connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onFinished(int,QProcess::ExitStatus)));
  connect(m_process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(onError(QProcess::ProcessError)));
  connect(m_process, SIGNAL(readyRead()), this, SLOT(onOutput()));

  QStringList args;
  if (QFile::exists(m_applyScriptPath))
  {
    args << QString::fromUtf8("-l") << m_applyScriptPath;
  }
  args << m_current;

  Q_EMIT statusChanged();
  m_process->start(m_rendererPath, args);
}

void SceneRenderer::onOutput()
{
  if (!m_process)
  {
    return;
  }
  const QByteArray data = m_process->readAll();

  m_outputTail.append(data);
  if (m_outputTail.size() > kOutputTailBytes)
  {
    m_outputTail = m_outputTail.right(kOutputTailBytes);
  }

  // Progress is reported line by line; keep an incomplete last line.
  m_partialLine.append(data);
  int newline;
  while ((newline = m_partialLine.indexOf('\n')) >= 0)
  {
    parseOutputLine(QString::fromUtf8(m_partialLine.left(newline)).trimmed());
    m_partialLine.remove(0, newline + 1);
  }
}

void SceneRenderer::parseOutputLine(const QString &line)
{
  // "Write1 ==> Frame: 12, Progress: 60.0%, 14.2 Fps, Time Remaining: 3 seconds"
  static const QRegExp frameLine(QString::fromUtf8(
      "^(\\S+) ==> Frame: (-?\\d+), Progress: ([0-9.]+)%, ([0-9.]+) Fps, Time Remaining: (.*)$"));
  // "Write1 ==> Rendering started" / "Write1 ==> Rendering finished"
  static const QRegExp stateLine(QString::fromUtf8("^(\\S+) ==> Rendering (started|finished)$"));

  QRegExp frame(frameLine);
  QRegExp state(stateLine);

  if (frame.exactMatch(line))
  {
    const int f = frame.cap(2).toInt();
    m_progress.node = frame.cap(1);
    m_progress.percent = frame.cap(3).toDouble();
    m_progress.fps = frame.cap(4).toDouble();
    m_progress.timeRemaining = frame.cap(5).trimmed();
    m_progress.firstFrame = (m_progress.firstFrame < 0) ? f : qMin(m_progress.firstFrame, f);
    m_progress.lastFrame = (m_progress.lastFrame < 0) ? f : qMax(m_progress.lastFrame, f);
    ++m_progress.framesDone;
    Q_EMIT progressChanged(m_current);
  }
  else if (state.exactMatch(line))
  {
    m_progress.node = state.cap(1);
    if (state.cap(2) == QString::fromUtf8("started"))
    {
      m_progress.percent = 0;
      m_progress.timeRemaining.clear();
    }
    else
    {
      m_progress.percent = 100;
      m_progress.timeRemaining = tr("Done");
    }
    Q_EMIT progressChanged(m_current);
  }
}

void SceneRenderer::onError(QProcess::ProcessError error)
{
  // Crashes and kills also end in finished(); only handle failure to start.
  if (error == QProcess::FailedToStart && m_process)
  {
    const QString message = tr("Could not start %1: %2").arg(m_rendererPath).arg(m_process->errorString());
    m_process->deleteLater();
    m_process = nullptr;
    finishCurrent(false, message);
  }
}

void SceneRenderer::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  if (!m_process)
  {
    return;
  }

  onOutput();
  if (!m_partialLine.isEmpty())
  {
    parseOutputLine(QString::fromUtf8(m_partialLine).trimmed());
    m_partialLine.clear();
  }
  m_process->deleteLater();
  m_process = nullptr;

  if (m_stopping)
  {
    finishCurrent(false, tr("Cancelled"));
  }
  else if (exitStatus != QProcess::NormalExit)
  {
    finishCurrent(false, tr("Renderer crashed.\n%1").arg(lastLines(m_outputTail, 8)));
  }
  else if (exitCode != 0)
  {
    finishCurrent(false, tr("Renderer exited with code %1.\n%2").arg(exitCode).arg(lastLines(m_outputTail, 8)));
  }
  else
  {
    finishCurrent(true, QString());
  }
}

void SceneRenderer::finishCurrent(bool ok, const QString &message)
{
  const QString project = m_current;

  RenderRecord record = m_scenes->renderRecord(project);
  record.time = QDateTime::currentDateTime();
  record.message = message;
  if (!m_progress.frameRange().isEmpty())
  {
    record.frameRange = m_progress.frameRange();
  }
  m_progress = RenderProgress();

  if (ok)
  {
    record.status = RenderRecord::eDone;
    record.output.clear();
    record.thumbnail.clear();

    const ProjectInfo info = readProjectInfo(project);
    for (int i = 0; i < info.outputs.size() && record.output.isEmpty(); ++i)
    {
      record.output = findExistingOutputFile(info.outputs.at(i));
    }
    if (record.output.isEmpty())
    {
      record.message = info.outputs.isEmpty() ? tr("Rendered, but the project has no Write node output.")
                                              : tr("Rendered, but no output file was found at %1").arg(info.outputs.first());
    }
    else
    {
      record.thumbnail = makeThumbnail(project, record.output);
    }
  }
  else
  {
    record.status = m_stopping ? RenderRecord::eNone : RenderRecord::eFailed;
  }

  m_scenes->setRenderRecord(project, record);
  m_current.clear();

  startNext();
}

QString SceneRenderer::makeThumbnail(const QString &project, const QString &output)
{
  if (m_thumbnailDir.isEmpty())
  {
    return QString();
  }

  QDir().mkpath(m_thumbnailDir);
  const QString hash = QString::fromLatin1(QCryptographicHash::hash(project.toUtf8(), QCryptographicHash::Md5).toHex());
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
  // Runs inside NatronRenderer (Python 2 or 3) after the project is loaded:
  // for every node with a natronStateKey parameter, put the store value of
  // that key into its "text" parameter.
  static const char script[] =
      "# Generated by Natron (dashboard). Applies the state store values to\n"
      "# the Text nodes bound to a key before NatronRenderer renders.\n"
      "import io\n"
      "import json\n"
      "import os\n"
      "\n"
      "\n"
      "def _natron_state_text(value):\n"
      "    if value is None:\n"
      "        return u''\n"
      "    if isinstance(value, bool):\n"
      "        return u'true' if value else u'false'\n"
      "    if isinstance(value, (dict, list)):\n"
      "        return json.dumps(value, separators=(',', ':'), sort_keys=True, ensure_ascii=False)\n"
      "    return u'%s' % (value,)\n"
      "\n"
      "\n"
      "def _natron_state_apply(group, values):\n"
      "    for node in group.getChildren():\n"
      "        key_param = node.getParam('" kProjectInfoStateKeyParam "')\n"
      "        text_param = node.getParam('text')\n"
      "        if key_param is not None and text_param is not None:\n"
      "            key = key_param.getValue()\n"
      "            if key and key in values:\n"
      "                text_param.setValue(_natron_state_text(values[key]))\n"
      "                print('natron-state: %s.text <- %s' % (node.getScriptName(), key))\n"
      "            elif key:\n"
      "                print('natron-state: %s: key %s not in store, text unchanged' % (node.getScriptName(), key))\n"
      "        _natron_state_apply(node, values)\n"
      "\n"
      "\n"
      "def _natron_state_main():\n"
      "    path = os.environ.get('NATRON_STATE_JSON')\n"
      "    if not path or not os.path.exists(path):\n"
      "        print('natron-state: no state file, nothing applied')\n"
      "        return\n"
      "    with io.open(path, encoding='utf-8') as f:\n"
      "        values = json.load(f)\n"
      "    _natron_state_apply(app, values)\n"
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
