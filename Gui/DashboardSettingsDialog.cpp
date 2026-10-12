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

#include "DashboardSettingsDialog.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/AppManager.h" // appPTR
#include "Engine/OSGLContext.h" // OpenGLRendererInfo
#include "Gui/KvGuiUtils.h"
#include "Gui/NotificationCenter.h"

#include "Custom/ndi/NdiOutput.h"
#include "Custom/ndi/ProgramOutput.h"
#include "Custom/scene/SceneRenderer.h"

// QSettings keys: render hardware, SDI output.
#define kSettingsRenderDevice "dashboard/renderDevice"   // -1: CPU only, else the OpenGL renderer's index
#define kSettingsRenderThreads "dashboard/renderThreads" // 0: automatic
#define kSettingsNdiEnabled "dashboard/programNdi"     // default: on
#define kSettingsSdiEnabled "dashboard/sdiEnabled"
#define kSettingsSdiPort "dashboard/sdiPort"             // kept while turned off
#define kSettingsSdiMode "dashboard/sdiMode"             // 0: the output format's
#define kSettingsSdiCardClock "dashboard/sdiCardClock"   // default: the system clock
#define kSdiStatusRefreshMs 500
#define kFocusFlashMs 2000 // a field opened from a notification is outlined this long

NATRON_NAMESPACE_ENTER

namespace {
QStringList
openGLRendererNames()
{
    QStringList names;
    const std::list<OpenGLRendererInfo>& renderers = appPTR->getOpenGLRenderers();

    for (std::list<OpenGLRendererInfo>::const_iterator it = renderers.begin(); it != renderers.end(); ++it) {
        names << QString::fromUtf8( (it->vendorName + ' ' + it->rendererName).c_str() );
    }

    return names;
}
} // anon namespace

DashboardSettingsDialog::DashboardSettingsDialog(::SceneRenderer* renderer,
                                                 ::ProgramOutput* program,
                                                 QWidget* parent)
    : QDialog(parent)
    , _renderer(renderer)
    , _program(program)
    , _deviceCombo(0)
    , _gpuNote(0)
    , _threadsSpin(0)
    , _ndiCheck(0)
    , _ndiStatus(0)
    , _sdiCheck(0)
    , _portCombo(0)
    , _refreshButton(0)
    , _modeCombo(0)
    , _clockCombo(0)
    , _sdiStatus(0)
    , _buttons(0)
    , _statusTimer(0)
    , _flashWidget(0)
{
    setWindowTitle( tr("Settings") );
    QSettings settings;
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Render hardware.
    QGroupBox* renderBox = new QGroupBox(tr("Render"), this);
    QFormLayout* renderForm = new QFormLayout(renderBox);
    _deviceCombo = new QComboBox(renderBox);
    _deviceCombo->addItem(tr("CPU only"), -1);
    const QStringList gpus = openGLRendererNames();
    for (int i = 0; i < gpus.size(); ++i) {
        _deviceCombo->addItem(tr("GPU: %1").arg( gpus.at(i) ), i);
    }
    const int device = settings.value(QString::fromUtf8(kSettingsRenderDevice), -1).toInt();
    _deviceCombo->setCurrentIndex( qMax(0, _deviceCombo->findData(device) ) );
    _deviceCombo->setToolTip( tr("The hardware scene renders use. With a GPU, the nodes that support GPU rendering "
                                 "render on it (if the project allows it, see its GPU Rendering setting); the others "
                                 "still render on the CPU.") );
    renderForm->addRow(tr("Render on:"), _deviceCombo);
    _gpuNote = KvGui::secondaryLabel(renderBox);
    _gpuNote->setText( tr("Nodes without GPU support still render on the CPU.") );
    renderForm->addRow(QString(), _gpuNote);
    if ( gpus.isEmpty() ) {
        QLabel* noGpu = KvGui::secondaryLabel(renderBox);
        noGpu->setText( tr("No GPU usable for rendering was found.") );
        renderForm->addRow(QString(), noGpu);
    }
    _threadsSpin = new QSpinBox(renderBox);
    _threadsSpin->setRange( 0, qMax(1, QThread::idealThreadCount() ) ); // up to what the system has
    _threadsSpin->setSpecialValueText( tr("Automatic") );
    _threadsSpin->setValue( settings.value(QString::fromUtf8(kSettingsRenderThreads), 0).toInt() );
    _threadsSpin->setToolTip( tr("CPU threads each render uses (this machine has %1). "
                                 "Several renders can run at once (Scene menu, Renders at Once).").arg( QThread::idealThreadCount() ) );
    renderForm->addRow(tr("CPU threads per render:"), _threadsSpin);
    renderBox->setEnabled(_renderer != 0);
    layout->addWidget(renderBox);

    // Program outputs: NDI, SDI, both or neither.
    QGroupBox* sdiBox = new QGroupBox(tr("Program Output"), this);
    QFormLayout* sdiForm = new QFormLayout(sdiBox);
    _ndiCheck = new QCheckBox( _program ? tr("Send Program as the NDI source \"%1\"").arg( _program->sourceName() )
                               : tr("Send Program as an NDI source"), sdiBox );
    _ndiCheck->setChecked( !_program || _program->ndiEnabled() );
    sdiForm->addRow(_ndiCheck);
    _ndiStatus = KvGui::secondaryLabel(sdiBox);
    _ndiStatus->setWordWrap(true);
    sdiForm->addRow(_ndiStatus);
    _sdiCheck = new QCheckBox(tr("Play Program out of an SDI port"), sdiBox);
    sdiForm->addRow(_sdiCheck);
    QHBoxLayout* portRow = new QHBoxLayout;
    _portCombo = new QComboBox(sdiBox);
    _portCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _refreshButton = new QPushButton(tr("Refresh"), sdiBox);
    _refreshButton->setToolTip( tr("List the SDI ports again (after plugging a device or installing the drivers)") );
    portRow->addWidget(_portCombo, 1);
    portRow->addWidget(_refreshButton);
    sdiForm->addRow(tr("Port:"), portRow);
    _modeCombo = new QComboBox(sdiBox);
    _modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    sdiForm->addRow(tr("Video mode:"), _modeCombo);
    _clockCombo = new QComboBox(sdiBox);
    _clockCombo->addItem(tr("Card's clock"), true);
    _clockCombo->addItem(tr("System clock"), false);
    _clockCombo->setToolTip( tr("Card's clock: frames are queued on the card, which plays one per frame time; the "
                                "smoothest SDI, and Program (NDI included) runs at the card's pace, a few frames later.\n"
                                "System clock: each frame goes out as soon as it is mixed, timed by the computer "
                                "(or by NDI); a frame may be repeated or dropped now and then.") );
    sdiForm->addRow(tr("Clock:"), _clockCombo);
    _sdiStatus = KvGui::secondaryLabel(sdiBox);
    _sdiStatus->setWordWrap(true);
    sdiForm->addRow(_sdiStatus);
    sdiBox->setEnabled(_program != 0);
    layout->addWidget(sdiBox);

    _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, Qt::Horizontal, this);
    layout->addWidget(_buttons);

    const QString port = _program ? _program->sdiPort() : QString();
    _sdiCheck->setChecked( !port.isEmpty() );
    fillPorts( port.isEmpty() ? settings.value( QString::fromUtf8(kSettingsSdiPort) ).toString() : port );
    fillModes( _program ? _program->sdiMode() : 0 );
    const bool cardClock = _program && !port.isEmpty() ? _program->sdiCardClock()
                           : settings.value(QString::fromUtf8(kSettingsSdiCardClock), false).toBool();
    _clockCombo->setCurrentIndex( cardClock ? 0 : 1 );
    updateAvailability();

    QObject::connect( _deviceCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(updateAvailability()) );
    QObject::connect( _ndiCheck, SIGNAL(toggled(bool)), this, SLOT(updateAvailability()) );
    QObject::connect( _sdiCheck, SIGNAL(toggled(bool)), this, SLOT(updateAvailability()) );
    QObject::connect( _refreshButton, SIGNAL(clicked()), this, SLOT(onRefreshPortsClicked()) );
    QObject::connect( _portCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onPortChosen(int)) );
    QObject::connect( _buttons, SIGNAL(clicked(QAbstractButton*)), this, SLOT(onButtonClicked(QAbstractButton*)) );

    _statusTimer = new QTimer(this);
    _statusTimer->setInterval(kSdiStatusRefreshMs);
    QObject::connect( _statusTimer, SIGNAL(timeout()), this, SLOT(refreshSdiStatus()) );
    _statusTimer->start();
    refreshSdiStatus();
}

void
DashboardSettingsDialog::fillPorts(const QString& current)
{
    QString error;

    _ports = Sdi::ports(&error);
    _portCombo->blockSignals(true);
    _portCombo->clear();
    for (int i = 0; i < _ports.size(); ++i) {
        const Sdi::Port& p = _ports.at(i);
        const bool showModel = !p.model.isEmpty() && !p.name.startsWith(p.model);
        _portCombo->addItem(showModel ? QString::fromUtf8("%1 (%2)").arg(p.name).arg(p.model) : p.name, p.name);
    }
    // A saved port that is not here now (unplugged) stays chosen, while
    // there are ports at all.
    if ( !_ports.isEmpty() && !current.isEmpty() && (_portCombo->findData(current) < 0) ) {
        _portCombo->addItem(tr("%1 (not found)").arg(current), current);
    }
    if (_portCombo->count() == 0) {
        _portCombo->addItem(tr("None found"), QString());
    }
    _portCombo->setCurrentIndex( qMax(0, _portCombo->findData(current) ) );
    _portCombo->blockSignals(false);
    _portCombo->setToolTip( _ports.isEmpty() ? error : QString() );
    _sdiCheck->setToolTip( _ports.isEmpty() ? error : QString() );
}

void
DashboardSettingsDialog::fillModes(quint32 current)
{
    const QString port = _portCombo->itemData( _portCombo->currentIndex() ).toString();

    _modeCombo->clear();
    _modeCombo->addItem( _program ? tr("Output format (%1x%2, %3 fps)").arg( _program->width() ).arg( _program->height() ).arg( _program->fps() )
                         : tr("Output format"), 0u );
    for (int i = 0; i < _ports.size(); ++i) {
        if (_ports.at(i).name != port) {
            continue;
        }
        const QList<Sdi::Mode>& modes = _ports.at(i).modes;
        for (int m = 0; m < modes.size(); ++m) {
            _modeCombo->addItem(modes.at(m).name, modes.at(m).id);
        }
    }
    int index = _modeCombo->findData(current);
    if ( (index < 0) && (current != 0) ) {
        _modeCombo->addItem(tr("Saved mode (not on this port)"), current);
        index = _modeCombo->count() - 1;
    }
    _modeCombo->setCurrentIndex( qMax(0, index) );
    _modeCombo->setToolTip( tr("Output format: the mode of the switcher's output size and frame rate. "
                               "Another mode shows Program scaled to its size.") );
}

void
DashboardSettingsDialog::updateAvailability()
{
    // Render: nothing to choose without a GPU.
    _deviceCombo->setEnabled(_deviceCombo->count() > 1);
    _gpuNote->setVisible(_deviceCombo->itemData( _deviceCombo->currentIndex() ).toInt() >= 0);

    // NDI: off and grayed without the NDI Runtime.
    QString ndiError;
    const bool ndi = Ndi::load(&ndiError);
    if (!ndi) {
        _ndiCheck->blockSignals(true);
        _ndiCheck->setChecked(false);
        _ndiCheck->blockSignals(false);
    }
    _ndiCheck->setEnabled(ndi);
    _ndiCheck->setToolTip(ndi ? QString() : ndiError);
    _ndiStatus->setEnabled( _ndiCheck->isChecked() || !ndi );

    // SDI: off, grayed and back to the defaults without a port (Refresh
    // stays, to look again).
    const bool sdi = !_ports.isEmpty();
    if (!sdi) {
        _sdiCheck->blockSignals(true);
        _sdiCheck->setChecked(false);
        _sdiCheck->blockSignals(false);
        _modeCombo->setCurrentIndex(0); // the output format's
        _clockCombo->setCurrentIndex(1); // the system clock
    }
    _sdiCheck->setEnabled(sdi);
    const bool on = sdi && _sdiCheck->isChecked();
    _portCombo->setEnabled(on);
    _modeCombo->setEnabled(on);
    _clockCombo->setEnabled(on);
    _refreshButton->setEnabled(!sdi || on);
}

void
DashboardSettingsDialog::onRefreshPortsClicked()
{
    QString port = _portCombo->itemData( _portCombo->currentIndex() ).toString();
    const quint32 mode = _modeCombo->itemData( _modeCombo->currentIndex() ).toUInt();

    if ( port.isEmpty() ) {
        port = QSettings().value( QString::fromUtf8(kSettingsSdiPort) ).toString();
    }
    fillPorts(port);
    fillModes(mode);
    updateAvailability();
    refreshSdiStatus();
}

void
DashboardSettingsDialog::onPortChosen(int /*index*/)
{
    fillModes( _modeCombo->itemData( _modeCombo->currentIndex() ).toUInt() );
}

void
DashboardSettingsDialog::onButtonClicked(QAbstractButton* button)
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
DashboardSettingsDialog::refreshSdiStatus()
{
    QString text;

    if (_program) {
        const QString error = _program->outputError();
        const int connections = _program->connections();
        QString ndiError;
        QString ndiText = !Ndi::load(&ndiError) ? ndiError
                          : !_program->ndiEnabled() ? tr("Off.")
                          : !error.isEmpty() ? tr("Not sending: %1").arg(error)
                          : (connections < 0) ? tr("Sending.")
                          : tr("Sending, %1 receiver(s).").arg(connections);
        if ( _ndiCheck->isEnabled() && (_ndiCheck->isChecked() != _program->ndiEnabled()) ) {
            ndiText += QString::fromUtf8(" ") + ( _ndiCheck->isChecked() ? tr("Apply to turn it on.") : tr("Apply to turn it off.") );
        }
        _ndiStatus->setText(ndiText);
    }
    if (!_program) {
        text = tr("No Program output.");
    } else if ( _program->sdiPort().isEmpty() ) {
        QString error;
        text = Sdi::load(&error) ? tr("Off.") : error;
    } else {
        bool ok = false;
        const QString status = _program->sdiStatus(&ok);
        text = ok ? tr("Playing: %1").arg(status) : tr("Not playing: %1").arg(status);
    }
    _sdiStatus->setText(text);
}

QStringList
DashboardSettingsDialog::rendererOptions(int device,
                                         int threads)
{
    // NatronRenderer applies these settings for its run only (see CLArgs).
    QStringList options;
    const int gpus = (int)appPTR->getOpenGLRenderers().size();

    if ( (device >= 0) && (device < gpus) ) {
        options << QString::fromUtf8("--opengl") << QString::fromUtf8("enabled");
        if (gpus > 1) {
            options << QString::fromUtf8("--setting") << QString::fromUtf8("chooseOpenGLRenderer=%1").arg(device);
        }
    } else {
        options << QString::fromUtf8("--opengl") << QString::fromUtf8("disabled");
    }
    // At most the system's threads (a value saved on a bigger machine).
    threads = qMin( threads, qMax(1, QThread::idealThreadCount() ) );
    if (threads > 0) {
        options << QString::fromUtf8("--setting") << QString::fromUtf8("noRenderThreads=%1").arg(threads);
    }

    return options;
}

void
DashboardSettingsDialog::apply()
{
    QSettings settings;
    const int device = _deviceCombo->itemData( _deviceCombo->currentIndex() ).toInt();
    const int threads = _threadsSpin->value();

    settings.setValue(QString::fromUtf8(kSettingsRenderDevice), device);
    settings.setValue(QString::fromUtf8(kSettingsRenderThreads), threads);
    if (_renderer) {
        _renderer->setRendererOptions( rendererOptions(device, threads) );
    }

    // NDI unavailable: its setting stays (for when the runtime is installed).
    if ( _ndiCheck->isEnabled() ) {
        settings.setValue( QString::fromUtf8(kSettingsNdiEnabled), _ndiCheck->isChecked() );
        if ( _program && (_program->ndiEnabled() != _ndiCheck->isChecked()) ) {
            _program->setNdiEnabled( _ndiCheck->isChecked() );
        }
    }

    const QString port = _sdiCheck->isChecked() ? _portCombo->itemData( _portCombo->currentIndex() ).toString() : QString();
    const quint32 mode = _modeCombo->itemData( _modeCombo->currentIndex() ).toUInt();
    const bool cardClock = _clockCombo->itemData( _clockCombo->currentIndex() ).toBool();
    // The port is kept when the output is turned off, to turn it on again.
    if ( !port.isEmpty() ) {
        settings.setValue(QString::fromUtf8(kSettingsSdiPort), port);
    }
    settings.setValue(QString::fromUtf8(kSettingsSdiMode), mode);
    settings.setValue(QString::fromUtf8(kSettingsSdiCardClock), cardClock);
    settings.setValue(QString::fromUtf8(kSettingsSdiEnabled), !port.isEmpty() );
    if ( _program && ( (port != _program->sdiPort()) || (mode != _program->sdiMode()) || (cardClock != _program->sdiCardClock()) ) ) {
        _program->setSdiOutput(port, mode, cardClock);
    }
    refreshSdiStatus();
}

void
DashboardSettingsDialog::focusField(const QString& field)
{
    _pendingFocus = field;
    // Once the dialog shows.
    QTimer::singleShot( 0, this, SLOT(applyPendingFocus()) );
}

void
DashboardSettingsDialog::applyPendingFocus()
{
    QWidget* w = 0;

    if ( _pendingFocus == QString::fromUtf8(kSettingsFieldRenderDevice) ) {
        w = _deviceCombo;
    } else if ( _pendingFocus == QString::fromUtf8(kSettingsFieldRenderThreads) ) {
        w = _threadsSpin;
        _threadsSpin->selectAll();
    } else if ( _pendingFocus == QString::fromUtf8(kSettingsFieldNdi) ) {
        w = _ndiCheck;
    } else if ( _pendingFocus == QString::fromUtf8(kSettingsFieldSdi) ) {
        w = _portCombo->isEnabled() ? static_cast<QWidget*>(_portCombo) : static_cast<QWidget*>(_sdiCheck);
    }
    _pendingFocus.clear();
    if (!w) {
        return;
    }
    w->setFocus(Qt::OtherFocusReason);
    // Outlined for a moment, to find it.
    onFlashEnded();
    _flashWidget = w;
    _flashStyle = w->styleSheet();
    w->setStyleSheet( _flashStyle + QString::fromUtf8(" %1 { border: 2px solid #e8a33d; border-radius: 3px; }").arg( QString::fromUtf8( w->metaObject()->className() ) ) );
    QTimer::singleShot( kFocusFlashMs, this, SLOT(onFlashEnded()) );
}

void
DashboardSettingsDialog::onFlashEnded()
{
    if (_flashWidget) {
        _flashWidget->setStyleSheet(_flashStyle);
        _flashWidget = 0;
    }
}

void
DashboardSettingsDialog::restore(::SceneRenderer* renderer,
                                 ::ProgramOutput* program)
{
    QSettings settings;

    if (renderer) {
        // Saved choices this machine cannot follow (another machine's, or
        // hardware that changed) are changed and saved, with a notification.
        int device = settings.value(QString::fromUtf8(kSettingsRenderDevice), -1).toInt();
        int threads = settings.value(QString::fromUtf8(kSettingsRenderThreads), 0).toInt();
        const int gpus = (int)appPTR->getOpenGLRenderers().size();
        if (device >= gpus) {
            NotificationCenter::instance()->add( tr("Rendering on the CPU"),
                                                 tr("The GPU chosen for rendering was not found; renders use the CPU only."),
                                                 true, NotificationTarget::settings( QString::fromUtf8(kSettingsFieldRenderDevice) ) );
            device = -1;
            settings.setValue(QString::fromUtf8(kSettingsRenderDevice), device);
        }
        const int maxThreads = qMax(1, QThread::idealThreadCount() );
        if (threads > maxThreads) {
            NotificationCenter::instance()->add( tr("Render threads lowered"),
                                                 tr("CPU threads per render lowered from %1 to %2, what this machine has.").arg(threads).arg(maxThreads),
                                                 true, NotificationTarget::settings( QString::fromUtf8(kSettingsFieldRenderThreads) ) );
            threads = maxThreads;
            settings.setValue(QString::fromUtf8(kSettingsRenderThreads), threads);
        }
        renderer->setRendererOptions( rendererOptions(device, threads) );
    }
    if (program) {
        program->setNdiEnabled( settings.value(QString::fromUtf8(kSettingsNdiEnabled), true).toBool() );
    }
    if ( program && settings.value( QString::fromUtf8(kSettingsSdiEnabled) ).toBool() ) {
        program->setSdiOutput( settings.value( QString::fromUtf8(kSettingsSdiPort) ).toString(),
                               settings.value( QString::fromUtf8(kSettingsSdiMode) ).toUInt(),
                               settings.value(QString::fromUtf8(kSettingsSdiCardClock), false).toBool() );
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_DashboardSettingsDialog.cpp"
