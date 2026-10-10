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

#include "ScenePanel.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QStyle>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QIcon>
#include <QItemSelectionModel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/GuiApplicationManager.h" // appPTR

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/NdiOutput.h"
#include "Custom/ndi/PlayoutChannel.h"
#include "Custom/scene/SceneRenderer.h"
#include "Custom/scene/SceneStore.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"
#include "Gui/HoverWidgets.h"
#include "Gui/KvGuiUtils.h"

#define kSceneColumnPreview 0
#define kSceneColumnProject 1
#define kSceneColumnProgress 2
#define kSceneColumnStatus 3
#define kSceneColumnControls 4
#define kSceneColumnCount 5

#define kSceneControlButtonSize 22
#define kSceneControlIconSize 20

#define kSceneThumbnailWidth 128
#define kSceneThumbnailHeight 72

NATRON_NAMESPACE_ENTER

NATRON_NAMESPACE_ANONYMOUS_ENTER

// Same player icons as the render progress panel of the editor.
QIcon
playerIcon(NATRON_ENUM::PixmapEnum normal,
           NATRON_ENUM::PixmapEnum checked)
{
    QPixmap normalPix, checkedPix;
    const int size = appPTR->adjustSizeToDPIX(kSceneControlIconSize);

    appPTR->getIcon(normal, size, &normalPix);
    QIcon icon;
    icon.addPixmap(normalPix, QIcon::Normal, QIcon::Off);
    if (checked != normal) {
        appPTR->getIcon(checked, size, &checkedPix);
        icon.addPixmap(checkedPix, QIcon::Normal, QIcon::On);
    }

    return icon;
}

QPushButton*
makeControlButton(const QIcon& icon,
                  const QString& tooltip,
                  QWidget* parent)
{
    QPushButton* button = new QPushButton(icon, QString(), parent);
    const int buttonSize = appPTR->adjustSizeToDPIX(kSceneControlButtonSize);
    const int iconSize = appPTR->adjustSizeToDPIX(kSceneControlIconSize);

    button->setFixedSize(buttonSize, buttonSize);
    button->setIconSize( QSize(iconSize, iconSize) );
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tooltip);

    return button;
}

// Keys matching typed text, best first: starts with it, contains it, then
// has its letters in order (fuzzy). Case-insensitive.
QStringList
fuzzyMatches(const QStringList& keys,
             const QString& typed)
{
    const QString query = typed.toLower();
    QStringList ranked[3];

    for (int i = 0; i < keys.size(); ++i) {
        const QString key = keys.at(i).toLower();
        if ( key.startsWith(query) ) {
            ranked[0] << keys.at(i);
        } else if ( key.contains(query) ) {
            ranked[1] << keys.at(i);
        } else {
            int at = 0;
            for (int c = 0; c < query.size() && at >= 0; ++c) {
                at = key.indexOf(query.at(c), at);
                if (at >= 0) {
                    ++at;
                }
            }
            if (at >= 0) {
                ranked[2] << keys.at(i);
            }
        }
    }

    return ranked[0] + ranked[1] + ranked[2];
}

// "Uses key" combo: opening the list with part of a key typed shows only the
// keys matching it.
class KeyComboBox
    : public QComboBox
{
public:

    explicit KeyComboBox(const QStringList& keys)
        : QComboBox()
        , _keys(keys)
    {
        setEditable(true);
        setInsertPolicy(QComboBox::NoInsert);
        addItems(keys);
    }

    virtual void showPopup() OVERRIDE
    {
        const QString typed = currentText().trimmed();
        const QStringList shown = ( typed.isEmpty() || _keys.contains(typed) ) ? _keys : fuzzyMatches(_keys, typed);
        const bool wasBlocked = blockSignals(true);

        clear();
        if ( shown.isEmpty() ) {
            addItem( QObject::tr("No matching key") );
            QStandardItemModel* items = qobject_cast<QStandardItemModel*>( model() );
            if (items) {
                items->item(0)->setEnabled(false);
            }
        } else {
            addItems(shown);
        }
        setCurrentIndex( shown.indexOf(typed) );
        setEditText(typed);
        blockSignals(wasBlocked);

        QComboBox::showPopup();
    }

private:

    QStringList _keys;
};

// Value types a project binds key for ("text", "image").
QStringList
boundTypes(const ProjectInfo& info,
           const QString& key)
{
    QStringList types;
    const QStringList parts = info.keyTypes.value(key).split( QLatin1Char(',') );

    for (int i = 0; i < parts.size(); ++i) {
        if ( !parts.at(i).isEmpty() ) {
            types << parts.at(i);
        }
    }

    return types;
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

ScenePanel::ScenePanel(::SceneStore* scenes,
                       ::SceneRenderer* renderer,
                       ::StateStore* state,
                       QWidget* parent)
    : QWidget(parent)
    , _scenes(scenes)
    , _renderer(renderer)
    , _state(state)
    , _currentSceneId()
    , _infos()
    , _projectWatcher(0)
    , _projectReloadTimer()
    , _rows()
    , _sceneList(0)
    , _sceneListPart(0)
    , _kvPart(0)
    , _ndiPart(0)
    , _detailStack(0)
    , _sceneTitle(0)
    , _sceneSummary(0)
    , _clearOutputDirAction(0)
    , _renderButton(0)
    , _stopButton(0)
    , _projectTable(0)
    , _kvTable(0)
    , _renderStatus(0)
    , _ndi(0)
    , _ndiEnabledCheck(0)
    , _ndiAlphaCheck(0)
    , _ndiModeCombo(0)
    , _ndiRuntimeLabel(0)
    , _ndiCueButton(0)
    , _ndiContinueButton(0)
    , _ndiPlayAllButton(0)
    , _ndiPauseAllButton(0)
    , _ndiStopAllButton(0)
    , _ndiReplayAllButton(0)
    , _ndiTable(0)
{
    // The parts are shown by the owner (e.g. in dock panels); this widget
    // itself stays hidden.
    _sceneListPart = createSceneList();
    createSceneDetail();
    hide();

    QObject::connect( _scenes, SIGNAL(scenesChanged()), this, SLOT(onScenesChanged()) );
    QObject::connect( _scenes, SIGNAL(renderRecordChanged(QString,QString)), this, SLOT(onRenderRecordChanged(QString,QString)) );
    if (_renderer) {
        QObject::connect( _renderer, SIGNAL(statusChanged()), this, SLOT(onRendererStatusChanged()) );
        QObject::connect( _renderer, SIGNAL(progressChanged(QString,QString)), this, SLOT(onRenderProgressChanged(QString,QString)) );
    }
    if (_state) {
        QObject::connect( _state, SIGNAL(valueChanged(QString)), this, SLOT(onStateValueChanged()) );
        QObject::connect( _state, SIGNAL(keysChanged()), this, SLOT(onStateChanged()) );
    }

    // Saving a project (renamed nodes, new bindings, Write nodes) updates the
    // opened scene. A save can touch the file several times: refresh once.
    _projectWatcher = new QFileSystemWatcher(this);
    _projectReloadTimer.setSingleShot(true);
    _projectReloadTimer.setInterval(300);
    QObject::connect( _projectWatcher, SIGNAL(fileChanged(QString)), this, SLOT(onProjectFileChanged(QString)) );
    QObject::connect( &_projectReloadTimer, SIGNAL(timeout()), this, SLOT(onProjectReloadTimeout()) );

    refreshSceneList();
    showScene();
}

ScenePanel::~ScenePanel()
{
}

void
ScenePanel::onProjectFileChanged(const QString& path)
{
    _infos.remove(path);
    // Saved by writing a new file over the old one: the watch is lost, add it again.
    if ( QFileInfo(path).exists() && !_projectWatcher->files().contains(path) ) {
        _projectWatcher->addPath(path);
    }
    _projectReloadTimer.start();
}

void
ScenePanel::onProjectReloadTimeout()
{
    if ( hasCurrentScene() ) {
        showScene();
    }
}

QWidget*
ScenePanel::createSceneList()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(w);
    KvGui::setPanelLayout(layout);

    _sceneList = new QListWidget(w);
    _sceneList->setSelectionMode(QAbstractItemView::SingleSelection);
    KvGui::styleList(_sceneList);
    EmptyViewHint::install( _sceneList, tr("Right-click to create a scene.") );
    _sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
    _sceneList->setToolTip( tr("Right-click to create, rename or delete scenes.") );
    layout->addWidget(_sceneList);

    QObject::connect( _sceneList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onSceneListContextMenu(QPoint)) );
    QObject::connect( _sceneList, SIGNAL(itemSelectionChanged()), this, SLOT(onSceneSelectionChanged()) );

    return w;
}

QWidget*
ScenePanel::createSceneDetail()
{
    _detailStack = new QStackedWidget(this);

    QLabel* empty = KvGui::secondaryLabel(_detailStack);
    empty->setText( tr("Select a scene, or right-click the scene list to create one.") );
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    _detailStack->addWidget(empty);

    QWidget* detail = new QWidget(_detailStack);
    QVBoxLayout* layout = new QVBoxLayout(detail);
    KvGui::setPanelLayout(layout);

    _sceneTitle = new QLabel(detail);
    _sceneTitle->setTextFormat(Qt::PlainText);
    QFont titleFont = _sceneTitle->font();
    titleFont.setBold(true);
    _sceneTitle->setFont(titleFont);
    _sceneSummary = KvGui::secondaryLabel(detail); // "1 Write node · 1 project · Output: ..."

    // Name, summary, the render buttons, and the scene's other actions under the menu icon.
    QHBoxLayout* header = new QHBoxLayout;
    header->addWidget(_sceneTitle);
    header->addSpacing(8);
    header->addWidget(_sceneSummary, 1);
    _renderButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED),
                                       tr("Render all Write nodes"), detail );
    _stopButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED),
                                     tr("Stop rendering and clear the queue"), detail );
    header->addWidget(_renderButton);
    header->addWidget(_stopButton);
    QMenu* menu = 0;
    QToolButton* menuButton = KvGui::panelMenuButton(detail, &menu);
    menuButton->setToolTip( tr("Scene actions") );
    QAction* addProjects = menu->addAction( tr("Add Projects..."), this, SLOT(onAddProjectsClicked()) );
    addProjects->setToolTip( tr("Add the Write nodes of one or more projects to this scene.") );
    menu->addSeparator();
    menu->addAction( tr("Choose Output Folder..."), this, SLOT(onChooseOutputDirClicked()) );
    _clearOutputDirAction = menu->addAction( tr("Use Project Output Paths"), this, SLOT(onClearOutputDirClicked()) );
    header->addWidget(menuButton);
    layout->addLayout(header);

    _projectTable = new QTableWidget(0, kSceneColumnCount, detail);
    QStringList headers;
    headers << tr("Preview") << tr("Project") << tr("Progress") << tr("Status") << tr("Controls");
    _projectTable->setHorizontalHeaderLabels(headers);
    KvGui::styleTable(_projectTable);
    _projectTable->verticalHeader()->setDefaultSectionSize(kSceneThumbnailHeight + 8);
    _projectTable->setIconSize( QSize(kSceneThumbnailWidth, kSceneThumbnailHeight) );
    _projectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _projectTable->setContextMenuPolicy(Qt::CustomContextMenu);
    _projectTable->setColumnWidth(kSceneColumnPreview, kSceneThumbnailWidth + 12);
    _projectTable->setColumnWidth(kSceneColumnProject, 170);
    _projectTable->setColumnWidth(kSceneColumnProgress, 110);
    _projectTable->setColumnWidth(kSceneColumnStatus, 220);
    _projectTable->setToolTip( tr("Double-click a preview to view the output, or a project to open it. Right-click for more actions.") );
    TableColumnMenu::install( _projectTable, QString::fromUtf8("dashboard/columns/scene"), QList<int>() );
    EmptyViewHint::install( _projectTable, tr("No Write nodes. Right-click to add projects.") );
    layout->addWidget(_projectTable, 1);

    QWidget* kvBox = new QWidget(this);
    QVBoxLayout* kvLayout = new QVBoxLayout(kvBox);
    KvGui::setPanelLayout(kvLayout);
    _kvTable = new QTableWidget(0, 5, kvBox);
    QStringList kvHeaders;
    kvHeaders << tr("Component") << tr("Uses key") << tr("Type") << tr("Value") << tr("Used by");
    _kvTable->setHorizontalHeaderLabels(kvHeaders);
    KvGui::styleTable(_kvTable);
    _kvTable->verticalHeader()->setDefaultSectionSize( _kvTable->fontMetrics().height() + 14 ); // fits the key combo
    _kvTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _kvTable->setColumnWidth(0, 150);
    _kvTable->setColumnWidth(1, 170);
    _kvTable->setColumnWidth(2, 70);
    _kvTable->setColumnWidth(3, 240);
    QList<int> kvHidden;
    kvHidden << 2 << 4; // Type (the value shows an image icon), Used by (in the component tooltip)
    TableColumnMenu::install(_kvTable, QString::fromUtf8("dashboard/columns/kvs"), kvHidden);
    EmptyViewHint::install( _kvTable, tr("No keys are bound in this scene's projects.") );
    _kvTable->setToolTip( tr("The keys bound in this scene's projects and the store key each one reads. "
                             "Values are edited in the Data panel.") );
    QObject::connect( _kvTable, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(onKvCellDoubleClicked(int,int)) );
    kvLayout->addWidget(_kvTable);
    _kvPart = kvBox;
    _ndiPart = createNdiTab(this);

    _renderStatus = new QLabel(detail);
    _renderStatus->setWordWrap(true);
    _renderStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(_renderStatus);

    _detailStack->addWidget(detail);

    QObject::connect( _renderButton, SIGNAL(clicked()), this, SLOT(onRenderClicked()) );
    QObject::connect( _stopButton, SIGNAL(clicked()), this, SLOT(onStopClicked()) );
    QObject::connect( _projectTable, SIGNAL(itemSelectionChanged()), this, SLOT(onProjectSelectionChanged()) );
    QObject::connect( _projectTable, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(onProjectCellDoubleClicked(int,int)) );
    QObject::connect( _projectTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onProjectTableContextMenu(QPoint)) );

    return _detailStack;
}

// ----- Scene list -----

QWidget*
ScenePanel::sceneListPart() const
{
    return _sceneListPart;
}

QWidget*
ScenePanel::scenePart() const
{
    return _detailStack;
}

QWidget*
ScenePanel::kvPart() const
{
    return _kvPart;
}

QWidget*
ScenePanel::ndiPart() const
{
    return _ndiPart;
}

bool
ScenePanel::hasCurrentScene() const
{
    ::Scene scene;

    return !_currentSceneId.isEmpty() && _scenes->scene(_currentSceneId, &scene);
}

void
ScenePanel::refreshSceneList()
{
    const bool wasBlocked = _sceneList->blockSignals(true);

    _sceneList->clear();
    const QList< ::Scene> scenes = _scenes->scenes();
    for (int i = 0; i < scenes.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem( tr("%1  (%2)").arg( scenes.at(i).name ).arg( scenes.at(i).items.size() ) );
        item->setData(Qt::UserRole, scenes.at(i).id);
        _sceneList->addItem(item);
        if (scenes.at(i).id == _currentSceneId) {
            item->setSelected(true);
            _sceneList->setCurrentItem(item);
        }
    }

    _sceneList->blockSignals(wasBlocked);
}

void
ScenePanel::onSceneSelectionChanged()
{
    QList<QListWidgetItem*> selected = _sceneList->selectedItems();
    const QString id = selected.isEmpty() ? QString() : selected.front()->data(Qt::UserRole).toString();

    if (id == _currentSceneId) {
        return;
    }

    _currentSceneId = id;
    _infos.clear(); // re-read the projects when a scene is opened
    showScene();

    Q_EMIT currentSceneChanged();
}

void
ScenePanel::onNewSceneClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText( this, tr("New Scene"), tr("Scene name:"), QLineEdit::Normal,
                                                tr("Scene %1").arg(_scenes->scenes().size() + 1), &ok ).trimmed();

    if ( !ok || name.isEmpty() ) {
        return;
    }

    _currentSceneId = _scenes->createScene(name);
    _infos.clear();
    refreshSceneList();
    showScene();

    Q_EMIT currentSceneChanged();
}

void
ScenePanel::onRenameSceneClicked()
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Scene"), tr("Scene name:"), QLineEdit::Normal, scene.name, &ok).trimmed();
    if ( ok && !name.isEmpty() ) {
        _scenes->renameScene(_currentSceneId, name);
    }
}

void
ScenePanel::onDeleteSceneClicked()
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    if ( QMessageBox::question( this, tr("Delete Scene"),
                                tr("Delete the scene \"%1\"? Its project files are not deleted.").arg(scene.name),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes ) {
        return;
    }

    _currentSceneId.clear();
    _scenes->removeScene(scene.id);

    Q_EMIT currentSceneChanged();
}

void
ScenePanel::onSceneListContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = _sceneList->itemAt(pos);
    QMenu menu(this);

    if (item) {
        _sceneList->setCurrentItem(item); // opens it: the actions apply to the opened scene
        menu.addAction( tr("New Scene..."), this, SLOT(onNewSceneClicked()) );
        menu.addAction( tr("Rename..."), this, SLOT(onRenameSceneClicked()) );
        menu.addSeparator();
        menu.addAction( tr("Delete"), this, SLOT(onDeleteSceneClicked()) );
    } else {
        menu.addAction( tr("New Scene..."), this, SLOT(onNewSceneClicked()) );
    }
    menu.exec( _sceneList->viewport()->mapToGlobal(pos) );
}

void
ScenePanel::onScenesChanged()
{
    ::Scene scene;

    if ( !_currentSceneId.isEmpty() && !_scenes->scene(_currentSceneId, &scene) ) {
        _currentSceneId.clear();
        Q_EMIT currentSceneChanged();
    }

    refreshSceneList();
    showScene();
}

// ----- Opened scene -----

QStringList
ScenePanel::currentItems() const
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return QStringList();
    }

    return scene.items;
}

QStringList
ScenePanel::selectedItems() const
{
    QStringList items;
    QList<QTableWidgetItem*> selected = _projectTable->selectedItems();

    for (int i = 0; i < selected.size(); ++i) {
        QTableWidgetItem* cell = _projectTable->item(selected.at(i)->row(), kSceneColumnProject);
        const QString item = cell ? cell->data(Qt::UserRole).toString() : QString();
        if ( !item.isEmpty() && !items.contains(item) ) {
            items << item;
        }
    }

    return items;
}

const ProjectInfo&
ScenePanel::projectInfo(const QString& project)
{
    QHash<QString, ProjectInfo>::iterator it = _infos.find(project);

    if ( it == _infos.end() ) {
        it = _infos.insert( project, readProjectInfo(project) );
    }

    return it.value();
}

QString
ScenePanel::valueText(const QString& key) const
{
    if ( !_state || !_state->has(key) ) {
        return QString();
    }

    return ::StateStore::toText( _state->get(key) );
}

void
ScenePanel::refresh()
{
    _infos.clear();
    showScene();
}

void
ScenePanel::showScene()
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        _detailStack->setCurrentIndex(0);
        refreshNdiTab();
        refreshButtons();

        return;
    }

    _detailStack->setCurrentIndex(1);

    // Watch the scene's project files (and only those).
    const QStringList projects = scene.projects();
    const QStringList watched = _projectWatcher->files();
    for (int i = 0; i < watched.size(); ++i) {
        if ( !projects.contains( watched.at(i) ) ) {
            _projectWatcher->removePath( watched.at(i) );
        }
    }
    for (int i = 0; i < projects.size(); ++i) {
        if ( !watched.contains( projects.at(i) ) && QFileInfo( projects.at(i) ).exists() ) {
            _projectWatcher->addPath( projects.at(i) );
        }
    }

    _sceneTitle->setText(scene.name);
    const int writers = scene.items.size();
    const int projectCount = scene.projects().size();
    const QString separator = QString::fromUtf8("  \xC2\xB7  "); // middle dot
    _sceneSummary->setText( ( (writers == 1) ? tr("1 Write node") : tr("%1 Write nodes").arg(writers) ) + separator +
                              ( (projectCount == 1) ? tr("1 project") : tr("%1 projects").arg(projectCount) ) + separator +
                              ( scene.outputDir.isEmpty() ? tr("Output: project paths")
                                                          : tr("Output: %1").arg( QFileInfo(scene.outputDir).fileName() ) ) );
    _sceneSummary->setToolTip( scene.outputDir.isEmpty() ? tr("Output: each project's Write node path. Change it from the scene menu.")
                                                           : tr("Output folder: %1. Change it from the scene menu.").arg( QDir::toNativeSeparators(scene.outputDir) ) );
    _clearOutputDirAction->setEnabled( !scene.outputDir.isEmpty() );

    const QStringList keepSelected = selectedItems();

    _rows.clear();
    _projectTable->setRowCount(0);
    _projectTable->setRowCount( scene.items.size() );
    for (int row = 0; row < scene.items.size(); ++row) {
        const QString& item = scene.items.at(row);
        QTableWidgetItem* projectItem = new QTableWidgetItem;
        projectItem->setData(Qt::UserRole, item);
        _projectTable->setItem(row, kSceneColumnProject, projectItem);

        RowWidgets widgets;
        widgets.row = row;
        // A slim bar, centred in the (thumbnail-high) row.
        QWidget* progressCell = new QWidget;
        QVBoxLayout* progressLayout = new QVBoxLayout(progressCell);
        progressLayout->setContentsMargins(4, 0, 4, 0);
        widgets.progress = new QProgressBar(progressCell);
        widgets.progress->setRange(0, 100);
        widgets.progress->setAlignment(Qt::AlignCenter);
        widgets.progress->setFixedHeight( progressCell->fontMetrics().height() + 4 );
        progressLayout->addStretch();
        progressLayout->addWidget(widgets.progress);
        progressLayout->addStretch();
        _projectTable->setCellWidget(row, kSceneColumnProgress, progressCell);
        QWidget* controls = createRowControls(item);
        _projectTable->setCellWidget(row, kSceneColumnControls, controls);
        widgets.pause = controls->findChild<QPushButton*>( QString::fromUtf8("pause") );
        widgets.render = controls->findChild<QPushButton*>( QString::fromUtf8("render") );
        widgets.stop = controls->findChild<QPushButton*>( QString::fromUtf8("stop") );
        _rows.insert(item, widgets);

        refreshProjectRow(row);
        if ( keepSelected.contains(item) ) {
            _projectTable->selectRow(row);
        }
    }

    refreshKvTable();
    refreshNdiTab();
    refreshButtons();
}

QWidget*
ScenePanel::createRowControls(const QString& item)
{
    QWidget* w = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(w);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(2);

    QPushButton* pause = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_ENABLED),
                                            tr("Pause or resume rendering"), w );
    pause->setObjectName( QString::fromUtf8("pause") );
    pause->setCheckable(true);
    QPushButton* render = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED),
                                             tr("Render this Write node"), w );
    render->setObjectName( QString::fromUtf8("render") );
    QPushButton* stop = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED),
                                           tr("Stop or dequeue this render"), w );
    stop->setObjectName( QString::fromUtf8("stop") );

    pause->setProperty("item", item);
    render->setProperty("item", item);
    stop->setProperty("item", item);

    layout->addWidget(pause);
    layout->addWidget(render);
    layout->addWidget(stop);
    layout->addStretch();

    QObject::connect( pause, SIGNAL(toggled(bool)), this, SLOT(onRowPauseToggled(bool)) );
    QObject::connect( render, SIGNAL(clicked()), this, SLOT(onRowRenderClicked()) );
    QObject::connect( stop, SIGNAL(clicked()), this, SLOT(onRowStopClicked()) );

    return w;
}

void
ScenePanel::refreshProjectRow(int row)
{
    QTableWidgetItem* projectItem = _projectTable->item(row, kSceneColumnProject);

    if (!projectItem) {
        return;
    }

    const QString item = projectItem->data(Qt::UserRole).toString();
    const QString path = ::SceneItem::project(item);
    const QString writer = ::SceneItem::writer(item);
    const QFileInfo fi(path);
    const ::RenderRecord record = _scenes->renderRecord(_currentSceneId, item);

    // Project: name only (path in the tooltip)
    QString name = fi.fileName();
    const QString ext = QString::fromUtf8("." NATRON_PROJECT_FILE_EXT);
    if ( name.endsWith(ext) ) {
        name.chop( ext.size() );
    }
    projectItem->setText( fi.exists() ? name : tr("%1 (missing)").arg(name) );

    // The Write node, in grey under the project name (its output in the tooltip)
    const ProjectInfo& info = projectInfo(path);
    QString writerText = writer.isEmpty() ? tr("(no Write node)") : writer;
    const bool writerMissing = !writer.isEmpty() && fi.exists() && !info.writers.contains(writer);
    if (writerMissing) {
        writerText = tr("%1 (missing)").arg(writer);
    }
    projectItem->setData(PlainItemDelegate::kSecondaryTextRole, writerText);
    if ( !fi.exists() || writerMissing ) {
        projectItem->setForeground( QColor(230, 90, 80) );
    }
    const QString output = info.writerOutputs.value(writer);
    projectItem->setToolTip( tr("%1\nWrite node: %2\n%3").arg( QDir::toNativeSeparators(path) ).arg(writerText)
                             .arg( output.isEmpty() ? tr("No output file") : tr("Output: %1").arg( QDir::toNativeSeparators(output) ) ) );

    // Preview: thumbnail of the last rendered output
    QTableWidgetItem* previewItem = new QTableWidgetItem;
    QPixmap thumb;
    if ( !record.thumbnail.isEmpty() && thumb.load(record.thumbnail) ) {
        previewItem->setData( Qt::DecorationRole, thumb.scaled(kSceneThumbnailWidth, kSceneThumbnailHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation) );
    } else {
        previewItem->setText( record.output.isEmpty() ? tr("no output yet") : tr("no preview") );
    }
    previewItem->setToolTip( record.output.isEmpty() ? tr("Not rendered yet.")
                                                     : tr("Output: %1\nDouble-click to open.").arg( QDir::toNativeSeparators(record.output) ) );
    _projectTable->setItem(row, kSceneColumnPreview, previewItem);

    refreshRowRenderState(item);
}

void
ScenePanel::refreshRowRenderState(const QString& item)
{
    QHash<QString, RowWidgets>::const_iterator it = _rows.find(item);

    if ( it == _rows.end() ) {
        return;
    }

    const RowWidgets& w = it.value();
    const ::RenderRecord record = _scenes->renderRecord(_currentSceneId, item);
    const ::RenderProgress progress = _renderer ? _renderer->progress(_currentSceneId, item) : ::RenderProgress();
    const bool queued = _renderer && _renderer->isQueued(_currentSceneId, item);
    // Today: time only.
    const QString timeFormat = (record.time.date() == QDate::currentDate()) ? QString::fromUtf8("hh:mm") : QString::fromUtf8("yyyy-MM-dd hh:mm");
    const QString separator = QString::fromUtf8(" \xC2\xB7 "); // middle dot

    QString status;
    QString detail; // grey second line
    int percent = 0;

    if (progress.active) {
        percent = int(progress.percent + 0.5);
        if (progress.paused) {
            status = tr("Paused");
        } else if ( progress.node.isEmpty() ) {
            status = tr("Starting...");
        } else {
            status = tr("Rendering");
            if (progress.fps > 0) {
                status += separator + tr("%1 fps").arg(progress.fps, 0, 'f', 1);
            }
            if ( !progress.timeRemaining.isEmpty() ) {
                status += separator + tr("%1 left").arg(progress.timeRemaining);
            }
        }
    } else if (queued) {
        status = tr("Queued");
    } else {
        switch (record.status) {
        case ::RenderRecord::eDone:
            status = tr("Finished %1").arg( record.time.toString(timeFormat) );
            percent = 100;
            break;
        case ::RenderRecord::eFailed:
            status = tr("Failed %1").arg( record.time.toString(timeFormat) );
            break;
        default:
            status = record.message.isEmpty() ? tr("Not rendered") : record.message;
            break;
        }
        if ( !record.message.isEmpty() && (record.status == ::RenderRecord::eDone || record.status == ::RenderRecord::eFailed) ) {
            detail = record.message.section(QLatin1Char('\n'), 0, 0);
        }
    }

    QTableWidgetItem* statusItem = new QTableWidgetItem(status);
    statusItem->setData(PlainItemDelegate::kSecondaryTextRole, detail);
    statusItem->setToolTip( record.message.isEmpty() || progress.active ? status : record.message );
    if ( !progress.active && !queued && (record.status == ::RenderRecord::eFailed) ) {
        statusItem->setForeground( QColor(230, 90, 80) );
    }
    _projectTable->setItem(w.row, kSceneColumnStatus, statusItem);

    w.progress->setValue(percent);
    w.progress->setFormat( tr("%p%") ); // the bar is too small for more

    const bool rendering = progress.active;
    const bool wasBlocked = w.pause->blockSignals(true);
    w.pause->setChecked(rendering && progress.paused);
    w.pause->blockSignals(wasBlocked);
    w.pause->setEnabled( rendering && _renderer->canPause() );
    w.render->setEnabled( _renderer && !rendering && !queued && QFileInfo( ::SceneItem::project(item) ).exists() );
    w.stop->setEnabled(rendering || queued);
}

void
ScenePanel::onRowPauseToggled(bool paused)
{
    const QString item = sender() ? sender()->property("item").toString() : QString();

    if ( !_renderer || !_renderer->isCurrent(_currentSceneId, item) ) {
        return;
    }
    if (paused) {
        _renderer->pause();
    } else {
        _renderer->resume();
    }
}

void
ScenePanel::onRowRenderClicked()
{
    const QString item = sender() ? sender()->property("item").toString() : QString();

    if ( _renderer && !item.isEmpty() && confirmRender() ) {
        _renderer->enqueue( _currentSceneId, QStringList(item) );
    }
}

void
ScenePanel::onRowStopClicked()
{
    const QString item = sender() ? sender()->property("item").toString() : QString();

    if ( _renderer && !item.isEmpty() ) {
        _renderer->cancel(_currentSceneId, item);
    }
}

void
ScenePanel::onRenderProgressChanged(const QString& sceneId,
                                    const QString& item)
{
    if (sceneId == _currentSceneId) {
        refreshRowRenderState(item);
    }
}

void
ScenePanel::refreshKvTable()
{
    ::Scene scene;
    _scenes->scene(_currentSceneId, &scene);

    // key -> projects using it, in order of first appearance
    QStringList keys;
    QHash<QString, QStringList> usedBy;
    QHash<QString, QStringList> components; // key -> node labels
    QHash<QString, QStringList> componentTips; // key -> "project: label (script name)"
    const QStringList projects = scene.projects();

    for (int i = 0; i < projects.size(); ++i) {
        const ProjectInfo& info = projectInfo( projects.at(i) );
        const QString fileName = QFileInfo( projects.at(i) ).fileName();
        for (int k = 0; k < info.stateKeys.size(); ++k) {
            const QString& key = info.stateKeys.at(k);
            if ( !keys.contains(key) ) {
                keys << key;
            }
            usedBy[key] << fileName;
            const QList<ProjectInfo::BoundNode> nodes = info.keyNodes.value(key);
            for (int n = 0; n < nodes.size(); ++n) {
                if ( !components[key].contains(nodes.at(n).label) ) {
                    components[key] << nodes.at(n).label;
                }
                componentTips[key] << ( (nodes.at(n).label == nodes.at(n).name) ? tr("%1: %2").arg(fileName).arg(nodes.at(n).label)
                                        : tr("%1: %2 (%3)").arg(fileName).arg(nodes.at(n).label).arg(nodes.at(n).name) );
            }
        }
    }

    QStringList storeKeys = _state ? _state->keys() : QStringList();
    storeKeys.sort();

    _kvTable->setRowCount(0);
    _kvTable->setRowCount( keys.size() );
    for (int row = 0; row < keys.size(); ++row) {
        const QString& key = keys.at(row);
        const QString used = scene.mappedKey(key);

        // Component: the bound nodes as labelled in the projects (the key if unknown).
        QTableWidgetItem* componentItem = new QTableWidgetItem( components.value(key).isEmpty() ? key
                                                                : components.value(key).join( QString::fromUtf8(", ") ) );
        componentItem->setData(Qt::UserRole, key);
        componentItem->setToolTip( tr("Key: %1").arg(key) +
                                   ( componentTips.value(key).isEmpty() ? QString()
                                     : QString::fromUtf8("\n") + componentTips.value(key).join( QString::fromUtf8("\n") ) ) );
        _kvTable->setItem(row, 0, componentItem);

        // Types the projects bind this key for: only store keys of those are offered.
        QStringList expected;
        for (int i = 0; i < projects.size(); ++i) {
            const QStringList types = boundTypes( projectInfo( projects.at(i) ), key );
            for (int t = 0; t < types.size(); ++t) {
                if ( !expected.contains( types.at(t) ) ) {
                    expected << types.at(t);
                }
            }
        }
        QStringList choices;
        for (int k = 0; k < storeKeys.size(); ++k) {
            if ( expected.isEmpty() || expected.contains( Kv::typeName( Kv::typeOf( _state->get( storeKeys.at(k) ) ) ) ) ) {
                choices << storeKeys.at(k);
            }
        }

        // Editable combo: which store key the scene uses for this key.
        QComboBox* combo = new KeyComboBox(choices);
        combo->setEditText(used);
        combo->setProperty("key", key);
        combo->setProperty("applied", used); // ignore repeated signals for the same edit
        combo->setToolTip( (used == key) ? tr("Reads %1. Select or type another key to override it for this scene.").arg(key)
                                         : tr("Reads %1 instead of %2.").arg(used).arg(key) );
        QObject::connect( combo->lineEdit(), SIGNAL(editingFinished()), this, SLOT(onKeyMappingEdited()) );
        QObject::connect( combo, SIGNAL(activated(int)), this, SLOT(onKeyMappingEdited()) );
        _kvTable->setCellWidget(row, 1, combo);

        _kvTable->setItem( row, 4, new QTableWidgetItem( usedBy.value(key).join( QString::fromUtf8(", ") ) ) );
        refreshKvRow(row, scene);
    }
}

void
ScenePanel::refreshKvRow(int row,
                         const ::Scene& scene)
{
    QTableWidgetItem* keyItem = _kvTable->item(row, 0);

    if (!keyItem) {
        return;
    }

    const QString key = keyItem->data(Qt::UserRole).toString();
    const QString used = scene.mappedKey(key);
    const bool known = _state && _state->has(used);
    const QVariant value = known ? _state->get(used) : QVariant();

    QTableWidgetItem* typeItem = new QTableWidgetItem( known ? KvGui::typeIcon(value) : QIcon(),
                                                       known ? KvGui::typeLabel(value) : QString() );
    _kvTable->setItem(row, 2, typeItem);

    QTableWidgetItem* valueItem = new QTableWidgetItem( known ? Kv::displayText(value) : tr("(missing: set it in the Data panel or via the API)") );
    QString tip = known ? KvGui::tooltip(value) : valueItem->text();
    if (known) {
        valueItem->setIcon( KvGui::typeIcon(value) ); // the Type column is hidden by default
    }

    const QStringList issues = kvIssues(key, scene);
    if ( !issues.isEmpty() ) {
        valueItem->setIcon( style()->standardIcon(QStyle::SP_MessageBoxWarning) );
        valueItem->setForeground( QColor(230, 150, 60) );
        tip = tr("<b>May change the render:</b><br/>%1<br/><br/>%2").arg( issues.join( QString::fromUtf8("<br/>") ) ).arg(tip);
    } else if (!known) {
        valueItem->setForeground( QColor(230, 90, 80) );
    }
    valueItem->setToolTip(tip);
    _kvTable->setItem(row, 3, valueItem);
}

QStringList
ScenePanel::kvIssues(const QString& key,
                     const ::Scene& scene)
{
    QStringList issues;

    if (!_state) {
        return issues;
    }

    const QString used = scene.mappedKey(key);
    if ( !_state->has(used) ) {
        return issues; // shown as missing
    }
    const QVariant value = _state->get(used);

    // Type the projects bind this key for (Text node: text, Read node: image).
    QStringList expected;
    const QStringList projects = scene.projects();
    for (int i = 0; i < projects.size(); ++i) {
        const QStringList types = boundTypes( projectInfo( projects.at(i) ), key );
        for (int t = 0; t < types.size(); ++t) {
            if ( !expected.contains( types.at(t) ) ) {
                expected << types.at(t);
            }
        }
    }
    const QString actual = Kv::typeName( Kv::typeOf(value) );
    for (int t = 0; t < expected.size(); ++t) {
        if ( expected.at(t) != actual ) {
            issues << tr("%1 is bound to a %2 node but %3 is %4: that node is left unchanged.")
                      .arg(key)
                      .arg( expected.at(t) == QString::fromUtf8("image") ? tr("Read (image)") : tr("Text") )
                      .arg(used)
                      .arg( (actual == QString::fromUtf8("image")) ? tr("an image") : tr("text") );
        }
    }

    if ( Kv::typeOf(value) == Kv::eTypeImage ) {
        if ( !Kv::imageInfo(value).exists ) {
            issues << tr("%1: image file not found (%2)").arg(used).arg( Kv::imagePath(value) );
        } else if ( (used != key) && _state->has(key) && (Kv::typeOf( _state->get(key) ) == Kv::eTypeImage) ) {
            const QStringList differences = Kv::imageDifferences(_state->get(key), value);
            for (int d = 0; d < differences.size(); ++d) {
                issues << tr("%1 -> %2: %3").arg(key).arg(used).arg( differences.at(d) );
            }
        }
    }

    return issues;
}

QStringList
ScenePanel::sceneIssues()
{
    ::Scene scene;
    QStringList issues;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return issues;
    }

    QStringList keys;
    const QStringList projects = scene.projects();
    for (int i = 0; i < projects.size(); ++i) {
        const QStringList projectKeys = projectInfo( projects.at(i) ).stateKeys;
        for (int k = 0; k < projectKeys.size(); ++k) {
            if ( !keys.contains( projectKeys.at(k) ) ) {
                keys << projectKeys.at(k);
            }
        }
    }
    for (int k = 0; k < keys.size(); ++k) {
        issues << kvIssues(keys.at(k), scene);
    }

    return issues;
}

bool
ScenePanel::confirmRender()
{
    const QStringList issues = sceneIssues();

    if ( issues.isEmpty() ) {
        return true;
    }

    QMessageBox box(QMessageBox::Warning, tr("Render Scene"),
                    tr("Some values of this scene may change the rendered result:"),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText( QString::fromUtf8("- ") + issues.join( QString::fromUtf8("\n- ") ) + tr("\n\nRender anyway?") );
    box.setDefaultButton(QMessageBox::No);

    return box.exec() == QMessageBox::Yes;
}

void
ScenePanel::onKvCellDoubleClicked(int row,
                                  int /*column*/)
{
    ::Scene scene;
    QTableWidgetItem* keyItem = _kvTable->item(row, 0);

    if ( !keyItem || !_state || !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    const QString used = scene.mappedKey( keyItem->data(Qt::UserRole).toString() );
    if ( _state->has(used) ) {
        KvGui::openImage( _state->get(used) );
    }
}

void
ScenePanel::onStateValueChanged()
{
    // Values only: keep the "Uses key" editors (the user may be typing).
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    for (int row = 0; row < _kvTable->rowCount(); ++row) {
        refreshKvRow(row, scene);
    }
}

void
ScenePanel::onKeyMappingEdited()
{
    QObject* source = sender();
    QComboBox* combo = qobject_cast<QComboBox*>(source);

    if (!combo && source) {
        combo = qobject_cast<QComboBox*>( source->parent() ); // the combo's line edit
    }
    if (!combo) {
        return;
    }

    const QString key = combo->property("key").toString();
    const QString used = combo->currentText().trimmed();

    // editingFinished and activated both fire for one edit, and focus moving
    // to a warning dialog fires editingFinished again: apply each value once.
    if ( used == combo->property("applied").toString() ) {
        return;
    }
    combo->setProperty("applied", used);

    // Rebuilding the table (scenesChanged) deletes this combo: do it later.
    QMetaObject::invokeMethod( this, "applyKeyMapping", Qt::QueuedConnection,
                               Q_ARG(QString, key), Q_ARG(QString, used) );
}

void
ScenePanel::applyKeyMapping(const QString& key,
                            const QString& usedKey)
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    // Refuse a key the store does not have, or of another type (text for an
    // image binding, or the reverse).
    const QString used = usedKey.trimmed();
    if ( _state && !used.isEmpty() && (used != key) && !_state->has(used) ) {
        QMessageBox::warning( this, tr("Uses key"),
                              tr("There is no key %1 in the store.\nPick one from the list, or add it in the Data panel first.").arg(used) );
        refreshKvTable(); // back to the previous key

        return;
    }
    if ( _state && !used.isEmpty() && (used != key) && _state->has(used) ) {
        ::Scene test = scene;
        test.keyMap.insert(key, used);
        const QString actual = Kv::typeName( Kv::typeOf( _state->get(used) ) );
        const QStringList projects = scene.projects();
        for (int i = 0; i < projects.size(); ++i) {
            const QStringList types = boundTypes( projectInfo( projects.at(i) ), key );
            if ( !types.isEmpty() && !types.contains(actual) ) {
                QMessageBox::warning( this, tr("Uses key"),
                                      tr("%1 is bound to %2 in the projects, but %3 is %4.\nPick a key of the same type.")
                                      .arg(key)
                                      .arg( types.contains( QString::fromUtf8("image") ) ? tr("an image (Read node)") : tr("text (Text node)") )
                                      .arg(used)
                                      .arg( (actual == QString::fromUtf8("image")) ? tr("an image") : tr("text") ) );
                refreshKvTable(); // back to the previous key

                return;
            }
        }
    }

    _scenes->setKeyMapping(_currentSceneId, key, used);
}

void
ScenePanel::onChooseOutputDirClicked()
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, tr("Output Folder for \"%1\"").arg(scene.name), scene.outputDir);
    if ( !dir.isEmpty() ) {
        _scenes->setOutputDir(_currentSceneId, dir);
    }
}

void
ScenePanel::onClearOutputDirClicked()
{
    _scenes->setOutputDir( _currentSceneId, QString() );
}

void
ScenePanel::refreshButtons()
{
    const bool scene = hasCurrentScene();
    const bool hasProjects = scene && !currentItems().isEmpty();
    const bool busy = _renderer && _renderer->isBusy();

    const bool hasSelection = scene && !selectedItems().isEmpty();
    _renderButton->setEnabled(_renderer && hasProjects);
    _renderButton->setToolTip( hasSelection ? tr("Render selected Write nodes")
                                            : tr("Render all Write nodes") );
    _stopButton->setEnabled(busy);

    if (!_renderer) {
        _renderStatus->setText( tr("Rendering is not available.") );
    } else if (busy) {
        ::Scene renderingScene;
        _scenes->scene(_renderer->currentSceneId(), &renderingScene);
        _renderStatus->setText( tr("Rendering %1 of %2 in scene \"%3\" (%4 more queued)...")
                                .arg( ::SceneItem::writer( _renderer->currentItem() ) )
                                .arg( QFileInfo( ::SceneItem::project( _renderer->currentItem() ) ).fileName() )
                                .arg(renderingScene.name)
                                .arg( _renderer->queuedCount() ) );
    } else {
        _renderStatus->clear();
    }
    _renderStatus->setVisible( !_renderStatus->text().isEmpty() ); // only while rendering or on a problem
}

void
ScenePanel::addProjectsToCurrentScene(const QStringList& projects)
{
    if ( !hasCurrentScene() ) {
        return;
    }
    _scenes->addItems( _currentSceneId, chooseItems(projects) );
}

QStringList
ScenePanel::chooseItems(const QStringList& projects)
{
    QStringList all;
    bool choice = false; // a project has several Write nodes

    for (int i = 0; i < projects.size(); ++i) {
        const QStringList items = ::SceneItem::allOf( projects.at(i) );
        all << items;
        choice = choice || items.size() > 1;
    }
    if (!choice) {
        return all;
    }

    // Projects with their Write nodes, all checked: uncheck the ones the
    // scene does not render.
    QDialog dialog(this);
    dialog.setWindowTitle( tr("Add Write Nodes to Scene") );
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget( new QLabel(tr("Write nodes to add to the scene (each gets its own row and NDI source):"), &dialog) );
    QTreeWidget* tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    QStringList headers;
    headers << tr("Write node") << tr("Output");
    tree->setHeaderLabels(headers);
    tree->setColumnWidth(0, 220);
    layout->addWidget(tree);

    for (int i = 0; i < projects.size(); ++i) {
        const QString path = QFileInfo( projects.at(i) ).absoluteFilePath();
        const ProjectInfo info = readProjectInfo(path);
        QTreeWidgetItem* projectItem = new QTreeWidgetItem( tree, QStringList( QFileInfo(path).fileName() ) );
        projectItem->setToolTip( 0, QDir::toNativeSeparators(path) );
        projectItem->setFlags( projectItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsTristate );
        const QStringList items = ::SceneItem::allOf(path);
        for (int w = 0; w < items.size(); ++w) {
            const QString writer = ::SceneItem::writer( items.at(w) );
            QStringList columns;
            columns << ( writer.isEmpty() ? tr("(no Write node)") : writer )
                    << QDir::toNativeSeparators( info.writerOutputs.value(writer) );
            QTreeWidgetItem* writerItem = new QTreeWidgetItem(projectItem, columns);
            writerItem->setFlags( writerItem->flags() | Qt::ItemIsUserCheckable );
            writerItem->setCheckState(0, Qt::Checked);
            writerItem->setData( 0, Qt::UserRole, items.at(w) );
        }
        projectItem->setExpanded(true);
    }

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    layout->addWidget(buttons);
    QObject::connect( buttons, SIGNAL(accepted()), &dialog, SLOT(accept()) );
    QObject::connect( buttons, SIGNAL(rejected()), &dialog, SLOT(reject()) );
    dialog.resize(560, 360);

    QStringList chosen;
    if (dialog.exec() != QDialog::Accepted) {
        return chosen;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* projectItem = tree->topLevelItem(i);
        for (int w = 0; w < projectItem->childCount(); ++w) {
            if (projectItem->child(w)->checkState(0) == Qt::Checked) {
                chosen << projectItem->child(w)->data(0, Qt::UserRole).toString();
            }
        }
    }

    return chosen;
}

void
ScenePanel::onAddProjectsClicked()
{
    const QString filter = tr("%1 projects (*.%2)").arg( QString::fromUtf8(NATRON_APPLICATION_NAME) ).arg( QString::fromUtf8(NATRON_PROJECT_FILE_EXT) );
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Projects to Scene"), QString(), filter);

    if ( !files.isEmpty() ) {
        addProjectsToCurrentScene(files);
    }
}

void
ScenePanel::onRemoveProjectsClicked()
{
    _scenes->removeItems( _currentSceneId, selectedItems() );
}

void
ScenePanel::onOpenInEditorClicked()
{
    const QStringList items = selectedItems();
    QStringList opened;

    for (int i = 0; i < items.size(); ++i) {
        const QString project = ::SceneItem::project( items.at(i) );
        if ( !opened.contains(project) ) {
            opened << project;
            appPTR->openProjectWindow(project);
        }
    }
}

void
ScenePanel::onRenderClicked()
{
    const QStringList selected = selectedItems();

    if ( _renderer && confirmRender() ) {
        _renderer->enqueue( _currentSceneId, selected.isEmpty() ? currentItems() : selected );
    }
}

void
ScenePanel::onRenderSelectedClicked()
{
    if ( _renderer && confirmRender() ) {
        _renderer->enqueue( _currentSceneId, selectedItems() );
    }
}

void
ScenePanel::onStopClicked()
{
    if (_renderer) {
        _renderer->stop();
    }
}

void
ScenePanel::onProjectSelectionChanged()
{
    refreshButtons();
}

void
ScenePanel::onProjectCellDoubleClicked(int row,
                                       int column)
{
    QTableWidgetItem* projectItem = _projectTable->item(row, kSceneColumnProject);

    if (!projectItem) {
        return;
    }
    const QString item = projectItem->data(Qt::UserRole).toString();

    if (column == kSceneColumnPreview) {
        const QString output = _scenes->renderRecord(_currentSceneId, item).output;
        if ( !output.isEmpty() && QFileInfo(output).exists() ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile(output) );
        }

        return;
    }

    appPTR->openProjectWindow( ::SceneItem::project(item) );
}

void
ScenePanel::onProjectTableContextMenu(const QPoint& pos)
{
    const int row = _projectTable->indexAt(pos).row();
    QMenu menu(this);

    if (row >= 0) {
        // Right-clicking outside the selection acts on the clicked row only.
        if ( !_projectTable->selectionModel()->isRowSelected( row, QModelIndex() ) ) {
            _projectTable->selectRow(row);
        }
        QAction* render = menu.addAction( tr("Render"), this, SLOT(onRenderSelectedClicked()) );
        render->setEnabled(_renderer != 0);
        menu.addAction( tr("Open in Editor"), this, SLOT(onOpenInEditorClicked()) );
        menu.addSeparator();
        menu.addAction( tr("Remove from Scene"), this, SLOT(onRemoveProjectsClicked()) );
    } else {
        menu.addAction( tr("Add Projects..."), this, SLOT(onAddProjectsClicked()) );
    }
    menu.exec( _projectTable->viewport()->mapToGlobal(pos) );
}

void
ScenePanel::onRenderRecordChanged(const QString& sceneId,
                                  const QString& item)
{
    if (sceneId != _currentSceneId) {
        return;
    }

    const int row = currentItems().indexOf(item);

    if (row < 0) {
        return;
    }

    // A finished render may follow changes to the project file.
    const ::RenderRecord record = _scenes->renderRecord(sceneId, item);
    if (record.status == ::RenderRecord::eDone || record.status == ::RenderRecord::eFailed) {
        _infos.remove( ::SceneItem::project(item) );
    }

    if ( row < _projectTable->rowCount() ) {
        refreshProjectRow(row);
    }
    refreshKvTable();
}

void
ScenePanel::onRendererStatusChanged()
{
    // Queue / pause changes affect every row's controls.
    for (QHash<QString, RowWidgets>::const_iterator it = _rows.constBegin(); it != _rows.constEnd(); ++it) {
        refreshRowRenderState( it.key() );
    }
    refreshButtons();
}

void
ScenePanel::onStateChanged()
{
    if ( !hasCurrentScene() ) {
        return;
    }

    refreshKvTable();
}

// ----- NDI output -----

QWidget*
ScenePanel::createNdiTab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    KvGui::setPanelLayout(layout);

    // [NDI on] [Alpha] Source: [mode]          runtime
    QHBoxLayout* settings = new QHBoxLayout;
    _ndiEnabledCheck = new QCheckBox(tr("Enabled"), tab);
    _ndiEnabledCheck->setToolTip( tr("Send each Write node of this scene as an NDI source.") );
    _ndiAlphaCheck = new QCheckBox(tr("Alpha"), tab);
    _ndiAlphaCheck->setToolTip( tr("Send with transparency. Requires an output format with alpha, "
                                   "such as PNG, ProRes 4444 or QuickTime Animation.") );
    _ndiModeCombo = new QComboBox(tab);
    _ndiModeCombo->addItem( tr("Rendered playout") );
    _ndiModeCombo->addItem( tr("Live render") );
    // Each entry explains itself in the list; the combo shows the selected one's.
    _ndiModeCombo->setItemData( 0, tr("Plays the last render at its frame rate."), Qt::ToolTipRole );
    _ndiModeCombo->setItemData( 1, tr("Sends frames as they render; requires an image-sequence output. "
                                      "Restarts when a value in use changes."), Qt::ToolTipRole );
    settings->addWidget(_ndiEnabledCheck);
    settings->addWidget(_ndiAlphaCheck);
    settings->addWidget( new QLabel(tr("Source:"), tab) );
    settings->addWidget(_ndiModeCombo);
    settings->addStretch();
    _ndiRuntimeLabel = KvGui::secondaryLabel(tab);
    settings->addWidget(_ndiRuntimeLabel);
    layout->addLayout(settings);

    // Cue / Continue (text: not obvious as icons), then the all-sources transport as icons.
    QHBoxLayout* transport = new QHBoxLayout;
    _ndiCueButton = new QPushButton(tr("Cue"), tab);
    _ndiCueButton->setToolTip( tr("Play each source from the start to its pause point") );
    _ndiContinueButton = new QPushButton(tr("Continue"), tab);
    _ndiContinueButton->setToolTip( tr("Resume each source past its pause point") );
    _ndiPlayAllButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED),
                                           tr("Play all sources"), tab );
    _ndiPauseAllButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_DISABLED),
                                            tr("Pause all sources"), tab );
    _ndiStopAllButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED),
                                           tr("Stop all sources"), tab );
    _ndiReplayAllButton = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_FIRST_FRAME, NATRON_ENUM::NATRON_PIXMAP_PLAYER_FIRST_FRAME),
                                             tr("Replay all sources from the start, ignoring pause points"), tab );
    transport->addWidget(_ndiCueButton);
    transport->addWidget(_ndiContinueButton);
    transport->addSpacing(12);
    transport->addWidget(_ndiPlayAllButton);
    transport->addWidget(_ndiPauseAllButton);
    transport->addWidget(_ndiStopAllButton);
    transport->addWidget(_ndiReplayAllButton);
    transport->addStretch();
    layout->addLayout(transport);

    _ndiTable = new QTableWidget(0, 6, tab);
    QStringList headers;
    headers << tr("NDI source") << tr("State") << tr("Position") << tr("Controls") << tr("Pause at") << tr("Receivers");
    _ndiTable->setHorizontalHeaderLabels(headers);
    KvGui::styleTable(_ndiTable);
    _ndiTable->verticalHeader()->setDefaultSectionSize( _ndiTable->fontMetrics().height() + 14 ); // fits the controls
    _ndiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _ndiTable->setColumnWidth(0, 200);
    _ndiTable->setColumnWidth(1, 150);
    _ndiTable->setColumnWidth(2, 170);
    _ndiTable->setColumnWidth(3, 130);
    _ndiTable->setColumnWidth(4, 100);
    QList<int> ndiHidden;
    ndiHidden << 5; // Receivers
    TableColumnMenu::install(_ndiTable, QString::fromUtf8("dashboard/columns/ndi"), ndiHidden);
    EmptyViewHint::install( _ndiTable, QString() ); // text set by refreshNdiTab()
    layout->addWidget(_ndiTable);

    _ndiRefreshTimer.setInterval(100);
    _ndiRefreshTimer.setSingleShot(true);

    QObject::connect( _ndiEnabledCheck, SIGNAL(toggled(bool)), this, SLOT(onNdiEnabledToggled(bool)) );
    QObject::connect( _ndiAlphaCheck, SIGNAL(toggled(bool)), this, SLOT(onNdiAlphaToggled(bool)) );
    QObject::connect( _ndiModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onNdiModeChanged(int)) );
    QObject::connect( _ndiCueButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( _ndiContinueButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( _ndiPlayAllButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( _ndiPauseAllButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( _ndiStopAllButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( _ndiReplayAllButton, SIGNAL(clicked()), this, SLOT(onNdiSceneAction()) );
    QObject::connect( &_ndiRefreshTimer, SIGNAL(timeout()), this, SLOT(refreshNdiRows()) );

    return tab;
}

void
ScenePanel::setNdiManager(::NdiManager* ndi)
{
    _ndi = ndi;
    if (_ndi) {
        QObject::connect( _ndi, SIGNAL(channelsChanged(QString)), this, SLOT(onNdiChannelsChanged(QString)) );
        QObject::connect( _ndi, SIGNAL(channelStatusChanged(QString,QString)), this, SLOT(onNdiChannelStatusChanged(QString,QString)) );
    }
    refreshNdiTab();
}

void
ScenePanel::refreshNdiTab()
{
    ::Scene scene;
    const bool hasScene = _scenes->scene(_currentSceneId, &scene);

    QString why;
    const bool runtime = ::Ndi::load(&why);
    _ndiRuntimeLabel->setText( runtime ? tr("NDI runtime: %1").arg( QFileInfo( ::Ndi::libraryPath() ).fileName() )
                                       : tr("NDI unavailable: %1").arg(why) );

    const bool blocked = _ndiEnabledCheck->blockSignals(true);
    _ndiAlphaCheck->blockSignals(true);
    _ndiModeCombo->blockSignals(true);
    _ndiEnabledCheck->setChecked(hasScene && scene.ndiEnabled);
    _ndiAlphaCheck->setChecked(hasScene && scene.ndiAlpha);
    _ndiModeCombo->setCurrentIndex( (hasScene && scene.ndiLive) ? 1 : 0 );
    _ndiModeCombo->setToolTip( _ndiModeCombo->itemData(_ndiModeCombo->currentIndex(), Qt::ToolTipRole).toString() );
    _ndiEnabledCheck->blockSignals(blocked);
    _ndiAlphaCheck->blockSignals(blocked);
    _ndiModeCombo->blockSignals(blocked);

    _ndiEnabledCheck->setEnabled(hasScene && _ndi);
    _ndiAlphaCheck->setEnabled(hasScene && _ndi);
    _ndiModeCombo->setEnabled(hasScene && _ndi);
    const bool active = hasScene && _ndi && scene.ndiEnabled;
    _ndiCueButton->setEnabled(active);
    _ndiContinueButton->setEnabled(active);
    _ndiPlayAllButton->setEnabled(active);
    _ndiPauseAllButton->setEnabled(active);
    _ndiStopAllButton->setEnabled(active);
    _ndiReplayAllButton->setEnabled(active);

    // One row per item / NDI source.
    _ndiRows.clear();
    _ndiTable->setRowCount(0);
    EmptyViewHint::install( _ndiTable, !hasScene ? tr("Select a scene.")
                            : !_ndi ? tr("NDI is not available.")
                            : !scene.ndiEnabled ? tr("Enable NDI output to list this scene's sources.")
                            : tr("This scene has no Write nodes.") );
    if (!active) {
        return;
    }
    _ndiTable->setRowCount( scene.items.size() );
    for (int row = 0; row < scene.items.size(); ++row) {
        const QString& item = scene.items.at(row);
        NdiRow r;
        r.row = row;

        QTableWidgetItem* nameItem = new QTableWidgetItem( ::NdiManager::sourceName(scene, item) );
        nameItem->setToolTip( tr("%1\nWrite node: %2").arg( QDir::toNativeSeparators( ::SceneItem::project(item) ) ).arg( ::SceneItem::writer(item) ) );
        _ndiTable->setItem(row, 0, nameItem);

        r.position = new QProgressBar;
        r.position->setRange(0, 1000);
        r.position->setAlignment(Qt::AlignCenter);
        _ndiTable->setCellWidget(row, 2, r.position);

        QWidget* controls = new QWidget;
        QHBoxLayout* cl = new QHBoxLayout(controls);
        cl->setContentsMargins(2, 0, 2, 0);
        cl->setSpacing(2);
        r.play = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED), tr("Play, continuing past the pause point"), controls );
        r.pause = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_ENABLED), tr("Pause"), controls );
        r.stop = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED), tr("Stop and clear the output"), controls );
        r.replay = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_FIRST_FRAME, NATRON_ENUM::NATRON_PIXMAP_PLAYER_FIRST_FRAME), tr("Replay from the start"), controls );
        r.loop = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_LOOP_MODE, NATRON_ENUM::NATRON_PIXMAP_PLAYER_LOOP_MODE), tr("Loop"), controls );
        r.loop->setCheckable(true);
        QPushButton* buttons[] = { r.play, r.pause, r.stop, r.replay, r.loop };
        const char* names[] = { "play", "pause", "stop", "replay", "loop" };
        for (int b = 0; b < 5; ++b) {
            buttons[b]->setObjectName( QString::fromUtf8(names[b]) );
            buttons[b]->setProperty("item", item);
            cl->addWidget(buttons[b]);
        }
        cl->addStretch();
        QObject::connect( r.play, SIGNAL(clicked()), this, SLOT(onNdiRowAction()) );
        QObject::connect( r.pause, SIGNAL(clicked()), this, SLOT(onNdiRowAction()) );
        QObject::connect( r.stop, SIGNAL(clicked()), this, SLOT(onNdiRowAction()) );
        QObject::connect( r.replay, SIGNAL(clicked()), this, SLOT(onNdiRowAction()) );
        QObject::connect( r.loop, SIGNAL(toggled(bool)), this, SLOT(onNdiLoopToggled(bool)) );
        _ndiTable->setCellWidget(row, 3, controls);

        r.pauseAt = new QDoubleSpinBox;
        r.pauseAt->setRange(-1, 24 * 3600);
        r.pauseAt->setDecimals(2);
        r.pauseAt->setSingleStep(0.5);
        r.pauseAt->setSpecialValueText( tr("none") ); // shown at the minimum (-1)
        r.pauseAt->setSuffix( tr(" s") );
        r.pauseAt->setToolTip( tr("Time, in seconds, at which Cue and Play stop. \"none\" disables the pause point.") );
        r.pauseAt->setValue( scene.pauseAt(item) );
        r.pauseAt->setProperty("item", item);
        r.pauseAt->setKeyboardTracking(false);
        QObject::connect( r.pauseAt, SIGNAL(valueChanged(double)), this, SLOT(onNdiPauseAtChanged(double)) );
        _ndiTable->setCellWidget(row, 4, r.pauseAt);

        _ndiRows.insert(item, r);
    }
    refreshNdiRows();
}

QString
ScenePanel::formatTime(double seconds)
{
    const int minutes = int(seconds / 60);
    const double rest = seconds - minutes * 60;

    return QString::fromUtf8("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(rest, 4, 'f', 1, QLatin1Char('0'));
}

void
ScenePanel::refreshNdiRows()
{
    if (!_ndi) {
        return;
    }

    for (QHash<QString, NdiRow>::const_iterator it = _ndiRows.constBegin(); it != _ndiRows.constEnd(); ++it) {
        const NdiRow& r = it.value();
        ::PlayoutChannel* c = _ndi->channel( _currentSceneId, it.key() );
        if (!c) {
            continue;
        }
        const ::PlayoutChannel::Status st = c->status();

        QString state = ::PlayoutChannel::stateName(st.state);
        if ( !st.message.isEmpty() ) {
            state += QString::fromUtf8(" - ") + st.message;
        }
        QTableWidgetItem* stateItem = new QTableWidgetItem(state);
        stateItem->setToolTip(state);
        if (st.state == ::PlayoutChannel::eError) {
            stateItem->setForeground( QColor(230, 90, 80) );
        } else if (st.state == ::PlayoutChannel::ePlaying || st.state == ::PlayoutChannel::eLive) {
            stateItem->setForeground( QColor(110, 200, 110) );
        }
        _ndiTable->setItem(r.row, 1, stateItem);

        const double duration = st.duration();
        r.position->setValue( duration > 0 ? int(1000.0 * qMin(1.0, st.seconds() / duration)) : 0 );
        r.position->setFormat( (st.state == ::PlayoutChannel::eLive) ? tr("live %1").arg( formatTime( st.seconds() ) )
                                                                     : tr("%1 / %2").arg( formatTime( st.seconds() ) ).arg( formatTime(duration) ) );

        const bool live = st.state == ::PlayoutChannel::eLive;
        const bool playable = !live && duration > 0;
        r.play->setEnabled(playable && st.state != ::PlayoutChannel::ePlaying);
        r.pause->setEnabled(st.state == ::PlayoutChannel::ePlaying);
        r.stop->setEnabled(!live && st.state != ::PlayoutChannel::eStopped);
        r.replay->setEnabled(playable);
        const bool blocked = r.loop->blockSignals(true);
        r.loop->setChecked(st.loop);
        r.loop->blockSignals(blocked);

        _ndiTable->setItem( r.row, 5, new QTableWidgetItem( st.connections < 0 ? tr("-") : QString::number(st.connections) ) );
    }
}

void
ScenePanel::onNdiChannelsChanged(const QString& sceneId)
{
    if (sceneId == _currentSceneId) {
        refreshNdiTab();
    }
}

void
ScenePanel::onNdiChannelStatusChanged(const QString& sceneId,
                                      const QString& /*item*/)
{
    // Channels report every frame: refresh the rows at most 10 times/s.
    if ( (sceneId == _currentSceneId) && !_ndiRefreshTimer.isActive() ) {
        _ndiRefreshTimer.start();
    }
}

void
ScenePanel::onNdiEnabledToggled(bool enabled)
{
    _scenes->setNdiEnabled(_currentSceneId, enabled);
}

void
ScenePanel::onNdiAlphaToggled(bool alpha)
{
    _scenes->setNdiAlpha(_currentSceneId, alpha);
}

void
ScenePanel::onNdiModeChanged(int index)
{
    _ndiModeCombo->setToolTip( _ndiModeCombo->itemData(index, Qt::ToolTipRole).toString() );
    _scenes->setNdiLive(_currentSceneId, index == 1);
}

void
ScenePanel::onNdiSceneAction()
{
    if (!_ndi) {
        return;
    }

    QObject* s = sender();
    if (s == _ndiCueButton) {
        _ndi->cueAll(_currentSceneId);
    } else if (s == _ndiContinueButton) {
        _ndi->continueAll(_currentSceneId);
    } else if (s == _ndiPlayAllButton) {
        _ndi->playAll(_currentSceneId);
    } else if (s == _ndiPauseAllButton) {
        _ndi->pauseAll(_currentSceneId);
    } else if (s == _ndiStopAllButton) {
        _ndi->stopAll(_currentSceneId);
    } else if (s == _ndiReplayAllButton) {
        _ndi->replayAll(_currentSceneId);
    }
}

void
ScenePanel::onNdiRowAction()
{
    QObject* s = sender();
    ::PlayoutChannel* c = (_ndi && s) ? _ndi->channel( _currentSceneId, s->property("item").toString() ) : 0;

    if (!c) {
        return;
    }

    const QString action = s->objectName();
    if ( action == QString::fromUtf8("play") ) {
        c->play();
    } else if ( action == QString::fromUtf8("pause") ) {
        c->pause();
    } else if ( action == QString::fromUtf8("stop") ) {
        c->stop();
    } else if ( action == QString::fromUtf8("replay") ) {
        c->replay();
    }
}

void
ScenePanel::onNdiLoopToggled(bool loop)
{
    QObject* s = sender();

    if (s) {
        _scenes->setNdiLoop( _currentSceneId, s->property("item").toString(), loop );
    }
}

void
ScenePanel::onNdiPauseAtChanged(double seconds)
{
    QObject* s = sender();

    if (s) {
        _scenes->setNdiPauseAt( _currentSceneId, s->property("item").toString(), (seconds < 0) ? -1.0 : seconds );
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_ScenePanel.cpp"
