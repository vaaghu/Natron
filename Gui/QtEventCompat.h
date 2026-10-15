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
#ifndef NATRON_GUI_QTEVENTCOMPAT_H
#define NATRON_GUI_QTEVENTCOMPAT_H

#include "Global/Macros.h"

#include <QtCore/QtGlobal>
#include <QtCore/QPoint>
#include <QtCore/QPointF>
#include <QtGui/QMouseEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QTabletEvent>

NATRON_NAMESPACE_ENTER

/* Event positions: Qt 6 deprecates pos(), x(), y(), localPos(), globalPos()...
 * in favor of position() and globalPosition(), which do not exist in Qt 5.15.
 * The QPoint versions round like the deprecated accessors did.*/
namespace QtCompat {

inline QPointF
eventPosF(const QMouseEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

inline QPointF
eventGlobalPosF(const QMouseEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition();
#else
    return e->screenPos();
#endif
}

inline QPointF
eventPosF(const QDropEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->posF();
#endif
}

inline QPointF
eventPosF(const QTabletEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->posF();
#endif
}

inline QPointF
eventGlobalPosF(const QTabletEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition();
#else
    return e->globalPosF();
#endif
}

template <typename Event>
inline QPoint
eventPos(const Event* e)
{
    return eventPosF(e).toPoint();
}

template <typename Event>
inline QPoint
eventGlobalPos(const Event* e)
{
    return eventGlobalPosF(e).toPoint();
}

} // namespace QtCompat

NATRON_NAMESPACE_EXIT

#endif // NATRON_GUI_QTEVENTCOMPAT_H
