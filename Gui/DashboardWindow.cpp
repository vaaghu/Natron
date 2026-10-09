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
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QShowEvent>
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
#include "Gui/HoverWidgets.h"
#include "Gui/KvGuiUtils.h"
#include "Gui/ScenePanel.h"

#include "Custom/server/HttpServer.h"
#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"

#define kDashboardColumnKey 0
#define kDashboardColumnType 1
#define kDashboardColumnValue 2

NATRON_NAMESPACE_ENTER

DashboardWindow::DashboardWindow(::StateStore* store,
                                 ::HttpServer* server,
                                 ::SceneStore* scenes,
                                 ::SceneRenderer* renderer,
                                 QWidget* parent)
    : QWidget(parent)
    , _store(store)
    , _server(server)
    , _sceneStore(scenes)
    , _sceneRenderer(renderer)
    , _recentList(0)
    , _openRecentButton(0)
    , _addToSceneButton(0)
    , _scenePanel(0)
    , _mainSplitter(0)
    , _initialSplitDone(false)
    , _serverStatusLabel(0)
    , _table(0)
    , _newTypeCombo(0)
    , _newKeyEdit(0)
    , _newValueEdit(0)
    , _addButton(0)
    , _removeButton(0)
    , _updatingTable(false)
{
    setWindowTitle( tr("%1 - Dashboard").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ) );
    resize(1400, 800);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // Projects 75%, Data 25% (also when the window is resized).
    _mainSplitter = new QSplitter(Qt::Horizontal, this);
    _mainSplitter->addWidget( createProjectsPanel() );
    _mainSplitter->addWidget( createDataPanel() );
    _mainSplitter->setStretchFactor(0, 3);
    _mainSplitter->setStretchFactor(1, 1);
    mainLayout->addWidget(_mainSplitter);

    if (_store) {
        QObject::connect( _store, SIGNAL(valueChanged(QString)), this, SLOT(onStoreValueChanged(QString)) );
        QObject::connect( _store, SIGNAL(keysChanged()), this, SLOT(onStoreKeysChanged()) );
    }

    if (_server) {
        QObject::connect( _server, SIGNAL(statusChanged()), this, SLOT(onServerStatusChanged()) );
    }

    refreshRecentProjects();
    rebuildTable();
    onServerStatusChanged();
}

DashboardWindow::~DashboardWindow()
{
}

void
DashboardWindow::setNdiManager(::NdiManager* ndi)
{
    if (_scenePanel) {
        _scenePanel->setNdiManager(ndi);
    }
}

QWidget*
DashboardWindow::createProjectsPanel()
{
    QGroupBox* box = new QGroupBox(tr("Projects"), this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    QSplitter* split = new QSplitter(Qt::Vertical, box);

    // Recent projects
    QWidget* recent = new QWidget(split);
    QVBoxLayout* recentLayout = new QVBoxLayout(recent);
    recentLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* newButton = new QPushButton(tr("New Project"), recent);
    newButton->setToolTip( tr("Open a new, empty project in a new window.") );
    QPushButton* openButton = new QPushButton(tr("Open Project..."), recent);
    openButton->setToolTip( tr("Choose a project file and open it in a new window.") );
    _openRecentButton = new QPushButton(tr("Open"), recent);
    _openRecentButton->setEnabled(false);
    _openRecentButton->setToolTip( tr("Open the selected recent projects.") );
    _addToSceneButton = new QPushButton(tr("Add to Scene"), recent);
    _addToSceneButton->setEnabled(false);
    _addToSceneButton->setToolTip( tr("Link the selected recent projects to the opened scene.") );
    buttons->addWidget(newButton);
    buttons->addWidget(openButton);
    buttons->addStretch();
    buttons->addWidget(_openRecentButton);
    buttons->addWidget(_addToSceneButton);
    recentLayout->addLayout(buttons);

    recentLayout->addWidget( new QLabel(tr("Recent projects"), recent) );

    _recentList = new QListWidget(recent);
    _recentList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    recentLayout->addWidget(_recentList);
    split->addWidget(recent);

    // Scenes
    if (_sceneStore) {
        _scenePanel = new ScenePanel(_sceneStore, _sceneRenderer, _store, split);
        split->addWidget(_scenePanel);
        QObject::connect( _scenePanel, SIGNAL(currentSceneChanged()), this, SLOT(onRecentSelectionChanged()) );
    }
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    layout->addWidget(split);

    QObject::connect( newButton, SIGNAL(clicked()), this, SLOT(onNewProjectClicked()) );
    QObject::connect( openButton, SIGNAL(clicked()), this, SLOT(onOpenProjectClicked()) );
    QObject::connect( _openRecentButton, SIGNAL(clicked()), this, SLOT(onOpenRecentClicked()) );
    QObject::connect( _addToSceneButton, SIGNAL(clicked()), this, SLOT(onAddToSceneClicked()) );
    QObject::connect( _recentList, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(onRecentItemActivated(QListWidgetItem*)) );
    QObject::connect( _recentList, SIGNAL(itemSelectionChanged()), this, SLOT(onRecentSelectionChanged()) );

    return box;
}

QWidget*
DashboardWindow::createDataPanel()
{
    QGroupBox* box = new QGroupBox(tr("Data"), this);
    QVBoxLayout* layout = new QVBoxLayout(box);

    _serverStatusLabel = new QLabel(box);
    _serverStatusLabel->setTextFormat(Qt::PlainText);
    _serverStatusLabel->setWordWrap(true);
    _serverStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_serverStatusLabel);

    _table = new QTableWidget(0, 3, box);
    QStringList headers;
    headers << tr("Key") << tr("Type") << tr("Value");
    _table->setHorizontalHeaderLabels(headers);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->verticalHeader()->setVisible(false);
    _table->setColumnWidth(kDashboardColumnKey, 110);
    _table->setColumnWidth(kDashboardColumnType, 70);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Double-click is handled here: edit text, open images.
    _table->setEditTriggers(QAbstractItemView::EditKeyPressed);
    _table->setToolTip( tr("Double-click a text value to edit it, an image to open it. "
                           "Hover a value for its edit / choose-image icon.") );
    HoverIconDelegate* valueDelegate = HoverIconDelegate::install(_table, kDashboardColumnValue);
    layout->addWidget(_table);

    QHBoxLayout* addRow = new QHBoxLayout;
    _newTypeCombo = new QComboBox(box);
    _newTypeCombo->addItem( tr("Text") );
    _newTypeCombo->addItem( KvGui::typeIcon( Kv::makeImage( QString::fromUtf8("x.png") ) ), tr("Image") );
    _newTypeCombo->setToolTip( tr("Type of the new value: text, or an image file.") );
    _newKeyEdit = new QLineEdit(box);
    _newKeyEdit->setPlaceholderText( tr("New key") );
    _newValueEdit = new HoverLineEdit(box);
    _newValueEdit->setPlaceholderText( tr("Value") );
    _newValueEdit->setActionIcon( KvGui::chooseImageIcon(), tr("Choose an image (makes this an image value)") );
    _addButton = new QPushButton(tr("Add"), box);
    _addButton->setEnabled(false);
    _addButton->setToolTip( tr("Add the key, or overwrite it if it already exists.") );
    _removeButton = new QPushButton(tr("Remove"), box);
    _removeButton->setEnabled(false);
    _removeButton->setToolTip( tr("Remove the selected keys.") );
    addRow->addWidget(_newTypeCombo);
    addRow->addWidget(_newKeyEdit, 1);
    layout->addLayout(addRow);
    layout->addWidget(_newValueEdit);
    QHBoxLayout* buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(_addButton);
    buttonRow->addWidget(_removeButton);
    layout->addLayout(buttonRow);

    QObject::connect( _table, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onTableItemChanged(QTableWidgetItem*)) );
    QObject::connect( _table, SIGNAL(itemDoubleClicked(QTableWidgetItem*)), this, SLOT(onTableItemDoubleClicked(QTableWidgetItem*)) );
    QObject::connect( _table, SIGNAL(itemSelectionChanged()), this, SLOT(onTableSelectionChanged()) );
    QObject::connect( valueDelegate, SIGNAL(iconClicked(QModelIndex)), this, SLOT(onValueIconClicked(QModelIndex)) );
    QObject::connect( _newTypeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onNewTypeChanged(int)) );
    QObject::connect( _newKeyEdit, SIGNAL(textChanged(QString)), this, SLOT(onNewKeyTextChanged(QString)) );
    QObject::connect( _newKeyEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _newValueEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _newValueEdit, SIGNAL(actionClicked()), this, SLOT(onNewValueChooseImage()) );
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

QStringList
DashboardWindow::selectedRecentProjects() const
{
    QStringList files;
    QList<QListWidgetItem*> selected = _recentList->selectedItems();

    for (int i = 0; i < selected.size(); ++i) {
        const QString file = selected.at(i)->data(Qt::UserRole).toString();
        if ( !file.isEmpty() ) {
            files << file;
        }
    }

    return files;
}

void
DashboardWindow::onOpenRecentClicked()
{
    const QStringList files = selectedRecentProjects();

    for (int i = 0; i < files.size(); ++i) {
        appPTR->openProjectWindow( files.at(i) );
    }
}

void
DashboardWindow::onAddToSceneClicked()
{
    if (_scenePanel) {
        _scenePanel->addProjectsToCurrentScene( selectedRecentProjects() );
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
    const bool hasSelection = !selectedRecentProjects().isEmpty();

    _openRecentButton->setEnabled(hasSelection);
    _addToSceneButton->setEnabled( hasSelection && _scenePanel && _scenePanel->hasCurrentScene() );
}

// ----- Data -----

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
    const bool image = Kv::typeOf(value) == Kv::eTypeImage;

    QTableWidgetItem* typeItem = new QTableWidgetItem( KvGui::typeIcon(value), KvGui::typeLabel(value) );
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    _table->setItem(row, kDashboardColumnType, typeItem);

    QTableWidgetItem* item = _table->item(row, kDashboardColumnValue);
    if (!item) {
        item = new QTableWidgetItem;
        _table->setItem(row, kDashboardColumnValue, item);
    }

    // Images show their file name; text is edited in place.
    item->setText( Kv::displayText(value) );
    item->setToolTip( KvGui::tooltip(value) );
    item->setData( HoverIconDelegate::kHoverIconRole, image ? KvGui::chooseImageIcon() : KvGui::editIcon() );
    item->setFlags( image ? (item->flags() & ~Qt::ItemIsEditable) : (item->flags() | Qt::ItemIsEditable) );
    if (image && !Kv::imageInfo(value).exists) {
        item->setForeground( QColor(230, 90, 80) );
    } else {
        item->setForeground( _table->palette().text() );
    }
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

    // Only text values are edited in place.
    const QVariant current = _store->get( keyItem->text() );
    if ( Kv::typeOf(current) == Kv::eTypeImage ) {
        return;
    }
    if ( Kv::textValue(current) != item->text() ) {
        _store->set( keyItem->text(), Kv::makeText( item->text() ) );
    }
}

void
DashboardWindow::onTableItemDoubleClicked(QTableWidgetItem* item)
{
    if (!item) {
        return;
    }

    QTableWidgetItem* keyItem = _table->item(item->row(), kDashboardColumnKey);
    if (!keyItem) {
        return;
    }

    const QVariant value = _store->get( keyItem->text() );
    if ( Kv::typeOf(value) == Kv::eTypeImage ) {
        KvGui::openImage(value);
    } else if ( QTableWidgetItem* valueItem = _table->item(item->row(), kDashboardColumnValue) ) {
        _table->editItem(valueItem);
    }
}

void
DashboardWindow::onValueIconClicked(const QModelIndex& index)
{
    QTableWidgetItem* keyItem = _table->item(index.row(), kDashboardColumnKey);
    QTableWidgetItem* valueItem = _table->item(index.row(), kDashboardColumnValue);

    if (!keyItem || !valueItem) {
        return;
    }

    const QString key = keyItem->text();
    const QVariant value = _store->get(key);

    if ( Kv::typeOf(value) == Kv::eTypeImage ) {
        const QString file = KvGui::chooseImageFile( this, Kv::imagePath(value) );
        if ( !file.isEmpty() ) {
            _store->set( key, Kv::makeImage(file) );
        }
    } else {
        _table->editItem(valueItem);
    }
}

void
DashboardWindow::onNewTypeChanged(int index)
{
    _newValueEdit->setPlaceholderText( (index == 1) ? tr("Image file path") : tr("Value") );
}

void
DashboardWindow::onNewValueChooseImage()
{
    const QString file = KvGui::chooseImageFile( this, _newValueEdit->text() );

    if ( !file.isEmpty() ) {
        _newTypeCombo->setCurrentIndex(1);
        _newValueEdit->setText( QDir::toNativeSeparators(file) );
    }
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

    if (_newTypeCombo->currentIndex() == 1) {
        const QString path = QDir::fromNativeSeparators( _newValueEdit->text().trimmed() );
        if ( path.isEmpty() ) {
            _newValueEdit->setFocus();

            return;
        }
        _store->set( key, Kv::makeImage(path) );
    } else {
        _store->set( key, Kv::makeText( _newValueEdit->text() ) );
    }

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

void
DashboardWindow::onServerStatusChanged()
{
    if (!_server) {
        _serverStatusLabel->setText( tr("HTTP server: not created.") );

        return;
    }

    const QString url = QString::fromUtf8("http://localhost:%1").arg( _server->port() );

    if ( _server->isListening() ) {
        _serverStatusLabel->setText( tr("HTTP server: listening on %1  (POST %1/state)").arg(url) );
    } else if ( !_server->lastError().isEmpty() ) {
        _serverStatusLabel->setText( tr("HTTP server: NOT running on %1: %2. Retrying every few seconds; "
                                        "close whatever uses the port, or start Natron with NATRON_HTTP_PORT=<port>.")
                                     .arg(url).arg( _server->lastError() ) );
    } else {
        _serverStatusLabel->setText( tr("HTTP server: starting on %1...").arg(url) );
    }
}

// ----- Window -----

void
DashboardWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);

    // Initial 75% / 25% split, from the real width (setSizes before the
    // window is shown gets skewed by the panels' minimum sizes).
    if (!_initialSplitDone) {
        _initialSplitDone = true;
        const int total = _mainSplitter->width() - _mainSplitter->handleWidth();
        _mainSplitter->setSizes( QList<int>() << (total * 3) / 4 << total - (total * 3) / 4 );
    }
}

void
DashboardWindow::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);

    // Coming back from an editor: projects may have been saved with new
    // bindings or renamed outputs.
    if ( (e->type() == QEvent::ActivationChange) && isActiveWindow() && _scenePanel ) {
        _scenePanel->refresh();
    }
}

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
