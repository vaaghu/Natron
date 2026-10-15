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

#include "SceneShortcutsDialog.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVariantMap>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/KvGuiUtils.h"

#include "Custom/scene/SceneShortcuts.h"
#include "Custom/scene/SceneStore.h"

#define kColumnScene 0
#define kColumnShortcut 1

NATRON_NAMESPACE_ENTER

namespace {
QString
nativeText(const QString& portable)
{
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

// Records one key combination: the keys pressed replace it; Backspace or
// Delete clears it. Its "keys" property holds it (PortableText).
class ShortcutRecorder
    : public QLineEdit
{
public:

    explicit ShortcutRecorder(QWidget* parent)
        : QLineEdit(parent)
    {
        setPlaceholderText( SceneShortcutsDialog::tr("None") );
        setContextMenuPolicy(Qt::NoContextMenu);
        setToolTip( SceneShortcutsDialog::tr("Press the keys to use (e.g. Ctrl+7); Backspace clears") );
    }

    QString keys() const
    {
        return property("keys").toString();
    }

    void setKeys(const QString& portable)
    {
        setProperty("keys", portable);
        setText( nativeText(portable) ); // textChanged: validated
    }

protected:

    virtual bool event(QEvent* e) OVERRIDE
    {
        // The keys come here, not to the application's shortcuts.
        if (e->type() == QEvent::ShortcutOverride) {
            e->accept();

            return true;
        }

        return QLineEdit::event(e);
    }

    virtual void keyPressEvent(QKeyEvent* e) OVERRIDE
    {
        const int key = e->key();
        const Qt::KeyboardModifiers modifiers = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);

        switch (key) {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_Meta:
        case Qt::Key_unknown:
            return; // waiting for the key
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!modifiers) {
                e->ignore(); // focus change, dialog keys
                return;
            }
            break;
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
            if (!modifiers) {
                setKeys( QString() );
                return;
            }
            break;
        default:
            break;
        }
        setKeys( QKeySequence( key | int(modifiers) ).toString(QKeySequence::PortableText) );
    }
};
} // anon namespace

SceneShortcutsDialog::SceneShortcutsDialog(::SceneStore* scenes,
                                           const QHash<QString, QString>& taken,
                                           QWidget* parent)
    : QDialog(parent)
    , _scenes(scenes)
    , _taken(taken)
    , _table(0)
    , _clearButton(0)
    , _defaultsButton(0)
    , _status(0)
    , _buttons(0)
{
    setWindowTitle( tr("Scene Shortcuts") );
    resize(480, 420);
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* intro = KvGui::secondaryLabel(this);
    intro->setWordWrap(true);
    intro->setText( tr("Each shortcut puts all the inputs of its scene on Program at once, from anywhere in the dashboard. "
                       "Preview stays as it is.") );
    layout->addWidget(intro);

    const QList< ::Scene > list = _scenes ? _scenes->scenes() : QList< ::Scene >();
    const QHash<QString, QString> current = SceneShortcuts::load(list);
    _table = new QTableWidget(list.size(), 2, this);
    _table->setHorizontalHeaderLabels( QStringList() << tr("Scene") << tr("Shortcut") );
    _table->verticalHeader()->setVisible(false);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < list.size(); ++row) {
        const ::Scene& scene = list.at(row);
        _sceneIds << scene.id;
        _table->setItem( row, kColumnScene, new QTableWidgetItem( QString::fromUtf8("%1. %2").arg(row + 1).arg(scene.name) ) );
        ShortcutRecorder* edit = new ShortcutRecorder(_table);
        edit->setKeys( current.value(scene.id) );
        QObject::connect( edit, SIGNAL(textChanged(QString)), this, SLOT(validate()) );
        _table->setCellWidget(row, kColumnShortcut, edit);
    }
    _table->resizeColumnToContents(kColumnScene);
    layout->addWidget(_table, 1);

    if ( list.isEmpty() ) {
        QLabel* none = KvGui::secondaryLabel(this);
        none->setText( tr("No scenes yet.") );
        layout->addWidget(none);
    }

    QHBoxLayout* row = new QHBoxLayout;
    _clearButton = new QPushButton(tr("Clear"), this);
    _clearButton->setToolTip( tr("No shortcut for the selected scene") );
    _defaultsButton = new QPushButton(tr("Defaults"), this);
    _defaultsButton->setToolTip( tr("Ctrl+1 to Ctrl+9 for the first nine scenes, none for the others") );
    row->addWidget(_clearButton);
    row->addWidget(_defaultsButton);
    row->addStretch(1);
    layout->addLayout(row);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setStyleSheet( QString::fromUtf8("color: #e05a4f;") );
    layout->addWidget(_status);

    _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, Qt::Horizontal, this);
    layout->addWidget(_buttons);

    QObject::connect( _clearButton, SIGNAL(clicked()), this, SLOT(onClearClicked()) );
    QObject::connect( _defaultsButton, SIGNAL(clicked()), this, SLOT(onDefaultsClicked()) );
    QObject::connect( _buttons, SIGNAL(clicked(QAbstractButton*)), this, SLOT(onButtonClicked(QAbstractButton*)) );
    QObject::connect( _table, SIGNAL(itemSelectionChanged()), this, SLOT(validate()) );
    validate();
}

QLineEdit*
SceneShortcutsDialog::recorder(int row) const
{
    return qobject_cast<QLineEdit*>( _table->cellWidget(row, kColumnShortcut) );
}

void
SceneShortcutsDialog::validate()
{
    QList<QString> keys;

    for (int row = 0; row < _sceneIds.size(); ++row) {
        keys << static_cast<ShortcutRecorder*>( recorder(row) )->keys();
    }
    QStringList problems;
    const QList<SceneShortcuts::Conflict> conflicts = SceneShortcuts::conflicts(keys, _taken);
    for (int i = 0; i < conflicts.size(); ++i) {
        const SceneShortcuts::Conflict& c = conflicts.at(i);
        const QString name = _table->item(c.row, kColumnScene)->text();
        if (c.otherRow >= 0) {
            problems << tr("%1 is set for both \"%2\" and \"%3\".").arg( nativeText(c.keys) )
                .arg( _table->item(c.otherRow, kColumnScene)->text() ).arg(name);
        } else {
            problems << tr("%1 (\"%2\") is already used by %3 in the dashboard.").arg( nativeText(c.keys) ).arg(name).arg(c.taken);
        }
    }
    _status->setText( problems.join( QString::fromUtf8("\n") ) );
    _status->setVisible( !problems.isEmpty() );
    _buttons->button(QDialogButtonBox::Ok)->setEnabled( problems.isEmpty() );
    _buttons->button(QDialogButtonBox::Apply)->setEnabled( problems.isEmpty() );
    _clearButton->setEnabled( _table->currentRow() >= 0 );
    _defaultsButton->setEnabled( !_sceneIds.isEmpty() );
}

void
SceneShortcutsDialog::onClearClicked()
{
    const int row = _table->currentRow();

    if ( (row >= 0) && (row < _sceneIds.size()) ) {
        static_cast<ShortcutRecorder*>( recorder(row) )->setKeys( QString() );
    }
}

void
SceneShortcutsDialog::onDefaultsClicked()
{
    for (int row = 0; row < _sceneIds.size(); ++row) {
        static_cast<ShortcutRecorder*>( recorder(row) )->setKeys( SceneShortcuts::defaultShortcut(row) );
    }
}

void
SceneShortcutsDialog::onButtonClicked(QAbstractButton* button)
{
    switch ( _buttons->buttonRole(button) ) {
    case QDialogButtonBox::AcceptRole:
        apply();
        accept();
        break;
    case QDialogButtonBox::ApplyRole:
        apply();
        break;
    default:
        reject();
        break;
    }
}

void
SceneShortcutsDialog::apply()
{
    // Every scene listed, none included: a scene keeps its shortcut when
    // the list is reordered. Scenes removed since are dropped.
    QVariantMap saved;

    for (int row = 0; row < _sceneIds.size(); ++row) {
        saved.insert( _sceneIds.at(row), static_cast<ShortcutRecorder*>( recorder(row) )->keys() );
    }
    SceneShortcuts::save(saved);
    Q_EMIT applied();
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_SceneShortcutsDialog.cpp"
