#include "TestUtil.h"

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/ProgramOutput.h"
#include "Custom/scene/SceneStore.h"

#include <QObject>

#include <memory>

#include "gtest/gtest.h"

namespace
{
typedef ProgramOutput::Source Source;
typedef ProgramOutput::Layers Layers;

// Two scenes of three and two inputs, without NDI output (the buses hold
// layers all the same), and a Program output on a fake sink.
class ProgramOutputCutTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_a = m_scenes.createScene(QStringLiteral("A"));
    m_scenes.addItems(m_a, QStringList() << QStringLiteral("/a.ntp#Write1") << QStringLiteral("/a.ntp#Write2")
                                         << QStringLiteral("/b.ntp#Write1"));
    m_b = m_scenes.createScene(QStringLiteral("B"));
    m_scenes.addItems(m_b, QStringList() << QStringLiteral("/c.ntp#Write1") << QStringLiteral("/c.ntp#Write2"));
    m_ndi.reset(new NdiManager(&m_scenes, nullptr, QString(), QString(), createFakeSink));
    m_program.reset(new ProgramOutput(m_ndi.get(), &m_scenes, QStringLiteral("Program test"), createFakeSink));
    QObject::connect(m_program.get(), &ProgramOutput::busChanged, &m_context, [this]() { ++m_busChanged; });
  }

  void TearDown() override
  {
    m_program.reset();
    m_ndi.reset();
  }

  Scene scene(const QString &id) const
  {
    Scene out;
    EXPECT_TRUE(m_scenes.scene(id, &out));
    return out;
  }

  Layers layersOf(const QString &id) const { return ProgramOutput::sceneLayers(scene(id)); }

  SceneStore m_scenes;
  QString m_a;
  QString m_b;
  std::unique_ptr<NdiManager> m_ndi;
  std::unique_ptr<ProgramOutput> m_program;
  QObject m_context;
  int m_busChanged = 0;
};
}

TEST_F(ProgramOutputCutTest, SceneLayersInSceneOrder)
{
  const Scene a = scene(m_a);
  const Layers layers = ProgramOutput::sceneLayers(a);

  ASSERT_EQ(3, layers.size());
  for (int i = 0; i < layers.size(); ++i)
  {
    EXPECT_EQ(m_a, layers.at(i).sceneId);
    EXPECT_EQ(a.items.at(i), layers.at(i).item);
  }
}

TEST_F(ProgramOutputCutTest, SceneLayersOfAnEmptyScene)
{
  Scene empty;
  empty.id = QStringLiteral("empty");

  EXPECT_TRUE(ProgramOutput::sceneLayers(empty).isEmpty());
}

TEST_F(ProgramOutputCutTest, CutToSceneGoesOnProgramAndPlays)
{
  m_program->cutTo(layersOf(m_a));

  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
  EXPECT_TRUE(m_program->isPlaying(ProgramOutput::eProgram));
  EXPECT_EQ(1, m_busChanged);
}

TEST_F(ProgramOutputCutTest, CutToSceneLeavesPreview)
{
  m_program->setLayers(ProgramOutput::ePreview, layersOf(m_b));
  m_busChanged = 0;

  m_program->cutTo(layersOf(m_a));

  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
  EXPECT_EQ(layersOf(m_b), m_program->layers(ProgramOutput::ePreview));
  EXPECT_FALSE(m_program->isPlaying(ProgramOutput::ePreview)); // not a cut: Preview holds
  EXPECT_EQ(1, m_busChanged);
}

TEST_F(ProgramOutputCutTest, CutToSceneReplacesAnotherSceneOnProgram)
{
  m_program->cutTo(layersOf(m_b));

  m_program->cutTo(layersOf(m_a));

  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
}

TEST_F(ProgramOutputCutTest, CutToTheSceneOnPreviewIsACut)
{
  // Preview goes on air and keeps playing in step with Program.
  m_program->setLayers(ProgramOutput::ePreview, layersOf(m_a));
  ASSERT_FALSE(m_program->isPlaying(ProgramOutput::ePreview));

  m_program->cutTo(layersOf(m_a));

  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::ePreview)); // Preview stays
  EXPECT_TRUE(m_program->isPlaying(ProgramOutput::eProgram));
  EXPECT_TRUE(m_program->isPlaying(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputCutTest, CutToTheSceneAlreadyOnProgramDoesNothing)
{
  m_program->cutTo(layersOf(m_a));
  m_program->pause(ProgramOutput::eProgram);
  m_busChanged = 0;

  m_program->cutTo(layersOf(m_a));

  EXPECT_EQ(0, m_busChanged);
  EXPECT_FALSE(m_program->isPlaying(ProgramOutput::eProgram)); // not restarted
}

TEST_F(ProgramOutputCutTest, CutToNothingDoesNothing)
{
  m_program->cutTo(layersOf(m_a));
  m_busChanged = 0;

  m_program->cutTo(Layers());
  m_program->cutTo(Source());

  EXPECT_EQ(0, m_busChanged);
  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
}

TEST_F(ProgramOutputCutTest, CutToEndsATransition)
{
  m_program->setLayers(ProgramOutput::ePreview, layersOf(m_b));
  m_program->setManualPosition(0.4);
  ASSERT_TRUE(m_program->inTransition());

  m_program->cutTo(layersOf(m_a));

  EXPECT_FALSE(m_program->inTransition());
  EXPECT_EQ(layersOf(m_a), m_program->layers(ProgramOutput::eProgram));
}

TEST_F(ProgramOutputCutTest, CutToLayersOfTwoScenesKeepsTheLastOnesScene)
{
  // A bus shows one scene.
  const Layers mixed = Layers() << layersOf(m_b).first() << layersOf(m_a).first() << layersOf(m_a).last();

  m_program->cutTo(mixed);

  EXPECT_EQ(Layers() << layersOf(m_a).first() << layersOf(m_a).last(), m_program->layers(ProgramOutput::eProgram));
}

TEST_F(ProgramOutputCutTest, CutToOneInputStillWorks)
{
  const Source input = layersOf(m_a).at(1);
  m_program->setLayers(ProgramOutput::ePreview, layersOf(m_b));

  m_program->cutTo(input);

  EXPECT_EQ(Layers() << input, m_program->layers(ProgramOutput::eProgram));
  EXPECT_EQ(layersOf(m_b), m_program->layers(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputCutTest, CutToTheOneInputOnPreviewIsACut)
{
  const Source input = layersOf(m_a).at(1);
  m_program->setPreview(input);

  m_program->cutTo(input);

  EXPECT_EQ(Layers() << input, m_program->layers(ProgramOutput::eProgram));
  EXPECT_EQ(Layers() << input, m_program->layers(ProgramOutput::ePreview));
}

TEST_F(ProgramOutputCutTest, ProgramPlacementsComeFromTheScene)
{
  LayerPlacement placement;
  placement.x = 0.25;
  placement.y = 0.75;
  placement.scale = 0.5;
  const Source input = layersOf(m_a).first();
  m_scenes.setLayerPlacement(m_a, input.item, placement);

  m_program->cutTo(layersOf(m_a));

  EXPECT_TRUE(placement == m_program->placement(ProgramOutput::eProgram, input));
}

TEST_F(ProgramOutputCutTest, ProgramPlacementsComeFromPreviewWhenThere)
{
  const Source input = layersOf(m_a).first();
  m_program->setLayers(ProgramOutput::ePreview, Layers() << input);
  LayerPlacement moved;
  moved.x = 0.1;
  m_program->setPlacement(ProgramOutput::ePreview, input, moved);

  m_program->cutTo(layersOf(m_a));

  EXPECT_TRUE(moved == m_program->placement(ProgramOutput::eProgram, input));
}

TEST_F(ProgramOutputCutTest, RemovedSceneLeavesProgram)
{
  m_program->cutTo(layersOf(m_a));

  m_scenes.removeScene(m_a);

  EXPECT_TRUE(m_program->layers(ProgramOutput::eProgram).isEmpty());
}
