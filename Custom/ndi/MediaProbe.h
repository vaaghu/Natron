#pragma once

#include <QString>

// What a playout channel needs to know about a rendered output: a video
// file, a single image, or an image sequence (frame_####.png).
struct MediaInfo
{
  bool ok;
  QString error;

  QString input;    // file, or ffmpeg sequence pattern (frame_%04d.png)
  bool sequence;
  int startNumber;  // first frame number of a sequence
  int width;
  int height;
  int fpsNum;
  int fpsDen;
  int frameCount;

  MediaInfo()
      : ok(false),
        sequence(false),
        startNumber(0),
        width(0),
        height(0),
        fpsNum(24),
        fpsDen(1),
        frameCount(0)
  {
  }

  double fps() const
  {
    return fpsDen > 0 ? double(fpsNum) / fpsDen : 0.0;
  }

  double duration() const
  {
    return fps() > 0 ? frameCount / fps() : 0.0;
  }
};

// outputPattern: the output path as written by the Write node (may hold
// ### / %04d). sequenceFps: frame rate of image sequences (the project's).
MediaInfo probeMedia(const QString &outputPattern, const QString &ffprobePath, double sequenceFps);

// Frame rate as an exact fraction (23.976 -> 24000/1001).
void fpsToFraction(double fps, int *num, int *den);
