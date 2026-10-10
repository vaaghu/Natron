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
#include <QBrush>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QStyle>
#include <QStyleOptionDockWidget>
#include <QTableView>
#include <QToolButton>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#define kHoverIconSize 16
#define kHoverIconMargin 4

NATRON_NAMESPACE_ENTER

PlainItemDelegate::PlainItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void
PlainItemDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    QStyleOptionViewItemV4 opt(option); // text and widget are in V4 with Qt 4
#else
    QStyleOptionViewItem opt(option);
#endif

    opt.state &= ~QStyle::State_HasFocus;

    const QString secondary = index.data(kSecondaryTextRole).toString();
    if ( secondary.isEmpty() ) {
        QStyledItemDelegate::paint(painter, opt, index);

        return;
    }

    // Background, selection and icon from the style; the two lines here.
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear();
    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget).adjusted(2, 0, -2, 0);
    const int lineHeight = opt.fontMetrics.height();
    const int top = textRect.top() + (textRect.height() - 2 * lineHeight) / 2;
    const bool selected = opt.state & QStyle::State_Selected;
    QColor dim = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    dim.setAlphaF(0.55);

    painter->save();
    painter->setFont(opt.font);
    painter->setPen( index.data(Qt::ForegroundRole).isValid() ? index.data(Qt::ForegroundRole).value<QBrush>().color()
                     : opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text) );
    painter->drawText(QRect(textRect.left(), top, textRect.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
    painter->setPen(dim);
    painter->drawText(QRect(textRect.left(), top + lineHeight, textRect.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(secondary, Qt::ElideRight, textRect.width()));
    painter->restore();
}

QSize
PlainItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);

    if ( !index.data(kSecondaryTextRole).toString().isEmpty() ) {
        size.setHeight( qMax(size.height(), 2 * option.fontMetrics.height() + 6) );
    }

    return size;
}

HoverIconDelegate::HoverIconDelegate(QObject* parent)
    : PlainItemDelegate(parent)
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
    PlainItemDelegate::paint(painter, option, index);

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

    return PlainItemDelegate::editorEvent(event, model, option, index);
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

QSize
DockTitleBar::sizeHint() const
{
    return QSize( fontMetrics().boundingRect( _dock->windowTitle() ).width() + 2 * height(), height() );
}

QSize
DockTitleBar::minimumSizeHint() const
{
    return QSize( 2 * height(), height() );
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

EmptyViewHint::EmptyViewHint(QAbstractItemView* view,
                             const QString& text)
    : QObject(view)
    , _view(view)
    , _label(0)
{
    _label = new QLabel(text, view->viewport());
    _label->setAlignment(Qt::AlignCenter);
    _label->setWordWrap(true);
    _label->setAttribute(Qt::WA_TransparentForMouseEvents); // right-click menus still work
    // The application style sheet sets label colors: dim the text color there.
    const QColor textColor = QApplication::palette().color(QPalette::Text);
    _label->setStyleSheet( QString::fromUtf8("QLabel { color: rgba(%1, %2, %3, 45%); }")
                           .arg( textColor.red() ).arg( textColor.green() ).arg( textColor.blue() ) );

    view->viewport()->installEventFilter(this);
    QAbstractItemModel* model = view->model();
    QObject::connect( model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(refresh()) );
    QObject::connect( model, SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(refresh()) );
    QObject::connect( model, SIGNAL(modelReset()), this, SLOT(refresh()) );
    QObject::connect( model, SIGNAL(layoutChanged()), this, SLOT(refresh()) );
    refresh();
}

EmptyViewHint*
EmptyViewHint::install(QAbstractItemView* view,
                       const QString& text)
{
    EmptyViewHint* hint = view->findChild<EmptyViewHint*>();

    if (hint) {
        hint->setText(text);
    } else {
        hint = new EmptyViewHint(view, text);
    }

    return hint;
}

void
EmptyViewHint::setText(const QString& text)
{
    _label->setText(text);
}

bool
EmptyViewHint::eventFilter(QObject* watched,
                           QEvent* e)
{
    if ( (watched == _view->viewport()) && (e->type() == QEvent::Resize) ) {
        _label->setGeometry( _view->viewport()->rect().adjusted(12, 12, -12, -12) );
    }

    return QObject::eventFilter(watched, e);
}

void
EmptyViewHint::refresh()
{
    _label->setVisible( _view->model()->rowCount( _view->rootIndex() ) == 0 );
}

TableColumnMenu::TableColumnMenu(QTableView* table,
                                 const QString& settingsKey,
                                 const QList<int>& hiddenByDefault)
    : QObject(table)
    , _table(table)
    , _settingsKey(settingsKey)
    , _button(0)
    , _menu(0)
{
    QHeaderView* header = table->horizontalHeader();

    _menu = new QMenu(table);
    _button = new QToolButton(header);
    _button->setIcon( table->style()->standardIcon(QStyle::SP_FileDialogDetailedView) );
    _button->setToolTip( tr("Show or hide columns") );
    _button->setAutoRaise(true);
    _button->setFocusPolicy(Qt::NoFocus);
    _button->setPopupMode(QToolButton::InstantPopup);
    _button->setStyleSheet( QString::fromUtf8("QToolButton::menu-indicator { image: none; }") );
    _button->setMenu(_menu);

    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->installEventFilter(this);
    QObject::connect( header, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onHeaderContextMenu(QPoint)) );
    QObject::connect( _menu, SIGNAL(aboutToShow()), this, SLOT(onAboutToShow()) );
    QObject::connect( _menu, SIGNAL(triggered(QAction*)), this, SLOT(onActionTriggered(QAction*)) );

    // Hidden columns are saved by their header label.
    QSettings settings;
    const bool saved = settings.contains(_settingsKey);
    const QStringList hidden = settings.value(_settingsKey).toStringList();
    for (int column = 0; column < _table->model()->columnCount(); ++column) {
        _table->setColumnHidden( column, saved ? hidden.contains( columnLabel(column) ) : hiddenByDefault.contains(column) );
    }
    placeButton();
}

TableColumnMenu*
TableColumnMenu::install(QTableView* table,
                         const QString& settingsKey,
                         const QList<int>& hiddenByDefault)
{
    return new TableColumnMenu(table, settingsKey, hiddenByDefault);
}

QString
TableColumnMenu::columnLabel(int column) const
{
    return _table->model()->headerData(column, Qt::Horizontal).toString();
}

void
TableColumnMenu::placeButton()
{
    QHeaderView* header = _table->horizontalHeader();
    const int size = header->height();

    _button->setGeometry(header->width() - size, 0, size, size);
    _button->raise();
}

bool
TableColumnMenu::eventFilter(QObject* watched,
                             QEvent* e)
{
    if ( (watched == _table->horizontalHeader()) && ( (e->type() == QEvent::Resize) || (e->type() == QEvent::Show) ) ) {
        placeButton();
    }

    return QObject::eventFilter(watched, e);
}

void
TableColumnMenu::onAboutToShow()
{
    _menu->clear();

    int visible = 0;
    for (int column = 0; column < _table->model()->columnCount(); ++column) {
        if ( !_table->isColumnHidden(column) ) {
            ++visible;
        }
    }
    for (int column = 0; column < _table->model()->columnCount(); ++column) {
        QAction* action = _menu->addAction( columnLabel(column) );
        action->setCheckable(true);
        action->setChecked( !_table->isColumnHidden(column) );
        action->setEnabled( _table->isColumnHidden(column) || (visible > 1) ); // keep one column
        action->setData(column);
    }
}

void
TableColumnMenu::onActionTriggered(QAction* action)
{
    const int column = action->data().toInt();

    _table->setColumnHidden( column, !action->isChecked() );

    QStringList hidden;
    for (int c = 0; c < _table->model()->columnCount(); ++c) {
        if ( _table->isColumnHidden(c) ) {
            hidden << columnLabel(c);
        }
    }
    QSettings settings;
    settings.setValue(_settingsKey, hidden);
}

void
TableColumnMenu::onHeaderContextMenu(const QPoint& pos)
{
    _menu->exec( _table->horizontalHeader()->mapToGlobal(pos) );
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_HoverWidgets.cpp"
