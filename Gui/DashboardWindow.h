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

#ifndef Gui_DashboardWindow_h
#define Gui_DashboardWindow_h

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <QStringList>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QCloseEvent;
class QComboBox;
class QDockWidget;
class QModelIndex;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QAction;
class QListWidgetItem;
class QPoint;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QUndoStack;
class StateStore; // Custom/state/StateStore.h
class HttpServer; // Custom/server/HttpServer.h
class SceneRenderer; // Custom/scene/SceneRenderer.h
class SceneStore; // Custom/scene/SceneStore.h
class NdiManager; // Custom/ndi/NdiManager.h

NATRON_NAMESPACE_ENTER

class ScenePanel;
class HoverLineEdit;

/**
 * @brief Start window shown instead of an empty project window.
 * Dock panels, like the editor's panes (drag to re-dock or stack as tabs,
 * float, close and reopen from the Window menu; the layout is kept):
 *   Projects  recent projects, with New/Open buttons (each opens a project window)
 *   Scenes    the scene list
 *   Scene     the opened scene's Write nodes and renders
 *   KVs       the keys the opened scene uses
 *   NDI       the opened scene's NDI output
 *   Data      the key/value pairs of the state store, editable
 * Closing it quits the application (after asking to save open projects).
 **/
class DashboardWindow
    : public QMainWindow
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    DashboardWindow(::StateStore* store,
                    ::HttpServer* server,
                    ::SceneStore* scenes,
                    ::SceneRenderer* renderer,
                    QWidget* parent = 0);

    virtual ~DashboardWindow();

    // NDI outputs, shown in the scene view.
    void setNdiManager(::NdiManager* ndi);

public Q_SLOTS:

    // Re-reads the recent projects list from the settings.
    void refreshRecentProjects();

private Q_SLOTS:

    void onNewProjectClicked();
    void onOpenProjectClicked();
    void onOpenRecentClicked();
    void onAddToSceneClicked();
    void onRecentItemActivated(QListWidgetItem* item);
    void onRecentContextMenu(const QPoint& pos);

    void onStoreValueChanged(const QString& key);
    void onStoreKeysChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    void onTableItemDoubleClicked(QTableWidgetItem* item);
    void onValueIconClicked(const QModelIndex& index);
    void onNewTypeChanged(int index);
    void onNewValueChooseImage();
    void onTableSelectionChanged();
    void onDataContextMenu(const QPoint& pos);
    void onNewKeyTextChanged(const QString& text);
    void onAddClicked();
    void onRemoveClicked();
    void onServerStatusChanged();
    void onResetLayoutClicked();

protected:

    virtual void closeEvent(QCloseEvent* e) OVERRIDE;
    virtual void changeEvent(QEvent* e) OVERRIDE;

private:

    QWidget* createProjectsPanel();
    QWidget* createDataPanel();
    QDockWidget* addPanel(const QString& objectName, const QString& title, QWidget* content);
    void createPanels();
    void saveLayout();

    QStringList selectedRecentProjects() const;

    void rebuildTable();
    int findRow(const QString& key) const;
    void setRowValue(int row, const QVariant& value);


    ::StateStore* _store;
    ::HttpServer* _server;
    ::SceneStore* _sceneStore;
    ::SceneRenderer* _sceneRenderer;

    QListWidget* _recentList;
    ScenePanel* _scenePanel;
    QByteArray _defaultLayout; // dock layout before the saved one is restored

    QLabel* _serverStatusLabel;
    QTableWidget* _table;
    QComboBox* _newTypeCombo;
    QLineEdit* _newKeyEdit;
    HoverLineEdit* _newValueEdit;
    QPushButton* _addButton;
    QAction* _removeAction;
    QUndoStack* _undoStack; // scene and data changes (Edit menu)
    QAction* _newProjectAction;  // Ctrl+Shift+N in the Projects panel
    QAction* _openProjectAction; // Ctrl+O in the Projects panel

    // Set while the table is filled from the store, so the resulting
    // itemChanged signals are not written back to the store.
    bool _updatingTable;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_DashboardWindow_h
