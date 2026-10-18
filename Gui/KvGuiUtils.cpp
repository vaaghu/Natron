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
#include <QBoxLayout>
#include <QColor>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QRegularExpression>
#include <QTableView>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/Settings.h"

#include "Gui/GuiApplicationManager.h" // appPTR
#include "Gui/HoverWidgets.h"

#include "Custom/state/KvValue.h"

#define kKvIconSize 16
#define kKvTooltipPreviewWidth 200
#define kPanelMargin 6
#define kPanelSpacing 4

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
    return text.toHtmlEscaped();
}

QColor
themeColor(void (Settings::*getter)(double*, double*, double*) const)
{
    double r, g, b;

    ( appPTR->getCurrentSettings().get()->*getter )(&r, &g, &b);

    return QColor::fromRgbF( qBound(0., r, 1.), qBound(0., g, 1.), qBound(0., b, 1.) );
}

// a, t of the way to b.
QColor
mix(const QColor& a,
    const QColor& b,
    double t)
{
    return QColor::fromRgbF( a.redF() + (b.redF() - a.redF()) * t,
                             a.greenF() + (b.greenF() - a.greenF()) * t,
                             a.blueF() + (b.blueF() - a.blueF()) * t );
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

namespace KvGui {
QIcon
typeIcon(const QVariant& value)
{
    if (Kv::typeOf(value) == Kv::eTypeImage) {
        return natronIcon(NATRON_ENUM::NATRON_PIXMAP_READ_IMAGE);
    }
    double rgba[4];
    if ( (Kv::typeOf(value) == Kv::eTypeColor) && Kv::parseColor(Kv::textValue(value), rgba) ) {
        // A swatch of the color.
        const int size = appPTR->adjustSizeToDPIX(kKvIconSize);
        QPixmap swatch(size, size);
        swatch.fill( QColor::fromRgbF(rgba[0], rgba[1], rgba[2]) );

        return QIcon(swatch);
    }

    return QIcon();
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
    const QString name = Kv::typeName( Kv::typeOf(value) );

    return typeLabelForName( name.isEmpty() ? QString::fromUtf8("text") : name );
}

QStringList
typeNames()
{
    QStringList names;

    names << QString::fromUtf8("text") << QString::fromUtf8("number") << QString::fromUtf8("bool")
          << QString::fromUtf8("color") << QString::fromUtf8("image");

    return names;
}

QString
typeLabelForName(const QString& name)
{
    if ( name == QString::fromUtf8("number") ) {
        return QObject::tr("Number");
    }
    if ( name == QString::fromUtf8("bool") ) {
        return QObject::tr("On/Off");
    }
    if ( name == QString::fromUtf8("color") ) {
        return QObject::tr("Color");
    }
    if ( name == QString::fromUtf8("image") ) {
        return QObject::tr("Image");
    }

    return QObject::tr("Text");
}

QString
chooseColor(QWidget* parent,
            const QString& startHex)
{
    double rgba[4] = { 1, 1, 1, 1 };
    Kv::parseColor(startHex, rgba);
    const QColor start = QColor::fromRgbF(rgba[0], rgba[1], rgba[2], rgba[3]);
    const QColor color = QColorDialog::getColor(start, parent, QObject::tr("Choose Color"), QColorDialog::ShowAlphaChannel);
    if ( !color.isValid() ) {
        return QString();
    }
    QString hex = color.name(); // #rrggbb
    if (color.alpha() != 255) {
        hex += QString::fromUtf8("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    }

    return hex;
}

QString
tooltip(const QVariant& value)
{
    if (Kv::typeOf(value) == Kv::eTypeColor) {
        // Applied to a bound Text node's text color.
        return QObject::tr("%1<br/><i>Text nodes bound to it take it as their text color.</i>").arg( escape( Kv::textValue(value) ) );
    }
    if (Kv::typeOf(value) != Kv::eTypeImage) {
        return escape( Kv::displayText(value) );
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

QString
dashboardStyleSheet()
{
    QFile file( QString::fromUtf8(":/Resources/Stylesheets/dashboard.qss") );

    if ( !file.open(QIODevice::ReadOnly | QIODevice::Text) ) {
        return QString();
    }

    // The theme colors (Preferences > Appearance), and shades between them:
    // mixing works with black or white themes too, where darker() and
    // lighter() do nothing.
    const QColor sunken = themeColor(&Settings::getSunkenColor);
    const QColor base = themeColor(&Settings::getBaseColor);
    const QColor raised = themeColor(&Settings::getRaisedColor);
    const QColor text = themeColor(&Settings::getTextColor);
    const QColor accent = themeColor(&Settings::getSelectionColor);
    const QColor view = mix(base, sunken, 0.5);
    QHash<QString, QColor> colors;

    colors[QString::fromUtf8("window")] = base;
    colors[QString::fromUtf8("sunken")] = sunken;
    colors[QString::fromUtf8("popup")] = mix(base, raised, 0.35);
    colors[QString::fromUtf8("view")] = view;
    colors[QString::fromUtf8("viewAlt")] = mix(view, raised, 0.18);
    colors[QString::fromUtf8("viewHover")] = mix(view, raised, 0.45);
    colors[QString::fromUtf8("selection")] = mix(view, accent, 0.4);
    colors[QString::fromUtf8("input")] = mix(base, sunken, 0.6);
    colors[QString::fromUtf8("inputDisabled")] = base;
    colors[QString::fromUtf8("control")] = mix(base, raised, 0.75);
    colors[QString::fromUtf8("controlHover")] = mix(raised, text, 0.1);
    colors[QString::fromUtf8("controlPressed")] = mix(base, sunken, 0.3);
    colors[QString::fromUtf8("controlDisabled")] = base;
    colors[QString::fromUtf8("borderSoft")] = mix(base, text, 0.08);
    colors[QString::fromUtf8("border")] = mix(base, text, 0.18);
    colors[QString::fromUtf8("borderStrong")] = mix(base, text, 0.32);
    colors[QString::fromUtf8("scrollHandle")] = mix(base, text, 0.28);
    colors[QString::fromUtf8("scrollHandleHover")] = mix(base, text, 0.45);
    colors[QString::fromUtf8("text")] = text;
    colors[QString::fromUtf8("textDim")] = mix(text, base, 0.4);
    colors[QString::fromUtf8("textDisabled")] = mix(text, base, 0.62);
    colors[QString::fromUtf8("accent")] = accent;
    colors[QString::fromUtf8("accentHover")] = mix(accent, Qt::white, 0.15);
    colors[QString::fromUtf8("accentDisabled")] = mix(accent, base, 0.6);
    colors[QString::fromUtf8("accentText")] = (accent.lightnessF() > 0.45) ? QColor(20, 20, 20) : QColor(Qt::white);

    // ${name} -> #rrggbb
    const QString source = QString::fromUtf8( file.readAll() );
    const QRegularExpression token( QString::fromUtf8("\\$\\{(\\w+)\\}") );
    QString sheet;
    qsizetype from = 0;
    QRegularExpressionMatchIterator it = token.globalMatch(source);
    while ( it.hasNext() ) {
        const QRegularExpressionMatch match = it.next();
        sheet += source.mid(from, match.capturedStart() - from);
        const QString name = match.captured(1);
        sheet += colors.value(name, text).name();
        from = match.capturedEnd();
    }
    sheet += source.mid(from);

    return sheet;
}

QToolButton*
panelMenuButton(QWidget* parent,
                QMenu** menu)
{
    QToolButton* button = new QToolButton(parent);

    button->setIcon( natronIcon(NATRON_ENUM::NATRON_PIXMAP_TAB_WIDGET_LAYOUT_BUTTON) );
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setStyleSheet( QString::fromUtf8("QToolButton::menu-indicator { image: none; }") );
    *menu = new QMenu(button);
    button->setMenu(*menu);

    return button;
}

void
setPanelLayout(QBoxLayout* layout)
{
    layout->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    layout->setSpacing(kPanelSpacing);
}

void
styleTable(QTableView* table)
{
    table->setShowGrid(false);
    table->setAlternatingRowColors(true); // colors: dashboard.qss
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setItemDelegate( new PlainItemDelegate(table) );
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 10);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setStretchLastSection(true);
}

void
styleList(QListView* list)
{
    list->setAlternatingRowColors(true); // colors: dashboard.qss
    list->setItemDelegate( new PlainItemDelegate(list) );
}

QLabel*
secondaryLabel(QWidget* parent)
{
    QLabel* label = new QLabel(parent);

    label->setProperty("secondary", true); // dimmed by dashboard.qss
    label->setTextFormat(Qt::PlainText);

    return label;
}
} // namespace KvGui

NATRON_NAMESPACE_EXIT
