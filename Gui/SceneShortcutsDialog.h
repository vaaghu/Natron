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

#ifndef Gui_SceneShortcutsDialog_h
#define Gui_SceneShortcutsDialog_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QAbstractButton;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class SceneStore; // Custom/scene/SceneStore.h

NATRON_NAMESPACE_ENTER

/**
 * @brief The dashboard's scene shortcuts (Settings menu), kept between runs:
 * a key combination per scene puts all its inputs on Program at once
 * (ScenePanel::cutToScene()), wherever the focus is in the dashboard.
 * By default the first nine scenes of the list get Ctrl+1 to Ctrl+9 (see
 * Custom/scene/SceneShortcuts). A combination used twice, or by another dashboard action, cannot be
 * applied (it would do neither).
 **/
class SceneShortcutsDialog
    : public QDialog
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    // taken: the dashboard's other shortcuts (QKeySequence::PortableText ->
    // what they do).
    SceneShortcutsDialog(::SceneStore* scenes,
                         const QHash<QString, QString>& taken,
                         QWidget* parent = 0);

Q_SIGNALS:

    // The shortcuts were saved (OK or Apply).
    void applied();

private Q_SLOTS:

    void validate();
    void onClearClicked();
    void onDefaultsClicked();
    void onButtonClicked(QAbstractButton* button);

private:

    QLineEdit* recorder(int row) const;
    void apply();

    ::SceneStore* _scenes;
    QHash<QString, QString> _taken;
    QStringList _sceneIds; // per row
    QTableWidget* _table;
    QPushButton* _clearButton;
    QPushButton* _defaultsButton;
    QLabel* _status;
    QDialogButtonBox* _buttons;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_SceneShortcutsDialog_h
