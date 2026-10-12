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

#ifndef Gui_SingleInstance_h
#define Gui_SingleInstance_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QObject>
#include <QString>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QLocalServer;

NATRON_NAMESPACE_ENTER

/**
 * @brief One GUI instance of the application per user at a time (the
 * dashboard, its NDI / SDI outputs and its HTTP port are not to be shared).
 * The first instance listens on a local socket named after the user; a
 * later one finds it, asks it to come to the front, and does not start.
 * Background renders (NatronRenderer, -b) are not concerned.
 **/
class SingleInstance
    : public QObject
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit SingleInstance(QObject* parent = 0);

    // True if this is the only instance: it then answers later launches.
    // False if another instance runs (it was asked to come to the front).
    bool acquire();

Q_SIGNALS:

    // A later launch wants this instance in front.
    void activationRequested();

private Q_SLOTS:

    void onNewConnection();

private:

    QString _name;
    QLocalServer* _server;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_SingleInstance_h
