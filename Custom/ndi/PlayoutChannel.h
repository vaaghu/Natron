#pragma once

#include "FrameSink.h"
#include "MediaProbe.h"

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>

class QProcess;

// One output (NDI source) playing a rendered output with transport
// controls, in its own thread:
//   play / pause / stop / replay, loop, a pause point (stop at N seconds,
//   then continue on play), and live frames (sent as they are rendered).
// Decoding: the bundled ffmpeg writes raw BGRA frames to a pipe.
// All public methods are thread-safe; statusChanged() is emitted from the
// channel thread (connect with a queued connection).
class PlayoutChannel : public QThread
{
  Q_OBJECT

public:
  enum State
  {
    eStopped,  // nothing on air (a clear frame was sent)
    ePlaying,
    ePaused,   // holding the current frame
    eLive,     // sending frames as the renderer produces them
    eError
  };

  PlayoutChannel(const QString &sourceName,
                 bool alpha,
                 const QString &ffmpegPath,
                 FrameSinkFactory sinkFactory,
                 QObject *parent = nullptr);
  ~PlayoutChannel();

  QString sourceName() const;
  bool alpha() const;

  // New rendered output to play, from the same position (a playing channel
  // continues playing it).
  void setMedia(const MediaInfo &media);

  void play();          // from the current position (continues after a pause point)
  void pause();
  void stop();          // back to the start, clears the output
  void replay();        // from the start
  void cue();           // from the start, stopping at the pause point
  void setLoop(bool loop);
  void setPauseAt(double seconds); // < 0: no pause point

  // Information shown with the status (e.g. why live is not possible).
  void setMessage(const QString &message);

  // Live: frames sent immediately as they arrive (enterLive() first).
  void enterLive(int width, int height, double fps);
  void pushLiveFrame(const QByteArray &bgra, int width, int height);
  void leaveLive();     // holds the last frame (ePaused at the end)

  struct Status
  {
    State state;
    int position;       // frame index (0 = first)
    int frameCount;
    double fps;
    double pauseAt;
    bool loop;
    int connections;    // NDI receivers (-1 unknown)
    QString message;    // error / info

    double seconds() const { return fps > 0 ? position / fps : 0.0; }
    double duration() const { return fps > 0 ? frameCount / fps : 0.0; }
  };
  Status status() const;

  static QString stateName(State state);

Q_SIGNALS:
  void statusChanged();

protected:
  void run();

private:
  // channel thread only
  bool startDecoder(int frame);
  void stopDecoder();
  bool readFrame(QByteArray *frame);
  void sendClearFrame();
  void sendFrame(const QByteArray &frame, int width, int height);
  void setStateLocked(State state, const QString &message = QString());

  const QString m_name;
  const bool m_alpha;
  const QString m_ffmpegPath;
  const FrameSinkFactory m_sinkFactory;

  mutable QMutex m_mutex;
  // shared state (m_mutex)
  MediaInfo m_media;
  bool m_mediaChanged;
  State m_state;
  int m_position;
  int m_seekTo;          // -1: none
  bool m_loop;
  double m_pauseAt;
  bool m_pauseArmed;     // stop at the pause point on this pass
  bool m_cueMode;        // started by cue(): each loop pass stops at the pause point too
  bool m_clearRequested;
  bool m_quit;
  QList<QByteArray> m_liveFrames;
  int m_liveWidth;
  int m_liveHeight;
  double m_liveFps;
  int m_connections;
  QString m_message;
  QString m_outputError; // the output (NDI) could not be opened: stays in eError

  // channel thread
  FrameSink *m_sink;
  QProcess *m_decoder;
  int m_decoderFrame;    // next frame the decoder will output
  QByteArray m_lastFrame;
  int m_lastWidth;
  int m_lastHeight;
};
