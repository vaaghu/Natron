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

#ifndef Gui_ScenePanel_h
#define Gui_ScenePanel_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Custom/scene/ProjectInfo.h"
#include "Custom/scene/SceneStore.h"

class QAction;
class QFileSystemWatcher;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPoint;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class NdiManager; // Custom/ndi/NdiManager.h
class SceneRenderer; // Custom/scene/SceneRenderer.h
class SceneStore; // Custom/scene/SceneStore.h
class StateStore; // Custom/state/StateStore.h

NATRON_NAMESPACE_ENTER

/**
 * @brief Dashboard panel managing scenes: named groups of Write nodes of
 * projects ("items", see SceneItem) rendered together with the current state
 * store values.
 * It is made of separate parts the owner lays out (the panel itself stays
 * hidden): the scene list, the opened scene (one row per Write node with its
 * last render status and output thumbnail), the KVs of the scene, and its
 * NDI output.
 **/
class ScenePanel
    : public QWidget
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    ScenePanel(::SceneStore* scenes,
               ::SceneRenderer* renderer,
               ::StateStore* state,
               QWidget* parent = 0);

    virtual ~ScenePanel();

    // Parts to show (reparented by the owner, e.g. into dock panels).
    QWidget* sceneListPart() const;
    QWidget* scenePart() const;
    QWidget* kvPart() const;
    QWidget* ndiPart() const;

    bool hasCurrentScene() const;

    // Adds Write nodes of projects to the opened scene (asks which ones when
    // a project has several).
    void addProjectsToCurrentScene(const QStringList& projects);

    // Re-reads the projects of the opened scene (bindings may have changed).
    void refresh();

    // NDI outputs of the scenes (optional).
    void setNdiManager(::NdiManager* ndi);

Q_SIGNALS:

    void currentSceneChanged();

private Q_SLOTS:

    void onNewSceneClicked();
    void onRenameSceneClicked();
    void onDeleteSceneClicked();
    void onSceneSelectionChanged();
    void onSceneListContextMenu(const QPoint& pos);

    void onAddProjectsClicked();
    void onRemoveProjectsClicked();
    void onOpenInEditorClicked();
    void onRenderClicked(); // the selected Write nodes, or all
    void onRenderSelectedClicked();
    void onStopClicked();
    void onProjectSelectionChanged();
    void onProjectCellDoubleClicked(int row, int column);
    void onProjectTableContextMenu(const QPoint& pos);

    // Per-row render controls (the item is the sender's "item" property).
    void onRowPauseToggled(bool paused);
    void onRowRenderClicked();
    void onRowStopClicked();

    void onScenesChanged();
    void onRenderRecordChanged(const QString& sceneId, const QString& item);
    void onRendererStatusChanged();
    void onRenderProgressChanged(const QString& sceneId, const QString& item);
    void onStateChanged();

    // A project file of the opened scene changed (saved from the editor):
    // re-read it, then refresh the scene (coalesced by _projectReloadTimer).
    void onProjectFileChanged(const QString& path);
    void onProjectReloadTimeout();

    // Projects open in an editor: picks up unsaved label and binding changes.
    void onOpenProjectsTimer();

    void onStateValueChanged();
    void onKeyMappingEdited();
    void onKvCellDoubleClicked(int row, int column);
    void applyKeyMapping(const QString& key, const QString& usedKey);
    void onChooseOutputDirClicked();
    void onClearOutputDirClicked();

    void refreshNdiRows();
    void onNdiChannelsChanged(const QString& sceneId);
    void onNdiChannelStatusChanged(const QString& sceneId, const QString& item);
    void onNdiEnabledToggled(bool enabled);
    void onNdiAlphaToggled(bool alpha);
    void onNdiModeChanged(int index);
    void onNdiSceneAction();
    void onNdiRowAction();
    void onNdiLoopToggled(bool loop);
    void onNdiPauseAtChanged(double seconds);

private:

    QWidget* createSceneList();
    QWidget* createSceneDetail();
    QWidget* createNdiTab(QWidget* parent);
    void refreshNdiTab();
    static QString formatTime(double seconds);

    void refreshSceneList();
    void showScene();
    void refreshProjectRow(int row);
    void refreshRowRenderState(const QString& item);
    QWidget* createRowControls(const QString& item);
    void refreshKvTable();
    void refreshKvRow(int row, const ::Scene& scene);

    // Problems of a key in the scene: type mismatch, missing image file,
    // image size/aspect/format changed by the key renaming.
    QStringList kvIssues(const QString& key, const ::Scene& scene);
    QStringList sceneIssues();
    // Asks before rendering a scene with issues. True to go ahead.
    bool confirmRender();
    void refreshButtons();

    QStringList currentItems() const;
    QStringList selectedItems() const;
    // Items of the projects to add: all their Write nodes, or the ones the
    // user picks when a project has several. Empty if cancelled.
    QStringList chooseItems(const QStringList& projects);
    const ProjectInfo& projectInfo(const QString& project);
    QString valueText(const QString& key) const;

    ::SceneStore* _scenes;
    ::SceneRenderer* _renderer;
    ::StateStore* _state;

    QString _currentSceneId;
    QHash<QString, ProjectInfo> _infos; // per project, read from the .ntp
    QFileSystemWatcher* _projectWatcher; // the opened scene's project files
    QTimer _projectReloadTimer;
    QTimer _openProjectsTimer;
    QHash<QString, QString> _openProjectStates; // project -> its bindings in the editor (empty: not open)
    QHash<QString, bool> _unsavedBindings; // project -> its bindings in the editor differ from the file

    // Widgets of an item row (created once per showScene()).
    struct RowWidgets
    {
        int row;
        QProgressBar* progress;
        QPushButton* pause;
        QPushButton* render;
        QPushButton* stop;
    };
    QHash<QString, RowWidgets> _rows; // item -> row widgets

    QWidget* _sceneListPart;
    QWidget* _kvPart;
    QWidget* _ndiPart;

    QListWidget* _sceneList;
    // Shortcuts of the scene list and of the opened scene, also in their menus.
    QAction* _newSceneAction;    // Ctrl+N
    QAction* _renameSceneAction; // F2
    QAction* _deleteSceneAction; // Delete
    QAction* _renderAction;      // Ctrl+R: the selected Write nodes, or all
    QAction* _openInEditorAction; // Ctrl+E
    QAction* _removeProjectsAction; // Delete

    QStackedWidget* _detailStack;
    QLabel* _sceneTitle;
    QLabel* _sceneSummary;
    QAction* _clearOutputDirAction;
    QPushButton* _renderButton;
    QPushButton* _stopButton;
    QTableWidget* _projectTable;
    QTableWidget* _kvTable;
    QLabel* _renderStatus;

    // NDI output tab
    ::NdiManager* _ndi;
    QCheckBox* _ndiEnabledCheck;
    QCheckBox* _ndiAlphaCheck;
    QComboBox* _ndiModeCombo;
    QLabel* _ndiRuntimeLabel;
    QPushButton* _ndiCueButton;
    QPushButton* _ndiContinueButton;
    QPushButton* _ndiPlayAllButton;
    QPushButton* _ndiPauseAllButton;
    QPushButton* _ndiStopAllButton;
    QPushButton* _ndiReplayAllButton;
    QTableWidget* _ndiTable;
    QTimer _ndiRefreshTimer;
    struct NdiRow
    {
        int row;
        QProgressBar* position;
        QPushButton* play;
        QPushButton* pause;
        QPushButton* stop;
        QPushButton* replay;
        QPushButton* loop;
        QDoubleSpinBox* pauseAt;
    };
    QHash<QString, NdiRow> _ndiRows; // item -> row widgets
};

NATRON_NAMESPACE_EXIT

#endif // Gui_ScenePanel_h
