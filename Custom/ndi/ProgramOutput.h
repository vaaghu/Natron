#pragma once

#include "FrameSink.h"
#include "../scene/SceneStore.h" // LayerPlacement

#include <QHash>

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class NdiManager;
class ProgramMixer; // ProgramOutput.cpp
class SceneStore;

// Program output of the dashboard's switcher, like a vision mixer: one NDI
// source of a fixed format (size, frame rate) showing the layers on
// Program, with the layers on Preview ready to go on air.
// A bus shows one scene: all its layers are items of the same scene (a
// layer from another scene replaces them).
// A layer is an input: an item of a scene (a Write node, see SceneItem),
// played by its scene's NDI playout channel (see NdiManager); the mixer
// takes the frames that channel sends, so the scene must have NDI output
// enabled. The layers of a bus are composited in order (the first at the
// bottom), with their transparency, over black, each at its placement in
// the format (fitted and centred by default). A layer taken to a bus gets
// its scene's saved placement; Cut and Fade carry the placements along.
// Each bus has its own clock (play, pause, stop, restart, seek, loop): a
// layer starts at its placement's start time on it, plays its render from
// there, and only shows until its render ends. Playing stops at the next
// cue point of the layers (in bus time).
//   cut()                 Preview goes on air at once; Preview stays as it is
//   fade(ms)              crossfades from Program to Preview, which then goes on air
//   setManualPosition(t)  the T-bar: 0..1 of the crossfade, 1 completes it
//   fadeToBlack(ms)       fades Program to black, or back from black
// Layers put on an empty Preview hold their first frame; Preview going on
// air plays from where its clock is, and keeps playing in step with Program.
// The NDI source can be turned off, and Program can also be played out of
// an SDI port (see Custom/sdi), in the output format's mode or a chosen
// one: NDI, SDI, both, or neither (the monitors still show Program).
// Frames are mixed and sent in a worker thread; the methods are for the
// main thread.
class ProgramOutput : public QObject
{
  Q_OBJECT

public:
  // An input (empty: none).
  struct Source
  {
    QString sceneId;
    QString item;

    Source() {}
    Source(const QString &scene, const QString &sceneItem)
        : sceneId(scene),
          item(sceneItem)
    {
    }

    bool isEmpty() const { return sceneId.isEmpty() || item.isEmpty(); }
    bool operator==(const Source &other) const { return sceneId == other.sceneId && item == other.item; }
    bool operator!=(const Source &other) const { return !(*this == other); }
  };
  typedef QList<Source> Layers; // bottom first

  enum Bus
  {
    ePreview,
    eProgram
  };

  ProgramOutput(NdiManager *ndi,
                SceneStore *scenes,
                const QString &sourceName,
                FrameSinkFactory sinkFactory,
                QObject *parent = nullptr);
  ~ProgramOutput();

  QString sourceName() const;

  // Output format of both buses.
  void setFormat(int width, int height, double fps);
  int width() const;
  int height() const;
  double fps() const;

  Layers layers(Bus bus) const;
  bool contains(Bus bus, const Source &source) const;
  void setLayers(Bus bus, const Layers &layers);
  void addLayer(Bus bus, const Source &source);    // on top; no change if there; another scene's: replaces the layers
  void removeLayer(Bus bus, const Source &source);
  void moveLayer(Bus bus, const Source &source, int steps); // > 0: up

  // Where a layer of a bus sits in the output.
  LayerPlacement placement(Bus bus, const Source &source) const;
  void setPlacement(Bus bus, const Source &source, const LayerPlacement &placement);

  // Bus playback (see above).
  void play(Bus bus);    // on to the next cue point
  void pause(Bus bus);
  void stop(Bus bus);    // back to the start, layers cleared
  void restart(Bus bus); // from the start to the first cue point
  void seek(Bus bus, double seconds);
  void setLoop(Bus bus, bool loop);
  bool loops(Bus bus) const;
  bool isPlaying(Bus bus) const;
  double position(Bus bus) const;
  double duration(Bus bus) const;            // to the end of its last layer
  double layerDuration(const Source &source) const; // its render's length (0: unknown)
  bool isActive(Bus bus, const Source &source) const; // showing now (within its block)
  QList<double> cuePoints(Bus bus) const;    // the layers' cue points in bus time, ascending

  // Only this input on Preview.
  void setPreview(const Source &source);
  // Only this input on Program; Preview stays (unless it is just that input: then like cut()).
  void cutTo(const Source &source);

  void cut();
  void fade(int durationMs);
  void setManualPosition(double position);
  bool inTransition() const;         // between Program and Preview
  bool inAutoTransition() const;     // fade() running
  double transitionPosition() const; // 0 (Program) .. 1 (Preview)

  void fadeToBlack(int durationMs); // toggles
  bool isBlack() const;             // faded or fading to black
  double blackLevel() const;        // 0..1

  // What is sent now / the Preview layers composited, at most maxWidth
  // wide; null before the first frame.
  QImage outputPreview(int maxWidth) const;
  QImage previewImage(int maxWidth) const;
  QString outputError() const; // the NDI source could not be opened (empty: fine, or off)
  int connections() const;     // receivers (-1: unknown)

  // Program as an NDI source (on by default).
  void setNdiEnabled(bool enabled);
  bool ndiEnabled() const;

  // SDI port playing Program (empty: none, see Sdi::ports()), its video
  // mode (0: the mode of the output format), and its clock: the card's
  // (frames queued on the card, which then paces Program, NDI included) or
  // the system's (see Sdi::Output).
  void setSdiOutput(const QString &port, quint32 mode, bool cardClock);
  QString sdiPort() const;
  quint32 sdiMode() const;
  bool sdiCardClock() const;
  // "port · mode" while playing, why not otherwise; empty: no port chosen.
  QString sdiStatus(bool *ok = nullptr) const;

Q_SIGNALS:
  // The layers on Preview or Program changed.
  void busChanged();
  // The transition or fade-to-black position changed.
  void transitionChanged();

private Q_SLOTS:
  void onTick();
  void onScenesChanged();

private:
  Layers &bus(Bus bus);
  static QString placementKey(const Source &source);
  LayerPlacement savedPlacement(const Source &source) const; // the scene's
  struct Clock
  {
    bool playing;
    bool stopped;   // at the start, layers cleared
    double position;
    double from;    // position when the timer started
    QElapsedTimer timer;
    double stopAt;  // cue point where playing stops (-1: none)
    bool loop;

    Clock()
        : playing(false),
          stopped(true),
          position(0),
          from(0),
          stopAt(-1),
          loop(false)
    {
    }
  };

  void advance(Bus bus); // the clock's position now
  void drive(Bus bus, const QStringList &skip); // makes the layers' channels follow the clock
  double nextCuePoint(Bus bus, double after) const;
  void stopTransition();
  void take(); // Program becomes a copy of Preview

  NdiManager *m_ndi;
  SceneStore *m_scenes;
  QString m_name;
  ProgramMixer *m_mixer;
  bool m_ndiEnabled;
  QString m_sdiPort;
  quint32 m_sdiMode;
  bool m_sdiCardClock;
  QTimer m_tick;
  Layers m_preview;
  Layers m_program;
  QHash<QString, LayerPlacement> m_placements[2]; // per Bus: placementKey() -> placement
  Clock m_clocks[2]; // per Bus
  QHash<QString, QElapsedTimer> m_seeks; // placementKey() -> since the bus last sought that layer
  int m_width;
  int m_height;
  double m_fps;
  // Transition: automatic (m_fadeMs > 0, timed from m_fadeFrom) or the T-bar.
  double m_position;
  int m_fadeMs;
  double m_fadeFrom;
  QElapsedTimer m_fadeClock;
  // Fade to black, towards m_blackTarget.
  bool m_blackTarget;
  double m_black;
  int m_blackMs;
  double m_blackFrom;
  QElapsedTimer m_blackClock;
};
