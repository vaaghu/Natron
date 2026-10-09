#include "MediaProbe.h"

#include "../scene/ProjectInfo.h"
#include "../util/BundledTool.h"

#include <QFileInfo>
#include <QImageReader>
#include <QList>
#include <QProcess>
#include <QRegExp>
#include <QStringList>

#include <cmath>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

// ffprobe "key=value" output of the first video stream.
QStringList ffprobe(const QString &ffprobePath, const QStringList &extraArgs, const QString &input)
{
  QStringList args;
  args << s("-v") << s("error") << extraArgs
       << s("-select_streams") << s("v:0")
       << s("-show_entries") << s("stream=width,height,r_frame_rate,nb_frames:format=duration")
       << s("-of") << s("default=nw=1") << input;

  QProcess process;
  prepareBundledTool(&process, ffprobePath);
  process.start(ffprobePath, args);
  if (!process.waitForFinished(10000))
  {
    process.kill();
    process.waitForFinished(1000);
    return QStringList();
  }
  return QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'));
}

QString valueOf(const QStringList &lines, const char *key)
{
  const QString prefix = s(key) + QLatin1Char('=');
  for (int i = 0; i < lines.size(); ++i)
  {
    if (lines.at(i).startsWith(prefix))
    {
      return lines.at(i).mid(prefix.size()).trimmed();
    }
  }
  return QString();
}

// ### / %4d / %04d -> %0Nd as ffmpeg's image2 demuxer wants.
QString ffmpegSequencePattern(const QString &pattern)
{
  QString out = pattern;
  QRegExp hashes(s("#+"));
  const int pos = hashes.indexIn(out);
  if (pos >= 0)
  {
    // (not arg(): it would treat the literal %0 as a placeholder)
    out.replace(pos, hashes.matchedLength(), s("%0") + QString::number(hashes.matchedLength()) + QLatin1Char('d'));
    return out;
  }
  QRegExp printfFrame(s("%0?(\\d*)d"));
  const int ppos = printfFrame.indexIn(out);
  if (ppos >= 0)
  {
    const int width = qMax(1, printfFrame.cap(1).toInt());
    out.replace(ppos, printfFrame.matchedLength(), s("%0") + QString::number(width) + QLatin1Char('d'));
  }
  return out;
}
}

void fpsToFraction(double fps, int *num, int *den)
{
  if (fps <= 0)
  {
    *num = 24;
    *den = 1;
    return;
  }
  // NTSC rates
  const double ntsc[] = {23.976, 29.97, 47.952, 59.94, 119.88};
  for (int i = 0; i < 5; ++i)
  {
    if (std::fabs(fps - ntsc[i]) < 0.01)
    {
      *num = int(std::floor(ntsc[i] * 1001.0 / 1000.0 + 0.5)) * 1000;
      *den = 1001;
      return;
    }
  }
  if (std::fabs(fps - std::floor(fps + 0.5)) < 0.001)
  {
    *num = int(std::floor(fps + 0.5));
    *den = 1;
    return;
  }
  *num = int(std::floor(fps * 1000.0 + 0.5));
  *den = 1000;
}

MediaInfo probeMedia(const QString &outputPattern, const QString &ffprobePath, double sequenceFps)
{
  MediaInfo info;

  if (isSequencePattern(outputPattern))
  {
    const QList<int> frames = existingSequenceFrames(outputPattern);
    if (frames.isEmpty())
    {
      info.error = s("not rendered yet (no frames of %1)").arg(QFileInfo(outputPattern).fileName());
      return info;
    }

    info.sequence = true;
    info.startNumber = frames.first();
    info.frameCount = frames.size();
    info.input = ffmpegSequencePattern(QFileInfo(outputPattern).absoluteFilePath());
    fpsToFraction(sequenceFps, &info.fpsNum, &info.fpsDen);

    const QString first = sequenceFrameFile(outputPattern, frames.first());
    const QSize size = QImageReader(first).size();
    if (size.isValid())
    {
      info.width = size.width();
      info.height = size.height();
    }
    else if (!ffprobePath.isEmpty())
    {
      const QStringList lines = ffprobe(ffprobePath, QStringList(), first);
      info.width = valueOf(lines, "width").toInt();
      info.height = valueOf(lines, "height").toInt();
    }
  }
  else
  {
    if (!QFileInfo(outputPattern).exists())
    {
      info.error = s("not rendered yet (%1 not found)").arg(QFileInfo(outputPattern).fileName());
      return info;
    }
    info.input = QFileInfo(outputPattern).absoluteFilePath();

    const QStringList lines = ffprobePath.isEmpty() ? QStringList() : ffprobe(ffprobePath, QStringList(), info.input);
    info.width = valueOf(lines, "width").toInt();
    info.height = valueOf(lines, "height").toInt();

    // r_frame_rate is "num/den"; still images report 25/1 or 0/0.
    const QStringList rate = valueOf(lines, "r_frame_rate").split(QLatin1Char('/'));
    if (rate.size() == 2 && rate.at(0).toInt() > 0 && rate.at(1).toInt() > 0)
    {
      info.fpsNum = rate.at(0).toInt();
      info.fpsDen = rate.at(1).toInt();
    }
    else
    {
      fpsToFraction(sequenceFps, &info.fpsNum, &info.fpsDen);
    }

    bool ok = false;
    info.frameCount = valueOf(lines, "nb_frames").toInt(&ok);
    if (!ok || info.frameCount <= 0)
    {
      const double duration = valueOf(lines, "duration").toDouble(&ok);
      info.frameCount = (ok && duration > 0) ? qMax(1, int(std::floor(duration * info.fps() + 0.5))) : 1;
    }

    if (info.width <= 0)
    {
      // No ffprobe: still image Qt can read.
      const QSize size = QImageReader(info.input).size();
      info.width = size.width();
      info.height = size.height();
      info.frameCount = 1;
    }
  }

  if (info.width <= 0 || info.height <= 0)
  {
    info.error = s("cannot read the size of %1").arg(QFileInfo(outputPattern).fileName());
    return info;
  }

  info.ok = true;
  return info;
}
