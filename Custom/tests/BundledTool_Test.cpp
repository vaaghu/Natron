#include "TestUtil.h"

#include "Custom/util/BundledTool.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>

#include "gtest/gtest.h"

namespace
{
const char kVar[] = "LD_LIBRARY_PATH";

// Sets LD_LIBRARY_PATH for the test, restoring it after.
class LibraryPathGuard
{
public:
  explicit LibraryPathGuard(const QByteArray &value)
      : m_had(qEnvironmentVariableIsSet(kVar)),
        m_old(qgetenv(kVar))
  {
    if (value.isNull())
    {
      qunsetenv(kVar);
    }
    else
    {
      qputenv(kVar, value);
    }
  }

  ~LibraryPathGuard()
  {
    if (m_had)
    {
      qputenv(kVar, m_old);
    }
    else
    {
      qunsetenv(kVar);
    }
  }

private:
  bool m_had;
  QByteArray m_old;
};
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
TEST(BundledTool, PutsTheBundleLibFolderFirst)
{
  QTemporaryDir dir;
  ASSERT_TRUE(QDir(dir.path()).mkpath(QString::fromUtf8("bin")));
  ASSERT_TRUE(QDir(dir.path()).mkpath(QString::fromUtf8("lib")));
  const QString tool = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("bin/ffmpeg"));
  const QString lib = QDir(QDir(dir.path()).absoluteFilePath(QString::fromUtf8("lib"))).absolutePath();

  {
    LibraryPathGuard guard{QByteArray()};
    EXPECT_EQ(lib, bundledToolEnvironment(tool).value(QString::fromUtf8(kVar)));
  }
  {
    LibraryPathGuard guard("/usr/other");
    EXPECT_EQ(lib + QString::fromUtf8(":/usr/other"), bundledToolEnvironment(tool).value(QString::fromUtf8(kVar)));
  }
}

TEST(BundledTool, LeavesTheEnvironmentWithoutLibFolder)
{
  QTemporaryDir dir;
  const QString tool = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("bin/ffmpeg"));
  LibraryPathGuard guard("/usr/other");
  EXPECT_EQ(QString::fromUtf8("/usr/other"), bundledToolEnvironment(tool).value(QString::fromUtf8(kVar)));
}

TEST(BundledTool, PrepareAppliesToTheProcess)
{
  QTemporaryDir dir;
  ASSERT_TRUE(QDir(dir.path()).mkpath(QString::fromUtf8("lib")));
  const QString tool = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("bin/ffprobe"));
  LibraryPathGuard guard{QByteArray()};

  QProcess process;
  prepareBundledTool(&process, tool);
  EXPECT_TRUE(process.processEnvironment().value(QString::fromUtf8(kVar)).endsWith(QString::fromUtf8("/lib")));
}
#endif

TEST(BundledTool, KeepsTheRestOfTheEnvironment)
{
  qputenv("NATRON_CUSTOM_TESTS_VAR", "kept");
  EXPECT_EQ(QString::fromUtf8("kept"), bundledToolEnvironment(QString::fromUtf8("/nonexistent/bin/ffmpeg"))
                                        .value(QString::fromUtf8("NATRON_CUSTOM_TESTS_VAR")));
  qunsetenv("NATRON_CUSTOM_TESTS_VAR");
}

TEST(BundledTool, PrepareIgnoresNullProcess)
{
  prepareBundledTool(nullptr, QString::fromUtf8("/nonexistent/bin/ffmpeg"));
}
