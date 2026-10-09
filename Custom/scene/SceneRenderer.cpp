#include "SceneRenderer.h"

#include "ProjectInfo.h"
#include "SceneStore.h"
#include "../state/Json.h"
#include "../state/StateStore.h"

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
#include <QVariantMap>

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

// File name of an output path written on any OS (C:\\a\\b.mov -> b.mov).
QString outputFileName(const QString &path)
{
  QString p = path;
  p.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return p.section(QLatin1Char('/'), -1);
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

QString SceneRenderer::currentSceneId() const
{
  return m_process ? m_current.sceneId : QString();
}

QString SceneRenderer::currentProject() const
{
  return m_process ? m_current.project : QString();
}

int SceneRenderer::queuedCount() const
{
  return m_queue.size();
}

bool SceneRenderer::isQueued(const QString &sceneId, const QString &project) const
{
  Job job;
  job.sceneId = sceneId;
  job.project = project;
  return m_queue.contains(job);
}

bool SceneRenderer::isCurrent(const QString &sceneId, const QString &project) const
{
  return m_process && m_current.sceneId == sceneId && m_current.project == project;
}

RenderProgress SceneRenderer::progress(const QString &sceneId, const QString &project) const
{
  return isCurrent(sceneId, project) ? m_progress : RenderProgress();
}

void SceneRenderer::setRecordStatus(const Job &job, int status, const QString &message)
{
  RenderRecord record = m_scenes->renderRecord(job.sceneId, job.project);
  record.status = static_cast<RenderRecord::Status>(status);
  record.message = message;
  m_scenes->setRenderRecord(job.sceneId, job.project, record);
}

void SceneRenderer::enqueue(const QString &sceneId, const QStringList &projects)
{
  for (int i = 0; i < projects.size(); ++i)
  {
    Job job;
    job.sceneId = sceneId;
    job.project = projects.at(i);
    if (isCurrent(job.sceneId, job.project) || m_queue.contains(job))
    {
      continue;
    }
    m_queue << job;
    setRecordStatus(job, RenderRecord::eQueued, QString());
  }

  Q_EMIT statusChanged();

  if (!m_process)
  {
    startNext();
  }
}

void SceneRenderer::stop()
{
  const QList<Job> cancelled = m_queue;
  m_queue.clear();

  for (int i = 0; i < cancelled.size(); ++i)
  {
    setRecordStatus(cancelled.at(i), RenderRecord::eNone, tr("Cancelled"));
  }

  if (m_process)
  {
    m_stopping = true;
    m_process->kill();
  }

  Q_EMIT statusChanged();
}

void SceneRenderer::cancel(const QString &sceneId, const QString &project)
{
  Job job;
  job.sceneId = sceneId;
  job.project = project;

  if (m_queue.removeAll(job) > 0)
  {
    setRecordStatus(job, RenderRecord::eNone, tr("Cancelled"));
    Q_EMIT statusChanged();
  }

  if (isCurrent(sceneId, project))
  {
    m_stopping = true;
    m_process->kill(); // SIGKILL also ends a stopped (paused) process
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
    Q_EMIT progressChanged(m_current.sceneId, m_current.project);
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
    Q_EMIT progressChanged(m_current.sceneId, m_current.project);
    Q_EMIT statusChanged();
  }
#endif
}

QStringList SceneRenderer::sceneOutputs(const QString &outputDir, const QString &project)
{
  const QStringList outputs = readProjectInfo(project).outputs;

  if (outputDir.isEmpty())
  {
    return outputs;
  }

  QStringList moved;
  for (int i = 0; i < outputs.size(); ++i)
  {
    moved << QDir(outputDir).absoluteFilePath(outputFileName(outputs.at(i)));
  }
  return moved;
}

void SceneRenderer::startNext()
{
  if (m_queue.isEmpty())
  {
    m_current = Job();
    Q_EMIT statusChanged();
    return;
  }

  m_current = m_queue.takeFirst();
  m_outputTail.clear();
  m_partialLine.clear();
  m_progress = RenderProgress();
  m_progress.active = true;
  m_stopping = false;

  Scene scene;
  if (!m_scenes->scene(m_current.sceneId, &scene))
  {
    finishCurrent(false, tr("Scene was deleted"));
    return;
  }
  if (!QFile::exists(m_current.project))
  {
    finishCurrent(false, tr("Project file not found"));
    return;
  }
  if (!QFile::exists(m_rendererPath))
  {
    finishCurrent(false, tr("NatronRenderer not found at %1").arg(m_rendererPath));
    return;
  }
  if (!scene.outputDir.isEmpty() && !QDir().mkpath(scene.outputDir))
  {
    finishCurrent(false, tr("Cannot create the output folder %1").arg(scene.outputDir));
    return;
  }

  // The renderer reads the values from the saved store.
  if (m_state)
  {
    m_state->saveNow();
  }

  RenderRecord record = m_scenes->renderRecord(m_current.sceneId, m_current.project);
  record.status = RenderRecord::eRendering;
  record.message.clear();
  m_scenes->setRenderRecord(m_current.sceneId, m_current.project, record);

  m_process = new QProcess(this);
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  m_process->setWorkingDirectory(QFileInfo(m_current.project).absolutePath());

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
  env.insert(QString::fromUtf8("NATRON_OUTPUT_DIR"), scene.outputDir);
  m_process->setProcessEnvironment(env);

  connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onFinished(int,QProcess::ExitStatus)));
  connect(m_process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(onError(QProcess::ProcessError)));
  connect(m_process, SIGNAL(readyRead()), this, SLOT(onOutput()));

  QStringList args;
  if (QFile::exists(m_applyScriptPath))
  {
    args << QString::fromUtf8("-l") << m_applyScriptPath;
  }
  args << m_current.project;

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
    m_progress.currentFrame = f;
    m_progress.node = frame.cap(1);
    m_progress.percent = frame.cap(3).toDouble();
    m_progress.fps = frame.cap(4).toDouble();
    m_progress.timeRemaining = frame.cap(5).trimmed();
    m_progress.firstFrame = (m_progress.firstFrame < 0) ? f : qMin(m_progress.firstFrame, f);
    m_progress.lastFrame = (m_progress.lastFrame < 0) ? f : qMax(m_progress.lastFrame, f);
    ++m_progress.framesDone;
    Q_EMIT progressChanged(m_current.sceneId, m_current.project);
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
    Q_EMIT progressChanged(m_current.sceneId, m_current.project);
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
  const Job job = m_current;

  RenderRecord record = m_scenes->renderRecord(job.sceneId, job.project);
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

    Scene scene;
    m_scenes->scene(job.sceneId, &scene);
    const QStringList outputs = sceneOutputs(scene.outputDir, job.project);
    for (int i = 0; i < outputs.size() && record.output.isEmpty(); ++i)
    {
      record.output = findExistingOutputFile(outputs.at(i));
    }
    if (record.output.isEmpty())
    {
      record.message = outputs.isEmpty() ? tr("Rendered, but the project has no Write node output.")
                                         : tr("Rendered, but no output file was found at %1").arg(outputs.first());
    }
    else
    {
      record.thumbnail = makeThumbnail(job, record.output);
    }
  }
  else
  {
    record.status = m_stopping ? RenderRecord::eNone : RenderRecord::eFailed;
  }

  m_scenes->setRenderRecord(job.sceneId, job.project, record);
  m_current = Job();

  startNext();
}

QString SceneRenderer::makeThumbnail(const Job &job, const QString &output)
{
  if (m_thumbnailDir.isEmpty())
  {
    return QString();
  }

  QDir().mkpath(m_thumbnailDir);
  const QByteArray id = (job.sceneId + QLatin1Char('|') + job.project).toUtf8();
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
      "def _natron_state_value(value, wanted):\n"
      "    # Typed values: {'type': 'text', 'value': ...} / {'type': 'image', 'path': ...}.\n"
      "    # Returns the parameter value, or None if the type does not fit.\n"
      "    if isinstance(value, dict) and 'type' in value:\n"
      "        if value['type'] != wanted:\n"
      "            return None\n"
      "        return u'%s' % (value.get('value' if wanted == 'text' else 'path') or u'',)\n"
      "    # Older plain values.\n"
      "    if value is None:\n"
      "        return u''\n"
      "    if isinstance(value, bool):\n"
      "        return u'true' if value else u'false'\n"
      "    if isinstance(value, (dict, list)):\n"
      "        return json.dumps(value, separators=(',', ':'), sort_keys=True, ensure_ascii=False)\n"
      "    return u'%s' % (value,)\n"
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
      "            target = node.getParam(target_name)\n"
      "            if bound and target is not None:\n"
      "                if key not in values:\n"
      "                    print('natron-state: %s: key %s not in store, unchanged' % (node.getScriptName(), key))\n"
      "                else:\n"
      "                    v = _natron_state_value(values[key], wanted)\n"
      "                    if v is None:\n"
      "                        print('natron-state: %s: key %s is not %s, unchanged' % (node.getScriptName(), key, wanted))\n"
      "                    else:\n"
      "                        target.setValue(v)\n"
      "                        print('natron-state: %s.%s <- %s' % (node.getScriptName(), target_name, key))\n"
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
