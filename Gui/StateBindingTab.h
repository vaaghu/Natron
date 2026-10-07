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

#ifndef Gui_StateBindingTab_h
#define Gui_StateBindingTab_h

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
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/EngineFwd.h"

class QComboBox;
class QLabel;
class QPushButton;
class StateStore; // Custom/state/StateStore.h

NATRON_NAMESPACE_ENTER

/**
 * @brief "State" tab shown in the properties panel of Text nodes.
 * Binds the node's text parameter to a key of the app's StateStore:
 * whenever the key's value changes, the text parameter is overwritten with it.
 **/
class StateBindingTab
    : public QWidget
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    StateBindingTab(const NodePtr& node,
                    ::StateStore* store,
                    QWidget* parent = 0);

    virtual ~StateBindingTab();

    // Plugin ID of the nodes this tab applies to.
    static bool isSupportedNode(const NodePtr& node);

    QString getBoundKey() const
    {
        return _boundKey;
    }

public Q_SLOTS:

    void setBoundKey(const QString& key);

private Q_SLOTS:

    void onKeyEdited(const QString& key);
    void onUnbindClicked();
    void onStoreValueChanged(const QString& key);
    void onStoreKeysChanged();

private:

    void refreshKeyList();
    void refreshPreview();
    void applyValueToNode();

    NodeWPtr _node;
    ::StateStore* _store;
    QString _boundKey;
    QComboBox* _keyCombo;
    QLabel* _valueLabel;
    QPushButton* _unbindButton;
};

NATRON_NAMESPACE_EXIT

#endif // Gui_StateBindingTab_h
