#include "TestUtil.h"

#include "Custom/ndi/PlayoutChannel.h"

#include <QDir>
#include <QTemporaryDir>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
MediaInfo media(int frameCount, int fps = 25, int width = 2, int height = 2)
{
  MediaInfo m;
  m.ok = true;
  m.input = QString::fromUtf8("/nonexistent/clip.mov");
  m.width = width;
  m.height = height;
  m.fpsNum = fps;
  m.fpsDen = 1;
  m.frameCount = frameCount;
  return m;
}

MediaInfo failed(const QString &error)
{
  MediaInfo m;
  m.error = error;
  return m;
}

// The channel's state machine, without its thread running.
class PlayoutChannelState : public ::testing::Test
{
protected:
  PlayoutChannelState()
      : m_channel(QString::fromUtf8("Show - p"), true, QString(), createFakeSink),
        m_statusChanged(0)
  {
    QObject::connect(&m_channel, &PlayoutChannel::statusChanged, &m_context, [this]() { ++m_statusChanged; });
  }

  PlayoutChannel::Status status() const { return m_channel.status(); }

  QObject m_context;
  PlayoutChannel m_channel;
  int m_statusChanged;
};
}

TEST(PlayoutChannelNames, StateName)
{
  EXPECT_EQ(QString::fromUtf8("Stopped"), PlayoutChannel::stateName(PlayoutChannel::eStopped));
  EXPECT_EQ(QString::fromUtf8("Playing"), PlayoutChannel::stateName(PlayoutChannel::ePlaying));
  EXPECT_EQ(QString::fromUtf8("Paused"), PlayoutChannel::stateName(PlayoutChannel::ePaused));
  EXPECT_EQ(QString::fromUtf8("Live"), PlayoutChannel::stateName(PlayoutChannel::eLive));
  EXPECT_EQ(QString::fromUtf8("Error"), PlayoutChannel::stateName(PlayoutChannel::eError));
}

TEST_F(PlayoutChannelState, Initial)
{
  EXPECT_EQ(QString::fromUtf8("Show - p"), m_channel.sourceName());
  EXPECT_TRUE(m_channel.alpha());
  const PlayoutChannel::Status s = status();
  EXPECT_EQ(PlayoutChannel::eStopped, s.state);
  EXPECT_EQ(0, s.position);
  EXPECT_EQ(0, s.frameCount);
  EXPECT_DOUBLE_EQ(-1.0, s.pauseAt);
  EXPECT_FALSE(s.loop);
  EXPECT_EQ(-1, s.connections);
  EXPECT_TRUE(m_channel.preview(100).isNull());
  QByteArray frame;
  int w = 0, h = 0;
  EXPECT_FALSE(m_channel.currentFrame(&frame, &w, &h));
}

TEST_F(PlayoutChannelState, NothingPlaysWithoutMedia)
{
  m_channel.play();
  m_channel.replay();
  m_channel.cue();
  m_channel.seek(1);
  EXPECT_EQ(PlayoutChannel::eStopped, status().state);
  EXPECT_EQ(0, m_statusChanged);
}

TEST_F(PlayoutChannelState, Transport)
{
  m_channel.setMedia(media(50));
  EXPECT_EQ(1, m_statusChanged);
  EXPECT_EQ(50, status().frameCount);
  EXPECT_DOUBLE_EQ(25.0, status().fps);
  EXPECT_DOUBLE_EQ(2.0, status().duration());

  m_channel.play();
  EXPECT_EQ(PlayoutChannel::ePlaying, status().state);
  m_channel.pause();
  EXPECT_EQ(PlayoutChannel::ePaused, status().state);
  const int changed = m_statusChanged;
  m_channel.pause(); // not playing: nothing
  EXPECT_EQ(changed, m_statusChanged);

  m_channel.seek(1.0);
  EXPECT_EQ(25, status().position);
  EXPECT_DOUBLE_EQ(1.0, status().seconds());
  EXPECT_EQ(PlayoutChannel::ePaused, status().state);
  m_channel.seek(100);
  EXPECT_EQ(49, status().position);
  m_channel.seek(-3);
  EXPECT_EQ(0, status().position);

  m_channel.seek(1.0);
  m_channel.stop();
  EXPECT_EQ(PlayoutChannel::eStopped, status().state);
  EXPECT_EQ(0, status().position);

  m_channel.seek(1.0);
  m_channel.replay();
  EXPECT_EQ(PlayoutChannel::ePlaying, status().state);
  EXPECT_EQ(0, status().position);

  // Seeking while playing keeps playing.
  m_channel.seek(0.4);
  EXPECT_EQ(PlayoutChannel::ePlaying, status().state);
  EXPECT_EQ(10, status().position);
}

TEST_F(PlayoutChannelState, PausePointsAndLoop)
{
  m_channel.setMedia(media(50));
  m_channel.setPausePoints(QList<double>() << 1.5 << -1 << 0.5);
  EXPECT_EQ(QList<double>() << 0.5 << 1.5, status().pausePoints);
  EXPECT_DOUBLE_EQ(0.5, status().pauseAt);

  m_channel.setPauseAt(1.0);
  EXPECT_EQ(QList<double>() << 1.0, status().pausePoints);
  m_channel.setPauseAt(-1);
  EXPECT_TRUE(status().pausePoints.isEmpty());
  EXPECT_DOUBLE_EQ(-1.0, status().pauseAt);

  m_channel.setLoop(true);
  EXPECT_TRUE(status().loop);

  m_channel.setPauseAt(1.0);
  m_channel.cue();
  EXPECT_EQ(PlayoutChannel::ePlaying, status().state);
  EXPECT_EQ(0, status().position);
}

TEST_F(PlayoutChannelState, NewMedia)
{
  m_channel.setMedia(media(50));
  m_channel.seek(1.6); // frame 40
  m_channel.setMedia(media(20)); // a shorter re-render: same place, within it
  EXPECT_EQ(19, status().position);
  EXPECT_EQ(PlayoutChannel::ePaused, status().state);

  m_channel.play();
  m_channel.setMedia(failed(QString::fromUtf8("not rendered yet")));
  EXPECT_EQ(PlayoutChannel::eStopped, status().state);
  EXPECT_EQ(0, status().position);
  EXPECT_EQ(QString::fromUtf8("not rendered yet"), status().message);

  m_channel.play(); // still nothing to play
  EXPECT_EQ(PlayoutChannel::eStopped, status().state);

  m_channel.setMedia(media(10));
  EXPECT_TRUE(status().message.isEmpty());
  m_channel.stop();
  m_channel.setMessage(QString::fromUtf8("hello"));
  EXPECT_EQ(QString::fromUtf8("hello"), status().message);
}

TEST_F(PlayoutChannelState, PlayFromAHeldFrame)
{
  m_channel.setMedia(media(10));
  m_channel.seek(100);
  m_channel.play();
  EXPECT_EQ(PlayoutChannel::ePlaying, status().state);
  EXPECT_EQ(9, status().position);
}

TEST_F(PlayoutChannelState, Live)
{
  m_channel.setMedia(media(50));
  m_channel.enterLive(4, 2, 0);
  EXPECT_EQ(PlayoutChannel::eLive, status().state);
  EXPECT_DOUBLE_EQ(25.0, status().fps); // 0: default
  EXPECT_EQ(0, status().frameCount);

  // Transport is ignored while live.
  m_channel.play();
  m_channel.stop();
  m_channel.replay();
  m_channel.cue();
  m_channel.seek(1);
  EXPECT_EQ(PlayoutChannel::eLive, status().state);

  // A new render arriving meanwhile is kept for after.
  m_channel.setMedia(media(30));
  EXPECT_EQ(PlayoutChannel::eLive, status().state);

  m_channel.leaveLive();
  EXPECT_EQ(PlayoutChannel::ePaused, status().state);
  EXPECT_EQ(29, status().position); // the last frame of the render
  m_channel.leaveLive(); // not live: nothing
  EXPECT_EQ(PlayoutChannel::ePaused, status().state);

  // Frames pushed when not live are dropped.
  m_channel.pushLiveFrame(QByteArray(32, 'x'), 4, 2);
}

namespace
{
QByteArray solid(int width, int height, char b, char g, char r, char a)
{
  QByteArray frame;
  for (int i = 0; i < width * height; ++i)
  {
    frame.append(b).append(g).append(r).append(a);
  }
  return frame;
}
}

TEST(PlayoutChannelThread, SendsLiveFramesToTheOutput)
{
  const QString name = QString::fromUtf8("live channel");
  PlayoutChannel channel(name, false, QString(), createFakeSink);
  channel.start();
  channel.enterLive(2, 2, 30);
  const QByteArray red = solid(2, 2, 0, 0, char(255), char(255));
  channel.pushLiveFrame(red, 2, 2);

  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(name).frames >= 1; }));
  const FakeSinkRecord record = fakeSinkRecord(name);
  EXPECT_EQ(1, record.opened);
  EXPECT_FALSE(record.alpha);
  EXPECT_EQ(red, record.lastFrame);
  EXPECT_EQ(2, record.width);
  EXPECT_EQ(30, record.fpsNum);
  EXPECT_EQ(1, record.fpsDen);

  ASSERT_TRUE(waitFor([&]() { return channel.status().position == 1; }));
  EXPECT_EQ(1, channel.status().frameCount);
  QByteArray frame;
  int w = 0, h = 0;
  ASSERT_TRUE(channel.currentFrame(&frame, &w, &h));
  EXPECT_EQ(red, frame);
  const QImage preview = channel.preview(100);
  ASSERT_FALSE(preview.isNull());
  EXPECT_EQ(2, preview.width());
  EXPECT_EQ(255, qRed(preview.pixel(0, 0)));
  EXPECT_EQ(1, channel.preview(1).width()); // scaled down

  // Receivers are counted about every second.
  ASSERT_TRUE(waitFor([&]() { return channel.status().connections == kFakeSinkConnections; }, 4000));
}

TEST(PlayoutChannelThread, ReportsAnOutputThatCannotOpen)
{
  PlayoutChannel channel(QString::fromUtf8("fail channel"), true, QString(), createFakeSink);
  channel.start();
  ASSERT_TRUE(waitFor([&]() { return !channel.status().message.isEmpty(); }));
  EXPECT_EQ(QString::fromUtf8("NDI output not available: boom (retrying)"), channel.status().message);

  channel.setMessage(QString::fromUtf8("info"));
  EXPECT_EQ(QString::fromUtf8("NDI output not available: boom (retrying); info"), channel.status().message);

  // Frames still go round (the switcher uses them).
  channel.enterLive(1, 1, 25);
  channel.pushLiveFrame(solid(1, 1, 1, 2, 3, 4), 1, 1);
  ASSERT_TRUE(waitFor([&]() {
    QByteArray f;
    int w, h;
    return channel.currentFrame(&f, &w, &h);
  }));
}

TEST(PlayoutChannelThread, NoSinkFactory)
{
  PlayoutChannel channel(QString::fromUtf8("none"), true, QString(), nullptr);
  channel.start();
  ASSERT_TRUE(waitFor([&]() { return !channel.status().message.isEmpty(); }));
  EXPECT_EQ(QString::fromUtf8("NDI output not available: no output (retrying)"), channel.status().message);
}

TEST(PlayoutChannelThread, MissingDecoder)
{
  PlayoutChannel channel(QString::fromUtf8("no decoder"), true, QString::fromUtf8("/nonexistent/ffmpeg"), createFakeSink);
  channel.start();
  channel.setMedia(media(10));
  channel.play();
  ASSERT_TRUE(waitFor([&]() { return channel.status().state == PlayoutChannel::eError; }, 8000));
  EXPECT_EQ(QString::fromUtf8("cannot start ffmpeg (/nonexistent/ffmpeg)"), channel.status().message);
}

#ifdef Q_OS_UNIX
namespace
{
// ffmpeg stand-in writing frameCount frames of 2x2 BGRA (all zero).
QString fakeDecoder(const QTemporaryDir &dir, int frameCount)
{
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("ffmpeg"));
  writeScript(path, "head -c " + QByteArray::number(frameCount * 16) + " /dev/zero");
  return path;
}
}

TEST(PlayoutChannelThread, PlaysToTheEnd)
{
  QTemporaryDir dir;
  const QString name = QString::fromUtf8("play channel");
  PlayoutChannel channel(name, true, fakeDecoder(dir, 5), createFakeSink);
  channel.start();
  channel.setMedia(media(5));
  channel.play();

  ASSERT_TRUE(waitFor([&]() { return channel.status().state == PlayoutChannel::ePaused; }, 8000));
  EXPECT_EQ(QString::fromUtf8("End"), channel.status().message);
  EXPECT_EQ(5, channel.status().position);
  EXPECT_EQ(5, fakeSinkRecord(name).frames);
  EXPECT_EQ(25, fakeSinkRecord(name).fpsNum);

  // Play again: from the start.
  channel.play();
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(name).frames == 10; }, 8000));
}

TEST(PlayoutChannelThread, StopsAtPausePoints)
{
  QTemporaryDir dir;
  const QString name = QString::fromUtf8("cue channel");
  PlayoutChannel channel(name, true, fakeDecoder(dir, 10), createFakeSink);
  channel.start();
  channel.setMedia(media(10));
  channel.setPausePoints(QList<double>() << 0.2); // frame 5
  channel.cue();

  ASSERT_TRUE(waitFor([&]() { return channel.status().state == PlayoutChannel::ePaused; }, 8000));
  EXPECT_EQ(QString::fromUtf8("Pause point"), channel.status().message);
  EXPECT_EQ(5, channel.status().position);

  channel.play(); // continues past it
  ASSERT_TRUE(waitFor([&]() { return channel.status().message == QString::fromUtf8("End"); }, 8000));
  EXPECT_EQ(10, channel.status().position);
}

TEST(PlayoutChannelThread, Loops)
{
  QTemporaryDir dir;
  const QString name = QString::fromUtf8("loop channel");
  PlayoutChannel channel(name, true, fakeDecoder(dir, 3), createFakeSink);
  channel.start();
  channel.setMedia(media(3));
  channel.setLoop(true);
  channel.play();
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(name).frames >= 8; }, 8000));
  EXPECT_EQ(PlayoutChannel::ePlaying, channel.status().state);
}

TEST(PlayoutChannelThread, StopClearsTheOutput)
{
  QTemporaryDir dir;
  const QString name = QString::fromUtf8("clear channel");
  PlayoutChannel channel(name, false, fakeDecoder(dir, 2), createFakeSink);
  channel.start();
  channel.setMedia(media(2));
  channel.play();
  ASSERT_TRUE(waitFor([&]() { return channel.status().message == QString::fromUtf8("End"); }, 8000));
  const int frames = fakeSinkRecord(name).frames;

  channel.stop();
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(name).frames == frames + 1; }));
  // Opaque output: a black frame.
  EXPECT_EQ(solid(2, 2, 0, 0, 0, char(255)), fakeSinkRecord(name).lastFrame);
}

TEST(PlayoutChannelThread, SeekWhilePausedShowsThatFrame)
{
  QTemporaryDir dir;
  const QString name = QString::fromUtf8("seek channel");
  PlayoutChannel channel(name, true, fakeDecoder(dir, 1), createFakeSink);
  channel.start();
  channel.setMedia(media(10));
  channel.seek(0.2);
  ASSERT_TRUE(waitFor([&]() { return fakeSinkRecord(name).frames == 1; }));
  EXPECT_EQ(PlayoutChannel::ePaused, channel.status().state);
  EXPECT_EQ(5, channel.status().position);
}
#endif
