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

#include "DashboardWindow.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/GuiApplicationManager.h" // appPTR
#include "Gui/GuiDefines.h" // NATRON_MAX_RECENT_FILES

#include "Custom/state/Json.h"
#include "Custom/state/StateStore.h"

#define kDashboardColumnKey 0
#define kDashboardColumnValue 1

NATRON_NAMESPACE_ENTER

DashboardWindow::DashboardWindow(::StateStore* store,
                                 QWidget* parent)
    : QWidget(parent)
    , _store(store)
    , _recentList(0)
    , _openRecentButton(0)
    , _table(0)
    , _newKeyEdit(0)
    , _newValueEdit(0)
    , _addButton(0)
    , _removeButton(0)
    , _updatingTable(false)
{
    setWindowTitle( tr("%1 - Dashboard").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ) );
    resize(1000, 600);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget( createProjectsPanel() );
    splitter->addWidget( createDataPanel() );
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter);

    if (_store) {
        QObject::connect( _store, SIGNAL(valueChanged(QString)), this, SLOT(onStoreValueChanged(QString)) );
        QObject::connect( _store, SIGNAL(keysChanged()), this, SLOT(onStoreKeysChanged()) );
    }

    refreshRecentProjects();
    rebuildTable();
}

DashboardWindow::~DashboardWindow()
{
}

QWidget*
DashboardWindow::createProjectsPanel()
{
    QGroupBox* box = new QGroupBox(tr("Projects"), this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* newButton = new QPushButton(tr("New Project"), box);
    newButton->setToolTip( tr("Open a new, empty project in a new window.") );
    QPushButton* openButton = new QPushButton(tr("Open Project..."), box);
    openButton->setToolTip( tr("Choose a project file and open it in a new window.") );
    buttons->addWidget(newButton);
    buttons->addWidget(openButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    layout->addWidget( new QLabel(tr("Recent projects"), box) );

    _recentList = new QListWidget(box);
    _recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(_recentList);

    _openRecentButton = new QPushButton(tr("Open"), box);
    _openRecentButton->setEnabled(false);
    QHBoxLayout* openRow = new QHBoxLayout;
    openRow->addStretch();
    openRow->addWidget(_openRecentButton);
    layout->addLayout(openRow);

    QObject::connect( newButton, SIGNAL(clicked()), this, SLOT(onNewProjectClicked()) );
    QObject::connect( openButton, SIGNAL(clicked()), this, SLOT(onOpenProjectClicked()) );
    QObject::connect( _openRecentButton, SIGNAL(clicked()), this, SLOT(onOpenRecentClicked()) );
    QObject::connect( _recentList, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(onRecentItemActivated(QListWidgetItem*)) );
    QObject::connect( _recentList, SIGNAL(itemSelectionChanged()), this, SLOT(onRecentSelectionChanged()) );

    return box;
}

QWidget*
DashboardWindow::createDataPanel()
{
    QGroupBox* box = new QGroupBox(tr("Data"), this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    _table = new QTableWidget(0, 2, box);
    QStringList headers;
    headers << tr("Key") << tr("Value");
    _table->setHorizontalHeaderLabels(headers);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->verticalHeader()->setVisible(false);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    _table->setToolTip( tr("Double-click a value to edit it. Values that are valid JSON "
                           "(numbers, true/false, lists, quoted strings) keep their type; "
                           "anything else is stored as text.") );
    layout->addWidget(_table);

    QHBoxLayout* addRow = new QHBoxLayout;
    _newKeyEdit = new QLineEdit(box);
    _newKeyEdit->setPlaceholderText( tr("New key") );
    _newValueEdit = new QLineEdit(box);
    _newValueEdit->setPlaceholderText( tr("Value") );
    _addButton = new QPushButton(tr("Add"), box);
    _addButton->setEnabled(false);
    _addButton->setToolTip( tr("Add the key, or overwrite it if it already exists.") );
    _removeButton = new QPushButton(tr("Remove"), box);
    _removeButton->setEnabled(false);
    _removeButton->setToolTip( tr("Remove the selected keys.") );
    addRow->addWidget(_newKeyEdit, 1);
    addRow->addWidget(_newValueEdit, 2);
    addRow->addWidget(_addButton);
    addRow->addWidget(_removeButton);
    layout->addLayout(addRow);

    QObject::connect( _table, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onTableItemChanged(QTableWidgetItem*)) );
    QObject::connect( _table, SIGNAL(itemSelectionChanged()), this, SLOT(onTableSelectionChanged()) );
    QObject::connect( _newKeyEdit, SIGNAL(textChanged(QString)), this, SLOT(onNewKeyTextChanged(QString)) );
    QObject::connect( _newKeyEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _newValueEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _addButton, SIGNAL(clicked()), this, SLOT(onAddClicked()) );
    QObject::connect( _removeButton, SIGNAL(clicked()), this, SLOT(onRemoveClicked()) );

    if (!_store) {
        box->setEnabled(false);
    }

    return box;
}

// ----- Projects -----

void
DashboardWindow::refreshRecentProjects()
{
    _recentList->clear();

    QSettings settings;
    const QStringList files = settings.value( QString::fromUtf8("recentFileList") ).toStringList();

    int count = 0;
    for (int i = 0; i < files.size() && count < NATRON_MAX_RECENT_FILES; ++i) {
        QFileInfo fi( files.at(i) );
        if ( !fi.exists() ) {
            continue;
        }

        QListWidgetItem* item = new QListWidgetItem( tr("%1    (%2)").arg( fi.fileName() ).arg( QDir::toNativeSeparators( fi.absolutePath() ) ) );
        item->setData( Qt::UserRole, fi.absoluteFilePath() );
        item->setToolTip( QDir::toNativeSeparators( fi.absoluteFilePath() ) );
        _recentList->addItem(item);
        ++count;
    }

    if (count == 0) {
        QListWidgetItem* item = new QListWidgetItem( tr("No recent projects") );
        item->setFlags(Qt::NoItemFlags);
        _recentList->addItem(item);
    }

    onRecentSelectionChanged();
}

void
DashboardWindow::onNewProjectClicked()
{
    appPTR->newProjectWindow();
}

void
DashboardWindow::onOpenProjectClicked()
{
    const QString filter = tr("%1 projects (*.%2)").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ).arg( QString::fromUtf8(NATRON_PROJECT_FILE_EXT) );
    const QString file = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), filter);

    if ( !file.isEmpty() ) {
        appPTR->openProjectWindow(file);
    }
}

void
DashboardWindow::onOpenRecentClicked()
{
    QList<QListWidgetItem*> selected = _recentList->selectedItems();

    if ( !selected.isEmpty() ) {
        onRecentItemActivated( selected.front() );
    }
}

void
DashboardWindow::onRecentItemActivated(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QString file = item->data(Qt::UserRole).toString();
    if ( !file.isEmpty() ) {
        appPTR->openProjectWindow(file);
    }
}

void
DashboardWindow::onRecentSelectionChanged()
{
    QList<QListWidgetItem*> selected = _recentList->selectedItems();

    _openRecentButton->setEnabled( !selected.isEmpty() && !selected.front()->data(Qt::UserRole).toString().isEmpty() );
}

// ----- Data -----

QVariant
DashboardWindow::parseUserValue(const QString& text)
{
    bool ok = false;
    const QVariant parsed = Json::parse(text.trimmed().toUtf8(), &ok);

    if (ok) {
        return parsed;
    }

    return text;
}

int
DashboardWindow::findRow(const QString& key) const
{
    for (int row = 0; row < _table->rowCount(); ++row) {
        QTableWidgetItem* item = _table->item(row, kDashboardColumnKey);
        if ( item && (item->text() == key) ) {
            return row;
        }
    }

    return -1;
}

void
DashboardWindow::setRowValue(int row,
                             const QVariant& value)
{
    QTableWidgetItem* item = _table->item(row, kDashboardColumnValue);

    if (!item) {
        item = new QTableWidgetItem;
        _table->setItem(row, kDashboardColumnValue, item);
    }

    const QString text = ::StateStore::toText(value);
    item->setText(text);
    item->setToolTip(text);
}

void
DashboardWindow::rebuildTable()
{
    if (!_store) {
        return;
    }

    // Keep the selection across rebuilds.
    QStringList selectedKeys;
    QList<QTableWidgetItem*> selected = _table->selectedItems();
    for (int i = 0; i < selected.size(); ++i) {
        if (selected.at(i)->column() == kDashboardColumnKey) {
            selectedKeys << selected.at(i)->text();
        }
    }

    QStringList keys = _store->keys();
    keys.sort();

    _updatingTable = true;
    _table->setRowCount(0);
    _table->setRowCount( keys.size() );
    for (int row = 0; row < keys.size(); ++row) {
        QTableWidgetItem* keyItem = new QTableWidgetItem( keys.at(row) );
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        _table->setItem(row, kDashboardColumnKey, keyItem);
        setRowValue( row, _store->get( keys.at(row) ) );
        if ( selectedKeys.contains( keys.at(row) ) ) {
            _table->selectRow(row);
        }
    }
    _updatingTable = false;

    onTableSelectionChanged();
}

void
DashboardWindow::onStoreValueChanged(const QString& key)
{
    const int row = findRow(key);

    // New or removed keys are handled by onStoreKeysChanged().
    if ( (row < 0) || !_store->has(key) ) {
        return;
    }

    _updatingTable = true;
    setRowValue( row, _store->get(key) );
    _updatingTable = false;
}

void
DashboardWindow::onStoreKeysChanged()
{
    rebuildTable();
}

void
DashboardWindow::onTableItemChanged(QTableWidgetItem* item)
{
    if ( _updatingTable || !item || (item->column() != kDashboardColumnValue) ) {
        return;
    }

    QTableWidgetItem* keyItem = _table->item(item->row(), kDashboardColumnKey);
    if (!keyItem) {
        return;
    }

    const QVariant value = parseUserValue( item->text() );

    // Only write real edits, not a re-display of the same value.
    if ( ::StateStore::toText(value) == ::StateStore::toText( _store->get( keyItem->text() ) ) &&
         ( value.type() == _store->get( keyItem->text() ).type() ) ) {
        return;
    }

    _store->set(keyItem->text(), value);
}

void
DashboardWindow::onTableSelectionChanged()
{
    _removeButton->setEnabled( !_table->selectedItems().isEmpty() );
}

void
DashboardWindow::onNewKeyTextChanged(const QString& text)
{
    _addButton->setEnabled( !text.trimmed().isEmpty() );
}

void
DashboardWindow::onAddClicked()
{
    const QString key = _newKeyEdit->text().trimmed();

    if ( key.isEmpty() || !_store ) {
        return;
    }

    _store->set( key, parseUserValue( _newValueEdit->text() ) );

    _newKeyEdit->clear();
    _newValueEdit->clear();
    _newKeyEdit->setFocus();

    const int row = findRow(key);
    if (row >= 0) {
        _table->selectRow(row);
        _table->scrollToItem( _table->item(row, kDashboardColumnKey) );
    }
}

void
DashboardWindow::onRemoveClicked()
{
    QStringList keys;
    QList<QTableWidgetItem*> selected = _table->selectedItems();

    for (int i = 0; i < selected.size(); ++i) {
        QTableWidgetItem* keyItem = _table->item(selected.at(i)->row(), kDashboardColumnKey);
        if ( keyItem && !keys.contains( keyItem->text() ) ) {
            keys << keyItem->text();
        }
    }

    for (int i = 0; i < keys.size(); ++i) {
        _store->remove( keys.at(i) );
    }
}

// ----- Window -----

void
DashboardWindow::closeEvent(QCloseEvent* e)
{
    // Closing the dashboard quits the application. Stay open if the user
    // cancels saving one of the open projects.
    if ( appPTR->requestQuit(true) ) {
        e->accept();
    } else {
        e->ignore();
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_DashboardWindow.cpp"
