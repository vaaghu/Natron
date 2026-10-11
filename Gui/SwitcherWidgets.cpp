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
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QResizeEvent>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

// Input boxes: picture width (the box adds its margins), 16:9.
#define kTileMonitorWidth 224
#define kTileSpacing 6
#define kTileBorder 2

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

QSize
VideoMonitor::sizeHint() const
{
    return QSize(480, 270);
}

QSize
VideoMonitor::minimumSizeHint() const
{
    return QSize(160, 90);
}

int
VideoMonitor::heightForWidth(int width) const
{
    return width * 9 / 16;
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
