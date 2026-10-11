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

#include "SwitcherPanel.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/KvGuiUtils.h"
#include "Gui/SwitcherWidgets.h"

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/PlayoutChannel.h"
#include "Custom/scene/SceneStore.h"

// QSettings key: transition duration.
#define kSwitcherSettingsDuration "dashboard/transitionMs"
#define kSwitcherDefaultDurationMs 500
#define kSwitcherRefreshMs 40 // monitors: 25 images/s
#define kSwitcherTBarSteps 1000

NATRON_NAMESPACE_ENTER

NATRON_NAMESPACE_ANONYMOUS_ENTER

QString
rgb(const QColor& color)
{
    return QString::fromUtf8("rgb(%1, %2, %3)").arg( color.red() ).arg( color.green() ).arg( color.blue() );
}

// "1:05.2"
QString
formatTime(double seconds)
{
    const int minutes = int(seconds / 60);
    const double rest = seconds - minutes * 60;

    return QString::fromUtf8("%1:%2").arg(minutes).arg(rest, 4, 'f', 1, QLatin1Char('0'));
}

NATRON_NAMESPACE_ANONYMOUS_EXIT

SwitcherPanel::SwitcherPanel(::ProgramOutput* program,
                             ::NdiManager* ndi,
                             ::SceneStore* scenes,
                             QWidget* parent)
    : QWidget(parent)
    , _program(program)
    , _ndi(ndi)
    , _scenes(scenes)
    , _previewTitle(0)
    , _previewMonitor(0)
    , _previewStatus(0)
    , _programTitle(0)
    , _programMonitor(0)
    , _programStatus(0)
    , _onAirLabel(0)
    , _cutButton(0)
    , _fadeButton(0)
    , _durationSpin(0)
    , _fadeToBlackButton(0)
    , _tbar(0)
    , _tbarLatched(false)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    KvGui::setPanelLayout(layout);
    layout->setSpacing(8);

    // Preview | transitions | Program
    layout->addWidget(createBus(tr("Preview"), Switcher::previewColor(), &_previewTitle, &_previewMonitor, &_previewStatus), 1);

    QWidget* transitions = new QWidget(this);
    QVBoxLayout* column = new QVBoxLayout(transitions);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    _cutButton = new QPushButton(tr("Cut"), transitions);
    _cutButton->setToolTip( tr("Put Preview on air at once. The input on Program goes to Preview.") );
    _fadeButton = new QPushButton(tr("Fade"), transitions);
    _fadeButton->setToolTip( tr("Crossfade from Program to Preview in the time below, then swap them.") );
    _durationSpin = new QSpinBox(transitions);
    _durationSpin->setRange(0, 10000);
    _durationSpin->setSingleStep(100);
    _durationSpin->setSuffix( tr(" ms") );
    _durationSpin->setToolTip( tr("Duration of Fade and Fade to Black") );
    _fadeToBlackButton = new QPushButton(tr("FTB"), transitions);
    _fadeToBlackButton->setCheckable(true);
    _fadeToBlackButton->setToolTip( tr("Fade to Black: fade Program to black, or back from black") );
    _tbar = new QSlider(Qt::Vertical, transitions);
    _tbar->setRange(0, kSwitcherTBarSteps);
    _tbar->setInvertedAppearance(true); // pulled down, like a T-bar
    _tbar->setToolTip( tr("T-bar: drag down to crossfade by hand; the transition completes at the bottom.") );
    column->addWidget(_cutButton);
    column->addWidget(_fadeButton);
    column->addWidget(_durationSpin);
    column->addSpacing(6);
    column->addWidget(_fadeToBlackButton);
    column->addSpacing(6);
    column->addWidget(_tbar, 1, Qt::AlignHCenter);
    transitions->setFixedWidth( qMax(96, _durationSpin->sizeHint().width() + 8) );
    layout->addWidget(transitions);

    QWidget* programBus = createBus(tr("Program"), Switcher::programColor(), &_programTitle, &_programMonitor, &_programStatus);
    // ON AIR / BLACK, in the Program title bar.
    _onAirLabel = new QLabel(_programTitle);
    QFont bold = _onAirLabel->font();
    bold.setBold(true);
    _onAirLabel->setFont(bold);
    QHBoxLayout* titleLayout = new QHBoxLayout(_programTitle);
    titleLayout->setContentsMargins(6, 0, 6, 0);
    titleLayout->addStretch();
    titleLayout->addWidget(_onAirLabel);
    layout->addWidget(programBus, 1);

    QSettings settings;
    _durationSpin->setValue( settings.value(QString::fromUtf8(kSwitcherSettingsDuration), kSwitcherDefaultDurationMs).toInt() );

    QObject::connect( _cutButton, SIGNAL(clicked()), this, SLOT(onCutClicked()) );
    QObject::connect( _fadeButton, SIGNAL(clicked()), this, SLOT(onFadeClicked()) );
    QObject::connect( _fadeToBlackButton, SIGNAL(clicked()), this, SLOT(onFadeToBlackClicked()) );
    QObject::connect( _tbar, SIGNAL(sliderMoved(int)), this, SLOT(onTBarMoved(int)) );
    QObject::connect( _tbar, SIGNAL(sliderReleased()), this, SLOT(onTBarReleased()) );
    QObject::connect( _durationSpin, SIGNAL(valueChanged(int)), this, SLOT(onDurationChanged(int)) );
    if (_program) {
        QObject::connect( _program, SIGNAL(busChanged()), this, SLOT(onBusChanged()) );
        QObject::connect( _program, SIGNAL(transitionChanged()), this, SLOT(onTransitionChanged()) );
    }
    if (_scenes) {
        // Renamed scenes: new labels.
        QObject::connect( _scenes, SIGNAL(scenesChanged()), this, SLOT(onBusChanged()) );
    }

    _refreshTimer.setInterval(kSwitcherRefreshMs);
    QObject::connect( &_refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshTimer()) );
    _refreshTimer.start();

    if (!_program) {
        transitions->setEnabled(false);
        _previewMonitor->setPlaceholder( tr("The Program output is not available.") );
        _programMonitor->setPlaceholder( tr("The Program output is not available.") );
    }
    onBusChanged();
    onTransitionChanged();
}

SwitcherPanel::~SwitcherPanel()
{
}

QWidget*
SwitcherPanel::createBus(const QString& title,
                         const QColor& color,
                         QLabel** titleLabel,
                         VideoMonitor** monitor,
                         QLabel** status)
{
    QWidget* bus = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(bus);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    // "Preview: Scene 1 - intro", white on the tally color.
    *titleLabel = new QLabel(title, bus);
    (*titleLabel)->setTextFormat(Qt::PlainText);
    (*titleLabel)->setMinimumWidth(1);
    (*titleLabel)->setStyleSheet( QString::fromUtf8("QLabel { background: %1; color: white; font-weight: bold; padding: 3px 6px; border-radius: 2px; }")
                                  .arg( rgb(color) ) );
    layout->addWidget(*titleLabel);

    *monitor = new VideoMonitor(bus);
    layout->addWidget(*monitor, 1);

    *status = KvGui::secondaryLabel(bus);
    (*status)->setMinimumWidth(1);
    layout->addWidget(*status);

    return bus;
}

QString
SwitcherPanel::sourceLabel(const ::ProgramOutput::Source& source) const
{
    ::Scene scene;

    if ( source.isEmpty() || !_scenes || !_scenes->scene(source.sceneId, &scene) ) {
        return QString();
    }

    return ::NdiManager::sourceName(scene, source.item);
}

QString
SwitcherPanel::inputState(const ::ProgramOutput::Source& source) const
{
    if ( source.isEmpty() ) {
        return QString();
    }
    ::PlayoutChannel* channel = _ndi ? _ndi->channel(source.sceneId, source.item) : 0;
    if (!channel) {
        return tr("NDI output off");
    }
    const ::PlayoutChannel::Status st = channel->status();
    QString state = ::PlayoutChannel::stateName(st.state);
    if (st.duration() > 0) {
        state += QString::fromUtf8("  %1 / %2").arg( formatTime( st.seconds() ) ).arg( formatTime( st.duration() ) );
    }

    return state;
}

QImage
SwitcherPanel::inputImage(const ::ProgramOutput::Source& source,
                          int width)
{
    if (source.isEmpty() || !_scenes) {
        return QImage();
    }

    // Playing (or holding a frame) on its NDI source: that frame.
    ::PlayoutChannel* channel = _ndi ? _ndi->channel(source.sceneId, source.item) : 0;
    if ( channel && (channel->status().state != ::PlayoutChannel::eStopped) ) {
        const QImage frame = channel->preview(width);
        if ( !frame.isNull() ) {
            return frame;
        }
    }

    // Otherwise the last render's thumbnail (re-read when it changes).
    const QString thumbnail = _scenes->renderRecord(source.sceneId, source.item).thumbnail;
    if ( thumbnail.isEmpty() ) {
        return QImage();
    }
    const QFileInfo info(thumbnail);
    const QString key = thumbnail + QLatin1Char('|') + info.lastModified().toString(Qt::ISODate);
    QHash<QString, QImage>::const_iterator it = _thumbnails.find(key);
    if ( it == _thumbnails.end() ) {
        if (_thumbnails.size() > 32) {
            _thumbnails.clear();
        }
        it = _thumbnails.insert( key, QImage(thumbnail) );
    }

    return it.value();
}

void
SwitcherPanel::onBusChanged()
{
    const QString previewLabel = _program ? sourceLabel( _program->preview() ) : QString();
    const QString programLabel = _program ? sourceLabel( _program->program() ) : QString();

    _previewTitle->setText( previewLabel.isEmpty() ? tr("Preview") : tr("Preview: %1").arg(previewLabel) );
    _programTitle->setText( programLabel.isEmpty() ? tr("Program") : tr("Program: %1").arg(programLabel) );
    _previewTitle->setToolTip( previewLabel.isEmpty() ? tr("Click an input box to put it on Preview.") : previewLabel );
    _programTitle->setToolTip( programLabel.isEmpty() ? tr("Nothing on air. Use Cut or Fade, or the Cut button of an input box.") : programLabel );
    if (_program) {
        _previewMonitor->setPlaceholder( tr("Click an input box to put it on Preview") );
        _programMonitor->setPlaceholder( tr("Nothing on air") );
    }
    refreshButtons();
}

void
SwitcherPanel::onTransitionChanged()
{
    if (!_program) {
        return;
    }

    // The T-bar follows automatic transitions, but not while it is held.
    if ( !_tbar->isSliderDown() ) {
        const bool blocked = _tbar->blockSignals(true);
        _tbar->setValue( int(_program->transitionPosition() * kSwitcherTBarSteps + 0.5) );
        _tbar->blockSignals(blocked);
    }
    refreshButtons();
}

void
SwitcherPanel::refreshButtons()
{
    const bool hasPreview = _program && !_program->preview().isEmpty();
    const bool hasProgram = _program && !_program->program().isEmpty();
    const bool black = _program && _program->isBlack();

    _cutButton->setEnabled(hasPreview);
    _fadeButton->setEnabled( hasPreview && !_program->inAutoTransition() );
    _tbar->setEnabled(hasPreview);

    const bool blocked = _fadeToBlackButton->blockSignals(true);
    _fadeToBlackButton->setChecked(black);
    _fadeToBlackButton->blockSignals(blocked);
    // Called on every transition step: restyle only when it changes.
    const QString style = black ? QString::fromUtf8("QPushButton { background: %1; color: white; font-weight: bold; }").arg( rgb( Switcher::blackColor() ) )
                                : QString();
    if (_fadeToBlackButton->styleSheet() != style) {
        _fadeToBlackButton->setStyleSheet(style);
    }

    if (black) {
        _onAirLabel->setText( tr("BLACK") );
    } else if (hasProgram) {
        _onAirLabel->setText( tr("ON AIR") );
    } else {
        _onAirLabel->clear();
    }
}

void
SwitcherPanel::onRefreshTimer()
{
    if ( !isVisible() || !_program ) {
        return;
    }

    const QString previewState = inputState( _program->preview() );
    _previewMonitor->setImage( inputImage( _program->preview(), _previewMonitor->width() ) );
    _previewMonitor->setBadge(previewState);
    _previewStatus->setText(previewState);

    // Program: what the output sends.
    _programMonitor->setImage( _program->program().isEmpty() && !_program->inTransition() ? QImage()
                               : _program->outputPreview( _programMonitor->width() ) );
    _programMonitor->setBadge( _program->inTransition() ? tr("Fade %1%").arg( int(_program->transitionPosition() * 100 + 0.5) )
                               : inputState( _program->program() ) );

    const QString error = _program->outputError();
    if ( !error.isEmpty() ) {
        _programStatus->setText( tr("NDI source %1 not available: %2").arg( _program->sourceName() ).arg(error) );
    } else {
        const int connections = _program->connections();
        _programStatus->setText( (connections < 0) ? tr("NDI source: %1").arg( _program->sourceName() )
                                 : (connections == 1) ? tr("NDI source: %1  \xC2\xB7  1 receiver").arg( _program->sourceName() )
                                 : tr("NDI source: %1  \xC2\xB7  %2 receivers").arg( _program->sourceName() ).arg(connections) );
    }
    _programStatus->setToolTip( _programStatus->text() );
}

void
SwitcherPanel::onCutClicked()
{
    if (_program) {
        _program->cut();
    }
}

void
SwitcherPanel::onFadeClicked()
{
    if (_program) {
        _program->fade( _durationSpin->value() );
    }
}

void
SwitcherPanel::onFadeToBlackClicked()
{
    if (_program) {
        _program->fadeToBlack( _durationSpin->value() );
    }
    refreshButtons();
}

void
SwitcherPanel::onTBarMoved(int value)
{
    if (!_program || _tbarLatched) {
        return;
    }
    _program->setManualPosition( double(value) / kSwitcherTBarSteps );
    if (value >= kSwitcherTBarSteps) {
        // Done: the bar goes back once released (moving it now would start
        // another transition).
        _tbarLatched = true;
    }
}

void
SwitcherPanel::onTBarReleased()
{
    _tbarLatched = false;
    onTransitionChanged();
}

void
SwitcherPanel::onDurationChanged(int ms)
{
    QSettings settings;

    settings.setValue(QString::fromUtf8(kSwitcherSettingsDuration), ms);
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_SwitcherPanel.cpp"
