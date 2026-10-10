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

#include "HoverWidgets.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QAbstractItemView>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionDockWidget>
#include <QToolButton>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#define kHoverIconSize 16
#define kHoverIconMargin 4

NATRON_NAMESPACE_ENTER

HoverIconDelegate::HoverIconDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

HoverIconDelegate*
HoverIconDelegate::install(QAbstractItemView* view,
                           int column)
{
    HoverIconDelegate* delegate = new HoverIconDelegate(view);

    view->setItemDelegateForColumn(column, delegate);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover, true);

    return delegate;
}

QRect
HoverIconDelegate::iconRect(const QRect& cell)
{
    return QRect(cell.right() - kHoverIconSize - kHoverIconMargin,
                 cell.top() + (cell.height() - kHoverIconSize) / 2,
                 kHoverIconSize, kHoverIconSize);
}

void
HoverIconDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const QVariant iconData = index.data(kHoverIconRole);
    if ( (option.state & QStyle::State_MouseOver) && iconData.isValid() ) {
        const QIcon icon = qvariant_cast<QIcon>(iconData);
        icon.paint( painter, iconRect(option.rect) );
    }
}

bool
HoverIconDelegate::editorEvent(QEvent* event,
                               QAbstractItemModel* model,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index)
{
    const bool hasIcon = index.data(kHoverIconRole).isValid();

    if ( hasIcon && ( (event->type() == QEvent::MouseButtonPress) ||
                      (event->type() == QEvent::MouseButtonRelease) ||
                      (event->type() == QEvent::MouseButtonDblClick) ) ) {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        if ( iconRect(option.rect).contains( mouse->pos() ) ) {
            if (event->type() == QEvent::MouseButtonRelease) {
                Q_EMIT iconClicked(index);
            }

            return true; // the icon click must not start editing/selecting
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

HoverLineEdit::HoverLineEdit(QWidget* parent)
    : QLineEdit(parent)
    , _button(0)
{
    _button = new QToolButton(this);
    _button->setCursor(Qt::ArrowCursor);
    _button->setAutoRaise(true);
    _button->setFocusPolicy(Qt::NoFocus);
    _button->setIconSize( QSize(kHoverIconSize, kHoverIconSize) );
    _button->hide();
    setTextMargins(0, 0, kHoverIconSize + 2 * kHoverIconMargin, 0);

    QObject::connect( _button, SIGNAL(clicked()), this, SIGNAL(actionClicked()) );
}

void
HoverLineEdit::setActionIcon(const QIcon& icon,
                             const QString& tooltip)
{
    _button->setIcon(icon);
    _button->setToolTip(tooltip);
}

void
HoverLineEdit::enterEvent(QEvent* e)
{
    QLineEdit::enterEvent(e);
    if ( !_button->icon().isNull() ) {
        _button->show();
    }
}

void
HoverLineEdit::leaveEvent(QEvent* e)
{
    QLineEdit::leaveEvent(e);
    _button->hide();
}

void
HoverLineEdit::resizeEvent(QResizeEvent* e)
{
    QLineEdit::resizeEvent(e);

    const int size = kHoverIconSize + kHoverIconMargin;
    _button->setGeometry(width() - size - kHoverIconMargin / 2, (height() - size) / 2, size, size);
}

DockTitleBar::DockTitleBar(QDockWidget* dock)
    : QWidget(dock)
    , _dock(dock)
    , _floatButton(0)
    , _closeButton(0)
    , _dockHovered(false)
{
    _floatButton = new QToolButton(this);
    _floatButton->setIcon( style()->standardIcon(QStyle::SP_TitleBarNormalButton) );
    _floatButton->setToolTip( tr("Float") );
    _closeButton = new QToolButton(this);
    _closeButton->setIcon( style()->standardIcon(QStyle::SP_TitleBarCloseButton) );
    _closeButton->setToolTip( tr("Close") );

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch();
    QToolButton* buttons[] = { _floatButton, _closeButton };
    for (int i = 0; i < 2; ++i) {
        buttons[i]->setAutoRaise(true);
        buttons[i]->setFocusPolicy(Qt::NoFocus);
        buttons[i]->setIconSize( QSize(kHoverIconSize, kHoverIconSize) );
        layout->addWidget(buttons[i]);
    }
    // Same height whether the buttons show or not.
    setFixedHeight( qMax( _closeButton->sizeHint().height(), fontMetrics().height() + 4 ) );
    setButtonsVisible(false);

    _dock->installEventFilter(this);
    QObject::connect( _floatButton, SIGNAL(clicked()), this, SLOT(onFloatClicked()) );
    QObject::connect( _closeButton, SIGNAL(clicked()), _dock, SLOT(close()) );
    QObject::connect( _dock, SIGNAL(topLevelChanged(bool)), this, SLOT(onTopLevelChanged(bool)) );
}

DockTitleBar*
DockTitleBar::install(QDockWidget* dock)
{
    DockTitleBar* bar = new DockTitleBar(dock);

    dock->setTitleBarWidget(bar);

    return bar;
}

bool
DockTitleBar::eventFilter(QObject* watched,
                          QEvent* e)
{
    if (watched == _dock) {
        if (e->type() == QEvent::Enter) {
            _dockHovered = true;
            update();
        } else if (e->type() == QEvent::Leave) {
            _dockHovered = false;
            setButtonsVisible(false);
            update();
        } else if (e->type() == QEvent::WindowTitleChange) {
            update();
        } else if ( (e->type() == QEvent::MouseButtonRelease) && _dock->isFloating() ) {
            makeWindow(); // dragged out: now that the drag is over
        }
    }

    return QWidget::eventFilter(watched, e);
}

void
DockTitleBar::enterEvent(QEvent* e)
{
    QWidget::enterEvent(e);
    _dockHovered = true; // the dock may not have seen its Enter yet
    setButtonsVisible(true);
    update();
}

void
DockTitleBar::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    setButtonsVisible(false);
}

void
DockTitleBar::paintEvent(QPaintEvent* /*e*/)
{
    if (!_dockHovered) {
        return;
    }

    QPainter painter(this);
    QStyleOptionDockWidget option;
    option.initFrom(this);
    option.rect = rect();
    option.title = _dock->windowTitle();
    style()->drawControl(QStyle::CE_DockWidgetTitle, &option, &painter, this);
}

void
DockTitleBar::onFloatClicked()
{
    _dock->setFloating( !_dock->isFloating() );
}

void
DockTitleBar::onTopLevelChanged(bool floating)
{
    _floatButton->setToolTip( floating ? tr("Dock") : tr("Float") );
    // While dragged out, changing the window would end the drag: wait for the
    // mouse release.
    if ( floating && (QApplication::mouseButtons() == Qt::NoButton) ) {
        makeWindow();
    }
}

void
DockTitleBar::makeWindow()
{
    if ( (_dock->windowFlags() & Qt::WindowType_Mask) == Qt::Window ) {
        return; // already one
    }

    const QRect geometry = _dock->geometry();
    _dock->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                          Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    _dock->setGeometry(geometry);
    _dock->show();
}

void
DockTitleBar::setButtonsVisible(bool visible)
{
    const QDockWidget::DockWidgetFeatures features = _dock->features();

    _floatButton->setVisible( visible && (features & QDockWidget::DockWidgetFloatable) );
    _closeButton->setVisible( visible && (features & QDockWidget::DockWidgetClosable) );
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_HoverWidgets.cpp"
