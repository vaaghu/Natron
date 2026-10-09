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

#include "KvGuiUtils.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QTextDocument>
#include <QUrl>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/GuiApplicationManager.h" // appPTR

#include "Custom/state/KvValue.h"

#define kKvIconSize 16
#define kKvTooltipPreviewWidth 200

NATRON_NAMESPACE_ENTER

NATRON_NAMESPACE_ANONYMOUS_ENTER

QIcon
natronIcon(NATRON_ENUM::PixmapEnum e)
{
    QPixmap pix;

    appPTR->getIcon(e, appPTR->adjustSizeToDPIX(kKvIconSize), &pix);

    return QIcon(pix);
}

QString
escape(const QString& text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    return text.toHtmlEscaped();
#else
    return Qt::escape(text);
#endif
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

namespace KvGui {
QIcon
typeIcon(const QVariant& value)
{
    return (Kv::typeOf(value) == Kv::eTypeImage) ? natronIcon(NATRON_ENUM::NATRON_PIXMAP_READ_IMAGE) : QIcon();
}

QIcon
editIcon()
{
    return natronIcon(NATRON_ENUM::NATRON_PIXMAP_PENCIL);
}

QIcon
chooseImageIcon()
{
    return natronIcon(NATRON_ENUM::NATRON_PIXMAP_OPEN_FILE);
}

QString
typeLabel(const QVariant& value)
{
    return (Kv::typeOf(value) == Kv::eTypeImage) ? QObject::tr("Image") : QObject::tr("Text");
}

QString
tooltip(const QVariant& value)
{
    if (Kv::typeOf(value) != Kv::eTypeImage) {
        return escape( Kv::textValue(value) );
    }

    const Kv::ImageInfo info = Kv::imageInfo(value);
    QString html = QString::fromUtf8("<b>%1</b><br/>").arg( escape(info.name) );
    if (info.exists) {
        if (info.width > 0) {
            html += QObject::tr("%1 x %2, %3").arg(info.width).arg(info.height).arg( info.format.toUpper() );
        } else {
            html += info.format.toUpper();
        }
        // Qt can show the preview only for formats it reads itself.
        if ( !QPixmap(info.path).isNull() ) {
            html += QString::fromUtf8("<br/><img src=\"%1\" width=\"%2\"/>")
                    .arg( escape( QUrl::fromLocalFile(info.path).toString() ) )
                    .arg(kKvTooltipPreviewWidth);
        }
        html += QObject::tr("<br/><i>Double-click to open.</i>");
    } else {
        html += QObject::tr("<font color=\"#e65a50\">File not found</font>");
    }
    html += QString::fromUtf8("<br/><small>%1</small>").arg( escape( QDir::toNativeSeparators(info.path) ) );

    return html;
}

bool
openImage(const QVariant& value)
{
    const Kv::ImageInfo info = Kv::imageInfo(value);

    if ( (Kv::typeOf(value) != Kv::eTypeImage) || !info.exists ) {
        return false;
    }

    return QDesktopServices::openUrl( QUrl::fromLocalFile(info.path) );
}

QString
chooseImageFile(QWidget* parent,
                const QString& startPath)
{
    const QString filter = QObject::tr("Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.dpx *.tga *.bmp *.webp *.psd);;All files (*)");

    return QFileDialog::getOpenFileName(parent, QObject::tr("Choose Image"),
                                        startPath.isEmpty() ? QString() : QFileInfo(startPath).absolutePath(),
                                        filter);
}
} // namespace KvGui

NATRON_NAMESPACE_EXIT
