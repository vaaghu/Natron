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

#ifndef Gui_DashboardSettingsDialog_h
#define Gui_DashboardSettingsDialog_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDialog>
#include <QList>
#include <QStringList>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Custom/sdi/SdiOutput.h"

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class ProgramOutput; // Custom/ndi/ProgramOutput.h
class SceneRenderer; // Custom/scene/SceneRenderer.h

NATRON_NAMESPACE_ENTER

/**
 * @brief The dashboard's Settings (Settings menu), kept between runs:
 *   Render      the hardware scene renders use: the CPU only, or a GPU
 *               (OpenGL renderer) for the nodes that support it, and the
 *               CPU threads of each render (NatronRenderer options, see
 *               SceneRenderer::setRendererOptions(); Natron's own
 *               preferences are left as they are)
 *   Program     the switcher's Program outputs, either or both: the NDI
 *               source, and an SDI port with its video mode and clock
 **/
class DashboardSettingsDialog
    : public QDialog
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    // Either may be null (its part is then disabled).
    DashboardSettingsDialog(::SceneRenderer* renderer,
                            ::ProgramOutput* program,
                            QWidget* parent = 0);

    // Applies the saved settings (at startup).
    static void restore(::SceneRenderer* renderer, ::ProgramOutput* program);

private Q_SLOTS:

    void onSdiEnabledToggled(bool on);
    void onRefreshPortsClicked();
    void onPortChosen(int index);
    void onButtonClicked(QAbstractButton* button);
    void refreshSdiStatus();

private:

    void fillPorts(const QString& current);
    void fillModes(quint32 current);
    void apply();
    static QStringList rendererOptions(int device, int threads);

    ::SceneRenderer* _renderer;
    ::ProgramOutput* _program;
    QList<Sdi::Port> _ports;

    QComboBox* _deviceCombo;  // -1: CPU only, else the OpenGL renderer's index
    QSpinBox* _threadsSpin;   // 0: automatic
    QCheckBox* _ndiCheck;
    QLabel* _ndiStatus;
    QCheckBox* _sdiCheck;
    QComboBox* _portCombo;
    QPushButton* _refreshButton;
    QComboBox* _modeCombo;    // 0: the output format's
    QComboBox* _clockCombo;   // true: the card's clock
    QLabel* _sdiStatus;
    QDialogButtonBox* _buttons;
    QTimer* _statusTimer;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_DashboardSettingsDialog_h
