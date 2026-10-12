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

#ifndef Gui_NotificationCenter_h
#define Gui_NotificationCenter_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

NATRON_NAMESPACE_ENTER

// Where a notification leads when clicked.
struct NotificationTarget
{
    enum Kind
    {
        eNone,
        eSettings,  // a field of the dashboard's Settings (field: see DashboardSettingsDialog::focusField())
        eSceneItem  // an input box: a Write node of a scene (scene id, item)
    };

    Kind kind;
    QString field;   // eSettings
    QString sceneId; // eSceneItem
    QString item;

    NotificationTarget()
        : kind(eNone)
    {
    }

    static NotificationTarget settings(const QString& field);
    static NotificationTarget sceneItem(const QString& sceneId, const QString& item);
};

struct Notification
{
    int id;
    QDateTime time;
    QString title;
    QString text;
    bool warning; // something failed or was changed for the user
    bool read;
    NotificationTarget target;

    Notification()
        : id(0)
        , warning(false)
        , read(false)
    {
    }
};

/**
 * @brief The dashboard's notifications (the bell in its menu bar), for this
 * run: renders done or failed, and settings the application changed by
 * itself (e.g. render threads lowered to what this machine has). One per
 * application, so they can be added before the dashboard exists.
 **/
class NotificationCenter
    : public QObject
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    static NotificationCenter* instance();

    // Returns the new notification's id.
    int add(const QString& title, const QString& text, bool warning, const NotificationTarget& target);

    QList<Notification> notifications() const; // newest first
    bool notification(int id, Notification* out) const;
    int unreadCount() const;
    void markRead(int id);
    void markAllRead();
    void clear();

Q_SIGNALS:

    void added(int id);
    void changed(); // added, read or cleared

private:

    explicit NotificationCenter(QObject* parent);

    QList<Notification> _notifications; // newest first
    int _nextId;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_NotificationCenter_h
