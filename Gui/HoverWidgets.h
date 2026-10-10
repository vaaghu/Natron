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

#ifndef Gui_HoverWidgets_h
#define Gui_HoverWidgets_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QIcon>
#include <QLineEdit>
#include <QModelIndex>
#include <QStyledItemDelegate>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QDockWidget;
class QToolButton;

NATRON_NAMESPACE_ENTER

/**
 * @brief Item delegate showing a small action icon at the right of a cell
 * while the mouse is over it. The icon comes from the item's
 * kHoverIconRole data (a QIcon); cells without one show nothing.
 * Install with HoverIconDelegate::install(view, column).
 **/
class HoverIconDelegate
    : public QStyledItemDelegate
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    enum
    {
        kHoverIconRole = Qt::UserRole + 100
    };

    explicit HoverIconDelegate(QObject* parent = 0);

    // Sets the delegate on a column and enables hover tracking on the view.
    static HoverIconDelegate* install(QAbstractItemView* view, int column);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const OVERRIDE;

protected:

    virtual bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) OVERRIDE;

Q_SIGNALS:

    void iconClicked(const QModelIndex& index);

private:

    static QRect iconRect(const QRect& cell);
};

/**
 * @brief Line edit with an action button inside its right edge, visible only
 * while the mouse is over the field.
 **/
class HoverLineEdit
    : public QLineEdit
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit HoverLineEdit(QWidget* parent = 0);

    void setActionIcon(const QIcon& icon, const QString& tooltip);

Q_SIGNALS:

    void actionClicked();

protected:

    virtual void enterEvent(QEvent* e) OVERRIDE;
    virtual void leaveEvent(QEvent* e) OVERRIDE;
    virtual void resizeEvent(QResizeEvent* e) OVERRIDE;

private:

    QToolButton* _button;
};

/**
 * @brief Title bar for a dock panel, drawn only while the mouse is over the
 * panel; its float and close buttons show only while the mouse is over the
 * title bar itself. Keeps its height when hidden so the panel content does
 * not move. Dragging and double-clicking it work as with the default one.
 * A floated panel becomes a normal window (system title bar, taskbar entry);
 * the float button or a double-click on this title bar docks it back.
 * Install with DockTitleBar::install(dock).
 **/
class DockTitleBar
    : public QWidget
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit DockTitleBar(QDockWidget* dock);

    static DockTitleBar* install(QDockWidget* dock);

protected:

    virtual bool eventFilter(QObject* watched, QEvent* e) OVERRIDE;
    virtual void enterEvent(QEvent* e) OVERRIDE;
    virtual void leaveEvent(QEvent* e) OVERRIDE;
    virtual void paintEvent(QPaintEvent* e) OVERRIDE;

private Q_SLOTS:

    void onFloatClicked();
    void onTopLevelChanged(bool floating);

private:

    void setButtonsVisible(bool visible);
    void makeWindow();

    QDockWidget* _dock;
    QToolButton* _floatButton;
    QToolButton* _closeButton;
    bool _dockHovered;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_HoverWidgets_h
