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
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/GuiApplicationManager.h" // appPTR

#include "Custom/scene/SceneRenderer.h"
#include "Custom/scene/SceneStore.h"
#include "Custom/state/StateStore.h"

#define kSceneColumnPreview 0
#define kSceneColumnProject 1
#define kSceneColumnKeys 2
#define kSceneColumnStatus 3

#define kSceneThumbnailWidth 128
#define kSceneThumbnailHeight 72

NATRON_NAMESPACE_ENTER

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
    , _sceneList(0)
    , _renameSceneButton(0)
    , _deleteSceneButton(0)
    , _detailStack(0)
    , _sceneTitle(0)
    , _removeProjectsButton(0)
    , _openInEditorButton(0)
    , _renderAllButton(0)
    , _renderSelectedButton(0)
    , _stopButton(0)
    , _projectTable(0)
    , _kvTable(0)
    , _renderStatus(0)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget( createSceneList() );
    splitter->addWidget( createSceneDetail() );
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    layout->addWidget(splitter);

    QObject::connect( _scenes, SIGNAL(scenesChanged()), this, SLOT(onScenesChanged()) );
    QObject::connect( _scenes, SIGNAL(renderRecordChanged(QString)), this, SLOT(onRenderRecordChanged(QString)) );
    if (_renderer) {
        QObject::connect( _renderer, SIGNAL(statusChanged()), this, SLOT(onRendererStatusChanged()) );
    }
    if (_state) {
        QObject::connect( _state, SIGNAL(valueChanged(QString)), this, SLOT(onStateChanged()) );
        QObject::connect( _state, SIGNAL(keysChanged()), this, SLOT(onStateChanged()) );
    }

    refreshSceneList();
    showScene();
}

ScenePanel::~ScenePanel()
{
}

QWidget*
ScenePanel::createSceneList()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget( new QLabel(tr("Scenes"), w) );

    _sceneList = new QListWidget(w);
    _sceneList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(_sceneList);

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* newButton = new QPushButton(tr("New"), w);
    newButton->setToolTip( tr("Create a scene: a named group of projects rendered together.") );
    _renameSceneButton = new QPushButton(tr("Rename"), w);
    _deleteSceneButton = new QPushButton(tr("Delete"), w);
    _deleteSceneButton->setToolTip( tr("Delete the scene. The project files are not touched.") );
    buttons->addWidget(newButton);
    buttons->addWidget(_renameSceneButton);
    buttons->addWidget(_deleteSceneButton);
    layout->addLayout(buttons);

    QObject::connect( newButton, SIGNAL(clicked()), this, SLOT(onNewSceneClicked()) );
    QObject::connect( _renameSceneButton, SIGNAL(clicked()), this, SLOT(onRenameSceneClicked()) );
    QObject::connect( _deleteSceneButton, SIGNAL(clicked()), this, SLOT(onDeleteSceneClicked()) );
    QObject::connect( _sceneList, SIGNAL(itemSelectionChanged()), this, SLOT(onSceneSelectionChanged()) );

    return w;
}

QWidget*
ScenePanel::createSceneDetail()
{
    _detailStack = new QStackedWidget(this);

    QLabel* empty = new QLabel(tr("Create a scene, or select one to see its projects."), _detailStack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    _detailStack->addWidget(empty);

    QWidget* detail = new QWidget(_detailStack);
    QVBoxLayout* layout = new QVBoxLayout(detail);
    layout->setContentsMargins(0, 0, 0, 0);

    _sceneTitle = new QLabel(detail);
    QFont titleFont = _sceneTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    _sceneTitle->setFont(titleFont);
    layout->addWidget(_sceneTitle);

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* addButton = new QPushButton(tr("Add Projects..."), detail);
    addButton->setToolTip( tr("Link project files to this scene. You can also select a recent project and use \"Add to Scene\".") );
    _removeProjectsButton = new QPushButton(tr("Remove"), detail);
    _removeProjectsButton->setToolTip( tr("Unlink the selected projects from the scene (files are not touched).") );
    _openInEditorButton = new QPushButton(tr("Open in Editor"), detail);
    _renderAllButton = new QPushButton(tr("Render All"), detail);
    _renderAllButton->setToolTip( tr("Render every project of the scene with the current KV values, one after the other.") );
    _renderSelectedButton = new QPushButton(tr("Render Selected"), detail);
    _stopButton = new QPushButton(tr("Stop"), detail);
    _stopButton->setToolTip( tr("Stop the current render and clear the queue.") );
    buttons->addWidget(addButton);
    buttons->addWidget(_removeProjectsButton);
    buttons->addWidget(_openInEditorButton);
    buttons->addStretch();
    buttons->addWidget(_renderAllButton);
    buttons->addWidget(_renderSelectedButton);
    buttons->addWidget(_stopButton);
    layout->addLayout(buttons);

    QSplitter* split = new QSplitter(Qt::Vertical, detail);

    _projectTable = new QTableWidget(0, 4, split);
    QStringList headers;
    headers << tr("Preview") << tr("Project") << tr("KVs used") << tr("Last render");
    _projectTable->setHorizontalHeaderLabels(headers);
    _projectTable->verticalHeader()->setVisible(false);
    _projectTable->verticalHeader()->setDefaultSectionSize(kSceneThumbnailHeight + 8);
    _projectTable->setIconSize( QSize(kSceneThumbnailWidth, kSceneThumbnailHeight) );
    _projectTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _projectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _projectTable->setWordWrap(true);
    _projectTable->horizontalHeader()->setStretchLastSection(true);
    _projectTable->setColumnWidth(kSceneColumnPreview, kSceneThumbnailWidth + 12);
    _projectTable->setColumnWidth(kSceneColumnProject, 220);
    _projectTable->setColumnWidth(kSceneColumnKeys, 220);
    _projectTable->setToolTip( tr("Double-click a preview to open the rendered output, or a project to open it in the editor.") );
    split->addWidget(_projectTable);

    QWidget* kvBox = new QWidget(split);
    QVBoxLayout* kvLayout = new QVBoxLayout(kvBox);
    kvLayout->setContentsMargins(0, 4, 0, 0);
    kvLayout->addWidget( new QLabel(tr("KVs used in this scene"), kvBox) );
    _kvTable = new QTableWidget(0, 3, kvBox);
    QStringList kvHeaders;
    kvHeaders << tr("Key") << tr("Value") << tr("Used by");
    _kvTable->setHorizontalHeaderLabels(kvHeaders);
    _kvTable->verticalHeader()->setVisible(false);
    _kvTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _kvTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _kvTable->horizontalHeader()->setStretchLastSection(true);
    _kvTable->setColumnWidth(0, 160);
    _kvTable->setColumnWidth(1, 220);
    _kvTable->setToolTip( tr("Edit the values in the Data panel; then re-render the scene.") );
    kvLayout->addWidget(_kvTable);
    split->addWidget(kvBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    layout->addWidget(split);

    _renderStatus = new QLabel(detail);
    _renderStatus->setWordWrap(true);
    _renderStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(_renderStatus);

    _detailStack->addWidget(detail);

    QObject::connect( addButton, SIGNAL(clicked()), this, SLOT(onAddProjectsClicked()) );
    QObject::connect( _removeProjectsButton, SIGNAL(clicked()), this, SLOT(onRemoveProjectsClicked()) );
    QObject::connect( _openInEditorButton, SIGNAL(clicked()), this, SLOT(onOpenInEditorClicked()) );
    QObject::connect( _renderAllButton, SIGNAL(clicked()), this, SLOT(onRenderAllClicked()) );
    QObject::connect( _renderSelectedButton, SIGNAL(clicked()), this, SLOT(onRenderSelectedClicked()) );
    QObject::connect( _stopButton, SIGNAL(clicked()), this, SLOT(onStopClicked()) );
    QObject::connect( _projectTable, SIGNAL(itemSelectionChanged()), this, SLOT(onProjectSelectionChanged()) );
    QObject::connect( _projectTable, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(onProjectCellDoubleClicked(int,int)) );

    return _detailStack;
}

// ----- Scene list -----

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
        QListWidgetItem* item = new QListWidgetItem( tr("%1  (%2)").arg( scenes.at(i).name ).arg( scenes.at(i).projects.size() ) );
        item->setData(Qt::UserRole, scenes.at(i).id);
        _sceneList->addItem(item);
        if (scenes.at(i).id == _currentSceneId) {
            item->setSelected(true);
            _sceneList->setCurrentItem(item);
        }
    }

    _sceneList->blockSignals(wasBlocked);

    _renameSceneButton->setEnabled( hasCurrentScene() );
    _deleteSceneButton->setEnabled( hasCurrentScene() );
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
    _renameSceneButton->setEnabled( hasCurrentScene() );
    _deleteSceneButton->setEnabled( hasCurrentScene() );
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
ScenePanel::currentProjects() const
{
    ::Scene scene;

    if ( !_scenes->scene(_currentSceneId, &scene) ) {
        return QStringList();
    }

    return scene.projects;
}

QStringList
ScenePanel::selectedProjects() const
{
    QStringList projects;
    QList<QTableWidgetItem*> selected = _projectTable->selectedItems();

    for (int i = 0; i < selected.size(); ++i) {
        QTableWidgetItem* item = _projectTable->item(selected.at(i)->row(), kSceneColumnProject);
        const QString path = item ? item->data(Qt::UserRole).toString() : QString();
        if ( !path.isEmpty() && !projects.contains(path) ) {
            projects << path;
        }
    }

    return projects;
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
        refreshButtons();

        return;
    }

    _detailStack->setCurrentIndex(1);
    _sceneTitle->setText( tr("%1 - %2 project(s)").arg(scene.name).arg( scene.projects.size() ) );

    const QStringList keepSelected = selectedProjects();

    _projectTable->setRowCount(0);
    _projectTable->setRowCount( scene.projects.size() );
    for (int row = 0; row < scene.projects.size(); ++row) {
        QTableWidgetItem* projectItem = new QTableWidgetItem;
        projectItem->setData( Qt::UserRole, scene.projects.at(row) );
        _projectTable->setItem(row, kSceneColumnProject, projectItem);
        refreshProjectRow(row);
        if ( keepSelected.contains( scene.projects.at(row) ) ) {
            _projectTable->selectRow(row);
        }
    }

    refreshKvTable();
    refreshButtons();
}

void
ScenePanel::refreshProjectRow(int row)
{
    QTableWidgetItem* projectItem = _projectTable->item(row, kSceneColumnProject);

    if (!projectItem) {
        return;
    }

    const QString path = projectItem->data(Qt::UserRole).toString();
    const QFileInfo fi(path);
    const bool exists = fi.exists();
    const ProjectInfo& info = projectInfo(path);
    const ::RenderRecord record = _scenes->renderRecord(path);

    // Project
    projectItem->setText( tr("%1\n%2").arg( fi.fileName() ).arg( QDir::toNativeSeparators( fi.absolutePath() ) ) );
    projectItem->setToolTip( QDir::toNativeSeparators(path) );

    // Preview: thumbnail of the last rendered output
    QTableWidgetItem* previewItem = new QTableWidgetItem;
    QPixmap thumb;
    if ( !record.thumbnail.isEmpty() && thumb.load(record.thumbnail) ) {
        previewItem->setData( Qt::DecorationRole, thumb.scaled(kSceneThumbnailWidth, kSceneThumbnailHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation) );
    } else {
        previewItem->setText( record.output.isEmpty() ? tr("no output yet") : tr("no preview") );
    }
    previewItem->setToolTip( record.output.isEmpty() ? tr("Render the project to get a preview.")
                                                     : tr("Output: %1\nDouble-click to open it.").arg( QDir::toNativeSeparators(record.output) ) );
    _projectTable->setItem(row, kSceneColumnPreview, previewItem);

    // KVs
    QStringList kvLines;
    if (!exists) {
        kvLines << tr("(file not found)");
    } else if (!info.ok) {
        kvLines << tr("(cannot read project: %1)").arg(info.error);
    } else if ( info.stateKeys.isEmpty() ) {
        kvLines << tr("(no bound KVs)");
    } else {
        for (int i = 0; i < info.stateKeys.size(); ++i) {
            const QString key = info.stateKeys.at(i);
            const bool known = _state && _state->has(key);
            kvLines << ( known ? tr("%1 = %2").arg(key).arg( valueText(key) ) : tr("%1 = (missing)").arg(key) );
        }
    }
    QTableWidgetItem* keysItem = new QTableWidgetItem( kvLines.join( QString::fromUtf8("\n") ) );
    keysItem->setToolTip( keysItem->text() );
    _projectTable->setItem(row, kSceneColumnKeys, keysItem);

    // Last render
    QString status;
    switch (record.status) {
    case ::RenderRecord::eQueued:
        status = tr("Queued");
        break;
    case ::RenderRecord::eRendering:
        status = tr("Rendering...");
        break;
    case ::RenderRecord::eDone:
        status = tr("Done %1").arg( record.time.toString( QString::fromUtf8("yyyy-MM-dd hh:mm") ) );
        break;
    case ::RenderRecord::eFailed:
        status = tr("Failed %1").arg( record.time.toString( QString::fromUtf8("yyyy-MM-dd hh:mm") ) );
        break;
    case ::RenderRecord::eNone:
        status = record.message.isEmpty() ? tr("Not rendered") : record.message;
        break;
    }
    if ( !record.message.isEmpty() && (record.status == ::RenderRecord::eDone || record.status == ::RenderRecord::eFailed) ) {
        status += QString::fromUtf8("\n") + record.message.section(QLatin1Char('\n'), 0, 0);
    }
    QTableWidgetItem* statusItem = new QTableWidgetItem(status);
    statusItem->setToolTip( record.message.isEmpty() ? status : record.message );
    if (record.status == ::RenderRecord::eFailed) {
        statusItem->setForeground( QColor(230, 90, 80) );
    }
    _projectTable->setItem(row, kSceneColumnStatus, statusItem);
}

void
ScenePanel::refreshKvTable()
{
    // key -> projects using it, in order of first appearance
    QStringList keys;
    QHash<QString, QStringList> usedBy;
    const QStringList projects = currentProjects();

    for (int i = 0; i < projects.size(); ++i) {
        const ProjectInfo& info = projectInfo( projects.at(i) );
        for (int k = 0; k < info.stateKeys.size(); ++k) {
            const QString& key = info.stateKeys.at(k);
            if ( !keys.contains(key) ) {
                keys << key;
            }
            usedBy[key] << QFileInfo( projects.at(i) ).fileName();
        }
    }

    _kvTable->setRowCount(0);
    _kvTable->setRowCount( keys.size() );
    for (int row = 0; row < keys.size(); ++row) {
        const QString& key = keys.at(row);
        const bool known = _state && _state->has(key);
        _kvTable->setItem( row, 0, new QTableWidgetItem(key) );
        QTableWidgetItem* valueItem = new QTableWidgetItem( known ? valueText(key) : tr("(missing: set it in the Data panel or via the API)") );
        if (!known) {
            valueItem->setForeground( QColor(230, 90, 80) );
        }
        valueItem->setToolTip( valueItem->text() );
        _kvTable->setItem(row, 1, valueItem);
        _kvTable->setItem( row, 2, new QTableWidgetItem( usedBy.value(key).join( QString::fromUtf8(", ") ) ) );
    }
}

void
ScenePanel::refreshButtons()
{
    const bool scene = hasCurrentScene();
    const bool hasProjects = scene && !currentProjects().isEmpty();
    const bool hasSelection = scene && !selectedProjects().isEmpty();
    const bool busy = _renderer && _renderer->isBusy();

    _removeProjectsButton->setEnabled(hasSelection);
    _openInEditorButton->setEnabled(hasSelection);
    _renderAllButton->setEnabled(_renderer && hasProjects);
    _renderSelectedButton->setEnabled(_renderer && hasSelection);
    _stopButton->setEnabled(busy);

    if (!_renderer) {
        _renderStatus->setText( tr("Rendering is not available.") );
    } else if (busy) {
        _renderStatus->setText( tr("Rendering %1 (%2 more queued)...")
                                .arg( QFileInfo( _renderer->currentProject() ).fileName() )
                                .arg( _renderer->queuedCount() ) );
    } else {
        _renderStatus->setText( tr("Renders use the KV values at the time of the render (renderer: %1).")
                                .arg( QDir::toNativeSeparators( _renderer->rendererPath() ) ) );
    }
}

void
ScenePanel::addProjectsToCurrentScene(const QStringList& projects)
{
    if ( !hasCurrentScene() ) {
        return;
    }
    _scenes->addProjects(_currentSceneId, projects);
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
    _scenes->removeProjects( _currentSceneId, selectedProjects() );
}

void
ScenePanel::onOpenInEditorClicked()
{
    const QStringList projects = selectedProjects();

    for (int i = 0; i < projects.size(); ++i) {
        appPTR->openProjectWindow( projects.at(i) );
    }
}

void
ScenePanel::onRenderAllClicked()
{
    if (_renderer) {
        _renderer->enqueue( currentProjects() );
    }
}

void
ScenePanel::onRenderSelectedClicked()
{
    if (_renderer) {
        _renderer->enqueue( selectedProjects() );
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
    const QString path = projectItem->data(Qt::UserRole).toString();

    if (column == kSceneColumnPreview) {
        const QString output = _scenes->renderRecord(path).output;
        if ( !output.isEmpty() && QFileInfo(output).exists() ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile(output) );
        }

        return;
    }

    appPTR->openProjectWindow(path);
}

void
ScenePanel::onRenderRecordChanged(const QString& project)
{
    const QStringList projects = currentProjects();
    const int row = projects.indexOf(project);

    if (row < 0) {
        return;
    }

    // A finished render may follow changes to the project file.
    const ::RenderRecord record = _scenes->renderRecord(project);
    if (record.status == ::RenderRecord::eDone || record.status == ::RenderRecord::eFailed) {
        _infos.remove(project);
    }

    if ( row < _projectTable->rowCount() ) {
        refreshProjectRow(row);
    }
    refreshKvTable();
}

void
ScenePanel::onRendererStatusChanged()
{
    refreshButtons();
}

void
ScenePanel::onStateChanged()
{
    if ( !hasCurrentScene() ) {
        return;
    }

    for (int row = 0; row < _projectTable->rowCount(); ++row) {
        refreshProjectRow(row);
    }
    refreshKvTable();
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_ScenePanel.cpp"
