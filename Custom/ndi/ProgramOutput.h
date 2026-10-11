#pragma once

#include "FrameSink.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

class NdiManager;
class ProgramMixer; // ProgramOutput.cpp
class SceneStore;

// Program output of the dashboard's switcher, like a vision mixer: one NDI
// source showing the input on Program, with another input on Preview ready
// to go on air. An input is an item of a scene (a Write node, see
// SceneItem), played by its scene's NDI playout channel (see NdiManager):
// the mixer takes the frames that channel sends, so the scene must have NDI
// output enabled.
//   cut()                 Preview goes on air at once; the old Program goes to Preview
//   fade(ms)              crossfades from Program to Preview, then swaps them
//   setManualPosition(t)  the T-bar: 0..1 of the crossfade, 1 completes it
//   fadeToBlack(ms)       fades Program to black, or back from black
// An input taken to Program starts playing if it is stopped or at its end.
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

  ProgramOutput(NdiManager *ndi,
                SceneStore *scenes,
                const QString &sourceName,
                FrameSinkFactory sinkFactory,
                QObject *parent = nullptr);
  ~ProgramOutput();

  QString sourceName() const;

  Source preview() const;
  Source program() const;
  void setPreview(const Source &source);
  // Straight to Program; Preview stays (unless it is that input: then like cut()).
  void cutTo(const Source &source);

  void cut();
  void fade(int durationMs);
  void setManualPosition(double position);
  bool inTransition() const;        // between Program and Preview
  bool inAutoTransition() const;    // fade() running
  double transitionPosition() const; // 0 (Program) .. 1 (Preview)

  void fadeToBlack(int durationMs); // toggles
  bool isBlack() const;             // faded or fading to black
  double blackLevel() const;        // 0..1

  // What is sent now, at most maxWidth wide; null before the first frame.
  QImage outputPreview(int maxWidth) const;
  QString outputError() const; // the NDI source could not be opened (empty: fine)
  int connections() const;     // receivers (-1: unknown)

Q_SIGNALS:
  // The Preview or Program input changed.
  void busChanged();
  // The transition or fade-to-black position changed.
  void transitionChanged();

private Q_SLOTS:
  void onTick();
  void onScenesChanged();

private:
  void startPlaying(const Source &source);
  void stopTransition();
  void swap(); // Preview <-> Program

  NdiManager *m_ndi;
  SceneStore *m_scenes;
  QString m_name;
  ProgramMixer *m_mixer;
  QTimer m_tick;
  Source m_preview;
  Source m_program;
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
