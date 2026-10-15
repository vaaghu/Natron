#include "TestUtil.h"

#include "Custom/sdi/SdiOutput.h"

#include <cstdio>

#include "gtest/gtest.h"

// Without the Blackmagic drivers (the usual case on build machines) SDI
// is unavailable; with them, only what needs no card is checked.

TEST(SdiOutput, ModeAndPortDefaults)
{
  Sdi::Mode mode;
  EXPECT_EQ(0u, mode.id);
  EXPECT_EQ(0, mode.width);
  EXPECT_EQ(0, mode.height);
  EXPECT_DOUBLE_EQ(0.0, mode.fps);
  EXPECT_FALSE(mode.interlaced);
  EXPECT_TRUE(mode.name.isEmpty());

  Sdi::Port port;
  EXPECT_TRUE(port.modes.isEmpty());
}

TEST(SdiOutput, ClosedOutput)
{
  Sdi::Output output;
  EXPECT_FALSE(output.isOpen());
  EXPECT_FALSE(output.isClocked());
  EXPECT_TRUE(output.modeName().isEmpty());
  output.sendFrame(QByteArray(16, '\0'), 2, 2); // ignored
  output.close();
  output.close();
}

TEST(SdiOutput, UnknownPort)
{
  Sdi::Output output;
  QString error;
  EXPECT_FALSE(output.open(QString::fromUtf8("No Such Port"), 0, 1920, 1080, 25, false, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_FALSE(output.isOpen());
  EXPECT_TRUE(output.modeName().isEmpty());
}

TEST(SdiOutput, WithoutTheDrivers)
{
  if (Sdi::isAvailable())
  {
    printf("DeckLink drivers installed: skipped\n");
    return;
  }
  QString error;
  EXPECT_FALSE(Sdi::load(&error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_FALSE(Sdi::load(nullptr));
#if defined(Q_OS_LINUX)
  EXPECT_TRUE(error.startsWith(QString::fromUtf8("Blackmagic Desktop Video drivers not installed")));
#else
  EXPECT_EQ(QString::fromUtf8("SDI output is only available on Linux"), error);
#endif

  QString portsError;
  EXPECT_TRUE(Sdi::ports(&portsError).isEmpty());
  EXPECT_FALSE(portsError.isEmpty());
  EXPECT_TRUE(Sdi::ports().isEmpty());
}
