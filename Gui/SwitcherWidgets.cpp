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

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "SwitcherWidgets.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QApplication>
#include <QContextMenuEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPolygon>
#include <QProgressBar>
#include <QResizeEvent>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include <cmath>

// Input boxes: picture width (the box adds its margins), 16:9.
#define kTileMonitorWidth 224
#define kTileSpacing 6
#define kTileBorder 2

// Timeline: how close a click picks a cue point.
#define kTimelinePickDistance 7

NATRON_NAMESPACE_ENTER

namespace Switcher {
QColor
previewColor()
{
    return QColor(232, 150, 40);
}

QColor
programColor()
{
    return QColor(52, 168, 83);
}

QColor
idleColor()
{
    return QColor(44, 72, 110);
}

QColor
blackColor()
{
    return QColor(210, 50, 45);
}
} // namespace Switcher

NATRON_NAMESPACE_ANONYMOUS_ENTER

QString
rgb(const QColor& color)
{
    return QString::fromUtf8("rgb(%1, %2, %3)").arg( color.red() ).arg( color.green() ).arg( color.blue() );
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

// ----- VideoMonitor -----

VideoMonitor::VideoMonitor(QWidget* parent)
    : QWidget(parent)
    , _image()
    , _placeholder()
    , _badge()
    , _border()
    , _aspectWidth(16)
    , _aspectHeight(9)
    , _canvasFrame(false)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void
VideoMonitor::setImage(const QImage& image)
{
    _image = image;
    update();
}

void
VideoMonitor::setPlaceholder(const QString& text)
{
    if (text != _placeholder) {
        _placeholder = text;
        update();
    }
}

void
VideoMonitor::setBadge(const QString& text)
{
    if (text != _badge) {
        _badge = text;
        update();
    }
}

void
VideoMonitor::setBorderColor(const QColor& color)
{
    if (color != _border) {
        _border = color;
        update();
    }
}

void
VideoMonitor::setAspect(int width,
                        int height)
{
    if ( (width > 0) && (height > 0) && ( (width != _aspectWidth) || (height != _aspectHeight) ) ) {
        _aspectWidth = width;
        _aspectHeight = height;
        updateGeometry();
        update();
    }
}

void
VideoMonitor::setCanvasFrame(bool shown)
{
    if (shown != _canvasFrame) {
        _canvasFrame = shown;
        update();
    }
}

QSize
VideoMonitor::sizeHint() const
{
    return QSize( 480, heightForWidth(480) );
}

QSize
VideoMonitor::minimumSizeHint() const
{
    return QSize(160, 90);
}

int
VideoMonitor::heightForWidth(int width) const
{
    return width * _aspectHeight / _aspectWidth;
}

QRect
VideoMonitor::pictureRect() const
{
    const int border = _border.isValid() ? 3 : 0;
    const QRect area = rect().adjusted(border, border, -border, -border);
    QSize size(_aspectWidth, _aspectHeight);

    size.scale(area.size(), Qt::KeepAspectRatio); // QSize::scaled() is Qt 5 only

    return QRect( area.x() + (area.width() - size.width()) / 2, area.y() + (area.height() - size.height()) / 2, size.width(), size.height() );
}

void
VideoMonitor::paintEvent(QPaintEvent* /*e*/)
{
    QPainter painter(this);
    const int border = _border.isValid() ? 3 : 0;
    const QRect area = rect().adjusted(border, border, -border, -border);

    painter.fillRect( rect(), _border.isValid() ? _border : QColor(Qt::black) );
    // Canvas frame: the picture in black on grey, so its edges show.
    const QRect picture = _canvasFrame ? pictureRect() : area;
    painter.fillRect( area, _canvasFrame ? QColor(58, 58, 58) : QColor(Qt::black) );
    painter.fillRect( picture, QColor(Qt::black) );

    if ( !_image.isNull() ) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize size = _image.size();
        size.scale(picture.size(), Qt::KeepAspectRatio); // QSize::scaled() is Qt 5 only
        painter.drawImage( QRect(picture.x() + (picture.width() - size.width()) / 2, picture.y() + (picture.height() - size.height()) / 2,
                                 size.width(), size.height() ), _image );
    }
    if (_canvasFrame) {
        painter.setPen( QColor(150, 150, 150) );
        painter.setBrush(Qt::NoBrush);
        painter.drawRect( picture.adjusted(0, 0, -1, -1) );
    }
    if ( _image.isNull() && !_placeholder.isEmpty() ) {
        painter.setPen( QColor(140, 140, 140) );
        painter.drawText(picture.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap, _placeholder);
    }

    if ( !_badge.isEmpty() ) {
        QFont font = painter.font();
        if (font.pointSizeF() > 0) {
            font.setPointSizeF(font.pointSizeF() * 0.85);
            painter.setFont(font);
        }
        const QRect text = painter.fontMetrics().boundingRect(_badge).adjusted(-4, -1, 4, 1);
        const QRect box(picture.left() + 4, picture.bottom() - text.height() - 3, text.width(), text.height());
        painter.fillRect( box, QColor(0, 0, 0, 170) );
        painter.setPen( QColor(220, 220, 220) );
        painter.drawText(box, Qt::AlignCenter, _badge);
    }
}

// ----- LayerMonitor -----

// Corner handles: half size, and how far from the centre lines a drag snaps.
#define kLayerHandleSize 4
#define kLayerSnapPixels 6

LayerMonitor::LayerMonitor(QWidget* parent)
    : VideoMonitor(parent)
    , _canvasWidth(1920)
    , _canvasHeight(1080)
    , _boxes()
    , _selected()
    , _drag(eDragNone)
    , _pressPos()
    , _pressPlacement()
{
    setFocusPolicy(Qt::ClickFocus); // arrow keys
    setMouseTracking(true);
}

void
LayerMonitor::setCanvas(int width,
                        int height)
{
    if ( (width > 0) && (height > 0) ) {
        _canvasWidth = width;
        _canvasHeight = height;
        setAspect(width, height);
    }
}

void
LayerMonitor::setBoxes(const QList<Box>& boxes)
{
    if (_drag != eDragNone) {
        return;
    }
    _boxes = boxes;
    if (indexOf(_selected) < 0) {
        _selected.clear();
    }
    update();
}

void
LayerMonitor::setSelected(const QString& key)
{
    if (key != _selected) {
        _selected = key;
        update();
    }
}

QString
LayerMonitor::selected() const
{
    return _selected;
}

int
LayerMonitor::indexOf(const QString& key) const
{
    for (int i = 0; i < _boxes.size(); ++i) {
        if (_boxes.at(i).key == key) {
            return i;
        }
    }

    return -1;
}

QRectF
LayerMonitor::boxRect(const Box& box) const
{
    const QRect picture = pictureRect();
    const double toWidget = double( picture.width() ) / _canvasWidth;
    QSize fitted = box.size.isValid() ? box.size : QSize(_canvasWidth, _canvasHeight);

    fitted.scale(_canvasWidth, _canvasHeight, Qt::KeepAspectRatio);
    const double w = fitted.width() * box.placement.scale * toWidget;
    const double h = fitted.height() * box.placement.scale * toWidget;

    return QRectF(picture.x() + box.placement.x * picture.width() - w / 2,
                  picture.y() + box.placement.y * picture.height() - h / 2, w, h);
}

int
LayerMonitor::boxAt(const QPoint& pos) const
{
    for (int i = _boxes.size() - 1; i >= 0; --i) {
        if ( boxRect( _boxes.at(i) ).contains(pos) ) {
            return i;
        }
    }

    return -1;
}

bool
LayerMonitor::onHandle(const QPoint& pos) const
{
    const int index = indexOf(_selected);

    if (index < 0) {
        return false;
    }
    const QRectF r = boxRect( _boxes.at(index) );
    const QPointF corners[] = { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() };
    for (int i = 0; i < 4; ++i) {
        if ( (qAbs(corners[i].x() - pos.x()) <= kLayerHandleSize + 2) && (qAbs(corners[i].y() - pos.y()) <= kLayerHandleSize + 2) ) {
            return true;
        }
    }

    return false;
}

void
LayerMonitor::place(int index,
                    const LayerPlacement& placement,
                    bool done)
{
    if ( (index < 0) || (index >= _boxes.size()) ) {
        return;
    }
    _boxes[index].placement = placement;
    update();
    if (done) {
        Q_EMIT placementChanged(_boxes.at(index).key, placement);
    } else {
        Q_EMIT placementChanging(_boxes.at(index).key, placement);
    }
}

void
LayerMonitor::paintEvent(QPaintEvent* e)
{
    VideoMonitor::paintEvent(e);
    if ( _boxes.isEmpty() ) {
        return;
    }

    QPainter painter(this);
    painter.setClipRect( rect() );
    // Every layer faintly; the selected one with its handles and name.
    for (int i = 0; i < _boxes.size(); ++i) {
        if (_boxes.at(i).key == _selected) {
            continue;
        }
        painter.setPen( QPen(QColor(255, 255, 255, 70), 1, Qt::DashLine) );
        painter.setBrush(Qt::NoBrush);
        painter.drawRect( boxRect( _boxes.at(i) ) );
    }
    const int index = indexOf(_selected);
    if (index < 0) {
        return;
    }
    const QColor accent = Switcher::previewColor();
    const QRectF r = boxRect( _boxes.at(index) );
    painter.setPen( QPen(accent, 1) );
    painter.drawRect(r);
    const QPointF corners[] = { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() };
    painter.setBrush(accent);
    for (int i = 0; i < 4; ++i) {
        painter.drawRect( QRectF(corners[i].x() - kLayerHandleSize, corners[i].y() - kLayerHandleSize, 2 * kLayerHandleSize, 2 * kLayerHandleSize) );
    }
    const QString label = _boxes.at(index).label;
    const QRect text = painter.fontMetrics().boundingRect(label).adjusted(-4, -1, 4, 1);
    const QRect tag( int( r.left() ), int( r.top() ) - text.height() - 1, text.width(), text.height() );
    painter.fillRect(tag.translated(0, (tag.top() < 0) ? text.height() + 2 : 0), accent);
    painter.setPen(Qt::black);
    painter.drawText(tag.translated(0, (tag.top() < 0) ? text.height() + 2 : 0), Qt::AlignCenter, label);

    // Centre lines while moving a layer onto them.
    if (_drag == eDragMove) {
        const QRect picture = pictureRect();
        painter.setPen( QPen(QColor(255, 255, 255, 120), 1, Qt::DotLine) );
        if (_boxes.at(index).placement.x == 0.5) {
            painter.drawLine( picture.center().x(), picture.top(), picture.center().x(), picture.bottom() );
        }
        if (_boxes.at(index).placement.y == 0.5) {
            painter.drawLine( picture.left(), picture.center().y(), picture.right(), picture.center().y() );
        }
    }
}

void
LayerMonitor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        VideoMonitor::mousePressEvent(e);

        return;
    }
    if ( onHandle( e->pos() ) ) {
        _drag = eDragScale;
    } else {
        const int index = boxAt( e->pos() );
        if (index < 0) {
            return;
        }
        if (_boxes.at(index).key != _selected) {
            _selected = _boxes.at(index).key;
            Q_EMIT layerSelected(_selected);
        }
        _drag = eDragMove;
    }
    _pressPos = e->pos();
    _pressPlacement = _boxes.at( indexOf(_selected) ).placement;
    update();
}

void
LayerMonitor::mouseMoveEvent(QMouseEvent* e)
{
    const int index = indexOf(_selected);

    if ( (_drag == eDragNone) || (index < 0) ) {
        setCursor( onHandle( e->pos() ) ? Qt::SizeFDiagCursor : (boxAt( e->pos() ) >= 0) ? Qt::SizeAllCursor : Qt::ArrowCursor );

        return;
    }

    const QRect picture = pictureRect();
    if ( picture.isEmpty() ) {
        return;
    }
    LayerPlacement placement = _pressPlacement;
    if (_drag == eDragMove) {
        placement.x += double( e->pos().x() - _pressPos.x() ) / picture.width();
        placement.y += double( e->pos().y() - _pressPos.y() ) / picture.height();
        // Snap to the centre lines.
        if (qAbs(placement.x - 0.5) * picture.width() < kLayerSnapPixels) {
            placement.x = 0.5;
        }
        if (qAbs(placement.y - 0.5) * picture.height() < kLayerSnapPixels) {
            placement.y = 0.5;
        }
    } else {
        // Scale by how far the mouse is from the layer's centre.
        const QPointF centre( picture.x() + _pressPlacement.x * picture.width(), picture.y() + _pressPlacement.y * picture.height() );
        const double from = std::sqrt( std::pow(_pressPos.x() - centre.x(), 2) + std::pow(_pressPos.y() - centre.y(), 2) );
        const double to = std::sqrt( std::pow(e->pos().x() - centre.x(), 2) + std::pow(e->pos().y() - centre.y(), 2) );
        if (from > 1) {
            placement.scale = qBound(0.02, _pressPlacement.scale * to / from, 20.0);
        }
    }
    place(index, placement, false);
}

void
LayerMonitor::mouseReleaseEvent(QMouseEvent* e)
{
    const int index = indexOf(_selected);

    if ( (_drag != eDragNone) && (index >= 0) ) {
        _drag = eDragNone;
        if (_boxes.at(index).placement != _pressPlacement) {
            place(index, _boxes.at(index).placement, true);
        }
        update();
    }
    _drag = eDragNone;
    VideoMonitor::mouseReleaseEvent(e);
}

void
LayerMonitor::keyPressEvent(QKeyEvent* e)
{
    const int index = indexOf(_selected);
    const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1; // pixels of the output
    int dx = 0, dy = 0;

    switch ( e->key() ) {
    case Qt::Key_Left: dx = -step; break;
    case Qt::Key_Right: dx = step; break;
    case Qt::Key_Up: dy = -step; break;
    case Qt::Key_Down: dy = step; break;
    default:
        VideoMonitor::keyPressEvent(e);

        return;
    }
    if ( (index < 0) || (_drag != eDragNone) ) {
        return;
    }
    LayerPlacement placement = _boxes.at(index).placement;
    placement.x += double(dx) / _canvasWidth;
    placement.y += double(dy) / _canvasHeight;
    place(index, placement, true);
}

void
LayerMonitor::contextMenuEvent(QContextMenuEvent* e)
{
    const int index = boxAt( e->pos() );

    if (index < 0) {
        return;
    }
    if (_boxes.at(index).key != _selected) {
        _selected = _boxes.at(index).key;
        Q_EMIT layerSelected(_selected);
        update();
    }
    QMenu menu(this);
    QAction* centre = menu.addAction( tr("Centre") );
    QAction* reset = menu.addAction( tr("Reset Position and Size") );
    QAction* chosen = menu.exec( e->globalPos() );
    if (chosen == centre) {
        LayerPlacement placement = _boxes.at(index).placement;
        placement.x = 0.5;
        placement.y = 0.5;
        place(index, placement, true);
    } else if (chosen == reset) {
        LayerPlacement placement; // fitted and centred, starting when it did
        placement.start = _boxes.at(index).placement.start;
        place(index, placement, true);
    }
}

// ----- BusTimeline -----

// Timeline: rows of the tracks, and how many show before the rows get thinner.
#define kTimelineRowHeight 20
#define kTimelineMaxRows 6
#define kTimelineSnapPixels 6

BusTimeline::BusTimeline(QWidget* parent)
    : QWidget(parent)
    , _tracks()
    , _fps(25)
    , _position(0)
    , _viewLength(10)
    , _markers()
    , _selected()
    , _editable(true)
    , _drag(eDragNone)
    , _dragIndex(-1)
    , _dragFrom(0)
    , _pressX(0)
    , _dragMoved(false)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight( sizeHint().height() );
}

int
BusTimeline::rulerHeight() const
{
    return fontMetrics().height() + 8;
}

int
BusTimeline::rowHeight() const
{
    return kTimelineRowHeight;
}

void
BusTimeline::setTracks(const QList<Track>& tracks)
{
    if (_drag == eDragBlock) {
        return;
    }
    const bool rowsChanged = tracks.size() != _tracks.size();
    _tracks = tracks;
    bool found = false;
    for (int i = 0; i < _tracks.size() && !found; ++i) {
        found = _tracks.at(i).key == _selected;
    }
    if (!found) {
        _selected.clear();
    }
    updateViewLength();
    if (rowsChanged) {
        setFixedHeight( sizeHint().height() );
    }
    update();
}

void
BusTimeline::updateViewLength()
{
    // The bus with room after it (to move blocks later), 10 s at least.
    double end = 0;
    for (int i = 0; i < _tracks.size(); ++i) {
        end = qMax(end, _tracks.at(i).start + _tracks.at(i).duration);
    }
    _viewLength = qMax(10.0, std::ceil(end * 1.25));
}

void
BusTimeline::setFps(double fps)
{
    _fps = (fps > 0) ? fps : 25;
}

void
BusTimeline::setPosition(double seconds)
{
    if ( (_drag != eDragScrub) && (seconds != _position) ) {
        _position = seconds;
        update();
    }
}

void
BusTimeline::setMarkers(const QList<double>& seconds)
{
    if ( (_drag != eDragMarker) && (seconds != _markers) ) {
        _markers = seconds;
        update();
    }
}

void
BusTimeline::setSelected(const QString& key)
{
    if (key != _selected) {
        _selected = key;
        update();
    }
}

QString
BusTimeline::selected() const
{
    return _selected;
}

void
BusTimeline::setEditable(bool editable)
{
    _editable = editable;
}

double
BusTimeline::position() const
{
    return _position;
}

double
BusTimeline::fps() const
{
    return _fps;
}

double
BusTimeline::previousMarker() const
{
    double found = -1;
    const double frame = 0.5 / _fps;

    for (int i = 0; i < _markers.size(); ++i) {
        if (_markers.at(i) < _position - frame) {
            found = qMax(found, _markers.at(i));
        }
    }

    return found;
}

double
BusTimeline::nextMarker() const
{
    double found = -1;
    const double frame = 0.5 / _fps;

    for (int i = 0; i < _markers.size(); ++i) {
        if ( (_markers.at(i) > _position + frame) && ( (found < 0) || (_markers.at(i) < found) ) ) {
            found = _markers.at(i);
        }
    }

    return found;
}

QSize
BusTimeline::sizeHint() const
{
    const int rows = qBound(1, _tracks.size(), kTimelineMaxRows);

    return QSize(400, rulerHeight() + rows * rowHeight() + 2);
}

QSize
BusTimeline::minimumSizeHint() const
{
    return QSize( 120, sizeHint().height() );
}

QRect
BusTimeline::track() const
{
    // Margins so the playhead at the ends is whole.
    return rect().adjusted(6, 0, -6, 0);
}

double
BusTimeline::snapped(double seconds) const
{
    return std::floor(seconds * _fps + 0.5) / _fps;
}

double
BusTimeline::secondsAt(int x) const
{
    const QRect t = track();

    if (t.width() <= 0) {
        return 0;
    }

    return snapped( qBound( 0.0, double(x - t.left()) / t.width() * _viewLength, _viewLength ) );
}

int
BusTimeline::xAt(double seconds) const
{
    const QRect t = track();

    return t.left() + int(seconds / _viewLength * t.width() + 0.5);
}

QRect
BusTimeline::blockRect(int index) const
{
    // The top layer in the first row; rows get thinner past kTimelineMaxRows.
    const int rows = qMax(1, _tracks.size());
    const int available = height() - rulerHeight() - 2;
    const int row = qMax(4, qMin(rowHeight(), available / rows));
    const int y = rulerHeight() + 1 + (_tracks.size() - 1 - index) * row;
    const Track& t = _tracks.at(index);
    const int left = xAt(t.start);
    const int right = (t.duration > 0) ? xAt(t.start + t.duration) : left + 24;

    return QRect( left, y + 1, qMax(4, right - left), row - 2 );
}

int
BusTimeline::blockAt(const QPoint& pos) const
{
    for (int i = 0; i < _tracks.size(); ++i) {
        if ( blockRect(i).contains(pos) ) {
            return i;
        }
    }

    return -1;
}

int
BusTimeline::markerAt(const QPoint& pos) const
{
    if ( pos.y() > rulerHeight() ) {
        return -1;
    }
    int found = -1;
    int best = kTimelinePickDistance + 1;
    for (int i = 0; i < _markers.size(); ++i) {
        const int distance = qAbs( xAt( _markers.at(i) ) - pos.x() );
        if (distance < best) {
            best = distance;
            found = i;
        }
    }

    return found;
}

void
BusTimeline::scrubTo(int x)
{
    const double seconds = secondsAt(x);

    if (seconds != _position) {
        _position = seconds;
        update();
        Q_EMIT seekRequested(seconds);
    }
}

void
BusTimeline::paintEvent(QPaintEvent* /*e*/)
{
    QPainter painter(this);
    const QPalette pal = palette();
    const QRect t = track();
    const int ruler = rulerHeight();
    const QColor text = pal.color(QPalette::Text);
    const QColor tickColor = text.darker(160);

    painter.fillRect( rect(), pal.color(QPalette::Base).darker(125) );
    painter.fillRect( QRect(0, 0, width(), ruler), pal.color(QPalette::Window).darker(115) );

    // Ruler: ticks every few pixels, labelled ones far enough apart.
    const double pixelsPerSecond = t.width() / _viewLength;
    static const double steps[] = { 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    const int stepCount = int( sizeof(steps) / sizeof(steps[0]) );
    double minor = steps[stepCount - 1];
    double major = steps[stepCount - 1];
    const int labelWidth = painter.fontMetrics().width( QString::fromUtf8("00:00") ) + 12;
    for (int i = stepCount - 1; i >= 0; --i) {
        if (steps[i] * pixelsPerSecond >= 8) {
            minor = steps[i];
        }
        if (steps[i] * pixelsPerSecond >= labelWidth) {
            major = steps[i];
        }
    }
    painter.setPen(tickColor);
    for (double second = 0; second <= _viewLength + 0.001; second += minor) {
        const int x = xAt(second);
        const bool labelled = std::fabs( std::fmod(second + 0.001, major) ) < 0.01;
        painter.drawLine(x, ruler - 1, x, ruler - (labelled ? 7 : 3) );
        if (labelled) {
            const int s = int(second + 0.5);
            painter.drawText( x + 3, ruler - 7, QString::fromUtf8("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')) );
        }
    }

    // Track rows, the blocks with their layer's name.
    for (int i = 0; i < _tracks.size(); ++i) {
        const QRect block = blockRect(i);
        const QRect row(0, block.top() - 1, width(), block.height() + 2);
        if ( (_tracks.size() - 1 - i) % 2 ) {
            painter.fillRect( row, pal.color(QPalette::Base).darker(115) );
        }
        const Track& layer = _tracks.at(i);
        const bool selected = (layer.key == _selected);
        QColor fill = (layer.duration > 0) ? QColor(31, 122, 106) : QColor(90, 90, 90);
        if (selected) {
            fill = fill.lighter(135);
        }
        painter.fillRect(block, fill);
        painter.setPen( selected ? Switcher::previewColor() : fill.darker(150) );
        painter.drawRect( block.adjusted(0, 0, -1, -1) );
        painter.setPen(Qt::white);
        painter.drawText( block.adjusted(5, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          painter.fontMetrics().elidedText( (layer.duration > 0) ? layer.label : layer.label + tr(" (not playing)"),
                                                            Qt::ElideRight, qMax(0, block.width() - 8) ) );
    }

    // Cue points: ticks on the ruler.
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < _markers.size(); ++i) {
        const int x = xAt( _markers.at(i) );
        painter.setBrush( (_drag == eDragMarker && i == _dragIndex) ? QColor(255, 255, 255) : QColor(255, 200, 60) );
        painter.drawRect(x - 1, 2, 3, ruler - 4);
    }

    // Playhead across the ruler and the tracks.
    const QColor playhead(255, 150, 40);
    const int x = xAt(_position);
    painter.setPen(playhead);
    painter.drawLine( x, 0, x, height() );
    QPolygon handle;
    handle << QPoint(x - 5, 0) << QPoint(x + 5, 0) << QPoint(x, 6);
    painter.setBrush(playhead);
    painter.drawPolygon(handle);
}

void
BusTimeline::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);

        return;
    }
    _pressX = e->pos().x();
    _dragMoved = false;

    const int marker = markerAt( e->pos() );
    if (marker >= 0) {
        _drag = eDragMarker;
        _dragIndex = marker;
        _dragFrom = _markers.at(marker);
        update();

        return;
    }
    const int block = ( e->pos().y() > rulerHeight() ) ? blockAt( e->pos() ) : -1;
    if (block >= 0) {
        if (_tracks.at(block).key != _selected) {
            _selected = _tracks.at(block).key;
            Q_EMIT trackSelected(_selected);
            update();
        }
        if (_editable) {
            _drag = eDragBlock;
            _dragIndex = block;
            _dragFrom = _tracks.at(block).start;
        }

        return;
    }
    _drag = eDragScrub;
    scrubTo( e->pos().x() );
}

void
BusTimeline::mouseMoveEvent(QMouseEvent* e)
{
    const QRect t = track();
    const double delta = (t.width() > 0) ? double(e->pos().x() - _pressX) / t.width() * _viewLength : 0;

    switch (_drag) {
    case eDragScrub:
        scrubTo( e->pos().x() );
        break;
    case eDragMarker: {
        const double seconds = secondsAt( e->pos().x() );
        if (seconds != _markers.at(_dragIndex) ) {
            _markers[_dragIndex] = seconds;
            _dragMoved = true;
            update();
        }
        break;
    }
    case eDragBlock: {
        // Snap the start or the end to 0, the playhead and the other blocks.
        const double length = _tracks.at(_dragIndex).duration;
        double start = qMax( 0.0, snapped(_dragFrom + delta) );
        QList<double> targets;
        targets << 0 << _position;
        for (int i = 0; i < _tracks.size(); ++i) {
            if (i != _dragIndex) {
                targets << _tracks.at(i).start << _tracks.at(i).start + _tracks.at(i).duration;
            }
        }
        const double tolerance = kTimelineSnapPixels * _viewLength / qMax(1, t.width());
        for (int i = 0; i < targets.size(); ++i) {
            if (std::fabs(start - targets.at(i)) < tolerance) {
                start = targets.at(i);
                break;
            }
            if ( (length > 0) && (std::fabs(start + length - targets.at(i)) < tolerance) && (targets.at(i) - length >= 0) ) {
                start = targets.at(i) - length;
                break;
            }
        }
        if (start != _tracks.at(_dragIndex).start) {
            _tracks[_dragIndex].start = start;
            _dragMoved = true;
            update();
            Q_EMIT startChanging(_tracks.at(_dragIndex).key, start);
        }
        break;
    }
    default:
        if (markerAt( e->pos() ) >= 0) {
            setCursor(Qt::SizeHorCursor);
        } else if ( _editable && (e->pos().y() > rulerHeight()) && (blockAt( e->pos() ) >= 0) ) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        break;
    }
}

void
BusTimeline::mouseReleaseEvent(QMouseEvent* e)
{
    const DragEnum drag = _drag;
    const int index = _dragIndex;

    _drag = eDragNone;
    _dragIndex = -1;
    if ( (drag == eDragMarker) && (index >= 0) ) {
        const double to = _markers.at(index);
        if ( _dragMoved && (to != _dragFrom) ) {
            Q_EMIT markerMoved(_dragFrom, to);
        } else {
            // A click on a cue point goes there.
            _position = _dragFrom;
            Q_EMIT seekRequested(_dragFrom);
        }
    } else if ( (drag == eDragBlock) && (index >= 0) && (index < _tracks.size()) ) {
        if ( _dragMoved && (_tracks.at(index).start != _dragFrom) ) {
            Q_EMIT startChanged(_tracks.at(index).key, _tracks.at(index).start);
        }
        updateViewLength();
    }
    update();
    QWidget::mouseReleaseEvent(e);
}

void
BusTimeline::mouseDoubleClickEvent(QMouseEvent* e)
{
    if ( (e->button() != Qt::LeftButton) || (e->pos().y() > rulerHeight()) ) {
        return;
    }
    const int marker = markerAt( e->pos() );
    if (marker >= 0) {
        Q_EMIT markerRemoved( _markers.at(marker) );
    } else {
        Q_EMIT markerAdded( secondsAt( e->pos().x() ) );
    }
}

void
BusTimeline::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    const int block = ( e->pos().y() > rulerHeight() ) ? blockAt( e->pos() ) : -1;

    if (block >= 0) {
        // A block: its start.
        if (_tracks.at(block).key != _selected) {
            _selected = _tracks.at(block).key;
            Q_EMIT trackSelected(_selected);
            update();
        }
        if (!_editable) {
            return;
        }
        QAction* atPlayhead = menu.addAction( tr("Start at Playhead") );
        QAction* atZero = menu.addAction( tr("Start at 0:00") );
        QAction* chosen = menu.exec( e->globalPos() );
        if ( (chosen != atPlayhead) && (chosen != atZero) ) {
            return;
        }
        const double start = (chosen == atPlayhead) ? snapped(_position) : 0.0;
        if (start != _tracks.at(block).start) {
            _tracks[block].start = start;
            updateViewLength();
            update();
            Q_EMIT startChanged(_tracks.at(block).key, start);
        }

        return;
    }

    // The ruler: cue points.
    const int marker = markerAt( e->pos() );
    const double here = secondsAt( e->pos().x() );
    QAction* previous = menu.addAction( tr("Go to Previous Cue Point") );
    QAction* next = menu.addAction( tr("Go to Next Cue Point") );
    previous->setEnabled(previousMarker() >= 0);
    next->setEnabled(nextMarker() >= 0);
    menu.addSeparator();
    QAction* add = menu.addAction( tr("Add Cue Point Here") );
    QAction* addAtPlayhead = menu.addAction( tr("Add Cue Point at Playhead") );
    QAction* remove = 0;
    if (marker >= 0) {
        remove = menu.addAction( tr("Delete Cue Point") );
    }
    menu.addSeparator();
    QAction* removeAll = menu.addAction( tr("Delete All Cue Points") );
    removeAll->setEnabled( !_markers.isEmpty() );

    QAction* chosen = menu.exec( e->globalPos() );
    if ( chosen && ( (chosen == previous) || (chosen == next) ) ) {
        const double to = (chosen == previous) ? previousMarker() : nextMarker();
        _position = to;
        update();
        Q_EMIT seekRequested(to);
    } else if (chosen == add) {
        Q_EMIT markerAdded(here);
    } else if (chosen == addAtPlayhead) {
        Q_EMIT markerAdded(_position);
    } else if ( remove && (chosen == remove) ) {
        Q_EMIT markerRemoved( _markers.at(marker) );
    } else if (chosen == removeAll) {
        const QList<double> all = _markers;
        for (int i = 0; i < all.size(); ++i) {
            Q_EMIT markerRemoved( all.at(i) );
        }
    }
}

// ----- InputTile -----

InputTile::InputTile(const QString& item,
                     int number,
                     QWidget* parent)
    : QFrame(parent)
    , _item(item)
    , _tally(eTallyNone)
    , _selected(false)
    , _header(0)
    , _number(0)
    , _title(0)
    , _tallyLabel(0)
    , _monitor(0)
    , _status(0)
    , _progress(0)
    , _controls(0)
{
    setObjectName( QString::fromUtf8("inputTile") );
    setFocusPolicy(Qt::ClickFocus); // the panel's shortcuts (Delete...) then apply
    // All boxes the same width, whatever their titles (see TileGrid).
    setFixedWidth(kTileMonitorWidth + 2 * kTileBorder + 8);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kTileBorder, kTileBorder, kTileBorder, kTileBorder + 2);
    layout->setSpacing(3);

    // [number] [title]                 [tally]
    _header = new QWidget(this);
    _header->setObjectName( QString::fromUtf8("inputTileHeader") );
    _header->setAttribute(Qt::WA_StyledBackground);
    QHBoxLayout* header = new QHBoxLayout(_header);
    header->setContentsMargins(5, 2, 5, 2);
    header->setSpacing(6);
    _number = new QLabel(QString::number(number), _header);
    QFont bold = _number->font();
    bold.setBold(true);
    _number->setFont(bold);
    _title = new QLabel(_header);
    _title->setTextFormat(Qt::PlainText);
    _title->setMinimumWidth(1); // long names are cut, not widening the box
    _tallyLabel = new QLabel(_header);
    _tallyLabel->setFont(bold);
    header->addWidget(_number);
    header->addWidget(_title, 1);
    header->addWidget(_tallyLabel);
    layout->addWidget(_header);

    _monitor = new VideoMonitor(this);
    _monitor->setFixedSize(kTileMonitorWidth, kTileMonitorWidth * 9 / 16);
    layout->addWidget(_monitor, 0, Qt::AlignHCenter);

    QHBoxLayout* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(4, 0, 4, 0);
    _status = new QLabel(this);
    _status->setTextFormat(Qt::PlainText);
    _status->setMinimumWidth(1);
    _progress = new QProgressBar(this);
    _progress->setRange(0, 100);
    _progress->setAlignment(Qt::AlignCenter);
    _progress->setFixedWidth(64);
    _progress->setFixedHeight( _status->fontMetrics().height() + 2 );
    statusRow->addWidget(_status, 1);
    statusRow->addWidget(_progress);
    layout->addLayout(statusRow);

    _controls = new QHBoxLayout;
    _controls->setContentsMargins(4, 0, 4, 0);
    _controls->setSpacing(2);
    layout->addLayout(_controls);

    updateStyle();
}

QString
InputTile::item() const
{
    return _item;
}

void
InputTile::setTitle(const QString& title,
                    const QString& subtitle,
                    bool problem)
{
    _title->setText( subtitle.isEmpty() ? title : QString::fromUtf8("%1  \xC2\xB7  %2").arg(title).arg(subtitle) ); // middle dot
    _title->setStyleSheet( problem ? QString::fromUtf8("QLabel { color: rgb(255, 160, 150); }") : QString() );
}

void
InputTile::setTally(TallyEnum tally)
{
    if (tally != _tally) {
        _tally = tally;
        updateStyle();
    }
}

InputTile::TallyEnum
InputTile::tally() const
{
    return _tally;
}

void
InputTile::setSelected(bool selected)
{
    if (selected != _selected) {
        _selected = selected;
        updateStyle();
    }
}

bool
InputTile::isSelected() const
{
    return _selected;
}

VideoMonitor*
InputTile::monitor() const
{
    return _monitor;
}

void
InputTile::setStatus(const QString& text,
                     const QString& tooltip,
                     bool problem)
{
    _status->setText(text);
    _status->setToolTip(tooltip);
    _status->setStyleSheet( problem ? QString::fromUtf8("QLabel { color: rgb(230, 90, 80); }") : QString() );
}

QProgressBar*
InputTile::progressBar() const
{
    return _progress;
}

QHBoxLayout*
InputTile::controlRow() const
{
    return _controls;
}

void
InputTile::updateStyle()
{
    const QColor header = (_tally == eTallyProgram) ? Switcher::programColor()
                          : (_tally == eTallyPreview) ? Switcher::previewColor() : Switcher::idleColor();
    const QColor border = _selected ? QApplication::palette().color(QPalette::Highlight) : QApplication::palette().color(QPalette::Window).darker(130);
    const QColor background = QApplication::palette().color(QPalette::Window).darker(115);

    setStyleSheet( QString::fromUtf8("QFrame#inputTile { background: %1; border: %2px solid %3; border-radius: 3px; }"
                                     "QWidget#inputTileHeader { background: %4; border-radius: 2px; }"
                                     "QWidget#inputTileHeader QLabel { color: white; }")
                   .arg( rgb(background) ).arg(kTileBorder).arg( rgb(border) ).arg( rgb(header) ) );
    _tallyLabel->setText( (_tally == eTallyProgram) ? tr("PGM") : (_tally == eTallyPreview) ? tr("PRV") : QString() );
    _monitor->setBorderColor( (_tally == eTallyProgram) ? Switcher::programColor()
                              : (_tally == eTallyPreview) ? Switcher::previewColor() : QColor() );
}

void
InputTile::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        Q_EMIT clicked(_item, e->modifiers());
    }
    QFrame::mousePressEvent(e);
}

void
InputTile::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        Q_EMIT doubleClicked(_item);
    }
}

void
InputTile::contextMenuEvent(QContextMenuEvent* e)
{
    Q_EMIT contextMenuRequested( _item, e->globalPos() );
}

// ----- TileGrid -----

TileGrid::TileGrid(QWidget* parent)
    : QWidget(parent)
    , _tiles()
{
}

void
TileGrid::setTiles(const QList<QWidget*>& tiles)
{
    for (int i = 0; i < _tiles.size(); ++i) {
        _tiles.at(i)->hide();
        _tiles.at(i)->deleteLater(); // may be the sender of the signal that rebuilt the grid
    }
    _tiles = tiles;
    for (int i = 0; i < _tiles.size(); ++i) {
        _tiles.at(i)->setParent(this);
        _tiles.at(i)->show();
    }
    relayout();
}

const QList<QWidget*>&
TileGrid::tiles() const
{
    return _tiles;
}

void
TileGrid::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    relayout();
}

void
TileGrid::relayout()
{
    if ( _tiles.isEmpty() ) {
        setMinimumHeight(0);

        return;
    }

    const QSize tile = _tiles.front()->sizeHint().expandedTo( _tiles.front()->minimumSize() ).boundedTo( _tiles.front()->maximumSize() );
    const int columns = qMax( 1, (width() - kTileSpacing) / (tile.width() + kTileSpacing) );
    const int rows = (_tiles.size() + columns - 1) / columns;

    for (int i = 0; i < _tiles.size(); ++i) {
        const int column = i % columns;
        const int row = i / columns;
        _tiles.at(i)->setGeometry( kTileSpacing + column * (tile.width() + kTileSpacing),
                                   kTileSpacing + row * (tile.height() + kTileSpacing),
                                   tile.width(), tile.height() );
    }
    // The scroll area scrolls once the rows are taller than it.
    setMinimumHeight( kTileSpacing + rows * (tile.height() + kTileSpacing) );
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_SwitcherWidgets.cpp"
