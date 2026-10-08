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
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Custom/scene/ProjectInfo.h"

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class SceneRenderer; // Custom/scene/SceneRenderer.h
class SceneStore; // Custom/scene/SceneStore.h
class StateStore; // Custom/state/StateStore.h

NATRON_NAMESPACE_ENTER

/**
 * @brief Dashboard panel managing scenes: named groups of projects that are
 * rendered together with the current state store values.
 * Left: scene list. Right: the opened scene (its projects with the KVs they
 * use, last render status and output thumbnail; all KVs of the scene).
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

    bool hasCurrentScene() const;

    // Links projects to the opened scene.
    void addProjectsToCurrentScene(const QStringList& projects);

    // Re-reads the projects of the opened scene (bindings may have changed).
    void refresh();

Q_SIGNALS:

    void currentSceneChanged();

private Q_SLOTS:

    void onNewSceneClicked();
    void onRenameSceneClicked();
    void onDeleteSceneClicked();
    void onSceneSelectionChanged();

    void onAddProjectsClicked();
    void onRemoveProjectsClicked();
    void onOpenInEditorClicked();
    void onRenderAllClicked();
    void onRenderSelectedClicked();
    void onStopClicked();
    void onProjectSelectionChanged();
    void onProjectCellDoubleClicked(int row, int column);

    void onScenesChanged();
    void onRenderRecordChanged(const QString& project);
    void onRendererStatusChanged();
    void onStateChanged();

private:

    QWidget* createSceneList();
    QWidget* createSceneDetail();

    void refreshSceneList();
    void showScene();
    void refreshProjectRow(int row);
    void refreshKvTable();
    void refreshButtons();

    QStringList currentProjects() const;
    QStringList selectedProjects() const;
    const ProjectInfo& projectInfo(const QString& project);
    QString valueText(const QString& key) const;

    ::SceneStore* _scenes;
    ::SceneRenderer* _renderer;
    ::StateStore* _state;

    QString _currentSceneId;
    QHash<QString, ProjectInfo> _infos; // per project, read from the .ntp

    QListWidget* _sceneList;
    QPushButton* _renameSceneButton;
    QPushButton* _deleteSceneButton;

    QStackedWidget* _detailStack;
    QLabel* _sceneTitle;
    QPushButton* _removeProjectsButton;
    QPushButton* _openInEditorButton;
    QPushButton* _renderAllButton;
    QPushButton* _renderSelectedButton;
    QPushButton* _stopButton;
    QTableWidget* _projectTable;
    QTableWidget* _kvTable;
    QLabel* _renderStatus;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_ScenePanel_h
