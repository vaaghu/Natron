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
#include <QWidget>
#include <QString>
#include <QStringList>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

class QCloseEvent;
class QComboBox;
class QModelIndex;
class QEvent;
class QShowEvent;
class QSplitter;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
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
 * Left: recent projects, with New/Open buttons (each opens a project window).
 * Right: the key/value pairs of the state store, editable.
 * Closing it quits the application (after asking to save open projects).
 **/
class DashboardWindow
    : public QWidget
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
    void onRecentSelectionChanged();

    void onStoreValueChanged(const QString& key);
    void onStoreKeysChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    void onTableItemDoubleClicked(QTableWidgetItem* item);
    void onValueIconClicked(const QModelIndex& index);
    void onNewTypeChanged(int index);
    void onNewValueChooseImage();
    void onTableSelectionChanged();
    void onNewKeyTextChanged(const QString& text);
    void onAddClicked();
    void onRemoveClicked();
    void onServerStatusChanged();

protected:

    virtual void closeEvent(QCloseEvent* e) OVERRIDE;
    virtual void changeEvent(QEvent* e) OVERRIDE;
    virtual void showEvent(QShowEvent* e) OVERRIDE;

private:

    QWidget* createProjectsPanel();
    QWidget* createDataPanel();

    QStringList selectedRecentProjects() const;

    void rebuildTable();
    int findRow(const QString& key) const;
    void setRowValue(int row, const QVariant& value);


    ::StateStore* _store;
    ::HttpServer* _server;
    ::SceneStore* _sceneStore;
    ::SceneRenderer* _sceneRenderer;

    QListWidget* _recentList;
    QPushButton* _openRecentButton;
    QPushButton* _addToSceneButton;
    ScenePanel* _scenePanel;
    QSplitter* _mainSplitter;
    bool _initialSplitDone;

    QLabel* _serverStatusLabel;
    QTableWidget* _table;
    QComboBox* _newTypeCombo;
    QLineEdit* _newKeyEdit;
    HoverLineEdit* _newValueEdit;
    QPushButton* _addButton;
    QPushButton* _removeButton;

    // Set while the table is filled from the store, so the resulting
    // itemChanged signals are not written back to the store.
    bool _updatingTable;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_DashboardWindow_h
