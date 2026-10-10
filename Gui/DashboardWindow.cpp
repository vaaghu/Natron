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
#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>
#include <QToolTip>
#include <QUndoCommand>
#include <QUndoStack>
#include <QToolButton>
#include <QVariantMap>
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

// QSettings keys of the dashboard window layout.
#define kDashboardSettingsGeometry "dashboard/geometry"
#define kDashboardSettingsState "dashboard/dockState"
#define kDashboardLayoutVersion 1

// Tooltips show once the mouse has stayed still this long.
#define kDashboardToolTipDelayMs 2000

NATRON_NAMESPACE_ENTER

NATRON_NAMESPACE_ANONYMOUS_ENTER

// Removal of state store keys, undone by setting them back. Pushed after
// the keys are removed: the first redo() does nothing.
class KeyRemovalCommand
    : public QUndoCommand
{
public:

    KeyRemovalCommand(::StateStore* store,
                      const QList<QPair<QString, QVariant> >& removed)
        : QUndoCommand( (removed.size() == 1) ? QObject::tr("Remove Key %1").arg(removed.first().first)
                                              : QObject::tr("Remove %1 Keys").arg( removed.size() ) )
        , _store(store)
        , _removed(removed)
        , _done(true)
    {
    }

    virtual void undo() OVERRIDE
    {
        for (int i = 0; i < _removed.size(); ++i) {
            _store->set(_removed.at(i).first, _removed.at(i).second);
        }
    }

    virtual void redo() OVERRIDE
    {
        if (_done) {
            _done = false;

            return;
        }
        for (int i = 0; i < _removed.size(); ++i) {
            _store->remove(_removed.at(i).first);
        }
    }

private:

    ::StateStore* _store;
    QList<QPair<QString, QVariant> > _removed;
    bool _done;
};

// Values set at once (import), undone by putting back the previous ones
// (an invalid QVariant: the key did not exist). Pushed after the values are
// set: the first redo() does nothing.
class KeyValuesCommand
    : public QUndoCommand
{
public:

    KeyValuesCommand(::StateStore* store,
                     const QHash<QString, QVariant>& before,
                     const QHash<QString, QVariant>& after,
                     const QString& text)
        : QUndoCommand(text)
        , _store(store)
        , _before(before)
        , _after(after)
        , _done(true)
    {
    }

    virtual void undo() OVERRIDE
    {
        apply(_before);
    }

    virtual void redo() OVERRIDE
    {
        if (_done) {
            _done = false;

            return;
        }
        apply(_after);
    }

private:

    void apply(const QHash<QString, QVariant>& values)
    {
        for (QHash<QString, QVariant>::const_iterator it = values.constBegin(); it != values.constEnd(); ++it) {
            if ( it.value().isValid() ) {
                _store->set( it.key(), it.value() );
            } else {
                _store->remove( it.key() );
            }
        }
    }

    ::StateStore* _store;
    QHash<QString, QVariant> _before;
    QHash<QString, QVariant> _after;
    bool _done;
};

// Number typed by the user: "0.5" anywhere, or in the user's locale ("0,5").
bool
parseNumber(const QString& text,
            double* number)
{
    bool ok = false;

    *number = QLocale::c().toDouble(text.trimmed(), &ok);
    if (!ok) {
        *number = QLocale().toDouble(text.trimmed(), &ok);
    }

    return ok;
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

DashboardWindow::DashboardWindow(::StateStore* store,
                                 ::HttpServer* server,
                                 ::SceneStore* scenes,
                                 ::SceneRenderer* renderer,
                                 QWidget* parent)
    : QMainWindow(parent)
    , _store(store)
    , _server(server)
    , _sceneStore(scenes)
    , _sceneRenderer(renderer)
    , _recentList(0)
    , _scenePanel(0)
    , _defaultLayout()
    , _serverStatusLabel(0)
    , _table(0)
    , _newTypeCombo(0)
    , _newKeyEdit(0)
    , _newValueEdit(0)
    , _addButton(0)
    , _removeAction(0)
    , _undoStack(0)
    , _searchEdit(0)
    , _newProjectAction(0)
    , _openProjectAction(0)
    , _updatingTable(false)
{
    setWindowTitle( tr("%1 - Dashboard").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ) );
    resize(1400, 800);

    createPanels();
    DelayedToolTips::install(this, kDashboardToolTipDelayMs);

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
    // Also when the application quits from an editor window.
    saveLayout();
}

void
DashboardWindow::setNdiManager(::NdiManager* ndi)
{
    if (_scenePanel) {
        _scenePanel->setNdiManager(ndi);
    }
}

QDockWidget*
DashboardWindow::addPanel(const QString& objectName,
                          const QString& title,
                          QWidget* content)
{
    QDockWidget* dock = new QDockWidget(title, this);
    dock->setObjectName(objectName); // saved layout
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    DockTitleBar::install(dock); // shown on hover only

    return dock;
}

void
DashboardWindow::createPanels()
{
    // Panels only (no central widget), any of them can go anywhere.
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    // Edit: undo / redo of scene and data changes.
    _undoStack = new QUndoStack(this);
    QMenu* editMenu = menuBar()->addMenu( tr("&Edit") );
    QAction* undo = _undoStack->createUndoAction( this, tr("&Undo") );
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = _undoStack->createRedoAction( this, tr("&Redo") );
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undo);
    editMenu->addAction(redo);

    if (_sceneStore) {
        _scenePanel = new ScenePanel(_sceneStore, _sceneRenderer, _store, this);
        _scenePanel->setUndoStack(_undoStack);
    }

    // Default layout:
    //   Projects | Scene         | Data
    //   Scenes   | KVs / NDI     |
    QDockWidget* projects = addPanel( QString::fromUtf8("projects"), tr("Projects"), createProjectsPanel() );
    QDockWidget* data = addPanel( QString::fromUtf8("data"), tr("Data"), createDataPanel() );
    addDockWidget(Qt::LeftDockWidgetArea, projects);
    addDockWidget(Qt::RightDockWidgetArea, data);

    QList<QDockWidget*> panels;
    panels << projects;
    if (_scenePanel) {
        QDockWidget* scenes = addPanel( QString::fromUtf8("scenes"), tr("Scenes"), _scenePanel->sceneListPart() );
        QDockWidget* scene = addPanel( QString::fromUtf8("scene"), tr("Scene"), _scenePanel->scenePart() );
        QDockWidget* kvs = addPanel( QString::fromUtf8("kvs"), tr("KVs used in this scene"), _scenePanel->kvPart() );
        QDockWidget* ndi = addPanel( QString::fromUtf8("ndi"), tr("NDI output"), _scenePanel->ndiPart() );
        addDockWidget(Qt::LeftDockWidgetArea, scene);
        splitDockWidget(projects, scene, Qt::Horizontal);
        splitDockWidget(projects, scenes, Qt::Vertical);
        splitDockWidget(scene, kvs, Qt::Vertical);
        tabifyDockWidget(kvs, ndi);
        kvs->raise();
        panels << scenes << scene << kvs << ndi;
    }
    panels << data;

    QMenu* windowMenu = menuBar()->addMenu( tr("&Window") );
    for (int i = 0; i < panels.size(); ++i) {
        windowMenu->addAction( panels.at(i)->toggleViewAction() );
    }
    windowMenu->addSeparator();
    QAction* reset = windowMenu->addAction( tr("Reset Layout") );
    QObject::connect( reset, SIGNAL(triggered()), this, SLOT(onResetLayoutClicked()) );

    // The user's layout from the last run.
    _defaultLayout = saveState(kDashboardLayoutVersion);
    QSettings settings;
    restoreGeometry( settings.value( QString::fromUtf8(kDashboardSettingsGeometry) ).toByteArray() );
    restoreState(settings.value( QString::fromUtf8(kDashboardSettingsState) ).toByteArray(), kDashboardLayoutVersion);
}

void
DashboardWindow::onResetLayoutClicked()
{
    restoreState(_defaultLayout, kDashboardLayoutVersion);
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (int i = 0; i < docks.size(); ++i) {
        docks.at(i)->setFloating(false);
        docks.at(i)->show();
    }
}

void
DashboardWindow::saveLayout()
{
    QSettings settings;

    settings.setValue( QString::fromUtf8(kDashboardSettingsGeometry), saveGeometry() );
    settings.setValue( QString::fromUtf8(kDashboardSettingsState), saveState(kDashboardLayoutVersion) );
}

QWidget*
DashboardWindow::createProjectsPanel()
{
    QWidget* recent = new QWidget(this);
    QVBoxLayout* recentLayout = new QVBoxLayout(recent);
    KvGui::setPanelLayout(recentLayout);

    QHBoxLayout* header = new QHBoxLayout;
    QLabel* caption = KvGui::secondaryLabel(recent);
    caption->setText( tr("Recent") );
    header->addWidget(caption);
    header->addStretch();
    QMenu* menu = 0;
    QToolButton* menuButton = KvGui::panelMenuButton(recent, &menu);
    menuButton->setToolTip( tr("Project actions") );
    _newProjectAction = new QAction(tr("New Project"), recent);
    _newProjectAction->setShortcut( QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_N) );
    _openProjectAction = new QAction(tr("Open Project..."), recent);
    _openProjectAction->setShortcut( QKeySequence(Qt::CTRL + Qt::Key_O) );
    QAction* projectActions[] = { _newProjectAction, _openProjectAction };
    for (int i = 0; i < 2; ++i) {
        // Only while the focus is in this panel: the editor has its own.
        projectActions[i]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        recent->addAction(projectActions[i]);
        menu->addAction(projectActions[i]);
    }
    QObject::connect( _newProjectAction, SIGNAL(triggered()), this, SLOT(onNewProjectClicked()) );
    QObject::connect( _openProjectAction, SIGNAL(triggered()), this, SLOT(onOpenProjectClicked()) );
    header->addWidget(menuButton);
    recentLayout->addLayout(header);

    _recentList = new QListWidget(recent);
    _recentList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    KvGui::styleList(_recentList);
    EmptyViewHint::install( _recentList, tr("No recent projects") );
    _recentList->setContextMenuPolicy(Qt::CustomContextMenu);
    _recentList->setToolTip( tr("Double-click a project to open it. Right-click for more actions.") );
    recentLayout->addWidget(_recentList);

    QObject::connect( _recentList, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(onRecentItemActivated(QListWidgetItem*)) );
    QObject::connect( _recentList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onRecentContextMenu(QPoint)) );

    return recent;
}

QWidget*
DashboardWindow::createDataPanel()
{
    QWidget* box = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(box);
    KvGui::setPanelLayout(layout);

    _serverStatusLabel = new QLabel(box);
    _serverStatusLabel->setTextFormat(Qt::PlainText);
    _serverStatusLabel->setWordWrap(true);
    _serverStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_serverStatusLabel);

    // [search........][menu: import / export]
    QHBoxLayout* header = new QHBoxLayout;
    _searchEdit = new QLineEdit(box);
    _searchEdit->setPlaceholderText( tr("Search keys and values (Ctrl+F)") );
    header->addWidget(_searchEdit, 1);
    QMenu* menu = 0;
    QToolButton* menuButton = KvGui::panelMenuButton(box, &menu);
    menuButton->setToolTip( tr("Data actions") );
    menu->addAction( tr("Import Values..."), this, SLOT(onImportValues()) );
    menu->addAction( tr("Export Values..."), this, SLOT(onExportValues()) );
    header->addWidget(menuButton);
    layout->addLayout(header);
    QAction* find = new QAction(tr("Search"), box);
    find->setShortcut(QKeySequence::Find);
    find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    box->addAction(find);
    QObject::connect( find, SIGNAL(triggered()), _searchEdit, SLOT(setFocus()) );
    QObject::connect( find, SIGNAL(triggered()), _searchEdit, SLOT(selectAll()) );
    QObject::connect( _searchEdit, SIGNAL(textChanged(QString)), this, SLOT(onSearchTextChanged()) );

    _table = new QTableWidget(0, 3, box);
    QStringList headers;
    headers << tr("Key") << tr("Type") << tr("Value");
    _table->setHorizontalHeaderLabels(headers);
    KvGui::styleTable(_table);
    _table->setColumnWidth(kDashboardColumnKey, 120);
    _table->setColumnWidth(kDashboardColumnType, 70);
    // Double-click is handled here: edit text, open images.
    _table->setEditTriggers(QAbstractItemView::EditKeyPressed);
    _table->setContextMenuPolicy(Qt::CustomContextMenu);
    _table->setToolTip( tr("Double-click a value to edit it (an image opens, on/off toggles). Right-click for more actions.") );
    HoverIconDelegate* valueDelegate = HoverIconDelegate::install(_table, kDashboardColumnValue);
    QList<int> hiddenColumns;
    hiddenColumns << kDashboardColumnType; // the value shows an image icon or color swatch
    TableColumnMenu::install(_table, QString::fromUtf8("dashboard/columns/data"), hiddenColumns);
    EmptyViewHint::install( _table, tr("No keys. Add one below or use the HTTP API.") );
    layout->addWidget(_table);

    _removeAction = new QAction(tr("Remove"), _table);
    _removeAction->setShortcut(QKeySequence::Delete);
    _removeAction->setShortcutContext(Qt::WidgetShortcut);
    _removeAction->setEnabled(false);
    _table->addAction(_removeAction);

    // New key: [type][key][value][+]
    QHBoxLayout* addRow = new QHBoxLayout;
    _newTypeCombo = new QComboBox(box);
    const QStringList types = KvGui::typeNames();
    for (int i = 0; i < types.size(); ++i) {
        const QString& name = types.at(i);
        const QVariant sample = ( name == QString::fromUtf8("image") ) ? Kv::makeImage( QString::fromUtf8("x.png") )
                                : ( name == QString::fromUtf8("color") ) ? Kv::makeColor( QString::fromUtf8("#e6a03c") ) : QVariant();
        _newTypeCombo->addItem(KvGui::typeIcon(sample), KvGui::typeLabelForName(name), name);
    }
    _newTypeCombo->setToolTip( tr("Value type") );
    _newKeyEdit = new QLineEdit(box);
    _newKeyEdit->setPlaceholderText( tr("New key") );
    _newValueEdit = new HoverLineEdit(box);
    _newValueEdit->setPlaceholderText( tr("Value") );
    _addButton = new QPushButton(QString::fromUtf8("+"), box);
    _addButton->setEnabled(false);
    _addButton->setFixedWidth( _addButton->sizeHint().height() );
    _addButton->setToolTip( tr("Add the key, replacing any existing value") );
    addRow->addWidget(_newTypeCombo);
    addRow->addWidget(_newKeyEdit, 1);
    addRow->addWidget(_newValueEdit, 2);
    addRow->addWidget(_addButton);
    layout->addLayout(addRow);

    QObject::connect( _table, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onTableItemChanged(QTableWidgetItem*)) );
    QObject::connect( _table, SIGNAL(itemDoubleClicked(QTableWidgetItem*)), this, SLOT(onTableItemDoubleClicked(QTableWidgetItem*)) );
    QObject::connect( _table, SIGNAL(itemSelectionChanged()), this, SLOT(onTableSelectionChanged()) );
    QObject::connect( valueDelegate, SIGNAL(iconClicked(QModelIndex)), this, SLOT(onValueIconClicked(QModelIndex)) );
    QObject::connect( _newTypeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onNewTypeChanged(int)) );
    QObject::connect( _newKeyEdit, SIGNAL(textChanged(QString)), this, SLOT(onNewKeyTextChanged(QString)) );
    QObject::connect( _newKeyEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _newValueEdit, SIGNAL(returnPressed()), this, SLOT(onAddClicked()) );
    QObject::connect( _newValueEdit, SIGNAL(actionClicked()), this, SLOT(onNewValueActionClicked()) );
    onNewTypeChanged(0);
    QObject::connect( _addButton, SIGNAL(clicked()), this, SLOT(onAddClicked()) );
    QObject::connect( _removeAction, SIGNAL(triggered()), this, SLOT(onRemoveClicked()) );
    QObject::connect( _table, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onDataContextMenu(QPoint)) );

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

        QListWidgetItem* item = new QListWidgetItem( fi.fileName() ); // the folder is in the tooltip
        item->setData( Qt::UserRole, fi.absoluteFilePath() );
        item->setToolTip( QDir::toNativeSeparators( fi.absoluteFilePath() ) );
        _recentList->addItem(item);
        ++count;
    }
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
DashboardWindow::onRecentContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = _recentList->itemAt(pos);
    QMenu menu(this);

    if (item) {
        // Right-clicking outside the selection acts on the clicked project only.
        if ( !item->isSelected() ) {
            _recentList->clearSelection();
            item->setSelected(true);
        }
        menu.addAction( tr("Open"), this, SLOT(onOpenRecentClicked()) );
        QAction* addToScene = menu.addAction( tr("Add to Scene"), this, SLOT(onAddToSceneClicked()) );
        addToScene->setEnabled( _scenePanel && _scenePanel->hasCurrentScene() );
        menu.addSeparator();
    }
    menu.addAction(_newProjectAction);
    menu.addAction(_openProjectAction);
    menu.exec( _recentList->viewport()->mapToGlobal(pos) );
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
    const Kv::Type type = Kv::typeOf(value);
    const bool image = type == Kv::eTypeImage;
    // Text and numbers are edited in place; the others with a click.
    const bool inPlace = (type == Kv::eTypeText) || (type == Kv::eTypeNumber) || (type == Kv::eTypeInvalid);

    QTableWidgetItem* typeItem = new QTableWidgetItem( KvGui::typeIcon(value), KvGui::typeLabel(value) );
    typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
    _table->setItem(row, kDashboardColumnType, typeItem);

    QTableWidgetItem* item = _table->item(row, kDashboardColumnValue);
    if (!item) {
        item = new QTableWidgetItem;
        _table->setItem(row, kDashboardColumnValue, item);
    }

    // Images show their file name, colors a swatch, on/off On or Off.
    item->setText( Kv::displayText(value) );
    item->setIcon( KvGui::typeIcon(value) );
    item->setToolTip( KvGui::tooltip(value) );
    item->setData( HoverIconDelegate::kHoverIconRole, image ? KvGui::chooseImageIcon() : KvGui::editIcon() );
    item->setFlags( inPlace ? (item->flags() | Qt::ItemIsEditable) : (item->flags() & ~Qt::ItemIsEditable) );
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

    applySearch();
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
    applySearch();
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

    // Only text and numbers are edited in place.
    const QVariant current = _store->get( keyItem->text() );
    if ( Kv::typeOf(current) == Kv::eTypeNumber ) {
        double number = 0;
        if ( !parseNumber(item->text(), &number) ) {
            QToolTip::showText( _table->viewport()->mapToGlobal( _table->visualItemRect(item).bottomLeft() ),
                                tr("%1 is not a number: the value is unchanged.").arg( item->text() ) );
            _updatingTable = true;
            setRowValue(item->row(), current); // back to the stored value
            _updatingTable = false;

            return;
        }
        if ( Kv::textValue(current) != Kv::textValue( Kv::makeNumber(number) ) ) {
            _store->set( keyItem->text(), Kv::makeNumber(number) );
        }

        return;
    }
    if ( (Kv::typeOf(current) != Kv::eTypeText) && (Kv::typeOf(current) != Kv::eTypeInvalid) ) {
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

    editValue(item->row(), true);
}

void
DashboardWindow::editValue(int row,
                           bool open)
{
    QTableWidgetItem* keyItem = _table->item(row, kDashboardColumnKey);
    QTableWidgetItem* valueItem = _table->item(row, kDashboardColumnValue);

    if (!keyItem || !valueItem) {
        return;
    }

    const QString key = keyItem->text();
    const QVariant value = _store->get(key);
    switch ( Kv::typeOf(value) ) {
    case Kv::eTypeImage:
        if (open) {
            KvGui::openImage(value);
        } else {
            const QString file = KvGui::chooseImageFile( this, Kv::imagePath(value) );
            if ( !file.isEmpty() ) {
                _store->set( key, Kv::makeImage(file) );
            }
        }
        break;
    case Kv::eTypeBool:
        _store->set( key, Kv::makeBool( Kv::textValue(value) != QString::fromUtf8("true") ) );
        break;
    case Kv::eTypeColor: {
        const QString hex = KvGui::chooseColor( this, Kv::textValue(value) );
        if ( !hex.isEmpty() ) {
            _store->set( key, Kv::makeColor(hex) );
        }
        break;
    }
    default:
        _table->editItem(valueItem);
        break;
    }
}

void
DashboardWindow::onValueIconClicked(const QModelIndex& index)
{
    editValue(index.row(), false);
}

void
DashboardWindow::onNewTypeChanged(int index)
{
    Q_UNUSED(index);
    const QString type = newTypeName();

    if ( type == QString::fromUtf8("image") ) {
        _newValueEdit->setPlaceholderText( tr("Image file path") );
        _newValueEdit->setActionIcon( KvGui::chooseImageIcon(), tr("Choose an image file") );
    } else if ( type == QString::fromUtf8("color") ) {
        _newValueEdit->setPlaceholderText( tr("#rrggbb") );
        _newValueEdit->setActionIcon( KvGui::editIcon(), tr("Choose a color") );
    } else if ( type == QString::fromUtf8("number") ) {
        _newValueEdit->setPlaceholderText( tr("Number") );
        _newValueEdit->setActionIcon( QIcon(), QString() );
    } else if ( type == QString::fromUtf8("bool") ) {
        _newValueEdit->setPlaceholderText( tr("on or off") );
        _newValueEdit->setActionIcon( QIcon(), QString() );
    } else {
        _newValueEdit->setPlaceholderText( tr("Value") );
        _newValueEdit->setActionIcon( QIcon(), QString() );
    }
}

QString
DashboardWindow::newTypeName() const
{
    return _newTypeCombo->itemData( _newTypeCombo->currentIndex() ).toString();
}

QVariant
DashboardWindow::newValueFromForm(QString* error) const
{
    const QString type = newTypeName();
    const QString text = _newValueEdit->text();

    if ( type == QString::fromUtf8("image") ) {
        const QString path = QDir::fromNativeSeparators( text.trimmed() );
        if ( path.isEmpty() ) {
            *error = tr("Enter an image file path, or choose one with the icon.");

            return QVariant();
        }

        return Kv::makeImage(path);
    }
    if ( type == QString::fromUtf8("number") ) {
        double number = 0;
        if ( !parseNumber(text, &number) ) {
            *error = tr("Enter a number, e.g. 42 or 0.5.");

            return QVariant();
        }

        return Kv::makeNumber(number);
    }
    if ( type == QString::fromUtf8("bool") ) {
        QVariantMap map;
        map.insert( QString::fromUtf8("type"), type );
        map.insert( QString::fromUtf8("value"), text.trimmed().isEmpty() ? QString::fromUtf8("off") : text );
        QString unused;
        const QVariant value = Kv::normalize(map, true, &unused);
        if ( !value.isValid() ) {
            *error = tr("Enter on or off.");
        }

        return value;
    }
    if ( type == QString::fromUtf8("color") ) {
        const QVariant value = Kv::makeColor(text);
        if ( !value.isValid() ) {
            *error = tr("Enter a color as #rrggbb, or choose one with the icon.");
        }

        return value;
    }

    return Kv::makeText(text);
}

void
DashboardWindow::onNewValueActionClicked()
{
    if ( newTypeName() == QString::fromUtf8("color") ) {
        const QString hex = KvGui::chooseColor( this, _newValueEdit->text() );
        if ( !hex.isEmpty() ) {
            _newValueEdit->setText(hex);
        }

        return;
    }

    const QString file = KvGui::chooseImageFile( this, _newValueEdit->text() );
    if ( !file.isEmpty() ) {
        _newValueEdit->setText( QDir::toNativeSeparators(file) );
    }
}

void
DashboardWindow::onSearchTextChanged()
{
    applySearch();
}

void
DashboardWindow::applySearch()
{
    const QString search = _searchEdit ? _searchEdit->text().trimmed() : QString();

    for (int row = 0; row < _table->rowCount(); ++row) {
        bool match = search.isEmpty();
        for (int column = 0; !match && column < _table->columnCount(); ++column) {
            QTableWidgetItem* item = _table->item(row, column);
            match = item && item->text().contains(search, Qt::CaseInsensitive);
        }
        _table->setRowHidden(row, !match);
    }
}

void
DashboardWindow::onExportValues()
{
    if (!_store) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName( this, tr("Export Values"), QString::fromUtf8("values.json"), tr("JSON (*.json)") );
    if ( path.isEmpty() ) {
        return;
    }

    // The same format the HTTP API takes: { "key": { "type": ..., ... }, ... }
    QVariantMap values;
    const QStringList keys = _store->keys();
    for (int i = 0; i < keys.size(); ++i) {
        values.insert( keys.at(i), _store->get( keys.at(i) ) );
    }
    QFile file(path);
    if ( !file.open(QIODevice::WriteOnly | QIODevice::Truncate) || (file.write( Json::serialize(values) ) < 0) ) {
        QMessageBox::warning( this, tr("Export Values"), tr("Could not write %1: %2").arg( QDir::toNativeSeparators(path) ).arg( file.errorString() ) );
    }
}

void
DashboardWindow::onImportValues()
{
    if (!_store) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName( this, tr("Import Values"), QString(), tr("JSON (*.json);;All files (*)") );
    if ( path.isEmpty() ) {
        return;
    }

    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) ) {
        QMessageBox::warning( this, tr("Import Values"), tr("Could not read %1: %2").arg( QDir::toNativeSeparators(path) ).arg( file.errorString() ) );

        return;
    }
    bool ok = false;
    QString parseError;
    const QVariant parsed = Json::parse(file.readAll(), &ok, &parseError);
    if ( !ok || (parsed.type() != QVariant::Map) ) {
        QMessageBox::warning( this, tr("Import Values"), tr("%1 is not a JSON object of keys and values. %2")
                              .arg( QDir::toNativeSeparators(path) ).arg(parseError) );

        return;
    }

    // Set every valid value; the import can be undone as one step.
    QHash<QString, QVariant> before;
    QHash<QString, QVariant> after;
    QStringList skipped;
    const QVariantMap map = parsed.toMap();
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        QString error;
        const QVariant value = Kv::normalize(it.value(), true, &error);
        if ( it.key().trimmed().isEmpty() || !value.isValid() ) {
            skipped << tr("%1: %2").arg( it.key() ).arg( error.isEmpty() ? tr("empty key") : error );
            continue;
        }
        before.insert( it.key(), _store->has( it.key() ) ? _store->get( it.key() ) : QVariant() );
        after.insert( it.key(), value );
        _store->set( it.key(), value );
    }
    if ( !after.isEmpty() ) {
        _undoStack->push( new KeyValuesCommand( _store, before, after, tr("Import %1 Values").arg( after.size() ) ) );
    }
    if ( !skipped.isEmpty() ) {
        QMessageBox::warning( this, tr("Import Values"), tr("Imported %1 values; skipped %2:\n%3")
                              .arg( after.size() ).arg( skipped.size() ).arg( skipped.join( QString::fromUtf8("\n") ) ) );
    }
}

void
DashboardWindow::onTableSelectionChanged()
{
    _removeAction->setEnabled( !_table->selectedItems().isEmpty() );
}

void
DashboardWindow::onDataContextMenu(const QPoint& pos)
{
    const int row = _table->indexAt(pos).row();
    QTableWidgetItem* keyItem = (row < 0) ? 0 : _table->item(row, kDashboardColumnKey);

    if (!keyItem) {
        return;
    }
    // Right-clicking outside the selection acts on the clicked key only.
    if ( !_table->selectionModel()->isRowSelected( row, QModelIndex() ) ) {
        _table->selectRow(row);
    }

    const QVariant value = _store->get( keyItem->text() );
    QMenu menu(this);
    QAction* open = 0;
    QAction* edit = 0;
    switch ( Kv::typeOf(value) ) {
    case Kv::eTypeImage:
        open = menu.addAction( tr("Open Image") );
        edit = menu.addAction( tr("Choose Image...") );
        break;
    case Kv::eTypeBool:
        edit = menu.addAction( (Kv::textValue(value) == QString::fromUtf8("true")) ? tr("Turn Off") : tr("Turn On") );
        break;
    case Kv::eTypeColor:
        edit = menu.addAction( tr("Choose Color...") );
        break;
    default:
        edit = menu.addAction( tr("Edit Value") );
        break;
    }
    menu.addSeparator();
    menu.addAction(_removeAction);

    QAction* chosen = menu.exec( _table->viewport()->mapToGlobal(pos) );
    if ( chosen && (chosen == open) ) {
        editValue(row, true);
    } else if ( chosen && (chosen == edit) ) {
        editValue(row, false);
    }
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

    QString error;
    const QVariant value = newValueFromForm(&error);
    if ( !value.isValid() ) {
        _newValueEdit->setFocus();
        _newValueEdit->selectAll();
        QToolTip::showText( _newValueEdit->mapToGlobal( QPoint( 0, _newValueEdit->height() ) ), error, _newValueEdit );

        return;
    }
    _store->set(key, value);

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

    QList<QPair<QString, QVariant> > removed;
    for (int i = 0; i < keys.size(); ++i) {
        removed << qMakePair( keys.at(i), _store->get( keys.at(i) ) );
        _store->remove( keys.at(i) );
    }
    if ( !removed.isEmpty() ) {
        _undoStack->push( new KeyRemovalCommand(_store, removed) );
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

    // Only shown when something is wrong or pending.
    _serverStatusLabel->setVisible( !_server->isListening() );
    if ( _server->isListening() ) {
        _serverStatusLabel->clear();
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
DashboardWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);

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
        saveLayout();
        e->accept();
    } else {
        e->ignore();
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_DashboardWindow.cpp"
