#include "TestUtil.h"

#include "Custom/scene/ProjectInfo.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
class ProjectInfoTest : public ::testing::Test
{
protected:
  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }
  QString projectDir() const { return QFileInfo(path(QString::fromUtf8("p.ntp"))).absolutePath(); }

  QTemporaryDir m_dir;
};
}

TEST_F(ProjectInfoTest, MissingFile)
{
  const ProjectInfo info = readProjectInfo(path(QString::fromUtf8("missing.ntp")));
  EXPECT_FALSE(info.ok);
  EXPECT_FALSE(info.error.isEmpty());
}

TEST_F(ProjectInfoTest, EmptyProject)
{
  ASSERT_TRUE(writeProject(path(QString::fromUtf8("p.ntp")), QList<Node>()));
  const ProjectInfo info = readProjectInfo(path(QString::fromUtf8("p.ntp")));
  EXPECT_TRUE(info.ok);
  EXPECT_TRUE(info.stateKeys.isEmpty());
  EXPECT_TRUE(info.writers.isEmpty());
  EXPECT_TRUE(info.outputs.isEmpty());
  EXPECT_DOUBLE_EQ(24.0, info.fps); // Natron's default
}

TEST_F(ProjectInfoTest, ReadsBoundKeysAndTheirNodes)
{
  QList<Node> nodes;
  nodes << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("player & name"), QString::fromUtf8("Title"))
        << readNode(QString::fromUtf8("Read1"), QString::fromUtf8("logo"), QString::fromUtf8("Logo"))
        << textNode(QString::fromUtf8("Text2"), QString::fromUtf8("logo"))
        << textNode(QString::fromUtf8("Text3"), QString::fromUtf8("player & name"))
        << textNode(QString::fromUtf8("Text4"), QString::fromUtf8("  ")) // empty: not bound
        << readNode(QString::fromUtf8("Read2"), QString());          // no binding parameter
  ASSERT_TRUE(writeProject(path(QString::fromUtf8("p.ntp")), nodes));

  const ProjectInfo info = readProjectInfo(path(QString::fromUtf8("p.ntp")));
  ASSERT_TRUE(info.ok);
  EXPECT_EQ(QStringList() << QString::fromUtf8("player & name") << QString::fromUtf8("logo"), info.stateKeys);
  EXPECT_EQ(QString::fromUtf8("text"), info.keyTypes.value(QString::fromUtf8("player & name")));
  EXPECT_EQ(QString::fromUtf8("image,text"), info.keyTypes.value(QString::fromUtf8("logo")));

  const QList<ProjectInfo::BoundNode> logo = info.keyNodes.value(QString::fromUtf8("logo"));
  ASSERT_EQ(2, logo.size());
  EXPECT_EQ(QString::fromUtf8("Logo"), logo.at(0).label);
  EXPECT_EQ(QString::fromUtf8("Read1"), logo.at(0).name);
  EXPECT_EQ(QString::fromUtf8("Text2"), logo.at(1).label); // no label: the script name
  EXPECT_EQ(QString::fromUtf8("Text2"), logo.at(1).name);

  const QList<ProjectInfo::BoundNode> player = info.keyNodes.value(QString::fromUtf8("player & name"));
  ASSERT_EQ(2, player.size());
  EXPECT_EQ(QString::fromUtf8("Title"), player.at(0).label);
  EXPECT_EQ(QString::fromUtf8("Text3"), player.at(1).name);
}

TEST_F(ProjectInfoTest, ReadsWriteNodesInsideGroups)
{
  QList<Node> inner;
  inner << writeNode(QString::fromUtf8("Write1"), QString::fromUtf8("[Project]/out/inner_####.png"))
        << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("inside"));
  QList<Node> nodes;
  nodes << groupNode(QString::fromUtf8("Group1"), inner)
        << writeNode(QString::fromUtf8("Write2"), QString::fromUtf8("renders/out.mov"), QString::fromUtf8("fr.inria.openfx.WriteFFmpeg"))
        << writeNode(QString::fromUtf8("Write3"), QString::fromUtf8("C:/renders/win.mov"))
        << writeNode(QString::fromUtf8("Write4"), QString::fromUtf8("/abs/out.mov"))
        << writeNode(QString::fromUtf8("Write5"), QString::fromUtf8("/abs/out.mov")) // same output: listed once
        << writeNode(QString::fromUtf8("Write6"), QString());                    // no output
  ASSERT_TRUE(writeProject(path(QString::fromUtf8("p.ntp")), nodes));

  const ProjectInfo info = readProjectInfo(path(QString::fromUtf8("p.ntp")));
  ASSERT_TRUE(info.ok);
  EXPECT_EQ(QStringList() << QString::fromUtf8("Group1.Write1") << QString::fromUtf8("Write2") << QString::fromUtf8("Write3")
                          << QString::fromUtf8("Write4") << QString::fromUtf8("Write5") << QString::fromUtf8("Write6"),
            info.writers);
  EXPECT_EQ(projectDir() + QString::fromUtf8("/out/inner_####.png"), info.writerOutputs.value(QString::fromUtf8("Group1.Write1")));
  EXPECT_EQ(projectDir() + QString::fromUtf8("/renders/out.mov"), info.writerOutputs.value(QString::fromUtf8("Write2")));
  EXPECT_EQ(QString::fromUtf8("C:/renders/win.mov"), info.writerOutputs.value(QString::fromUtf8("Write3")));
  EXPECT_EQ(QString::fromUtf8("/abs/out.mov"), info.writerOutputs.value(QString::fromUtf8("Write5")));
  EXPECT_FALSE(info.writerOutputs.contains(QString::fromUtf8("Write6")));
  EXPECT_EQ(4, info.outputs.size());
  EXPECT_EQ(QStringList() << QString::fromUtf8("inside"), info.stateKeys);
  EXPECT_EQ(QString::fromUtf8("Group1.Text1"), info.keyNodes.value(QString::fromUtf8("inside")).first().name);
}

TEST_F(ProjectInfoTest, ReadsTheFrameRate)
{
  ASSERT_TRUE(writeProject(path(QString::fromUtf8("p.ntp")), QList<Node>() << textNode(QString::fromUtf8("Text1"), QString::fromUtf8("a")), 29.97));
  EXPECT_DOUBLE_EQ(29.97, readProjectInfo(path(QString::fromUtf8("p.ntp"))).fps);
}

TEST_F(ProjectInfoTest, UnescapesEntities)
{
  const QByteArray xml =
      "<?xml version=\"1.0\"?>\n<Project><NodesCollection><item>"
      "<Plugin_script_name>Text1</Plugin_script_name><Plugin_id>net.fxarena.openfx.Text</Plugin_id>"
      "<item><Name>natronStateKey</Name><Value>a&#65;&#x42;&lt;&gt;&quot;&apos;&amp;&foo;</Value></item>"
      "</item></NodesCollection></Project>\n";
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("p.ntp")), xml));
  EXPECT_EQ(QStringList() << QString::fromUtf8("aAB<>\"'&&foo;"), readProjectInfo(path(QString::fromUtf8("p.ntp"))).stateKeys);
}

TEST(ProjectInfoSequences, IsSequencePattern)
{
  EXPECT_TRUE(isSequencePattern(QString::fromUtf8("/a/frame_####.png")));
  EXPECT_TRUE(isSequencePattern(QString::fromUtf8("/a/frame_%04d.exr")));
  EXPECT_TRUE(isSequencePattern(QString::fromUtf8("frame_%d.png")));
  EXPECT_FALSE(isSequencePattern(QString::fromUtf8("/a/movie.mov")));
  // Only the file name counts.
  EXPECT_FALSE(isSequencePattern(QString::fromUtf8("/renders#1/movie.mov")));
}

TEST(ProjectInfoSequences, SequenceFrameFile)
{
  EXPECT_EQ(QString::fromUtf8("/a/frame_0007.png"), sequenceFrameFile(QString::fromUtf8("/a/frame_####.png"), 7));
  EXPECT_EQ(QString::fromUtf8("/a/frame_12345.png"), sequenceFrameFile(QString::fromUtf8("/a/frame_###.png"), 12345));
  EXPECT_EQ(QString::fromUtf8("/a/frame_0007.exr"), sequenceFrameFile(QString::fromUtf8("/a/frame_%04d.exr"), 7));
  EXPECT_EQ(QString::fromUtf8("/a/frame_7.png"), sequenceFrameFile(QString::fromUtf8("/a/frame_%d.png"), 7));
}

TEST(ProjectInfoSequences, ExistingSequenceFrames)
{
  QTemporaryDir dir;
  const QDir d(dir.path());
  const char *files[] = {"f_0010.png", "f_0001.png", "f_0003.png", "f_0002.jpg", "g_0004.png", "f_x.png"};
  for (int i = 0; i < 6; ++i)
  {
    ASSERT_TRUE(writeFile(d.absoluteFilePath(QString::fromUtf8(files[i])), "x"));
  }
  EXPECT_EQ(QList<int>() << 1 << 3 << 10, existingSequenceFrames(d.absoluteFilePath(QString::fromUtf8("f_####.png"))));
  EXPECT_EQ(QList<int>() << 1 << 3 << 10, existingSequenceFrames(d.absoluteFilePath(QString::fromUtf8("f_%04d.png"))));
  EXPECT_TRUE(existingSequenceFrames(d.absoluteFilePath(QString::fromUtf8("h_####.png"))).isEmpty());
}

TEST(ProjectInfoSequences, FindExistingOutputFile)
{
  QTemporaryDir dir;
  const QDir d(dir.path());
  ASSERT_TRUE(writeFile(d.absoluteFilePath(QString::fromUtf8("movie.mov")), "x"));
  ASSERT_TRUE(writeFile(d.absoluteFilePath(QString::fromUtf8("f_0005.png")), "x"));
  ASSERT_TRUE(writeFile(d.absoluteFilePath(QString::fromUtf8("f_0002.png")), "x"));

  EXPECT_EQ(d.absoluteFilePath(QString::fromUtf8("movie.mov")), findExistingOutputFile(d.absoluteFilePath(QString::fromUtf8("movie.mov"))));
  EXPECT_TRUE(findExistingOutputFile(d.absoluteFilePath(QString::fromUtf8("other.mov"))).isEmpty());
  EXPECT_TRUE(findExistingOutputFile(QString()).isEmpty());
  // Sequences: the lowest-numbered frame.
  EXPECT_EQ(d.absoluteFilePath(QString::fromUtf8("f_0002.png")), findExistingOutputFile(d.absoluteFilePath(QString::fromUtf8("f_####.png"))));
  EXPECT_EQ(d.absoluteFilePath(QString::fromUtf8("f_0002.png")), findExistingOutputFile(d.absoluteFilePath(QString::fromUtf8("f_%04d.png"))));
  EXPECT_TRUE(findExistingOutputFile(d.absoluteFilePath(QString::fromUtf8("g_####.png"))).isEmpty());
}
