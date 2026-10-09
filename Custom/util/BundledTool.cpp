#include "BundledTool.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

QProcessEnvironment bundledToolEnvironment(const QString &toolPath)
{
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
  const QString libDir = QDir(QFileInfo(toolPath).absolutePath() + QString::fromUtf8("/../lib")).absolutePath();
  if (QFileInfo(libDir).isDir())
  {
    const QString var = QString::fromUtf8("LD_LIBRARY_PATH");
    const QString current = env.value(var);
    env.insert(var, current.isEmpty() ? libDir : libDir + QLatin1Char(':') + current);
  }
#else
  Q_UNUSED(toolPath);
#endif

  return env;
}

void prepareBundledTool(QProcess *process, const QString &toolPath)
{
  if (process)
  {
    process->setProcessEnvironment(bundledToolEnvironment(toolPath));
  }
}
