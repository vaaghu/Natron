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
#include <QList>
#include <QModelIndex>
#include <QStyledItemDelegate>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QAbstractItemView;
class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QPoint;
class QTableView;
class QToolButton;

NATRON_NAMESPACE_ENTER

/**
 * @brief Item delegate for the dashboard's tables and lists: no dotted focus
 * box around the current cell (rows are selected whole), and an optional
 * second line in grey (the item's kSecondaryTextRole data).
 **/
class PlainItemDelegate
    : public QStyledItemDelegate
{
public:

    enum
    {
        kSecondaryTextRole = Qt::UserRole + 101
    };

    explicit PlainItemDelegate(QObject* parent = 0);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const OVERRIDE;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const OVERRIDE;
};

/**
 * @brief Item delegate showing a small action icon at the right of a cell
 * while the mouse is over it. The icon comes from the item's
 * kHoverIconRole data (a QIcon); cells without one show nothing.
 * Install with HoverIconDelegate::install(view, column).
 **/
class HoverIconDelegate
    : public PlainItemDelegate
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

    // The dock lays its content out from these: always the bar's full height,
    // also while the buttons are hidden.
    virtual QSize sizeHint() const OVERRIDE;
    virtual QSize minimumSizeHint() const OVERRIDE;

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

/**
 * @brief Grey message in the middle of an empty list or table ("No recent
 * projects"), instead of a fake row. Mouse clicks go through to the view.
 **/
class EmptyViewHint
    : public QObject
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    static EmptyViewHint* install(QAbstractItemView* view, const QString& text);

    void setText(const QString& text);

protected:

    virtual bool eventFilter(QObject* watched, QEvent* e) OVERRIDE;

private Q_SLOTS:

    void refresh();

private:

    EmptyViewHint(QAbstractItemView* view, const QString& text);

    QAbstractItemView* _view;
    QLabel* _label;
};

/**
 * @brief Column chooser of a table: an icon at the right end of the header
 * (and a right-click on the header) opens a menu to show or hide each
 * column. The hidden columns are saved under settingsKey; until the user
 * changes them, hiddenByDefault are hidden.
 **/
class TableColumnMenu
    : public QObject
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    static TableColumnMenu* install(QTableView* table, const QString& settingsKey, const QList<int>& hiddenByDefault);

protected:

    virtual bool eventFilter(QObject* watched, QEvent* e) OVERRIDE;

private Q_SLOTS:

    void onAboutToShow();
    void onActionTriggered(QAction* action);
    void onHeaderContextMenu(const QPoint& pos);

private:

    TableColumnMenu(QTableView* table, const QString& settingsKey, const QList<int>& hiddenByDefault);

    QString columnLabel(int column) const;
    void placeButton();

    QTableView* _table;
    QString _settingsKey;
    QToolButton* _button;
    QMenu* _menu;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_HoverWidgets_h
