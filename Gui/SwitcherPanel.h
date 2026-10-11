/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2023 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#ifndef Gui_SwitcherPanel_h
#define Gui_SwitcherPanel_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QList>
#include <QPair>
#include <QString>
#include <QTimer>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Custom/ndi/ProgramOutput.h"

class QColor;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;
class NdiManager; // Custom/ndi/NdiManager.h
class PlayoutChannel; // Custom/ndi/PlayoutChannel.h
class SceneStore; // Custom/scene/SceneStore.h

NATRON_NAMESPACE_ENTER

class BusTimeline;
class VideoMonitor;

/**
 * @brief Dashboard panel switching the Program output (see ProgramOutput),
 * laid out like vision mixers:
 *   Preview bus | format, Cut, Fade, duration, Fade to Black, T-bar | Program bus
 * Each bus shows its layers composited in the output format, the list of
 * its layers (reordered and removed here; added from the input boxes of
 * the scene panel), and a timeline like the editor's: a playhead scrubbing
 * all its layers, transport controls, and the layers' cue points as
 * keyframes (where Cue and Play stop).
 **/
class SwitcherPanel
    : public QWidget
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    // program and ndi may be null (NDI outputs not created): the panel then
    // only says so.
    SwitcherPanel(::ProgramOutput* program,
                  ::NdiManager* ndi,
                  ::SceneStore* scenes,
                  QWidget* parent = 0);

    virtual ~SwitcherPanel();

private Q_SLOTS:

    void onBusChanged();
    void onTransitionChanged();
    void onRefreshTimer();
    void onCutClicked();
    void onFadeClicked();
    void onFadeToBlackClicked();
    void onTBarMoved(int value);
    void onTBarReleased();
    void onDurationChanged(int ms);
    void onFormatChosen(int index);
    void onFpsChosen(int index);

    // Per bus (the sender's "bus" property).
    void onLayerSelectionChanged();
    void onLayerUpClicked();
    void onLayerDownClicked();
    void onLayerRemoveClicked();
    void onTransportClicked(); // the sender's object name: play, pause, stop, cue, continue, previous, next, addCue
    void onTimelineSeek(double seconds);
    void onMarkerAdded(double seconds);
    void onMarkerMoved(double from, double to);
    void onMarkerRemoved(double seconds);

private:

    struct BusWidgets
    {
        ::ProgramOutput::Bus bus;
        QLabel* title;
        VideoMonitor* monitor;
        QListWidget* layers;
        QToolButton* up;
        QToolButton* down;
        QToolButton* remove;
        BusTimeline* timeline;
        QLabel* time;
        QLabel* status;
    };

    // [title] [monitor] [layers] [timeline] [transport] [status]
    QWidget* createBus(::ProgramOutput::Bus bus, const QString& title, const QColor& color);
    QWidget* createTransitions();
    BusWidgets& busOf(QObject* sender);
    QString sourceLabel(const ::ProgramOutput::Source& source) const;
    // The layers of a bus with their channels (null without NDI output).
    QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > channels(::ProgramOutput::Bus bus) const;
    // The selected layer of a bus (empty: none).
    ::ProgramOutput::Source selectedLayer(const BusWidgets& w) const;
    void refreshLayerList(BusWidgets& w);
    void refreshTimeline(BusWidgets& w);
    void refreshButtons();
    // Cue points of every layer of a bus: add one, move one (from -> to),
    // or remove one (to < 0).
    void editCuePoints(::ProgramOutput::Bus bus, double from, double to);
    void applyFormat(int width, int height, double fps);

    ::ProgramOutput* _program;
    ::NdiManager* _ndi;
    ::SceneStore* _scenes;

    BusWidgets _buses[2]; // indexed by ProgramOutput::Bus
    QLabel* _onAirLabel;

    QComboBox* _formatCombo;
    QComboBox* _fpsCombo;
    QPushButton* _cutButton;
    QPushButton* _fadeButton;
    QSpinBox* _durationSpin;
    QPushButton* _fadeToBlackButton;
    QSlider* _tbar;
    bool _tbarLatched; // the T-bar completed a transition: ignored until released

    QTimer _refreshTimer;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_SwitcherPanel_h
