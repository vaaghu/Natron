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
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
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

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_HoverWidgets.cpp"
