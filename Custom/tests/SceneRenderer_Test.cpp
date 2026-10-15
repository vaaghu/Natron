#include "TestUtil.h"

#include "Custom/scene/SceneRenderer.h"
#include "Custom/scene/SceneStore.h"
#include "Custom/state/StateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
class SceneRendererStatics : public ::testing::Test
{
protected:
  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }

  QString project()
  {
    const QString p = path(QString::fromUtf8("p.ntp"));
    EXPECT_TRUE(writeProject(p, QList<Node>() << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("[Project]/renders/out.mov"))
                                              << writeNode(QString::fromUtf8("Write2"), QString())));
    return p;
  }

  QTemporaryDir m_dir;
};
}

TEST_F(SceneRendererStatics, ItemOutputDirNextToTheWriteNodeOutput)
{
  const QString p = project();
  Scene scene;
  scene.id = QString::fromUtf8("{id}");
  scene.name = QString::fromUtf8(" My/Scene:1 ");
  const QString item = SceneItem::make(p, QString::fromUtf8("Write1"));
  EXPECT_EQ(path(QString::fromUtf8("renders/My_Scene_1")), SceneRenderer::itemOutputDir(scene, item));
  EXPECT_EQ(path(QString::fromUtf8("renders/My_Scene_1/out.mov")), SceneRenderer::itemOutput(scene, item));

  // Names that are not folder names: the scene id.
  scene.name = QString::fromUtf8("..");
  EXPECT_EQ(path(QString::fromUtf8("renders/{id}")), SceneRenderer::itemOutputDir(scene, item));
  scene.name = QString::fromUtf8("  ");
  EXPECT_EQ(path(QString::fromUtf8("renders/{id}")), SceneRenderer::itemOutputDir(scene, item));
}

TEST_F(SceneRendererStatics, ItemOutputInTheSceneOutputFolder)
{
  const QString p = project();
  Scene scene;
  scene.name = QString::fromUtf8("S");
  scene.outputDir = path(QString::fromUtf8("elsewhere"));
  const QString item = SceneItem::make(p, QString::fromUtf8("Write1"));
  EXPECT_EQ(path(QString::fromUtf8("elsewhere")), SceneRenderer::itemOutputDir(scene, item));
  EXPECT_EQ(path(QString::fromUtf8("elsewhere/out.mov")), SceneRenderer::itemOutput(scene, item));
}

TEST_F(SceneRendererStatics, ItemWithoutOutput)
{
  const QString p = project();
  Scene scene;
  scene.name = QString::fromUtf8("S");
  scene.outputDir = path(QString::fromUtf8("elsewhere"));
  EXPECT_TRUE(SceneRenderer::itemOutputDir(scene, SceneItem::make(p, QString::fromUtf8("Write2"))).isEmpty());
  EXPECT_TRUE(SceneRenderer::itemOutput(scene, SceneItem::make(p, QString::fromUtf8("Write2"))).isEmpty());
  EXPECT_TRUE(SceneRenderer::itemOutput(scene, SceneItem::make(p, QString::fromUtf8("Nope"))).isEmpty());
  EXPECT_TRUE(SceneRenderer::itemOutput(scene, SceneItem::make(path(QString::fromUtf8("missing.ntp")), QString::fromUtf8("Write1"))).isEmpty());
}

TEST_F(SceneRendererStatics, WritesTheApplyScript)
{
  const QString script = path(QString::fromUtf8("sub/apply.py"));
  ASSERT_TRUE(SceneRenderer::writeApplyScript(script));
  const QByteArray text = readFile(script);
  EXPECT_TRUE(text.contains("NATRON_STATE_JSON"));
  EXPECT_TRUE(text.contains("NATRON_STATE_KEYMAP"));
  EXPECT_TRUE(text.contains("NATRON_OUTPUT_DIR"));
  EXPECT_TRUE(text.contains("node.getParam('natronStateKey')"));
  EXPECT_TRUE(text.endsWith("_natron_state_main()\n"));

  // Valid Python, when there is one to check it with.
  const QString python = QStandardPaths::findExecutable(QString::fromUtf8("python3"));
  if (!python.isEmpty())
  {
    QProcess check;
    check.start(python, QStringList() << QString::fromUtf8("-c")
                                      << QString::fromUtf8("import ast,sys; ast.parse(open(sys.argv[1]).read())") << script);
    ASSERT_TRUE(check.waitForFinished(10000));
    EXPECT_EQ(0, check.exitCode()) << check.readAllStandardError().constData();
  }
}

TEST_F(SceneRendererStatics, ApplyScriptFailsOnUnwritablePath)
{
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("file")), "x"));
  EXPECT_FALSE(SceneRenderer::writeApplyScript(path(QString::fromUtf8("file/apply.py"))));
}

#ifdef Q_OS_UNIX
namespace
{
// NatronRenderer stand-in: logs its arguments and environment, then
// renders (copies a PNG to the output folder), fails, or hangs, as
// FAKE_RENDER_MODE says.
const char kFakeRenderer[] =
    "printf 'ARGS %s\\n' \"$*\" >> \"$FAKE_RENDER_LOG\"\n"
    "printf 'KEYMAP %s\\n' \"$NATRON_STATE_KEYMAP\" >> \"$FAKE_RENDER_LOG\"\n"
    "printf 'STATE %s\\n' \"$NATRON_STATE_JSON\" >> \"$FAKE_RENDER_LOG\"\n"
    "printf 'OUTDIR %s\\n' \"$NATRON_OUTPUT_DIR\" >> \"$FAKE_RENDER_LOG\"\n"
    "case \"$FAKE_RENDER_MODE\" in\n"
    "fail)\n"
    "  echo 'loading project'\n"
    "  echo 'ERROR: something bad'\n"
    "  exit 1;;\n"
    "hang)\n"
    "  echo 'Write1 ==> Rendering started'\n"
    "  exec sleep 30;;\n"
    "*)\n"
    "  echo 'Write1 ==> Rendering started'\n"
    "  echo 'Write1 ==> Frame: 12, Progress: 60.0%, 14.2 Fps, Time Remaining: 3 seconds'\n"
    "  cp \"$FAKE_RENDER_SOURCE\" \"$NATRON_OUTPUT_DIR/out.png\"\n"
    "  cp \"$FAKE_RENDER_SOURCE\" \"$NATRON_OUTPUT_DIR/second.png\"\n"
    "  echo 'Write1 ==> Rendering finished'\n"
    "  exit 0;;\n"
    "esac\n";

struct Finished
{
  QString sceneId;
  QString item;
  bool ok;
  QString message;
};

class SceneRendererJobs : public ::testing::Test
{
protected:
  void SetUp()
  {
    ASSERT_TRUE(m_dir.isValid());
    m_renderer = path(QString::fromUtf8("bin/NatronRenderer"));
    m_log = path(QString::fromUtf8("render.log"));
    ASSERT_TRUE(writeScript(m_renderer, kFakeRenderer));
    ASSERT_TRUE(writeImage(path(QString::fromUtf8("source.png")), 640, 360, 0xffff0000));
    qputenv("FAKE_RENDER_LOG", m_log.toUtf8());
    qputenv("FAKE_RENDER_SOURCE", path(QString::fromUtf8("source.png")).toUtf8());
    setMode("ok");

    m_project = path(QString::fromUtf8("p.ntp"));
    ASSERT_TRUE(writeProject(m_project, QList<Node>() << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("player"))
                                                      << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("[Project]/out.png"))
                                                      << writeNode(QString::fromUtf8("Write2"), QString::fromUtf8("[Project]/second.png"))));
    m_item1 = SceneItem::make(m_project, QString::fromUtf8("Write1"));
    m_item2 = SceneItem::make(m_project, QString::fromUtf8("Write2"));
    m_sceneId = m_scenes.createScene(QString::fromUtf8("Show"));
    m_scenes.addItems(m_sceneId, QStringList() << m_item1 << m_item2);
  }

  void TearDown()
  {
    qunsetenv("FAKE_RENDER_MODE");
    qunsetenv("FAKE_RENDER_LOG");
    qunsetenv("FAKE_RENDER_SOURCE");
  }

  static void setMode(const char *mode) { qputenv("FAKE_RENDER_MODE", mode); }

  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }

  // Lines of the fake renderer's log starting with prefix ("ARGS ").
  QStringList logLines(const char *prefix) const
  {
    QStringList lines;
    const QStringList all = QString::fromUtf8(readFile(m_log)).split(QLatin1Char('\n'));
    for (int i = 0; i < all.size(); ++i)
    {
      if (all.at(i).startsWith(QString::fromUtf8(prefix)))
      {
        lines << all.at(i).mid(int(qstrlen(prefix)));
      }
    }
    return lines;
  }

  SceneRenderer *makeRenderer(const QString &rendererPath, StateStore *state = nullptr)
  {
    SceneRenderer *r = new SceneRenderer(state, &m_scenes, rendererPath, path(QString::fromUtf8("apply.py")), QString(),
                                         path(QString::fromUtf8("thumbs")), &m_context);
    QObject::connect(r, &SceneRenderer::jobFinished, &m_context,
                     [this](const QString &scene, const QString &item, bool ok, const QString &message) {
                       Finished f = {scene, item, ok, message};
                       m_finished << f;
                     });
    QObject::connect(r, &SceneRenderer::allFinished, &m_context, [this]() { ++m_allFinished; });
    return r;
  }

  RenderRecord record(const QString &item) const { return m_scenes.renderRecord(m_sceneId, item); }

  QTemporaryDir m_dir;
  QObject m_context; // owns the renderers (deleted before the scene store)
  SceneStore m_scenes;
  QString m_renderer;
  QString m_log;
  QString m_project;
  QString m_item1;
  QString m_item2;
  QString m_sceneId;
  QList<Finished> m_finished;
  int m_allFinished = 0;
};
}

TEST_F(SceneRendererJobs, Defaults)
{
  SceneRenderer *r = makeRenderer(m_renderer);
  EXPECT_EQ(m_renderer, r->rendererPath());
  EXPECT_GE(r->maxParallel(), 1);
  EXPECT_FALSE(r->isBusy());
  EXPECT_EQ(0, r->queuedCount());
  EXPECT_TRUE(r->canPause());
  EXPECT_FALSE(r->progress(m_sceneId, m_item1).active);
  EXPECT_EQ(-1, r->progress(m_sceneId, m_item1).currentFrame);

  r->setMaxParallel(0);
  EXPECT_EQ(1, r->maxParallel());
  r->setRendererOptions(QStringList() << QString::fromUtf8("--opengl") << QString::fromUtf8("enabled"));
  EXPECT_EQ(QStringList() << QString::fromUtf8("--opengl") << QString::fromUtf8("enabled"), r->rendererOptions());
}

TEST_F(SceneRendererJobs, MissingRenderer)
{
  SceneRenderer *r = makeRenderer(path(QString::fromUtf8("bin/missing")));
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_EQ(1, m_finished.size());
  EXPECT_FALSE(m_finished.first().ok);
  EXPECT_TRUE(m_finished.first().message.startsWith(QString::fromUtf8("NatronRenderer not found at")));
  EXPECT_EQ(RenderRecord::eFailed, record(m_item1).status);
  EXPECT_FALSE(r->isBusy());
  EXPECT_GE(m_allFinished, 1);
}

TEST_F(SceneRendererJobs, JobsThatCannotStart)
{
  SceneRenderer *r = makeRenderer(m_renderer);

  // Deleted scene.
  r->enqueue(QString::fromUtf8("gone"), QStringList() << m_item1);
  ASSERT_EQ(1, m_finished.size());
  EXPECT_EQ(QString::fromUtf8("Scene was deleted"), m_finished.last().message);

  // Missing project.
  const QString missing = SceneItem::make(path(QString::fromUtf8("missing.ntp")), QString::fromUtf8("Write1"));
  m_scenes.addItems(m_sceneId, QStringList() << missing);
  r->enqueue(m_sceneId, QStringList() << missing);
  ASSERT_EQ(2, m_finished.size());
  EXPECT_EQ(QString::fromUtf8("Project file not found"), m_finished.last().message);

  // A project without a Write node.
  const QString none = SceneItem::make(m_project, QString());
  r->enqueue(m_sceneId, QStringList() << none);
  ASSERT_EQ(3, m_finished.size());
  EXPECT_TRUE(m_finished.last().message.startsWith(QString::fromUtf8("The project has no Write node:")));

  // A Write node renamed or deleted.
  const QString renamed = SceneItem::make(m_project, QString::fromUtf8("Write9"));
  r->enqueue(m_sceneId, QStringList() << renamed);
  ASSERT_EQ(4, m_finished.size());
  EXPECT_TRUE(m_finished.last().message.contains(QString::fromUtf8("no Write node named Write9")));

  // None of these are retried, nor ran the renderer.
  for (int i = 0; i < m_finished.size(); ++i)
  {
    EXPECT_FALSE(m_finished.at(i).ok);
  }
  EXPECT_TRUE(logLines("ARGS ").isEmpty());
}

TEST_F(SceneRendererJobs, WriteNodeWithoutOutput)
{
  ASSERT_TRUE(writeProject(m_project, QList<Node>() << writeNode(QString::fromUtf8("Write1"), QString())));
  SceneRenderer *r = makeRenderer(m_renderer);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_EQ(1, m_finished.size());
  EXPECT_EQ(QString::fromUtf8("Write1 has no output file: set one in the editor, save, and render again."), m_finished.last().message);
}

TEST_F(SceneRendererJobs, RendersAndRecordsTheOutput)
{
  StateStore state;
  ASSERT_TRUE(state.loadAndAutoSave(path(QString::fromUtf8("state.json"))));
  state.set(QString::fromUtf8("player"), QString::fromUtf8("COPPER"));
  m_scenes.setKeyMapping(m_sceneId, QString::fromUtf8("player"), QString::fromUtf8("player2"));
  ASSERT_TRUE(SceneRenderer::writeApplyScript(path(QString::fromUtf8("apply.py"))));

  SceneRenderer *r = makeRenderer(m_renderer, &state);
  r->setRendererOptions(QStringList() << QString::fromUtf8("--opengl") << QString::fromUtf8("enabled"));
  QList<RenderProgress> progress;
  QObject::connect(r, &SceneRenderer::progressChanged, &m_context,
                   [&](const QString &scene, const QString &item) { progress << r->progress(scene, item); });

  r->enqueue(m_sceneId, QStringList() << m_item1);
  EXPECT_TRUE(r->isBusy());
  EXPECT_TRUE(r->isCurrent(m_sceneId, m_item1));
  EXPECT_EQ(RenderRecord::eRendering, record(m_item1).status);
  EXPECT_TRUE(QFile::exists(path(QString::fromUtf8("state.json")))); // saved for the renderer

  ASSERT_TRUE(waitFor([&]() { return !m_finished.isEmpty(); }, 10000));
  EXPECT_TRUE(m_finished.first().ok) << m_finished.first().message.toStdString();
  EXPECT_FALSE(r->isBusy());
  EXPECT_GE(m_allFinished, 1);

  // Arguments and environment.
  const QStringList args = logLines("ARGS ");
  ASSERT_EQ(1, args.size());
  EXPECT_EQ(QString::fromUtf8("-l %1 --opengl enabled -w Write1 %2").arg(path(QString::fromUtf8("apply.py")), m_project), args.first());
  EXPECT_EQ(QStringList() << QString::fromUtf8("{\"player\":\"player2\"}"), logLines("KEYMAP "));
  EXPECT_EQ(QStringList() << path(QString::fromUtf8("state.json")), logLines("STATE "));
  // Movies are staged next to their final file.
  EXPECT_TRUE(logLines("OUTDIR ").first().startsWith(path(QString::fromUtf8("Show/.natron-render-"))));

  // Progress lines.
  ASSERT_GE(progress.size(), 2);
  bool sawFrame = false;
  for (int i = 0; i < progress.size(); ++i)
  {
    EXPECT_TRUE(progress.at(i).active);
    EXPECT_EQ(QString::fromUtf8("Write1"), progress.at(i).node);
    if (progress.at(i).currentFrame == 12 && progress.at(i).timeRemaining != QString::fromUtf8("Done"))
    {
      sawFrame = true;
      EXPECT_DOUBLE_EQ(60.0, progress.at(i).percent);
      EXPECT_DOUBLE_EQ(14.2, progress.at(i).fps);
      EXPECT_EQ(QString::fromUtf8("3 seconds"), progress.at(i).timeRemaining);
    }
  }
  EXPECT_TRUE(sawFrame);
  EXPECT_DOUBLE_EQ(100.0, progress.last().percent);
  EXPECT_EQ(QString::fromUtf8("Done"), progress.last().timeRemaining);

  // The output moved to its final place, with a thumbnail.
  const QString output = path(QString::fromUtf8("Show/out.png"));
  const RenderRecord done = record(m_item1);
  EXPECT_EQ(RenderRecord::eDone, done.status);
  EXPECT_EQ(output, done.output);
  EXPECT_TRUE(done.time.isValid());
  EXPECT_TRUE(QFile::exists(output));
  EXPECT_TRUE(QDir(path(QString::fromUtf8("Show"))).entryList(QStringList() << QString::fromUtf8(".natron-render-*"),
                                                             QDir::Dirs | QDir::Hidden).isEmpty());
  ASSERT_FALSE(done.thumbnail.isEmpty());
  EXPECT_EQ(320, QImage(done.thumbnail).width());
  EXPECT_EQ(180, QImage(done.thumbnail).height());
}

TEST_F(SceneRendererJobs, FailedRenderIsRetriedOnce)
{
  setMode("fail");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return !m_finished.isEmpty(); }, 10000));
  processEventsFor(100);

  ASSERT_EQ(1, m_finished.size());
  EXPECT_FALSE(m_finished.first().ok);
  EXPECT_EQ(QString::fromUtf8("something bad\n(renderer exit code 1)\nERROR: something bad"), m_finished.first().message);
  EXPECT_EQ(2, logLines("ARGS ").size());
  EXPECT_EQ(RenderRecord::eFailed, record(m_item1).status);
}

TEST_F(SceneRendererJobs, CancelQueuedAndRunningJobs)
{
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->setMaxParallel(1);
  r->enqueue(m_sceneId, QStringList() << m_item1 << m_item2 << m_item1);

  EXPECT_TRUE(r->isCurrent(m_sceneId, m_item1));
  EXPECT_TRUE(r->isQueued(m_sceneId, m_item2));
  EXPECT_FALSE(r->isQueued(m_sceneId, m_item1));
  EXPECT_EQ(1, r->queuedCount());
  ASSERT_EQ(1, r->runningItems().size());
  EXPECT_EQ(qMakePair(m_sceneId, m_item1), r->runningItems().first());
  EXPECT_EQ(RenderRecord::eQueued, record(m_item2).status);

  r->cancel(m_sceneId, m_item2);
  EXPECT_EQ(0, r->queuedCount());
  EXPECT_EQ(RenderRecord::eNone, record(m_item2).status);
  EXPECT_EQ(QString::fromUtf8("Cancelled"), record(m_item2).message);

  ASSERT_TRUE(waitFor([&]() { return logLines("ARGS ").size() == 1; })); // it ran
  r->cancel(m_sceneId, m_item1);
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
  EXPECT_EQ(RenderRecord::eNone, record(m_item1).status);
  EXPECT_EQ(QString::fromUtf8("Cancelled"), record(m_item1).message);
  EXPECT_TRUE(m_finished.isEmpty()); // cancelled jobs do not "finish"
  EXPECT_EQ(1, logLines("ARGS ").size()); // not retried
}

TEST_F(SceneRendererJobs, PauseAndResume)
{
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return r->progress(m_sceneId, m_item1).node == QString::fromUtf8("Write1"); }));

  r->pause(m_sceneId, m_item1);
  EXPECT_TRUE(r->progress(m_sceneId, m_item1).paused);
  r->resume(m_sceneId, m_item1);
  EXPECT_FALSE(r->progress(m_sceneId, m_item1).paused);

  // Pausing then stopping still ends it.
  r->pause(m_sceneId, m_item1);
  r->stop();
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
  EXPECT_EQ(RenderRecord::eNone, record(m_item1).status);
}

TEST_F(SceneRendererJobs, StopCancelsEverything)
{
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->setMaxParallel(1);
  r->enqueue(m_sceneId, QStringList() << m_item1 << m_item2);
  r->stop();
  EXPECT_EQ(0, r->queuedCount());
  EXPECT_EQ(QString::fromUtf8("Cancelled"), record(m_item2).message);
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
  EXPECT_EQ(RenderRecord::eNone, record(m_item1).status);
  EXPECT_TRUE(m_finished.isEmpty());
}

TEST_F(SceneRendererJobs, RendersInParallel)
{
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->setMaxParallel(2);
  r->enqueue(m_sceneId, QStringList() << m_item1 << m_item2);
  EXPECT_EQ(2, r->runningItems().size());
  EXPECT_EQ(0, r->queuedCount());
  r->stop();
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
}

TEST_F(SceneRendererJobs, SameOutputWaitsForTheRunningJob)
{
  // Without an output folder, both scenes render the same file: one at a time.
  setMode("hang");
  m_scenes.setOutputDir(m_sceneId, path(QString::fromUtf8("shared")));
  const QString other = m_scenes.createScene(QString::fromUtf8("Other"));
  m_scenes.addItems(other, QStringList() << m_item1);
  m_scenes.setOutputDir(other, path(QString::fromUtf8("shared")));

  SceneRenderer *r = makeRenderer(m_renderer);
  r->setMaxParallel(2);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  r->enqueue(other, QStringList() << m_item1);
  EXPECT_EQ(1, r->runningItems().size());
  EXPECT_TRUE(r->isQueued(other, m_item1));
  r->stop();
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
}

TEST_F(SceneRendererJobs, RerenderRestartsARunningJob)
{
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return logLines("ARGS ").size() == 1; }));

  setMode("ok");
  r->rerender(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return !m_finished.isEmpty(); }, 10000));
  EXPECT_TRUE(m_finished.first().ok);
  EXPECT_EQ(2, logLines("ARGS ").size());
  EXPECT_EQ(RenderRecord::eDone, record(m_item1).status);
}

TEST_F(SceneRendererJobs, RerenderQueuesIdleItems)
{
  SceneRenderer *r = makeRenderer(m_renderer);
  r->rerender(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return !m_finished.isEmpty(); }, 10000));
  EXPECT_TRUE(m_finished.first().ok);
}

TEST_F(SceneRendererJobs, SequenceOutputsAreNotStaged)
{
  ASSERT_TRUE(writeProject(m_project, QList<Node>() << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("[Project]/f_####.png"))));
  setMode("hang");
  SceneRenderer *r = makeRenderer(m_renderer);
  r->enqueue(m_sceneId, QStringList() << m_item1);
  ASSERT_TRUE(waitFor([&]() { return logLines("OUTDIR ").size() == 1; }));
  EXPECT_EQ(path(QString::fromUtf8("Show")), logLines("OUTDIR ").first());
  r->stop();
  ASSERT_TRUE(waitFor([&]() { return !r->isBusy(); }, 10000));
}
#endif
