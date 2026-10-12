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

#include "Custom/ndi/ProgramOutput.h"
#include "Custom/scene/SceneRenderer.h"

// QSettings keys: render hardware, SDI output.
#define kSettingsRenderDevice "dashboard/renderDevice"   // -1: CPU only, else the OpenGL renderer's index
#define kSettingsRenderThreads "dashboard/renderThreads" // 0: automatic
#define kSettingsNdiEnabled "dashboard/programNdi"     // default: on
#define kSettingsSdiEnabled "dashboard/sdiEnabled"
#define kSettingsSdiPort "dashboard/sdiPort"             // kept while turned off
#define kSettingsSdiMode "dashboard/sdiMode"             // 0: the output format's
#define kSdiStatusRefreshMs 500

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
    , _threadsSpin(0)
    , _ndiCheck(0)
    , _ndiStatus(0)
    , _sdiCheck(0)
    , _portCombo(0)
    , _refreshButton(0)
    , _modeCombo(0)
    , _sdiStatus(0)
    , _buttons(0)
    , _statusTimer(0)
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
    if ( gpus.isEmpty() ) {
        QLabel* noGpu = KvGui::secondaryLabel(renderBox);
        noGpu->setText( tr("No GPU usable for rendering was found.") );
        renderForm->addRow(QString(), noGpu);
    }
    _threadsSpin = new QSpinBox(renderBox);
    _threadsSpin->setRange( 0, qMax(64, QThread::idealThreadCount() * 2) );
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
    onSdiEnabledToggled( _sdiCheck->isChecked() );

    QObject::connect( _sdiCheck, SIGNAL(toggled(bool)), this, SLOT(onSdiEnabledToggled(bool)) );
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
    // A saved port that is not here now (unplugged) stays chosen.
    if ( !current.isEmpty() && (_portCombo->findData(current) < 0) ) {
        _portCombo->addItem(tr("%1 (not found)").arg(current), current);
    }
    if (_portCombo->count() == 0) {
        _portCombo->addItem(tr("None found"), QString());
    }
    _portCombo->setCurrentIndex( qMax(0, _portCombo->findData(current) ) );
    _portCombo->blockSignals(false);
    _portCombo->setToolTip( _ports.isEmpty() ? error : QString() );
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
DashboardSettingsDialog::onSdiEnabledToggled(bool on)
{
    _portCombo->setEnabled(on);
    _refreshButton->setEnabled(on);
    _modeCombo->setEnabled(on);
}

void
DashboardSettingsDialog::onRefreshPortsClicked()
{
    const QString port = _portCombo->itemData( _portCombo->currentIndex() ).toString();
    const quint32 mode = _modeCombo->itemData( _modeCombo->currentIndex() ).toUInt();

    fillPorts(port);
    fillModes(mode);
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
        _ndiStatus->setText( !_program->ndiEnabled() ? tr("Off.")
                             : !error.isEmpty() ? tr("Not sending: %1").arg(error)
                             : (connections < 0) ? tr("Sending.")
                             : tr("Sending, %1 receiver(s).").arg(connections) );
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

    settings.setValue( QString::fromUtf8(kSettingsNdiEnabled), _ndiCheck->isChecked() );
    if ( _program && (_program->ndiEnabled() != _ndiCheck->isChecked()) ) {
        _program->setNdiEnabled( _ndiCheck->isChecked() );
    }

    const QString port = _sdiCheck->isChecked() ? _portCombo->itemData( _portCombo->currentIndex() ).toString() : QString();
    const quint32 mode = _modeCombo->itemData( _modeCombo->currentIndex() ).toUInt();
    // The port is kept when the output is turned off, to turn it on again.
    if ( !port.isEmpty() ) {
        settings.setValue(QString::fromUtf8(kSettingsSdiPort), port);
    }
    settings.setValue(QString::fromUtf8(kSettingsSdiMode), mode);
    settings.setValue(QString::fromUtf8(kSettingsSdiEnabled), !port.isEmpty() );
    if ( _program && ( (port != _program->sdiPort()) || (mode != _program->sdiMode()) ) ) {
        _program->setSdiOutput(port, mode);
    }
    refreshSdiStatus();
}

void
DashboardSettingsDialog::restore(::SceneRenderer* renderer,
                                 ::ProgramOutput* program)
{
    QSettings settings;

    if (renderer) {
        renderer->setRendererOptions( rendererOptions( settings.value(QString::fromUtf8(kSettingsRenderDevice), -1).toInt(),
                                                       settings.value(QString::fromUtf8(kSettingsRenderThreads), 0).toInt() ) );
    }
    if (program) {
        program->setNdiEnabled( settings.value(QString::fromUtf8(kSettingsNdiEnabled), true).toBool() );
    }
    if ( program && settings.value( QString::fromUtf8(kSettingsSdiEnabled) ).toBool() ) {
        program->setSdiOutput( settings.value( QString::fromUtf8(kSettingsSdiPort) ).toString(),
                               settings.value( QString::fromUtf8(kSettingsSdiMode) ).toUInt() );
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_DashboardSettingsDialog.cpp"
