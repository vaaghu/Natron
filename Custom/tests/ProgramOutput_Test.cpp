#include "TestUtil.h"

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/PlayoutChannel.h"
#include "Custom/ndi/ProgramOutput.h"
#include "Custom/scene/SceneStore.h"

#include <QImage>

#include <memory>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
typedef ProgramOutput::Source Source;
typedef ProgramOutput::Layers Layers;

class ProgramOutputTest : public ::testing::Test
{
protected:
  void SetUp()
  {
    m_a = m_scenes.createScene(QString::fromUtf8("A"));
    m_b = m_scenes.createScene(QString::fromUtf8("B"));
    m_scenes.addItems(m_a, QStringList() << QString::fromUtf8("/a.ntp#W1") << QString::fromUtf8("/a.ntp#W2") << QString::fromUtf8("/a.ntp#W3"));
    m_scenes.addItems(m_b, QStringList() << QString::fromUtf8("/b.ntp#W1"));
    m_ndi.reset(new NdiManager(&m_scenes, nullptr, QString(), QString(), createFakeSink));
  }

  void TearDown()
  {
    m_program.reset();
    m_ndi.reset();
  }

  ProgramOutput *program(const QString &name = QString::fromUtf8("Program"))
  {
    m_program.reset(new ProgramOutput(m_ndi.get(), &m_scenes, name, createFakeSink));
    m_program->setFormat(320, 180, 30);
    QObject::connect(m_program.get(), &ProgramOutput::busChanged, [this]() { ++m_busChanged; });
    QObject::connect(m_program.get(), &ProgramOutput::transitionChanged, [this]() { ++m_transitionChanged; });
    return m_program.get();
  }

  Source a(int n) const { return Source(m_a, QString::fromUtf8("/a.ntp#W%1").arg(n)); }
  Source b() const { return Source(m_b, QString::fromUtf8("/b.ntp#W1")); }

  // Puts one frame of a solid color on the channel of a (live, so it
  // plays without a decoder): a 1/25 s long layer.
  PlayoutChannel *liveLayer(const Source &source, QRgb color, int frames = 1)
  {
    PlayoutChannel *channel = m_ndi->channel(source.sceneId, source.item);
    EXPECT_NE(nullptr, channel);
    if (!channel)
    {
      return nullptr;
    }
    channel->enterLive(2, 2, 25);
    QByteArray bgra;
    for (int i = 0; i < 4; ++i)
    {
      bgra.append(char(qBlue(color))).append(char(qGreen(color))).append(char(qRed(color))).append(char(qAlpha(color)));
    }
    for (int f = 0; f < frames; ++f)
    {
      channel->pushLiveFrame(bgra, 2, 2);
      EXPECT_TRUE(waitFor([&]() { return channel->status().position == f + 1; }));
    }
    return channel;
  }

  SceneStore m_scenes;
  QString m_a;
  QString m_b;
  std::unique_ptr<NdiManager> m_ndi;
  std::unique_ptr<ProgramOutput> m_program;
  int m_busChanged = 0;
  int m_transitionChanged = 0;
};
}

TEST(ProgramOutputSource, Basics)
{
  EXPECT_TRUE(Source().isEmpty());
  EXPECT_TRUE(Source(QString::fromUtf8("s"), QString()).isEmpty());
  EXPECT_FALSE(Source(QString::fromUtf8("s"), QString::fromUtf8("i")).isEmpty());
  EXPECT_TRUE(Source(QString::fromUtf8("s"), QString::fromUtf8("i")) == Source(QString::fromUtf8("s"), QString::fromUtf8("i")));
  EXPECT_TRUE(Source(QString::fromUtf8("s"), QString::fromUtf8("i")) != Source(QString::fromUtf8("s"), QString::fromUtf8("j")));
}

TEST_F(ProgramOutputTest, Format)
{
  m_program.reset(new ProgramOutput(m_ndi.get(), &m_scenes, QString::fromUtf8("Program"), createFakeSink));
  EXPECT_EQ(QString::fromUtf8("Program"), m_program->sourceName());
  EXPECT_EQ(1920, m_program->width());
  EXPECT_EQ(1080, m_program->height());
  EXPECT_DOUBLE_EQ(30.0, m_program->fps());

  m_program->setFormat(1280, 720, 0);
  EXPECT_EQ(1280, m_program->width());
  EXPECT_DOUBLE_EQ(30.0, m_program->fps());
  m_program->setFormat(0, 720, 50); // ignored
  EXPECT_EQ(1280, m_program->width());
  EXPECT_DOUBLE_EQ(30.0, m_program->fps());
  m_program->setFormat(640, 360, 59.94);
  EXPECT_DOUBLE_EQ(59.94, m_program->fps());
}

TEST_F(ProgramOutputTest, LayersOfABus)
{
  ProgramOutput *p = program();
  p->addLayer(ProgramOutput::ePreview, a(1));
  p->addLayer(ProgramOutput::ePreview, a(2));
  p->addLayer(ProgramOutput::ePreview, a(2)); // already there
  p->addLayer(ProgramOutput::ePreview, Source());
  EXPECT_EQ(Layers() << a(1) << a(2), p->layers(ProgramOutput::ePreview));
  EXPECT_TRUE(p->contains(ProgramOutput::ePreview, a(1)));
  EXPECT_FALSE(p->contains(ProgramOutput::eProgram, a(1)));
  EXPECT_EQ(2, m_busChanged);

  p->moveLayer(ProgramOutput::ePreview, a(1), 5);
  EXPECT_EQ(Layers() << a(2) << a(1), p->layers(ProgramOutput::ePreview));
  p->moveLayer(ProgramOutput::ePreview, a(1), 1); // already on top
  p->moveLayer(ProgramOutput::ePreview, a(3), 1); // not there
  EXPECT_EQ(3, m_busChanged);

  p->removeLayer(ProgramOutput::ePreview, a(2));
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::ePreview));
  p->removeLayer(ProgramOutput::ePreview, a(2));
  EXPECT_EQ(4, m_busChanged);

  // Another scene's layer starts a new stack.
  p->addLayer(ProgramOutput::ePreview, b());
  EXPECT_EQ(Layers() << b(), p->layers(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputTest, SetLayersKeepsOneSceneWithoutDuplicates)
{
  ProgramOutput *p = program();
  p->setLayers(ProgramOutput::ePreview, Layers() << b() << a(1) << Source() << a(2) << a(1));
  EXPECT_EQ(Layers() << a(1) << a(2), p->layers(ProgramOutput::ePreview));
  const int changed = m_busChanged;
  p->setLayers(ProgramOutput::ePreview, Layers() << a(1) << a(2)); // same
  EXPECT_EQ(changed, m_busChanged);
  p->setPreview(Source());
  EXPECT_TRUE(p->layers(ProgramOutput::ePreview).isEmpty());
}

TEST_F(ProgramOutputTest, Placements)
{
  LayerPlacement saved;
  saved.x = 0.25;
  saved.start = 1;
  m_scenes.setLayerPlacement(m_a, a(1).item, saved);
  ProgramOutput *p = program();

  p->addLayer(ProgramOutput::ePreview, a(1));
  p->addLayer(ProgramOutput::ePreview, a(2));
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(1)) == saved); // the scene's
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(2)).isDefault());

  LayerPlacement moved;
  moved.scale = 0.5;
  p->setPlacement(ProgramOutput::ePreview, a(2), moved);
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(2)) == moved);
  LayerPlacement invalid;
  invalid.scale = 0;
  p->setPlacement(ProgramOutput::ePreview, a(2), invalid);
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(2)) == moved);
  p->setPlacement(ProgramOutput::ePreview, a(3), moved); // not on the bus
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(3)).isDefault());

  // Kept for layers staying, and taken along to Program.
  p->addLayer(ProgramOutput::ePreview, a(3));
  EXPECT_TRUE(p->placement(ProgramOutput::ePreview, a(2)) == moved);
  p->cut();
  EXPECT_TRUE(p->placement(ProgramOutput::eProgram, a(2)) == moved);
  p->setLayers(ProgramOutput::eProgram, Layers() << a(2) << a(1));
  EXPECT_TRUE(p->placement(ProgramOutput::eProgram, a(2)) == moved);
}

TEST_F(ProgramOutputTest, CutAndCutTo)
{
  ProgramOutput *p = program();
  p->cut(); // empty Preview: nothing
  EXPECT_TRUE(p->layers(ProgramOutput::eProgram).isEmpty());

  p->setPreview(a(1));
  p->cut();
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::eProgram));
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::ePreview)); // stays
  EXPECT_TRUE(p->isPlaying(ProgramOutput::eProgram));

  p->cutTo(a(2)); // Preview stays
  EXPECT_EQ(Layers() << a(2), p->layers(ProgramOutput::eProgram));
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::ePreview));
  p->cutTo(a(1)); // just Preview: like cut()
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::eProgram));
  const int changed = m_busChanged;
  p->cutTo(a(1)); // already on air
  p->cutTo(Source());
  EXPECT_EQ(changed, m_busChanged);
}

TEST_F(ProgramOutputTest, Fade)
{
  ProgramOutput *p = program();
  p->fade(100); // empty Preview
  EXPECT_FALSE(p->inAutoTransition());

  p->setPreview(a(1));
  p->fade(0); // instant: a cut
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::eProgram));

  p->setPreview(a(2));
  p->fade(150);
  EXPECT_TRUE(p->inAutoTransition());
  ASSERT_TRUE(waitFor([&]() { return p->inTransition(); }));
  ASSERT_TRUE(waitFor([&]() { return !p->inAutoTransition(); }));
  EXPECT_FALSE(p->inTransition());
  EXPECT_DOUBLE_EQ(0.0, p->transitionPosition());
  EXPECT_EQ(Layers() << a(2), p->layers(ProgramOutput::eProgram));
  EXPECT_GT(m_transitionChanged, 2);
}

TEST_F(ProgramOutputTest, TBar)
{
  ProgramOutput *p = program();
  p->setManualPosition(0.5); // empty Preview
  EXPECT_FALSE(p->inTransition());

  p->setPreview(a(1));
  p->setManualPosition(0.4);
  EXPECT_TRUE(p->inTransition());
  EXPECT_DOUBLE_EQ(0.4, p->transitionPosition());
  EXPECT_TRUE(p->isPlaying(ProgramOutput::ePreview)); // Preview starts as it is mixed in
  p->setManualPosition(-1);
  EXPECT_DOUBLE_EQ(0.0, p->transitionPosition());
  p->setManualPosition(0.7);
  p->setManualPosition(2); // completes it
  EXPECT_FALSE(p->inTransition());
  EXPECT_EQ(Layers() << a(1), p->layers(ProgramOutput::eProgram));

  // Changing Program stops a transition.
  p->setPreview(a(2));
  p->setManualPosition(0.5);
  p->setLayers(ProgramOutput::eProgram, Layers() << a(3));
  EXPECT_FALSE(p->inTransition());
}

TEST_F(ProgramOutputTest, FadeToBlack)
{
  ProgramOutput *p = program();
  EXPECT_FALSE(p->isBlack());
  p->fadeToBlack(0);
  EXPECT_TRUE(p->isBlack());
  EXPECT_DOUBLE_EQ(1.0, p->blackLevel());
  p->fadeToBlack(0);
  EXPECT_FALSE(p->isBlack());
  EXPECT_DOUBLE_EQ(0.0, p->blackLevel());

  p->fadeToBlack(100);
  EXPECT_TRUE(p->isBlack());
  ASSERT_TRUE(waitFor([&]() { return p->blackLevel() >= 1.0; }));
  p->fadeToBlack(100);
  ASSERT_TRUE(waitFor([&]() { return p->blackLevel() <= 0.0; }));
}

TEST_F(ProgramOutputTest, RemovedScenesLeaveTheBuses)
{
  ProgramOutput *p = program();
  p->setPreview(a(1));
  p->cut();
  p->setPreview(b());
  p->setManualPosition(0.5);

  m_scenes.removeItems(m_a, QStringList() << a(1).item);
  EXPECT_TRUE(p->layers(ProgramOutput::eProgram).isEmpty());
  EXPECT_EQ(Layers() << b(), p->layers(ProgramOutput::ePreview));
  EXPECT_TRUE(p->inTransition());

  m_scenes.removeScene(m_b);
  EXPECT_TRUE(p->layers(ProgramOutput::ePreview).isEmpty());
  EXPECT_FALSE(p->inTransition()); // nothing to go to
}

TEST_F(ProgramOutputTest, BusClock)
{
  ProgramOutput *p = program();
  p->play(ProgramOutput::eProgram); // no layers: nothing
  EXPECT_FALSE(p->isPlaying(ProgramOutput::eProgram));

  p->setLayers(ProgramOutput::eProgram, Layers() << a(1)); // Program plays its new layers
  EXPECT_TRUE(p->isPlaying(ProgramOutput::eProgram));
  processEventsFor(60);
  EXPECT_GT(p->position(ProgramOutput::eProgram), 0.0);
  p->pause(ProgramOutput::eProgram);
  EXPECT_FALSE(p->isPlaying(ProgramOutput::eProgram));
  const double paused = p->position(ProgramOutput::eProgram);
  processEventsFor(50);
  EXPECT_DOUBLE_EQ(paused, p->position(ProgramOutput::eProgram));

  p->stop(ProgramOutput::eProgram);
  EXPECT_DOUBLE_EQ(0.0, p->position(ProgramOutput::eProgram));
  p->restart(ProgramOutput::eProgram);
  EXPECT_TRUE(p->isPlaying(ProgramOutput::eProgram));

  p->setLoop(ProgramOutput::eProgram, true);
  EXPECT_TRUE(p->loops(ProgramOutput::eProgram));
  EXPECT_FALSE(p->loops(ProgramOutput::ePreview));

  // No channels (NDI off): layers have no length.
  EXPECT_DOUBLE_EQ(0.0, p->layerDuration(a(1)));
  EXPECT_DOUBLE_EQ(0.0, p->duration(ProgramOutput::eProgram));
  p->seek(ProgramOutput::eProgram, 5);
  EXPECT_DOUBLE_EQ(0.0, p->position(ProgramOutput::eProgram));
  EXPECT_FALSE(p->isActive(ProgramOutput::eProgram, a(1)));

  // An empty Preview getting layers holds their first frame.
  p->setPreview(a(2));
  EXPECT_FALSE(p->isPlaying(ProgramOutput::ePreview));
  EXPECT_DOUBLE_EQ(0.0, p->position(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputTest, CuePointsInBusTime)
{
  m_scenes.setNdiCuePoints(m_a, a(1).item, QList<double>() << 2 << 1);
  m_scenes.setNdiCuePoints(m_a, a(2).item, QList<double>() << 2.5);
  LayerPlacement late;
  late.start = 0.5;
  m_scenes.setLayerPlacement(m_a, a(1).item, late);
  ProgramOutput *p = program();
  p->setLayers(ProgramOutput::ePreview, Layers() << a(1) << a(2));
  // a(1): 1.5 and 2.5; a(2): 2.5 (once).
  EXPECT_EQ(QList<double>() << 1.5 << 2.5, p->cuePoints(ProgramOutput::ePreview));
  EXPECT_TRUE(p->cuePoints(ProgramOutput::eProgram).isEmpty());
}

TEST_F(ProgramOutputTest, SeekWithinTheLayers)
{
  m_scenes.setNdiEnabled(m_a, true);
  ProgramOutput *p = program();
  PlayoutChannel *channel = liveLayer(a(1), 0xffff0000, 10); // 0.4 s
  ASSERT_NE(nullptr, channel);
  EXPECT_NEAR(0.4, p->layerDuration(a(1)), 1e-9);

  p->setPreview(a(1));
  EXPECT_NEAR(0.4, p->duration(ProgramOutput::ePreview), 1e-9);
  EXPECT_TRUE(p->isActive(ProgramOutput::ePreview, a(1)));
  p->seek(ProgramOutput::ePreview, 0.2);
  EXPECT_DOUBLE_EQ(0.2, p->position(ProgramOutput::ePreview));
  p->seek(ProgramOutput::ePreview, 10);
  EXPECT_NEAR(0.4, p->position(ProgramOutput::ePreview), 1e-9);
  EXPECT_FALSE(p->isActive(ProgramOutput::ePreview, a(1))); // past its end
  p->seek(ProgramOutput::ePreview, -1);
  EXPECT_DOUBLE_EQ(0.0, p->position(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputTest, MixesTheLayers)
{
  m_scenes.setNdiEnabled(m_a, true);
  ProgramOutput *p = program();
  ASSERT_NE(nullptr, liveLayer(a(1), 0xffff0000));

  // Preview: the 2x2 layer fitted (180x180) and centred over black.
  p->setPreview(a(1));
  ASSERT_TRUE(waitFor([&]() {
    const QImage image = p->previewImage(320);
    return !image.isNull() && qRed(image.pixel(160, 90)) == 255;
  }));
  QImage preview = p->previewImage(320);
  EXPECT_EQ(320, preview.width());
  EXPECT_EQ(180, preview.height());
  EXPECT_EQ(0, qRed(preview.pixel(5, 5)));
  EXPECT_EQ(0, qRed(preview.pixel(315, 90)));
  EXPECT_EQ(160, p->previewImage(160).width());

  // On air, and sent to the NDI source.
  p->cut();
  ASSERT_TRUE(waitFor([&]() {
    const QImage image = p->outputPreview(320);
    return !image.isNull() && qRed(image.pixel(160, 90)) == 255;
  }));
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(QString::fromUtf8("Program")).frames > 0; }));
  const FakeSinkRecord record = fakeSinkRecord(QString::fromUtf8("Program"));
  EXPECT_EQ(320, record.width);
  EXPECT_EQ(180, record.height);
  EXPECT_FALSE(record.alpha);
  EXPECT_EQ(30000, record.fpsNum);
  EXPECT_EQ(1000, record.fpsDen);

  // Faded to black.
  p->fadeToBlack(0);
  ASSERT_TRUE(waitFor([&]() { return qRed(p->outputPreview(320).pixel(160, 90)) == 0; }));
}

TEST_F(ProgramOutputTest, ScaledPlacement)
{
  m_scenes.setNdiEnabled(m_a, true);
  ProgramOutput *p = program();
  ASSERT_NE(nullptr, liveLayer(a(1), 0xff00ff00));
  p->setPreview(a(1));
  LayerPlacement corner;
  corner.x = 0.25;
  corner.y = 0.25;
  corner.scale = 0.25; // 45x45 around (80, 45)
  p->setPlacement(ProgramOutput::ePreview, a(1), corner);
  ASSERT_TRUE(waitFor([&]() {
    const QImage image = p->previewImage(320);
    return !image.isNull() && qGreen(image.pixel(80, 45)) == 255 && qGreen(image.pixel(160, 90)) == 0;
  }));
}

TEST_F(ProgramOutputTest, NdiOutputState)
{
  ProgramOutput *p = program();
  EXPECT_TRUE(p->ndiEnabled());
  ASSERT_TRUE(waitFor([&]() { return p->connections() == kFakeSinkConnections; }, 4000));
  EXPECT_TRUE(p->outputError().isEmpty());

  p->setNdiEnabled(false);
  EXPECT_FALSE(p->ndiEnabled());
  ASSERT_TRUE(waitFor([&]() { return p->connections() == -1; }));
}

TEST_F(ProgramOutputTest, NdiOutputThatCannotOpen)
{
  ProgramOutput *p = program(QString::fromUtf8("fail program"));
  ASSERT_TRUE(waitFor([&]() { return !p->outputError().isEmpty(); }));
  EXPECT_EQ(QString::fromUtf8("boom (retrying)"), p->outputError());
  // The monitors still show Program.
  ASSERT_TRUE(waitFor([&]() { return !p->outputPreview(100).isNull(); }));

  p->setNdiEnabled(false);
  ASSERT_TRUE(waitFor([&]() { return p->outputError().isEmpty(); }));
}

TEST_F(ProgramOutputTest, SdiOutputSettings)
{
  ProgramOutput *p = program();
  bool ok = true;
  EXPECT_TRUE(p->sdiStatus(&ok).isEmpty());
  EXPECT_FALSE(ok);
  EXPECT_TRUE(p->sdiPort().isEmpty());

  p->setSdiOutput(QString::fromUtf8("No Such Port"), 7, true);
  EXPECT_EQ(QString::fromUtf8("No Such Port"), p->sdiPort());
  EXPECT_EQ(7u, p->sdiMode());
  EXPECT_TRUE(p->sdiCardClock());
  // No such port here (or no drivers): retried.
  ASSERT_TRUE(waitFor([&]() { return p->sdiStatus().endsWith(QString::fromUtf8("(retrying)")); }));
  p->sdiStatus(&ok);
  EXPECT_FALSE(ok);

  p->setSdiOutput(QString(), 0, false);
  EXPECT_TRUE(p->sdiStatus(&ok).isEmpty());
  EXPECT_FALSE(p->sdiCardClock());
}
