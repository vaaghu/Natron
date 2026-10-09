#pragma once

#include <QByteArray>
#include <QString>

// Where a playout channel sends its frames: an NDI sender, or a test sink.
// Frames are 8-bit BGRA, top row first, tightly packed (width * 4 bytes per
// row). Used from the channel's worker thread only.
class FrameSink
{
public:
  virtual ~FrameSink() {}

  // alpha: send with transparency (BGRA) or as opaque video (BGRX).
  virtual bool open(const QString &name, bool alpha, QString *error) = 0;

  virtual void sendFrame(const QByteArray &bgra, int width, int height,
                         int fpsNum, int fpsDen) = 0;

  // True if sendFrame() itself waits to keep the frame rate (NDI does);
  // otherwise the channel paces the frames.
  virtual bool isClocked() const = 0;

  // Receivers connected right now (-1: unknown).
  virtual int connectionCount() = 0;
};

// Creates the sink of a channel (one per NDI source).
typedef FrameSink *(*FrameSinkFactory)();
