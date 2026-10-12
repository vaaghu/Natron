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

#include "SingleInstance.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QCryptographicHash>
#include <QDir>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#define kSingleInstanceTimeoutMs 1000

NATRON_NAMESPACE_ENTER

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
    , _server(0)
{
    // Per user (home folder), and a plain name (a socket file on Unix, a
    // pipe on Windows).
    const QByteArray user = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Md5).toHex().left(12);

    _name = QString::fromUtf8(NATRON_APPLICATION_NAME "-dashboard-") + QString::fromLatin1( user.constData() );
}

bool
SingleInstance::acquire()
{
    // Another instance answers: ask it to come to the front.
    {
        QLocalSocket socket;
        socket.connectToServer(_name);
        if ( socket.waitForConnected(kSingleInstanceTimeoutMs) ) {
            socket.write("activate\n");
            socket.waitForBytesWritten(kSingleInstanceTimeoutMs);
            socket.disconnectFromServer();

            return false;
        }
    }

    // None: answer from now on (a socket left by a crashed instance is
    // removed first).
    _server = new QLocalServer(this);
    QObject::connect( _server, SIGNAL(newConnection()), this, SLOT(onNewConnection()) );
    if ( !_server->listen(_name) ) {
        QLocalServer::removeServer(_name);
        _server->listen(_name); // still failing: run without the check
    }

    return true;
}

void
SingleInstance::onNewConnection()
{
    while ( QLocalSocket* socket = _server->nextPendingConnection() ) {
        QObject::connect( socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()) );
        Q_EMIT activationRequested();
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_SingleInstance.cpp"
