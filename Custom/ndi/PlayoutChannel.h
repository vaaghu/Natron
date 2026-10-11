#pragma once

#include "FrameSink.h"
#include "MediaProbe.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>

class QProcess;

// One output (NDI source) playing a rendered output with transport
// controls, in its own thread:
//   play / pause / stop / replay, seek, loop, pause points (stop at each
//   of them in turn, continuing on play), and live frames (sent as they
//   are rendered).
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
  // To a time: a playing channel goes on playing from there, otherwise it
  // holds that frame (paused).
  void seek(double seconds);
  void setLoop(bool loop);
  void setPauseAt(double seconds); // only this pause point; < 0: none
  void setPausePoints(const QList<double> &seconds);

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
    double pauseAt;     // first pause point (-1: none)
    QList<double> pausePoints;
    bool loop;
    int connections;    // NDI receivers (-1 unknown)
    QString message;    // error / info

    double seconds() const { return fps > 0 ? position / fps : 0.0; }
    double duration() const { return fps > 0 ? frameCount / fps : 0.0; }
  };
  Status status() const;

  // The frame last sent (what receivers show), at most maxWidth wide; null
  // before the first frame.
  QImage preview(int maxWidth) const;

  // The frame last sent, full size and shared (no copy); false before the
  // first frame.
  bool currentFrame(QByteArray *bgra, int *width, int *height) const;

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
  void showFrame(int frame); // decodes and sends one frame (seek while paused)
  void openSink(); // the output; on failure m_outputError says why
  void setStateLocked(State state, const QString &message = QString());
  // First pause point after frame (-1: none).
  int nextPauseFrameLocked(int frame) const;

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
  QList<double> m_pausePoints; // seconds, ascending
  bool m_pauseArmed;     // stop at m_pauseFrame
  int m_pauseFrame;
  bool m_cueMode;        // started by cue(): each loop pass stops at the pause point too
  bool m_clearRequested;
  bool m_quit;
  QList<QByteArray> m_liveFrames;
  int m_liveWidth;
  int m_liveHeight;
  double m_liveFps;
  int m_connections;
  QString m_message;
  QString m_outputError; // the output (NDI) could not be opened (retried; playing goes on)

  // channel thread
  FrameSink *m_sink;
  QProcess *m_decoder;
  int m_decoderFrame;    // next frame the decoder will output
  QByteArray m_lastFrame;
  int m_lastWidth;
  int m_lastHeight;
  // Copy of the last frame for preview() (m_mutex; QByteArray shares the data).
  QByteArray m_previewFrame;
  int m_previewWidth;
  int m_previewHeight;
};
