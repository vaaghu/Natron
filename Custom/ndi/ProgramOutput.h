#pragma once

#include "FrameSink.h"
#include "../scene/SceneStore.h" // LayerPlacement

#include <QHash>

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class NdiManager;
class ProgramMixer; // ProgramOutput.cpp
class SceneStore;

// Program output of the dashboard's switcher, like a vision mixer: one NDI
// source of a fixed format (size, frame rate) showing the layers on
// Program, with the layers on Preview ready to go on air.
// A layer is an input: an item of a scene (a Write node, see SceneItem),
// played by its scene's NDI playout channel (see NdiManager); the mixer
// takes the frames that channel sends, so the scene must have NDI output
// enabled. The layers of a bus are composited in order (the first at the
// bottom), with their transparency, over black, each at its placement in
// the format (fitted and centred by default). A layer taken to a bus gets
// its scene's saved placement; Cut and Fade carry the placements along.
//   cut()                 Preview goes on air at once; the old Program goes to Preview
//   fade(ms)              crossfades from Program to Preview, then swaps them
//   setManualPosition(t)  the T-bar: 0..1 of the crossfade, 1 completes it
//   fadeToBlack(ms)       fades Program to black, or back from black
// A stopped layer put on Preview is cued: it holds its first frame. Layers
// going on air play (unless already playing).
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
  void addLayer(Bus bus, const Source &source);    // on top; no change if there
  void removeLayer(Bus bus, const Source &source);
  void moveLayer(Bus bus, const Source &source, int steps); // > 0: up

  // Where a layer of a bus sits in the output.
  LayerPlacement placement(Bus bus, const Source &source) const;
  void setPlacement(Bus bus, const Source &source, const LayerPlacement &placement);

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
  QString outputError() const; // the NDI source could not be opened (empty: fine)
  int connections() const;     // receivers (-1: unknown)

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
  void cueLayers(const Layers &layers);  // stopped ones hold their first frame
  void playLayers(const Layers &layers); // going on air
  void stopTransition();
  void swap(); // Preview <-> Program

  NdiManager *m_ndi;
  SceneStore *m_scenes;
  QString m_name;
  ProgramMixer *m_mixer;
  QTimer m_tick;
  Layers m_preview;
  Layers m_program;
  QHash<QString, LayerPlacement> m_placements[2]; // per Bus: placementKey() -> placement
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
