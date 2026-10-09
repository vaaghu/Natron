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

#include "StateBindingTab.h"

#include <string>

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/EffectInstance.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"

#include "Custom/scene/ProjectInfo.h" // kProjectInfoStateKeyParam
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"
#include "Gui/KvGuiUtils.h"

// Text node from openfx-arena, and the script name of its text parameter.
// Bindable nodes and the parameter the bound value goes to:
// Text (openfx-arena) -> its text, Read -> its image/video file path.
// Keep in sync with the apply script in Custom/scene/SceneRenderer.cpp.
#define kStateBindingTextPluginID "net.fxarena.openfx.Text"
#define kStateBindingTextParamName "text"
#define kStateBindingReadParamName "filename"

// The bound key is saved in the project as a hidden user parameter on a
// hidden page, so the binding survives reopening the project and can be read
// by the dashboard and by NatronRenderer (see Custom/scene).
#define kStateBindingPageName "natronStatePage"
#define kStateBindingKeyParamName kProjectInfoStateKeyParam

NATRON_NAMESPACE_ENTER

NATRON_NAMESPACE_ANONYMOUS_ENTER

const char*
targetParamName(const NodePtr& node)
{
    if (!node) {
        return 0;
    }

    const std::string pluginID = node->getPluginID();
    if (pluginID == kStateBindingTextPluginID) {
        return kStateBindingTextParamName;
    }
    if (pluginID == PLUGINID_NATRON_READ) {
        return kStateBindingReadParamName;
    }

    return 0;
}

// The parameter receiving the bound value (a string or file parameter).
KnobStringBasePtr
getTargetKnob(const NodePtr& node)
{
    const char* name = targetParamName(node);

    if (!name) {
        return KnobStringBasePtr();
    }

    return std::dynamic_pointer_cast<KnobStringBase>( node->getKnobByName(name) );
}

bool
isImageNode(const NodePtr& node)
{
    return node && node->getPluginID() == PLUGINID_NATRON_READ;
}

// The hidden parameter holding the bound key; created on demand.
KnobStringPtr
getKeyKnob(const NodePtr& node,
           bool create)
{
    if (!node) {
        return KnobStringPtr();
    }

    KnobStringPtr knob = std::dynamic_pointer_cast<KnobString>( node->getKnobByName(kStateBindingKeyParamName) );
    if (knob || !create) {
        return knob;
    }

    EffectInstancePtr effect = node->getEffectInstance();
    if (!effect) {
        return KnobStringPtr();
    }

    KnobPagePtr page = effect->createPageKnob(kStateBindingPageName, "State binding", true);
    knob = effect->createStringKnob(kStateBindingKeyParamName, "State key", true);
    if (!page || !knob) {
        return KnobStringPtr();
    }
    page->setSecret(true);
    knob->setSecret(true);
    knob->setHintToolTip( std::string("State store key whose value replaces the text (set from the State tab).") );
    page->addKnob(knob);

    return knob;
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

StateBindingTab::StateBindingTab(const NodePtr& node,
                                 ::StateStore* store,
                                 QWidget* parent)
    : QWidget(parent)
    , _node(node)
    , _store(store)
    , _boundKey()
    , _keyCombo(0)
    , _valueLabel(0)
    , _unbindButton(0)
    , _imageNode(false)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    QFormLayout* form = new QFormLayout;

    QWidget* keyRow = new QWidget(this);
    QHBoxLayout* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);

    _keyCombo = new QComboBox(keyRow);
    _keyCombo->setEditable(true);
    _keyCombo->setInsertPolicy(QComboBox::NoInsert);
    _keyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _keyCombo->lineEdit()->setPlaceholderText( tr("Type or select a key") );
    const bool image = isImageNode(node);
    _imageNode = image;
    _keyCombo->setToolTip( image ? tr("Key in the state store whose value (an image or video file path) replaces this node's file.")
                                 : tr("Key in the state store whose value replaces this node's text.") );
    keyLayout->addWidget(_keyCombo);

    _unbindButton = new QPushButton(tr("Unbind"), keyRow);
    _unbindButton->setToolTip( image ? tr("Stop updating the file from the state store. The current file is kept.")
                                     : tr("Stop updating the text from the state store. The current text is kept.") );
    keyLayout->addWidget(_unbindButton);

    form->addRow(tr("Key"), keyRow);

    _valueLabel = new QLabel(this);
    _valueLabel->setTextFormat(Qt::PlainText);
    _valueLabel->setWordWrap(true);
    _valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _valueLabel->installEventFilter(this); // double-click opens an image
    form->addRow(tr("Value"), _valueLabel);

    mainLayout->addLayout(form);
    mainLayout->addStretch();

    QObject::connect( _keyCombo, SIGNAL(editTextChanged(QString)), this, SLOT(onKeyEdited(QString)) );
    QObject::connect( _unbindButton, SIGNAL(clicked()), this, SLOT(onUnbindClicked()) );

    if (_store) {
        QObject::connect( _store, SIGNAL(valueChanged(QString)), this, SLOT(onStoreValueChanged(QString)) );
        QObject::connect( _store, SIGNAL(keysChanged()), this, SLOT(onStoreKeysChanged()) );
    }

    refreshKeyList();

    // Restore the binding saved in the project.
    KnobStringPtr keyKnob = getKeyKnob(node, false);
    if (keyKnob) {
        const QString savedKey = QString::fromUtf8( keyKnob->getValue().c_str() ).trimmed();
        if ( !savedKey.isEmpty() ) {
            const bool wasBlocked = _keyCombo->blockSignals(true);
            _keyCombo->setEditText(savedKey);
            _keyCombo->blockSignals(wasBlocked);
            _boundKey = savedKey;
            // After the project has finished loading.
            QTimer::singleShot( 0, this, SLOT(applyValueToNode()) );
        }
    }

    refreshPreview();
}

StateBindingTab::~StateBindingTab()
{
}

bool
StateBindingTab::isSupportedNode(const NodePtr& node)
{
    return getTargetKnob(node).get() != 0;
}

void
StateBindingTab::setBoundKey(const QString& key)
{
    const QString trimmed = key.trimmed();

    if ( _keyCombo->currentText() != trimmed ) {
        const bool wasBlocked = _keyCombo->blockSignals(true);
        _keyCombo->setEditText(trimmed);
        _keyCombo->blockSignals(wasBlocked);
    }

    if (trimmed == _boundKey) {
        return;
    }

    _boundKey = trimmed;
    saveBindingToNode();
    refreshPreview();
    applyValueToNode();
}

void
StateBindingTab::saveBindingToNode()
{
    KnobStringPtr keyKnob = getKeyKnob( _node.lock(), !_boundKey.isEmpty() );

    if ( keyKnob && ( keyKnob->getValue() != _boundKey.toStdString() ) ) {
        keyKnob->setValue( _boundKey.toStdString() );
    }
}

void
StateBindingTab::onKeyEdited(const QString& key)
{
    setBoundKey(key);
}

void
StateBindingTab::onUnbindClicked()
{
    setBoundKey( QString() );
}

void
StateBindingTab::onStoreValueChanged(const QString& key)
{
    if ( _boundKey.isEmpty() || (key != _boundKey) ) {
        return;
    }

    refreshPreview();
    applyValueToNode();
}

void
StateBindingTab::onStoreKeysChanged()
{
    refreshKeyList();
}

bool
StateBindingTab::valueFitsNode(const QVariant& value) const
{
    return Kv::typeOf(value) == (_imageNode ? Kv::eTypeImage : Kv::eTypeText);
}

void
StateBindingTab::refreshKeyList()
{
    if (!_store) {
        return;
    }

    // Only keys of the type this node takes (text for Text, image for Read).
    QStringList keys;
    const QStringList all = _store->keys();
    for (int i = 0; i < all.size(); ++i) {
        if ( valueFitsNode( _store->get( all.at(i) ) ) ) {
            keys << all.at(i);
        }
    }
    keys.sort();

    // Rebuilding the list must not change the key being typed/bound.
    const bool wasBlocked = _keyCombo->blockSignals(true);
    const QString current = _keyCombo->currentText();
    _keyCombo->clear();
    _keyCombo->addItems(keys);
    _keyCombo->setEditText(current);
    _keyCombo->blockSignals(wasBlocked);
}

void
StateBindingTab::refreshPreview()
{
    _unbindButton->setEnabled( !_boundKey.isEmpty() );
    _valueLabel->setToolTip( QString() );

    if ( _boundKey.isEmpty() ) {
        _valueLabel->setText( tr("Not bound") );
    } else if ( !_store || !_store->has(_boundKey) ) {
        _valueLabel->setText( tr("Key not in store yet; the node will update when it is set.") );
    } else {
        const QVariant value = _store->get(_boundKey);
        if ( !valueFitsNode(value) ) {
            _valueLabel->setText( _imageNode ? tr("%1 is text, not an image: the file is left unchanged.").arg(_boundKey)
                                             : tr("%1 is an image, not text: the text is left unchanged.").arg(_boundKey) );
        } else {
            _valueLabel->setText( Kv::displayText(value) );
            _valueLabel->setToolTip( KvGui::tooltip(value) );
        }
    }
}

void
StateBindingTab::applyValueToNode()
{
    if ( !_store || _boundKey.isEmpty() || !_store->has(_boundKey) ) {
        return;
    }

    const QVariant value = _store->get(_boundKey);
    if ( !valueFitsNode(value) ) {
        return;
    }

    KnobStringBasePtr knob = getTargetKnob( _node.lock() );
    if (!knob) {
        return;
    }

    // Text nodes get the text, Read nodes the image path.
    const std::string text = ( _imageNode ? Kv::imagePath(value) : Kv::textValue(value) ).toStdString();
    if (knob->getValue() == text) {
        return;
    }

    knob->setValue(text);
}

bool
StateBindingTab::eventFilter(QObject* watched,
                             QEvent* e)
{
    if ( (watched == _valueLabel) && (e->type() == QEvent::MouseButtonDblClick) &&
         _store && _store->has(_boundKey) ) {
        KvGui::openImage( _store->get(_boundKey) );

        return true;
    }

    return QWidget::eventFilter(watched, e);
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_StateBindingTab.cpp"
