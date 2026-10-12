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

#include "NotificationCenter.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QCoreApplication>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#define kNotificationsKept 100 // older ones are dropped

NATRON_NAMESPACE_ENTER

NotificationTarget
NotificationTarget::settings(const QString& field)
{
    NotificationTarget target;

    target.kind = eSettings;
    target.field = field;

    return target;
}

NotificationTarget
NotificationTarget::sceneItem(const QString& sceneId,
                              const QString& item)
{
    NotificationTarget target;

    target.kind = eSceneItem;
    target.sceneId = sceneId;
    target.item = item;

    return target;
}

NotificationCenter::NotificationCenter(QObject* parent)
    : QObject(parent)
    , _nextId(1)
{
}

NotificationCenter*
NotificationCenter::instance()
{
    // Deleted with the application.
    static NotificationCenter* center = new NotificationCenter( QCoreApplication::instance() );

    return center;
}

int
NotificationCenter::add(const QString& title,
                        const QString& text,
                        bool warning,
                        const NotificationTarget& target)
{
    Notification n;

    n.id = _nextId++;
    n.time = QDateTime::currentDateTime();
    n.title = title;
    n.text = text;
    n.warning = warning;
    n.target = target;
    _notifications.prepend(n);
    while (_notifications.size() > kNotificationsKept) {
        _notifications.removeLast();
    }
    Q_EMIT added(n.id);
    Q_EMIT changed();

    return n.id;
}

QList<Notification>
NotificationCenter::notifications() const
{
    return _notifications;
}

bool
NotificationCenter::notification(int id,
                                 Notification* out) const
{
    for (int i = 0; i < _notifications.size(); ++i) {
        if (_notifications.at(i).id == id) {
            *out = _notifications.at(i);

            return true;
        }
    }

    return false;
}

int
NotificationCenter::unreadCount() const
{
    int count = 0;

    for (int i = 0; i < _notifications.size(); ++i) {
        if (!_notifications.at(i).read) {
            ++count;
        }
    }

    return count;
}

void
NotificationCenter::markRead(int id)
{
    for (int i = 0; i < _notifications.size(); ++i) {
        if ( (_notifications.at(i).id == id) && !_notifications.at(i).read ) {
            _notifications[i].read = true;
            Q_EMIT changed();
        }
    }
}

void
NotificationCenter::markAllRead()
{
    bool any = false;

    for (int i = 0; i < _notifications.size(); ++i) {
        any = any || !_notifications.at(i).read;
        _notifications[i].read = true;
    }
    if (any) {
        Q_EMIT changed();
    }
}

void
NotificationCenter::clear()
{
    if ( _notifications.isEmpty() ) {
        return;
    }
    _notifications.clear();
    Q_EMIT changed();
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_NotificationCenter.cpp"
