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

// Timeline: cue point diamonds (half size) and how close a click picks one.
#define kTimelineMarkerSize 5
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

void
VideoMonitor::paintEvent(QPaintEvent* /*e*/)
{
    QPainter painter(this);
    const int border = _border.isValid() ? 3 : 0;
    const QRect area = rect().adjusted(border, border, -border, -border);

    painter.fillRect( rect(), _border.isValid() ? _border : QColor(Qt::black) );
    painter.fillRect( area, QColor(Qt::black) );

    if ( !_image.isNull() ) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QSize size = _image.size();
        size.scale(area.size(), Qt::KeepAspectRatio); // QSize::scaled() is Qt 5 only
        painter.drawImage( QRect(area.x() + (area.width() - size.width()) / 2, area.y() + (area.height() - size.height()) / 2,
                                 size.width(), size.height() ), _image );
    } else if ( !_placeholder.isEmpty() ) {
        painter.setPen( QColor(140, 140, 140) );
        painter.drawText(area.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap, _placeholder);
    }

    if ( !_badge.isEmpty() ) {
        QFont font = painter.font();
        if (font.pointSizeF() > 0) {
            font.setPointSizeF(font.pointSizeF() * 0.85);
            painter.setFont(font);
        }
        const QRect text = painter.fontMetrics().boundingRect(_badge).adjusted(-4, -1, 4, 1);
        const QRect box(area.left() + 4, area.bottom() - text.height() - 3, text.width(), text.height());
        painter.fillRect( box, QColor(0, 0, 0, 170) );
        painter.setPen( QColor(220, 220, 220) );
        painter.drawText(box, Qt::AlignCenter, _badge);
    }
}

// ----- BusTimeline -----

BusTimeline::BusTimeline(QWidget* parent)
    : QWidget(parent)
    , _duration(0)
    , _fps(25)
    , _position(0)
    , _markers()
    , _scrubbing(false)
    , _dragMarker(-1)
    , _dragFrom(0)
    , _dragMoved(false)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void
BusTimeline::setRange(double duration,
                      double fps)
{
    if ( (duration != _duration) || (fps != _fps) ) {
        _duration = qMax(0.0, duration);
        _fps = (fps > 0) ? fps : 25;
        update();
    }
}

void
BusTimeline::setPosition(double seconds)
{
    if ( !_scrubbing && (seconds != _position) ) {
        _position = seconds;
        update();
    }
}

void
BusTimeline::setMarkers(const QList<double>& seconds)
{
    if ( (_dragMarker < 0) && (seconds != _markers) ) {
        _markers = seconds;
        update();
    }
}

double
BusTimeline::duration() const
{
    return _duration;
}

double
BusTimeline::fps() const
{
    return _fps;
}

double
BusTimeline::position() const
{
    return _position;
}

const QList<double>&
BusTimeline::markers() const
{
    return _markers;
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
    return QSize( 400, fontMetrics().height() + 26 );
}

QSize
BusTimeline::minimumSizeHint() const
{
    return QSize( 120, fontMetrics().height() + 26 );
}

QRect
BusTimeline::track() const
{
    // Margins so the playhead and the diamonds at the ends are whole.
    return rect().adjusted(8, 0, -8, 0);
}

double
BusTimeline::secondsAt(int x) const
{
    const QRect t = track();

    if ( (_duration <= 0) || (t.width() <= 0) ) {
        return 0;
    }
    const double seconds = qBound( 0.0, double(x - t.left()) / t.width() * _duration, _duration );

    return std::floor(seconds * _fps + 0.5) / _fps;
}

int
BusTimeline::xAt(double seconds) const
{
    const QRect t = track();

    return (_duration <= 0) ? t.left() : t.left() + int(seconds / _duration * t.width() + 0.5);
}

int
BusTimeline::markerAt(const QPoint& pos) const
{
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
    const int rulerBottom = height() - 1;
    const QColor tickColor = pal.color(QPalette::Text).darker(150);

    painter.fillRect( rect(), pal.color(QPalette::Base).darker(120) );
    if (_duration <= 0) {
        painter.setPen(tickColor);
        painter.drawText( rect(), Qt::AlignCenter, tr("No layers playing") );

        return;
    }

    // Played part.
    painter.fillRect( QRect( t.left(), 0, xAt(_position) - t.left(), height() ), pal.color(QPalette::Highlight).darker(250) );

    // Ruler: frame ticks every few pixels, numbered ones far enough apart
    // (in frames, as the editor's timeline).
    const double frames = _duration * _fps;
    const double pixelsPerFrame = t.width() / qMax(1.0, frames);
    static const int steps[] = { 1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000 };
    const int stepCount = int( sizeof(steps) / sizeof(steps[0]) );
    int minor = steps[stepCount - 1];
    int major = steps[stepCount - 1];
    for (int i = stepCount - 1; i >= 0; --i) {
        if (steps[i] * pixelsPerFrame >= 6) {
            minor = steps[i];
        }
        if ( steps[i] * pixelsPerFrame >= painter.fontMetrics().width( QString::number( int(frames) ) ) + 16 ) {
            major = steps[i];
        }
    }
    painter.setPen(tickColor);
    for (int frame = 0; frame <= int(frames); frame += minor) {
        const int x = xAt(frame / _fps);
        const bool numbered = (frame % major) == 0;
        painter.drawLine(x, rulerBottom, x, rulerBottom - (numbered ? 8 : 4) );
        if (numbered) {
            painter.drawText(x + 2, rulerBottom - 9, QString::number(frame) );
        }
    }

    // Cue points: keyframe diamonds.
    painter.setRenderHint(QPainter::Antialiasing);
    const int middle = height() / 2 + 2;
    for (int i = 0; i < _markers.size(); ++i) {
        const int x = xAt( _markers.at(i) );
        QPolygon diamond;
        diamond << QPoint(x, middle - kTimelineMarkerSize) << QPoint(x + kTimelineMarkerSize, middle)
                << QPoint(x, middle + kTimelineMarkerSize) << QPoint(x - kTimelineMarkerSize, middle);
        painter.setPen( QColor(20, 20, 20) );
        painter.setBrush( (i == _dragMarker) ? QColor(255, 255, 255) : QColor(120, 200, 255) );
        painter.drawPolygon(diamond);
    }
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Playhead with its frame.
    const QColor playhead(255, 150, 40);
    const int x = xAt(_position);
    painter.setPen(playhead);
    painter.drawLine( x, 0, x, height() );
    QPolygon handle;
    handle << QPoint(x - 5, 0) << QPoint(x + 5, 0) << QPoint(x, 6);
    painter.setBrush(playhead);
    painter.drawPolygon(handle);
    const QString label = QString::number( int(_position * _fps + 0.5) );
    const int labelWidth = painter.fontMetrics().width(label) + 6;
    const QRect box( qMin(x + 4, width() - labelWidth), 1, labelWidth, painter.fontMetrics().height() );
    painter.fillRect(box, playhead);
    painter.setPen(Qt::black);
    painter.drawText(box, Qt::AlignCenter, label);
}

void
BusTimeline::mousePressEvent(QMouseEvent* e)
{
    if ( (e->button() != Qt::LeftButton) || (_duration <= 0) ) {
        QWidget::mousePressEvent(e);

        return;
    }
    _dragMarker = markerAt( e->pos() );
    if (_dragMarker >= 0) {
        _dragFrom = _markers.at(_dragMarker);
        _dragMoved = false;
        update();

        return;
    }
    _scrubbing = true;
    scrubTo( e->pos().x() );
}

void
BusTimeline::mouseMoveEvent(QMouseEvent* e)
{
    if (_dragMarker >= 0) {
        const double seconds = secondsAt( e->pos().x() );
        if (seconds != _markers.at(_dragMarker) ) {
            _markers[_dragMarker] = seconds;
            _dragMoved = true;
            update();
        }
    } else if (_scrubbing) {
        scrubTo( e->pos().x() );
    } else if (_duration > 0) {
        setCursor( (markerAt( e->pos() ) >= 0) ? Qt::SizeHorCursor : Qt::ArrowCursor );
    }
}

void
BusTimeline::mouseReleaseEvent(QMouseEvent* e)
{
    if (_dragMarker >= 0) {
        const double to = _markers.at(_dragMarker);
        const bool moved = _dragMoved && (to != _dragFrom);
        _dragMarker = -1;
        update();
        if (moved) {
            Q_EMIT markerMoved(_dragFrom, to);
        } else {
            // A click on a cue point goes there.
            _position = _dragFrom;
            Q_EMIT seekRequested(_dragFrom);
        }
    } else if (_scrubbing) {
        _scrubbing = false;
    }
    QWidget::mouseReleaseEvent(e);
}

void
BusTimeline::mouseDoubleClickEvent(QMouseEvent* e)
{
    if ( (e->button() != Qt::LeftButton) || (_duration <= 0) ) {
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
    if (_duration <= 0) {
        return;
    }
    const int marker = markerAt( e->pos() );
    const double here = secondsAt( e->pos().x() );
    QMenu menu(this);
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
    if (chosen == add) {
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

    for (int i = 0; i < 2; ++i) {
        _rows[i] = new QHBoxLayout;
        _rows[i]->setContentsMargins(4, 0, 4, 0);
        _rows[i]->setSpacing(2);
        layout->addLayout(_rows[i]);
    }

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
InputTile::controlRow(int row) const
{
    return _rows[ qBound(0, row, 1) ];
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
