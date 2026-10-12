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

#include "Custom/scene/SceneStore.h" // LayerPlacement

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
    // Picture shape (height for width), e.g. the output format.
    void setAspect(int width, int height);
    // Shows the picture's frame, as the canvas of OBS: grey around it, a
    // thin outline on its edge, the image fitted inside.
    void setCanvasFrame(bool shown);

    virtual QSize sizeHint() const OVERRIDE;
    virtual QSize minimumSizeHint() const OVERRIDE;
    virtual int heightForWidth(int width) const OVERRIDE;

protected:

    virtual void paintEvent(QPaintEvent* e) OVERRIDE;
    // Where the picture goes: the widget, less the border, at the aspect ratio.
    QRect pictureRect() const;

private:

    QImage _image;
    QString _placeholder;
    QString _badge;
    QColor _border;
    int _aspectWidth;
    int _aspectHeight;
    bool _canvasFrame;
};

/**
 * @brief The Preview monitor: also shows the layers' outlines and lets the
 * user place them (in the output format set with setCanvas()):
 *   click            selects the top layer under the mouse
 *   drag             moves it (snaps to the centre lines)
 *   drag a corner    scales it, aspect ratio kept
 *   arrow keys       nudge it by a pixel of the output (Shift: 10)
 *   right-click      centre it, or reset it (fitted, centred)
 **/
class LayerMonitor
    : public VideoMonitor
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    struct Box
    {
        QString key;     // the layer
        QString label;
        QSize size;      // its frame, to fit it (invalid: the output's)
        LayerPlacement placement;
    };

    explicit LayerMonitor(QWidget* parent = 0);

    void setCanvas(int width, int height); // also the aspect ratio
    // Bottom layer first. Ignored while the user drags one.
    void setBoxes(const QList<Box>& boxes);
    void setSelected(const QString& key);
    QString selected() const;

Q_SIGNALS:

    void layerSelected(const QString& key);
    // While dragging (live), then once done (drag released, nudge, menu).
    void placementChanging(const QString& key, const LayerPlacement& placement);
    void placementChanged(const QString& key, const LayerPlacement& placement);

protected:

    virtual void paintEvent(QPaintEvent* e) OVERRIDE;
    virtual void mousePressEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent* e) OVERRIDE;
    virtual void keyPressEvent(QKeyEvent* e) OVERRIDE;
    virtual void contextMenuEvent(QContextMenuEvent* e) OVERRIDE;

private:

    enum DragEnum
    {
        eDragNone,
        eDragMove,
        eDragScale
    };

    int indexOf(const QString& key) const;
    QRectF boxRect(const Box& box) const; // in widget coordinates
    int boxAt(const QPoint& pos) const;   // top-most, -1: none
    bool onHandle(const QPoint& pos) const; // a corner of the selected layer
    void place(int index, const LayerPlacement& placement, bool done);

    int _canvasWidth;
    int _canvasHeight;
    QList<Box> _boxes;
    QString _selected;
    DragEnum _drag;
    QPoint _pressPos;
    LayerPlacement _pressPlacement;
};

/**
 * @brief Timeline of a switcher bus, as in video editors: a time ruler on
 * top, then one track per layer (the top layer first) with a block from the
 * layer's start to its end, and the playhead across them.
 *   click / drag the ruler or an empty track    moves the playhead (seekRequested)
 *   drag a block                                moves its layer's start (editable only);
 *                                               snaps to 0, the playhead and the other blocks
 *   ticks on the ruler                          cue points: drag to move,
 *                                               double-click to add or delete
 *   right-click                                 cue points, or a block's start
 **/
class BusTimeline
    : public QWidget
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    struct Track
    {
        QString key;     // the layer
        QString label;
        double start;    // seconds on the bus
        double duration; // its render (0: unknown, e.g. NDI output off)
    };

    explicit BusTimeline(QWidget* parent = 0);

    // Bottom layer first. Ignored while a block is dragged.
    void setTracks(const QList<Track>& tracks);
    void setFps(double fps);  // snapping
    // Ignored while the user drags the playhead.
    void setPosition(double seconds);
    void setMarkers(const QList<double>& seconds);
    void setSelected(const QString& key);
    QString selected() const;
    // Blocks can be dragged (Preview), or not (Program).
    void setEditable(bool editable);

    double position() const;
    double fps() const;
    // Nearest cue point before / after the playhead (-1: none).
    double previousMarker() const;
    double nextMarker() const;

    virtual QSize sizeHint() const OVERRIDE;
    virtual QSize minimumSizeHint() const OVERRIDE;

Q_SIGNALS:

    void seekRequested(double seconds);
    void trackSelected(const QString& key);
    // While dragging a block (live), then once released.
    void startChanging(const QString& key, double seconds);
    void startChanged(const QString& key, double seconds);
    void markerAdded(double seconds);
    void markerMoved(double from, double to);
    void markerRemoved(double seconds);

protected:

    virtual void paintEvent(QPaintEvent* e) OVERRIDE;
    virtual void mousePressEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent* e) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* e) OVERRIDE;
    virtual void contextMenuEvent(QContextMenuEvent* e) OVERRIDE;

private:

    enum DragEnum
    {
        eDragNone,
        eDragScrub,
        eDragMarker,
        eDragBlock
    };

    int rulerHeight() const;
    int rowHeight() const;
    QRect track() const;              // horizontal extent of the time axis
    double secondsAt(int x) const;    // snapped to a frame
    int xAt(double seconds) const;
    double snapped(double seconds) const; // to a frame
    QRect blockRect(int index) const; // index in _tracks
    int blockAt(const QPoint& pos) const;
    int markerAt(const QPoint& pos) const; // index, -1: none (on the ruler only)
    void scrubTo(int x);
    void updateViewLength();

    QList<Track> _tracks;
    double _fps;
    double _position;
    double _viewLength; // seconds shown
    QList<double> _markers;
    QString _selected;
    bool _editable;
    DragEnum _drag;
    int _dragIndex;     // marker or block being dragged
    double _dragFrom;   // its time before the drag
    int _pressX;
    bool _dragMoved;
};

/**
 * @brief One input (Write node of the scene) in the dashboard's input grid,
 * like the input boxes of vision mixers:
 *   [number  title / subtitle      tally]   header, colored by tally
 *   [picture                            ]   live frame or last render
 *   [status line          progress      ]
 *   [control row filled by the owner    ]
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
    // Row of controls under the status line, for the owner's buttons.
    QHBoxLayout* controlRow() const;

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
    QHBoxLayout* _controls;
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
