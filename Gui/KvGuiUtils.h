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

#ifndef Gui_KvGuiUtils_h
#define Gui_KvGuiUtils_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QIcon>
#include <QString>
#include <QVariant>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QBoxLayout;
class QLabel;
class QListView;
class QMenu;
class QTableView;
class QToolButton;
class QWidget;

NATRON_NAMESPACE_ENTER

// GUI helpers for the typed state store values (Custom/state/KvValue.h),
// shared by the dashboard, the scene panel and the State tab.
namespace KvGui
{
// Icons: type icon (image values only), hover actions.
QIcon typeIcon(const QVariant& value);
QIcon editIcon();
QIcon chooseImageIcon();

// "Text" / "Image"
QString typeLabel(const QVariant& value);

// Rich tooltip: the text, or the image details with a small preview.
QString tooltip(const QVariant& value);

// Opens an image value in the system viewer. Returns false if not an
// existing image.
bool openImage(const QVariant& value);

// File dialog for an image; empty if cancelled.
QString chooseImageFile(QWidget* parent, const QString& startPath);

// Small icon button opening a menu of a panel's actions (like the editor
// panes' corner button). Add the actions to *menu.
QToolButton* panelMenuButton(QWidget* parent, QMenu** menu);

// Dashboard look, shared by every panel: margins and spacing of a panel's
// layout; tables and lists without grid or focus box, with alternating
// rows and left-aligned headers; grey secondary text.
void setPanelLayout(QBoxLayout* layout);
void styleTable(QTableView* table);
void styleList(QListView* list);
QLabel* secondaryLabel(QWidget* parent);
}

NATRON_NAMESPACE_EXIT

#endif // Gui_KvGuiUtils_h
