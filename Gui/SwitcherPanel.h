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
#include <QHash>
#include <QImage>
#include <QString>
#include <QTimer>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Custom/ndi/ProgramOutput.h"

class QColor;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class NdiManager; // Custom/ndi/NdiManager.h
class SceneStore; // Custom/scene/SceneStore.h

NATRON_NAMESPACE_ENTER

class VideoMonitor;

/**
 * @brief Dashboard panel switching the Program output (see ProgramOutput),
 * laid out like vision mixers:
 *   Preview monitor | Cut, Fade, duration, Fade to Black, T-bar | Program monitor
 * Inputs are put on Preview (or straight on Program) from the input boxes of
 * the scene panel. The Program monitor shows what the Program NDI source
 * sends.
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

private:

    // [title bar] [monitor] [status line]
    QWidget* createBus(const QString& title, const QColor& color, QLabel** titleLabel, VideoMonitor** monitor, QLabel** status);
    QString sourceLabel(const ::ProgramOutput::Source& source) const;
    // What an input shows now: its channel's frame, or its last render.
    QImage inputImage(const ::ProgramOutput::Source& source, int width);
    // Its playback state, for the corner of the monitor ("Playing 0:03.2 / 0:10.0").
    QString inputState(const ::ProgramOutput::Source& source) const;
    void refreshButtons();

    ::ProgramOutput* _program;
    ::NdiManager* _ndi;
    ::SceneStore* _scenes;

    QLabel* _previewTitle;
    VideoMonitor* _previewMonitor;
    QLabel* _previewStatus;
    QLabel* _programTitle;
    VideoMonitor* _programMonitor;
    QLabel* _programStatus;
    QLabel* _onAirLabel;

    QPushButton* _cutButton;
    QPushButton* _fadeButton;
    QSpinBox* _durationSpin;
    QPushButton* _fadeToBlackButton;
    QSlider* _tbar;
    bool _tbarLatched; // the T-bar completed a transition: ignored until released

    QTimer _refreshTimer;
    QHash<QString, QImage> _thumbnails; // render thumbnail file -> image
};

NATRON_NAMESPACE_EXIT

#endif // Gui_SwitcherPanel_h
