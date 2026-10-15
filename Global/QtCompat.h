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
#ifndef NATRON_GLOBAL_QTCOMPAT_H
#define NATRON_GLOBAL_QTCOMPAT_H

#include "Global/Macros.h"

#include <QtCore/QtGlobal> // for Q_OS_*

#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QMutex>

#include <functional>

NATRON_NAMESPACE_ENTER

namespace QtCompat {
/*Removes the . and the extension from the filename and also
 * returns the extension as a string.*/
inline QString
removeFileExtension(QString & filename)
{
    //qDebug() << "remove file ext from" << filename;
    QFileInfo fi(filename);
    QString extension = fi.suffix();

    if ( !extension.isEmpty() ) {
        filename.truncate(filename.size() - extension.size() - 1);
    }

    //qDebug() << "->" << filename << fi.suffix();
    return extension;
}

// in Qt 4.8 QUrl is broken on mac, it returns /.file/id= for local files
// See https://bugreports.qt.io/browse/QTBUG-40449
inline QUrl
toLocalFileUrlFixed(const QUrl& url) { return url; }

/* Locks a QMutex or a QRecursiveMutex until destroyed: QMutexLocker is a
 * class template in Qt 6, so one QMutexLocker pointer cannot hold either.*/
class ScopedMutexLock
{
public:
    template <class Mutex>
    explicit ScopedMutexLock(Mutex* mutex)
        : _unlock([mutex]() { mutex->unlock(); })
    {
        mutex->lock();
    }

    ~ScopedMutexLock()
    {
        _unlock();
    }

    ScopedMutexLock(const ScopedMutexLock&) = delete;
    ScopedMutexLock& operator=(const ScopedMutexLock&) = delete;

private:
    std::function<void()> _unlock;
};

/* Converts a wildcard pattern to an anchored QRegularExpression pattern,
 * matching like QRegExp::Wildcard (or QRegExp::WildcardUnix with
 * unixEscapes, where a backslash escapes the next character):
 * '*' and '?' also match '/', unlike QRegularExpression::wildcardToRegularExpression
 * in Qt 5, and "[...]"/"[!...]" sets are kept.*/
inline QString
wildcardToRegularExpression(const QString & wildcard,
                            bool unixEscapes = false)
{
    QString rx;
    const int n = wildcard.size();

    for (int i = 0; i < n; ++i) {
        const QChar c = wildcard.at(i);
        if ( unixEscapes && ( c == QLatin1Char('\\') ) && (i + 1 < n) ) {
            ++i;
            rx += QRegularExpression::escape( QString( wildcard.at(i) ) );
        } else if ( c == QLatin1Char('*') ) {
            rx += QLatin1String(".*");
        } else if ( c == QLatin1Char('?') ) {
            rx += QLatin1Char('.');
        } else if ( c == QLatin1Char('[') ) {
            int j = i + 1;
            if ( (j < n) && ( wildcard.at(j) == QLatin1Char('!') ) ) {
                ++j;
            }
            if ( (j < n) && ( wildcard.at(j) == QLatin1Char(']') ) ) {
                ++j;
            }
            while ( (j < n) && ( wildcard.at(j) != QLatin1Char(']') ) ) {
                ++j;
            }
            if (j >= n) {
                rx += QLatin1String("\\[");
            } else {
                QString set = wildcard.mid(i + 1, j - i - 1);
                if ( set.startsWith( QLatin1Char('!') ) ) {
                    set[0] = QLatin1Char('^');
                }
                set.replace( QLatin1String("\\"), QLatin1String("\\\\") );
                set.replace( QLatin1String("["), QLatin1String("\\[") );
                rx += QLatin1Char('[') + set + QLatin1Char(']');
                i = j;
            }
        } else {
            rx += QRegularExpression::escape( QString(c) );
        }
    }

    return QRegularExpression::anchoredPattern(rx);
}

/* Equivalent of QRegExp(wildcard, cs, QRegExp::Wildcard[Unix]) for exact matches.*/
inline QRegularExpression
wildcardRegularExpression(const QString & wildcard,
                          Qt::CaseSensitivity cs,
                          bool unixEscapes = false)
{
    return QRegularExpression( wildcardToRegularExpression(wildcard, unixEscapes),
                               cs == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption );
}

} // namespace QtCompat

NATRON_NAMESPACE_EXIT

#endif // NATRON_GLOBAL_QTCOMPAT_H
