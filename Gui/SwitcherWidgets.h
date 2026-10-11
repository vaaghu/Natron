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

#ifndef Gui_SwitcherWidgets_h
#define Gui_SwitcherWidgets_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QColor>
#include <QFrame>
#include <QImage>
#include <QList>
#include <QString>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QHBoxLayout;
class QLabel;
class QProgressBar;

NATRON_NAMESPACE_ENTER

// Tally colors, as in vision mixers: Preview orange, Program green.
namespace Switcher {
QColor previewColor();
QColor programColor();
QColor idleColor();   // input boxes neither on Preview nor on Program
QColor blackColor();  // Fade to Black active
}

/**
 * @brief A video picture: an image fitted in black (letterboxed), or a
 * placeholder text when there is none, with an optional colored border
 * (tally).
 **/
class VideoMonitor
    : public QWidget
{
public:

    explicit VideoMonitor(QWidget* parent = 0);

    void setImage(const QImage& image);
    // Shown when there is no image.
    void setPlaceholder(const QString& text);
    // Small text in the bottom-left corner (e.g. "NDI off"); empty: none.
    void setBadge(const QString& text);
    // Invalid color: no border.
    void setBorderColor(const QColor& color);

    virtual QSize sizeHint() const OVERRIDE;
    virtual QSize minimumSizeHint() const OVERRIDE;
    virtual int heightForWidth(int width) const OVERRIDE;

protected:

    virtual void paintEvent(QPaintEvent* e) OVERRIDE;

private:

    QImage _image;
    QString _placeholder;
    QString _badge;
    QColor _border;
};

/**
 * @brief One input (Write node of the scene) in the dashboard's input grid,
 * like the input boxes of vision mixers:
 *   [number  title / subtitle      tally]   header, colored by tally
 *   [picture                            ]   live frame or last render
 *   [status line          progress      ]
 *   [control rows filled by the owner   ]
 * Clicking the box (outside its buttons) emits clicked(); the owner selects
 * it and puts it on Preview.
 **/
class InputTile
    : public QFrame
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    enum TallyEnum
    {
        eTallyNone,
        eTallyPreview,
        eTallyProgram
    };

    InputTile(const QString& item, int number, QWidget* parent = 0);

    QString item() const;

    // Title (project) and subtitle (Write node); problem: shown in red.
    void setTitle(const QString& title, const QString& subtitle, bool problem);
    void setTally(TallyEnum tally);
    TallyEnum tally() const;
    void setSelected(bool selected);
    bool isSelected() const;

    VideoMonitor* monitor() const;
    void setStatus(const QString& text, const QString& tooltip, bool problem);
    QProgressBar* progressBar() const;
    // Row of controls under the status line (0 or 1), for the owner's buttons.
    QHBoxLayout* controlRow(int row) const;

Q_SIGNALS:

    void clicked(const QString& item, Qt::KeyboardModifiers modifiers);
    void doubleClicked(const QString& item);
    void contextMenuRequested(const QString& item, const QPoint& globalPos);

protected:

    virtual void mousePressEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* e) OVERRIDE;
    virtual void contextMenuEvent(QContextMenuEvent* e) OVERRIDE;

private:

    void updateStyle();

    QString _item;
    TallyEnum _tally;
    bool _selected;
    QWidget* _header;
    QLabel* _number;
    QLabel* _title;
    QLabel* _tallyLabel;
    VideoMonitor* _monitor;
    QLabel* _status;
    QProgressBar* _progress;
    QHBoxLayout* _rows[2];
};

/**
 * @brief Lays out boxes of the same size in rows filling its width (put it
 * in a resizable QScrollArea: its minimum height follows the rows).
 **/
class TileGrid
    : public QWidget
{
public:

    explicit TileGrid(QWidget* parent = 0);

    // Replaces the boxes (the old ones are deleted; the new ones are reparented).
    void setTiles(const QList<QWidget*>& tiles);
    const QList<QWidget*>& tiles() const;

protected:

    virtual void resizeEvent(QResizeEvent* e) OVERRIDE;

private:

    void relayout();

    QList<QWidget*> _tiles;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_SwitcherWidgets_h
