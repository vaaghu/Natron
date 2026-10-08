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
#include <QColor>
#include <QComboBox>
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
#include <QIcon>
#include <QPixmap>
#include <QProgressBar>
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
#define kSceneColumnProgress 2
#define kSceneColumnStatus 3
#define kSceneColumnControls 4
#define kSceneColumnRemaining 5
#define kSceneColumnFrames 6
#define kSceneColumnCount 7

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
    , _rows()
    , _sceneList(0)
    , _renameSceneButton(0)
    , _deleteSceneButton(0)
    , _detailStack(0)
    , _sceneTitle(0)
    , _outputDirLabel(0)
    , _clearOutputDirButton(0)
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
    QObject::connect( _scenes, SIGNAL(renderRecordChanged(QString,QString)), this, SLOT(onRenderRecordChanged(QString,QString)) );
    if (_renderer) {
        QObject::connect( _renderer, SIGNAL(statusChanged()), this, SLOT(onRendererStatusChanged()) );
        QObject::connect( _renderer, SIGNAL(progressChanged(QString,QString)), this, SLOT(onRenderProgressChanged(QString,QString)) );
    }
    if (_state) {
        QObject::connect( _state, SIGNAL(valueChanged(QString)), this, SLOT(onStateValueChanged()) );
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

    QHBoxLayout* outputRow = new QHBoxLayout;
    outputRow->addWidget( new QLabel(tr("Output folder:"), detail) );
    _outputDirLabel = new QLabel(detail);
    _outputDirLabel->setTextFormat(Qt::PlainText);
    _outputDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputRow->addWidget(_outputDirLabel, 1);
    QPushButton* browseOutput = new QPushButton(tr("Choose..."), detail);
    browseOutput->setToolTip( tr("Write this scene's renders to a folder of their own (same file names as the projects' Write nodes), "
                                 "so the same project rendered in another scene is not overwritten.") );
    _clearOutputDirButton = new QPushButton(tr("Use project paths"), detail);
    _clearOutputDirButton->setToolTip( tr("Write where each project's Write node says.") );
    outputRow->addWidget(browseOutput);
    outputRow->addWidget(_clearOutputDirButton);
    layout->addLayout(outputRow);
    QObject::connect( browseOutput, SIGNAL(clicked()), this, SLOT(onChooseOutputDirClicked()) );
    QObject::connect( _clearOutputDirButton, SIGNAL(clicked()), this, SLOT(onClearOutputDirClicked()) );

    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* addButton = new QPushButton(tr("Add Projects..."), detail);
    addButton->setToolTip( tr("Link project files to this scene. You can also select a recent project and use \"Add to Scene\".") );
    _removeProjectsButton = new QPushButton(tr("Remove"), detail);
    _removeProjectsButton->setToolTip( tr("Unlink the selected projects from the scene (files are not touched).") );
    _openInEditorButton = new QPushButton(tr("Open in Editor"), detail);
    _renderAllButton = new QPushButton(tr("Render All"), detail);
    _renderAllButton->setToolTip( tr("Render every project of the scene with the current KV values, one after the other.") );
    _renderSelectedButton = new QPushButton(tr("Render Selected"), detail);
    _stopButton = new QPushButton(tr("Stop All"), detail);
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

    _projectTable = new QTableWidget(0, kSceneColumnCount, split);
    QStringList headers;
    headers << tr("Preview") << tr("Project") << tr("Progress") << tr("Status") << tr("Controls") << tr("Time remaining") << tr("Frame range");
    _projectTable->setHorizontalHeaderLabels(headers);
    _projectTable->verticalHeader()->setVisible(false);
    _projectTable->verticalHeader()->setDefaultSectionSize(kSceneThumbnailHeight + 8);
    _projectTable->setIconSize( QSize(kSceneThumbnailWidth, kSceneThumbnailHeight) );
    _projectTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _projectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _projectTable->setWordWrap(true);
    _projectTable->horizontalHeader()->setStretchLastSection(true);
    _projectTable->setColumnWidth(kSceneColumnPreview, kSceneThumbnailWidth + 12);
    _projectTable->setColumnWidth(kSceneColumnProject, 160);
    _projectTable->setColumnWidth(kSceneColumnProgress, 110);
    _projectTable->setColumnWidth(kSceneColumnStatus, 190);
    _projectTable->setColumnWidth(kSceneColumnControls, 100);
    _projectTable->setColumnWidth(kSceneColumnRemaining, 120);
    _projectTable->setToolTip( tr("Double-click a preview to open the rendered output, or a project to open it in the editor.") );
    split->addWidget(_projectTable);

    QWidget* kvBox = new QWidget(split);
    QVBoxLayout* kvLayout = new QVBoxLayout(kvBox);
    kvLayout->setContentsMargins(0, 4, 0, 0);
    kvLayout->addWidget( new QLabel(tr("KVs used in this scene"), kvBox) );
    _kvTable = new QTableWidget(0, 4, kvBox);
    QStringList kvHeaders;
    kvHeaders << tr("Key in projects") << tr("Uses key") << tr("Value") << tr("Used by");
    _kvTable->setHorizontalHeaderLabels(kvHeaders);
    _kvTable->verticalHeader()->setVisible(false);
    _kvTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _kvTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _kvTable->horizontalHeader()->setStretchLastSection(true);
    _kvTable->setColumnWidth(0, 160);
    _kvTable->setColumnWidth(1, 180);
    _kvTable->setColumnWidth(2, 220);
    _kvTable->setToolTip( tr("\"Uses key\": which store key this scene reads for each key bound in its projects "
                             "(e.g. PlayerName1 -> PlayerName2). The project files are not changed. "
                             "Edit the values in the Data panel; then re-render the scene.") );
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
    _outputDirLabel->setText( scene.outputDir.isEmpty() ? tr("(each project's own Write node path)") : QDir::toNativeSeparators(scene.outputDir) );
    _clearOutputDirButton->setEnabled( !scene.outputDir.isEmpty() );

    const QStringList keepSelected = selectedProjects();

    _rows.clear();
    _projectTable->setRowCount(0);
    _projectTable->setRowCount( scene.projects.size() );
    for (int row = 0; row < scene.projects.size(); ++row) {
        const QString& path = scene.projects.at(row);
        QTableWidgetItem* projectItem = new QTableWidgetItem;
        projectItem->setData(Qt::UserRole, path);
        _projectTable->setItem(row, kSceneColumnProject, projectItem);

        RowWidgets widgets;
        widgets.row = row;
        widgets.progress = new QProgressBar;
        widgets.progress->setRange(0, 100);
        widgets.progress->setAlignment(Qt::AlignCenter);
        _projectTable->setCellWidget(row, kSceneColumnProgress, widgets.progress);
        QWidget* controls = createRowControls(path);
        _projectTable->setCellWidget(row, kSceneColumnControls, controls);
        widgets.pause = controls->findChild<QPushButton*>( QString::fromUtf8("pause") );
        widgets.render = controls->findChild<QPushButton*>( QString::fromUtf8("render") );
        widgets.stop = controls->findChild<QPushButton*>( QString::fromUtf8("stop") );
        _rows.insert(path, widgets);

        refreshProjectRow(row);
        if ( keepSelected.contains(path) ) {
            _projectTable->selectRow(row);
        }
    }

    refreshKvTable();
    refreshButtons();
}

QWidget*
ScenePanel::createRowControls(const QString& project)
{
    QWidget* w = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(w);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(2);

    QPushButton* pause = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PAUSE_ENABLED),
                                            tr("Pause / resume the render."), w );
    pause->setObjectName( QString::fromUtf8("pause") );
    pause->setCheckable(true);
    QPushButton* render = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_PLAY_DISABLED),
                                             tr("Render this project (again) with the current KV values."), w );
    render->setObjectName( QString::fromUtf8("render") );
    QPushButton* stop = makeControlButton( playerIcon(NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED, NATRON_ENUM::NATRON_PIXMAP_PLAYER_STOP_DISABLED),
                                           tr("Stop this render, or remove it from the queue."), w );
    stop->setObjectName( QString::fromUtf8("stop") );

    pause->setProperty("project", project);
    render->setProperty("project", project);
    stop->setProperty("project", project);

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

    const QString path = projectItem->data(Qt::UserRole).toString();
    const QFileInfo fi(path);
    const ::RenderRecord record = _scenes->renderRecord(_currentSceneId, path);

    // Project: name only (path in the tooltip)
    QString name = fi.fileName();
    const QString ext = QString::fromUtf8("." NATRON_PROJECT_FILE_EXT);
    if ( name.endsWith(ext) ) {
        name.chop( ext.size() );
    }
    projectItem->setText( fi.exists() ? name : tr("%1 (missing)").arg(name) );
    projectItem->setToolTip( QDir::toNativeSeparators(path) );
    if ( !fi.exists() ) {
        projectItem->setForeground( QColor(230, 90, 80) );
    }

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

    refreshRowRenderState(path);
}

void
ScenePanel::refreshRowRenderState(const QString& project)
{
    QHash<QString, RowWidgets>::const_iterator it = _rows.find(project);

    if ( it == _rows.end() ) {
        return;
    }

    const RowWidgets& w = it.value();
    const ::RenderRecord record = _scenes->renderRecord(_currentSceneId, project);
    const ::RenderProgress progress = _renderer ? _renderer->progress(_currentSceneId, project) : ::RenderProgress();
    const bool queued = _renderer && _renderer->isQueued(_currentSceneId, project);
    const QString timeFormat = QString::fromUtf8("yyyy-MM-dd hh:mm");

    QString status;
    QString remaining = tr("N/A");
    QString frames = record.frameRange.isEmpty() ? tr("N/A") : record.frameRange;
    int percent = 0;

    if (progress.active) {
        percent = int(progress.percent + 0.5);
        if (progress.paused) {
            status = tr("Paused");
            remaining = tr("Paused");
        } else if ( progress.node.isEmpty() ) {
            status = tr("Starting...");
        } else {
            status = (progress.fps > 0) ? tr("Rendering %1 (%2 fps)").arg(progress.node).arg(progress.fps, 0, 'f', 1)
                                        : tr("Rendering %1").arg(progress.node);
            remaining = progress.timeRemaining.isEmpty() ? tr("...") : progress.timeRemaining;
        }
        if ( !progress.frameRange().isEmpty() ) {
            frames = progress.frameRange();
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
            status += QString::fromUtf8("\n") + record.message.section(QLatin1Char('\n'), 0, 0);
        }
    }

    QTableWidgetItem* statusItem = new QTableWidgetItem(status);
    statusItem->setToolTip( record.message.isEmpty() || progress.active ? status : record.message );
    if ( !progress.active && !queued && (record.status == ::RenderRecord::eFailed) ) {
        statusItem->setForeground( QColor(230, 90, 80) );
    }
    _projectTable->setItem(w.row, kSceneColumnStatus, statusItem);
    _projectTable->setItem( w.row, kSceneColumnRemaining, new QTableWidgetItem(remaining) );
    _projectTable->setItem( w.row, kSceneColumnFrames, new QTableWidgetItem(frames) );

    w.progress->setValue(percent);
    w.progress->setFormat( progress.active && !progress.node.isEmpty() ? tr("%1: %p%").arg(progress.node) : tr("%p%") );

    const bool rendering = progress.active;
    const bool wasBlocked = w.pause->blockSignals(true);
    w.pause->setChecked(rendering && progress.paused);
    w.pause->blockSignals(wasBlocked);
    w.pause->setEnabled( rendering && _renderer->canPause() );
    w.render->setEnabled( _renderer && !rendering && !queued && QFileInfo(project).exists() );
    w.stop->setEnabled(rendering || queued);
}

void
ScenePanel::onRowPauseToggled(bool paused)
{
    const QString project = sender() ? sender()->property("project").toString() : QString();

    if ( !_renderer || !_renderer->isCurrent(_currentSceneId, project) ) {
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
    const QString project = sender() ? sender()->property("project").toString() : QString();

    if ( _renderer && !project.isEmpty() ) {
        _renderer->enqueue( _currentSceneId, QStringList(project) );
    }
}

void
ScenePanel::onRowStopClicked()
{
    const QString project = sender() ? sender()->property("project").toString() : QString();

    if ( _renderer && !project.isEmpty() ) {
        _renderer->cancel(_currentSceneId, project);
    }
}

void
ScenePanel::onRenderProgressChanged(const QString& sceneId,
                                    const QString& project)
{
    if (sceneId == _currentSceneId) {
        refreshRowRenderState(project);
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
    const QStringList projects = scene.projects;

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

    QStringList storeKeys = _state ? _state->keys() : QStringList();
    storeKeys.sort();

    _kvTable->setRowCount(0);
    _kvTable->setRowCount( keys.size() );
    for (int row = 0; row < keys.size(); ++row) {
        const QString& key = keys.at(row);
        const QString used = scene.mappedKey(key);
        const bool known = _state && _state->has(used);

        _kvTable->setItem( row, 0, new QTableWidgetItem(key) );

        // Editable combo: which store key the scene uses for this key.
        QComboBox* combo = new QComboBox;
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItems(storeKeys);
        combo->setEditText(used);
        combo->setProperty("key", key);
        combo->setToolTip( (used == key) ? tr("Uses %1 as bound in the projects. Pick or type another key to rename it for this scene.").arg(key)
                                         : tr("This scene uses %1 instead of %2.").arg(used).arg(key) );
        QObject::connect( combo->lineEdit(), SIGNAL(editingFinished()), this, SLOT(onKeyMappingEdited()) );
        QObject::connect( combo, SIGNAL(activated(int)), this, SLOT(onKeyMappingEdited()) );
        _kvTable->setCellWidget(row, 1, combo);

        QTableWidgetItem* valueItem = new QTableWidgetItem( known ? valueText(used) : tr("(missing: set it in the Data panel or via the API)") );
        if (!known) {
            valueItem->setForeground( QColor(230, 90, 80) );
        }
        valueItem->setToolTip( valueItem->text() );
        _kvTable->setItem(row, 2, valueItem);
        _kvTable->setItem( row, 3, new QTableWidgetItem( usedBy.value(key).join( QString::fromUtf8(", ") ) ) );
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
        QTableWidgetItem* keyItem = _kvTable->item(row, 0);
        QTableWidgetItem* valueItem = _kvTable->item(row, 2);
        if (!keyItem || !valueItem) {
            continue;
        }
        const QString used = scene.mappedKey( keyItem->text() );
        const bool known = _state && _state->has(used);
        valueItem->setText( known ? valueText(used) : tr("(missing: set it in the Data panel or via the API)") );
        valueItem->setForeground( known ? _kvTable->palette().text() : QBrush( QColor(230, 90, 80) ) );
        valueItem->setToolTip( valueItem->text() );
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

    // Rebuilding the table (scenesChanged) deletes this combo: do it later.
    QMetaObject::invokeMethod( this, "applyKeyMapping", Qt::QueuedConnection,
                               Q_ARG(QString, key), Q_ARG(QString, used) );
}

void
ScenePanel::applyKeyMapping(const QString& key,
                            const QString& usedKey)
{
    if ( hasCurrentScene() ) {
        _scenes->setKeyMapping(_currentSceneId, key, usedKey);
    }
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
        ::Scene renderingScene;
        _scenes->scene(_renderer->currentSceneId(), &renderingScene);
        _renderStatus->setText( tr("Rendering %1 of scene \"%2\" (%3 more queued)...")
                                .arg( QFileInfo( _renderer->currentProject() ).fileName() )
                                .arg(renderingScene.name)
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
        _renderer->enqueue( _currentSceneId, currentProjects() );
    }
}

void
ScenePanel::onRenderSelectedClicked()
{
    if (_renderer) {
        _renderer->enqueue( _currentSceneId, selectedProjects() );
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
        const QString output = _scenes->renderRecord(_currentSceneId, path).output;
        if ( !output.isEmpty() && QFileInfo(output).exists() ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile(output) );
        }

        return;
    }

    appPTR->openProjectWindow(path);
}

void
ScenePanel::onRenderRecordChanged(const QString& sceneId,
                                  const QString& project)
{
    if (sceneId != _currentSceneId) {
        return;
    }

    const QStringList projects = currentProjects();
    const int row = projects.indexOf(project);

    if (row < 0) {
        return;
    }

    // A finished render may follow changes to the project file.
    const ::RenderRecord record = _scenes->renderRecord(sceneId, project);
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

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_ScenePanel.cpp"
