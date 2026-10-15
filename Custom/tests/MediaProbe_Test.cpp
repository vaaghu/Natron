#include "TestUtil.h"

#include "Custom/ndi/MediaProbe.h"

#include <QDir>
#include <QTemporaryDir>

#include "gtest/gtest.h"

using namespace TestUtil;

TEST(MediaProbe, FpsToFraction)
{
  int num = 0, den = 0;
  fpsToFraction(23.976, &num, &den);
  EXPECT_EQ(24000, num);
  EXPECT_EQ(1001, den);
  fpsToFraction(29.97, &num, &den);
  EXPECT_EQ(30000, num);
  EXPECT_EQ(1001, den);
  fpsToFraction(59.94, &num, &den);
  EXPECT_EQ(60000, num);
  EXPECT_EQ(1001, den);
  fpsToFraction(25, &num, &den);
  EXPECT_EQ(25, num);
  EXPECT_EQ(1, den);
  fpsToFraction(12.5, &num, &den);
  EXPECT_EQ(12500, num);
  EXPECT_EQ(1000, den);
  fpsToFraction(0, &num, &den);
  EXPECT_EQ(24, num);
  EXPECT_EQ(1, den);
  fpsToFraction(-5, &num, &den);
  EXPECT_EQ(24, num);
  EXPECT_EQ(1, den);
}

TEST(MediaProbe, MediaInfoFpsAndDuration)
{
  MediaInfo info;
  EXPECT_FALSE(info.ok);
  EXPECT_DOUBLE_EQ(24.0, info.fps());
  EXPECT_DOUBLE_EQ(0.0, info.duration());
  info.fpsNum = 30000;
  info.fpsDen = 1001;
  info.frameCount = 300;
  EXPECT_NEAR(29.97, info.fps(), 1e-3);
  EXPECT_NEAR(10.01, info.duration(), 1e-6);
  info.fpsDen = 0;
  EXPECT_DOUBLE_EQ(0.0, info.fps());
  EXPECT_DOUBLE_EQ(0.0, info.duration());
}

namespace
{
class MediaProbeTest : public ::testing::Test
{
protected:
  QString path(const QString &name) const { return QDir(m_dir.path()).absoluteFilePath(name); }
  QTemporaryDir m_dir;
};
}

TEST_F(MediaProbeTest, SequenceNotRenderedYet)
{
  const MediaInfo info = probeMedia(path(QString::fromUtf8("f_####.png")), QString(), 25);
  EXPECT_FALSE(info.ok);
  EXPECT_EQ(QString::fromUtf8("not rendered yet (no frames of f_####.png)"), info.error);
}

TEST_F(MediaProbeTest, ImageSequence)
{
  for (int frame = 5; frame <= 7; ++frame)
  {
    ASSERT_TRUE(writeImage(path(QString::fromUtf8("f_%1.png").arg(frame, 4, 10, QLatin1Char('0'))), 8, 4));
  }
  const MediaInfo info = probeMedia(path(QString::fromUtf8("f_####.png")), QString(), 29.97);
  ASSERT_TRUE(info.ok) << info.error.toStdString();
  EXPECT_TRUE(info.sequence);
  EXPECT_EQ(5, info.startNumber);
  EXPECT_EQ(3, info.frameCount);
  EXPECT_EQ(8, info.width);
  EXPECT_EQ(4, info.height);
  EXPECT_EQ(30000, info.fpsNum);
  EXPECT_EQ(1001, info.fpsDen);
  EXPECT_EQ(path(QString::fromUtf8("f_%04d.png")), info.input); // as ffmpeg's image2 demuxer wants it
}

TEST_F(MediaProbeTest, PrintfSequencePattern)
{
  ASSERT_TRUE(writeImage(path(QString::fromUtf8("f_3.png")), 2, 2));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("f_%d.png")), QString(), 24);
  ASSERT_TRUE(info.ok) << info.error.toStdString();
  EXPECT_EQ(path(QString::fromUtf8("f_%01d.png")), info.input);
  EXPECT_EQ(3, info.startNumber);
}

TEST_F(MediaProbeTest, MovieNotRenderedYet)
{
  const MediaInfo info = probeMedia(path(QString::fromUtf8("out.mov")), QString(), 25);
  EXPECT_FALSE(info.ok);
  EXPECT_EQ(QString::fromUtf8("not rendered yet (out.mov not found)"), info.error);
}

TEST_F(MediaProbeTest, StillImageWithoutFfprobe)
{
  ASSERT_TRUE(writeImage(path(QString::fromUtf8("still.png")), 16, 9));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("still.png")), QString(), 50);
  ASSERT_TRUE(info.ok) << info.error.toStdString();
  EXPECT_FALSE(info.sequence);
  EXPECT_EQ(path(QString::fromUtf8("still.png")), info.input);
  EXPECT_EQ(16, info.width);
  EXPECT_EQ(9, info.height);
  EXPECT_EQ(1, info.frameCount);
  EXPECT_EQ(50, info.fpsNum);
}

TEST_F(MediaProbeTest, UnreadableFile)
{
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("junk.mov")), "junk"));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("junk.mov")), QString(), 25);
  EXPECT_FALSE(info.ok);
  EXPECT_EQ(QString::fromUtf8("cannot read the size of junk.mov"), info.error);
}

#ifdef Q_OS_UNIX
TEST_F(MediaProbeTest, MovieThroughFfprobe)
{
  const QString ffprobe = path(QString::fromUtf8("ffprobe"));
  ASSERT_TRUE(writeScript(ffprobe, "printf 'width=64\\nheight=32\\nr_frame_rate=30000/1001\\nnb_frames=100\\nduration=3.336\\n'"));
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("clip.mov")), "movie"));

  const MediaInfo info = probeMedia(path(QString::fromUtf8("clip.mov")), ffprobe, 25);
  ASSERT_TRUE(info.ok) << info.error.toStdString();
  EXPECT_EQ(64, info.width);
  EXPECT_EQ(32, info.height);
  EXPECT_EQ(30000, info.fpsNum);
  EXPECT_EQ(1001, info.fpsDen);
  EXPECT_EQ(100, info.frameCount);
}

TEST_F(MediaProbeTest, FrameCountFromDuration)
{
  const QString ffprobe = path(QString::fromUtf8("ffprobe"));
  ASSERT_TRUE(writeScript(ffprobe, "printf 'width=64\\nheight=32\\nr_frame_rate=25/1\\nnb_frames=N/A\\nduration=2.0\\n'"));
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("clip.mkv")), "movie"));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("clip.mkv")), ffprobe, 30);
  ASSERT_TRUE(info.ok);
  EXPECT_EQ(25, info.fpsNum);
  EXPECT_EQ(50, info.frameCount);
}

TEST_F(MediaProbeTest, StillImageRateFromTheProject)
{
  // Still images report 0/0 or 25/1 and no duration.
  const QString ffprobe = path(QString::fromUtf8("ffprobe"));
  ASSERT_TRUE(writeScript(ffprobe, "printf 'width=10\\nheight=10\\nr_frame_rate=0/0\\n'"));
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("a.exr")), "image"));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("a.exr")), ffprobe, 29.97);
  ASSERT_TRUE(info.ok);
  EXPECT_EQ(30000, info.fpsNum);
  EXPECT_EQ(1001, info.fpsDen);
  EXPECT_EQ(1, info.frameCount);
}

TEST_F(MediaProbeTest, SequenceSizeThroughFfprobe)
{
  const QString ffprobe = path(QString::fromUtf8("ffprobe"));
  ASSERT_TRUE(writeScript(ffprobe, "printf 'width=40\\nheight=20\\n'"));
  ASSERT_TRUE(writeFile(path(QString::fromUtf8("f_0001.exr")), "not readable by Qt"));
  const MediaInfo info = probeMedia(path(QString::fromUtf8("f_####.exr")), ffprobe, 25);
  ASSERT_TRUE(info.ok) << info.error.toStdString();
  EXPECT_EQ(40, info.width);
  EXPECT_EQ(20, info.height);
}
#endif
