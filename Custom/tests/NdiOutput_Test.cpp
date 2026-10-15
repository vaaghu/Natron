#include "TestUtil.h"

#include "Custom/ndi/NdiOutput.h"

#include <QLibrary>

#include <cstdio>

#include <memory>

#include "gtest/gtest.h"

// With the fake NDI runtime (FakeNdi.cpp, CMake builds on Linux), NdiOutput
// is checked against it; otherwise only the "not installed" path is, unless
// a real runtime is installed.

#ifdef FAKE_NDI_DIR
namespace
{
typedef int (*IntFn)();
typedef void (*LastFrameFn)(int *);
typedef const char *(*NameFn)();

class FakeNdi
{
public:
  FakeNdi()
  {
    Ndi::load(); // the fake runtime is then the loaded library
    m_library.setFileName(Ndi::libraryPath());
    m_library.load();
  }

  int senders() { return call("fakeNdiSenders"); }
  int frames() { return call("fakeNdiFrames"); }
  int lastClock() { return call("fakeNdiLastClock"); }

  QString lastName()
  {
    NameFn fn = (NameFn)m_library.resolve("fakeNdiLastName");
    return fn ? QString::fromUtf8(fn()) : QString();
  }

  // xres, yres, FourCC, frame rate N, D, line stride
  QList<int> lastFrame()
  {
    int out[6] = {0, 0, 0, 0, 0, 0};
    LastFrameFn fn = (LastFrameFn)m_library.resolve("fakeNdiLastFrame");
    if (fn)
    {
      fn(out);
    }
    QList<int> list;
    for (int i = 0; i < 6; ++i)
    {
      list << out[i];
    }
    return list;
  }

private:
  int call(const char *name)
  {
    IntFn fn = (IntFn)m_library.resolve(name);
    return fn ? fn() : -100;
  }

  QLibrary m_library;
};

int fourCC(char a, char b, char c, char d)
{
  return int(uint(uchar(a)) | (uint(uchar(b)) << 8) | (uint(uchar(c)) << 16) | (uint(uchar(d)) << 24));
}
}

TEST(NdiOutput, LoadsTheRuntime)
{
  QString error;
  ASSERT_TRUE(Ndi::load(&error)) << error.toStdString();
  EXPECT_TRUE(error.isEmpty());
  EXPECT_TRUE(Ndi::isAvailable());
  EXPECT_TRUE(Ndi::libraryPath().endsWith(QString::fromUtf8("libndi.so")));
}

TEST(NdiOutput, SendsFrames)
{
  FakeNdi ndi;
  const int senders = ndi.senders();
  {
    std::unique_ptr<FrameSink> sink(Ndi::createSink());
    EXPECT_TRUE(sink->isClocked());
    EXPECT_EQ(-1, sink->connectionCount()); // not open
    QString error;
    ASSERT_TRUE(sink->open(QString::fromUtf8("Show - p"), true, &error)) << error.toStdString();
    EXPECT_EQ(senders + 1, ndi.senders());
    EXPECT_EQ(QString::fromUtf8("Show - p"), ndi.lastName());
    EXPECT_EQ(1, ndi.lastClock());
    EXPECT_EQ(3, sink->connectionCount());

    const int frames = ndi.frames();
    sink->sendFrame(QByteArray(4 * 2 * 4, '\0'), 4, 2, 30000, 1001);
    EXPECT_EQ(frames + 1, ndi.frames());
    EXPECT_EQ(QList<int>() << 4 << 2 << fourCC('B', 'G', 'R', 'A') << 30000 << 1001 << 16, ndi.lastFrame());

    sink->sendFrame(QByteArray(10, '\0'), 4, 2, 25, 1); // too short: dropped
    EXPECT_EQ(frames + 1, ndi.frames());
  }
  EXPECT_EQ(senders, ndi.senders()); // destroyed with the sink
}

TEST(NdiOutput, OpaqueAndUnclockedSinks)
{
  FakeNdi ndi;
  std::unique_ptr<FrameSink> sink(Ndi::createSink());
  sink->setClocked(false);
  EXPECT_FALSE(sink->isClocked());
  ASSERT_TRUE(sink->open(QString::fromUtf8("Program"), false, nullptr));
  EXPECT_EQ(0, ndi.lastClock());
  sink->sendFrame(QByteArray(4, '\0'), 1, 1, 25, 1);
  EXPECT_EQ(fourCC('B', 'G', 'R', 'X'), ndi.lastFrame().at(2));
}

#else

TEST(NdiOutput, WithoutTheRuntime)
{
  if (Ndi::isAvailable())
  {
    printf("NDI runtime installed (%s): skipped\n", Ndi::libraryPath().toUtf8().constData());
    return;
  }
  QString error;
  EXPECT_FALSE(Ndi::load(&error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_TRUE(Ndi::libraryPath().isEmpty());
  EXPECT_FALSE(Ndi::load(nullptr));

  std::unique_ptr<FrameSink> sink(Ndi::createSink());
  QString openError;
  EXPECT_FALSE(sink->open(QString::fromUtf8("x"), true, &openError));
  EXPECT_EQ(error, openError);
  EXPECT_EQ(-1, sink->connectionCount());
  sink->sendFrame(QByteArray(4, '\0'), 1, 1, 25, 1); // ignored
}

#endif
