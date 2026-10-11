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
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/KvGuiUtils.h"
#include "Gui/SwitcherWidgets.h"

#include "Custom/ndi/NdiManager.h"
#include "Custom/ndi/PlayoutChannel.h"
#include "Custom/scene/SceneStore.h"

#include <cmath>

// QSettings keys: transition duration, output format.
#define kSwitcherSettingsDuration "dashboard/transitionMs"
#define kSwitcherSettingsWidth "dashboard/programWidth"
#define kSwitcherSettingsHeight "dashboard/programHeight"
#define kSwitcherSettingsFps "dashboard/programFps"
#define kSwitcherDefaultDurationMs 500
#define kSwitcherRefreshMs 40 // monitors and timelines: 25 times/s
#define kSwitcherTBarSteps 1000
#define kSwitcherLayerRows 3  // the layer lists show this many layers before scrolling

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

QString
formatName(int width,
           int height)
{
    return QString::fromUtf8("%1 \xC3\x97 %2").arg(width).arg(height); // multiplication sign
}

QToolButton*
smallButton(const QString& text,
            const QString& tooltip,
            QWidget* parent)
{
    QToolButton* button = new QToolButton(parent);

    button->setText(text);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(false);

    return button;
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
    , _layerMonitor(0)
    , _onAirLabel(0)
    , _formatCombo(0)
    , _fpsCombo(0)
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
    layout->addWidget(createBus(::ProgramOutput::ePreview, tr("Preview"), Switcher::previewColor()), 1);
    QWidget* transitions = createTransitions();
    layout->addWidget(transitions);
    layout->addWidget(createBus(::ProgramOutput::eProgram, tr("Program"), Switcher::programColor()), 1);

    // ON AIR / BLACK, in the Program title bar.
    QLabel* programTitle = _buses[::ProgramOutput::eProgram].title;
    _onAirLabel = new QLabel(programTitle);
    QFont bold = _onAirLabel->font();
    bold.setBold(true);
    _onAirLabel->setFont(bold);
    QHBoxLayout* titleLayout = new QHBoxLayout(programTitle);
    titleLayout->setContentsMargins(6, 0, 6, 0);
    titleLayout->addStretch();
    titleLayout->addWidget(_onAirLabel);

    // Settings from the last run.
    QSettings settings;
    _durationSpin->setValue( settings.value(QString::fromUtf8(kSwitcherSettingsDuration), kSwitcherDefaultDurationMs).toInt() );
    applyFormat( settings.value(QString::fromUtf8(kSwitcherSettingsWidth), 1920).toInt(),
                 settings.value(QString::fromUtf8(kSwitcherSettingsHeight), 1080).toInt(),
                 settings.value(QString::fromUtf8(kSwitcherSettingsFps), 30.0).toDouble() );

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
        setEnabled(false);
        for (int b = 0; b < 2; ++b) {
            _buses[b].monitor->setPlaceholder( tr("The Program output is not available.") );
        }
    }
    onBusChanged();
    onTransitionChanged();
}

SwitcherPanel::~SwitcherPanel()
{
}

QWidget*
SwitcherPanel::createBus(::ProgramOutput::Bus bus,
                         const QString& title,
                         const QColor& color)
{
    BusWidgets& w = _buses[bus];
    QWidget* box = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    w.bus = bus;

    // "Preview: Scene 1 - intro (+2)", white on the tally color.
    w.title = new QLabel(title, box);
    w.title->setTextFormat(Qt::PlainText);
    w.title->setMinimumWidth(1);
    w.title->setStyleSheet( QString::fromUtf8("QLabel { background: %1; color: white; font-weight: bold; padding: 3px 6px; border-radius: 2px; }")
                            .arg( rgb(color) ) );
    layout->addWidget(w.title);

    if (bus == ::ProgramOutput::ePreview) {
        _layerMonitor = new LayerMonitor(box);
        _layerMonitor->setToolTip( tr("Drag a layer to move it, a corner to resize it; arrow keys nudge it. Right-click to centre or reset it.") );
        w.monitor = _layerMonitor;
        QObject::connect( _layerMonitor, SIGNAL(layerSelected(QString)), this, SLOT(onMonitorLayerSelected(QString)) );
        QObject::connect( _layerMonitor, SIGNAL(placementChanging(QString,LayerPlacement)), this, SLOT(onPlacementChanging(QString,LayerPlacement)) );
        QObject::connect( _layerMonitor, SIGNAL(placementChanged(QString,LayerPlacement)), this, SLOT(onPlacementChanged(QString,LayerPlacement)) );
    } else {
        w.monitor = new VideoMonitor(box);
    }
    w.monitor->setPlaceholder( (bus == ::ProgramOutput::ePreview) ? tr("Click an input box to put it on Preview; + adds layers")
                                                                : tr("Nothing on air") );
    w.monitor->setCanvasFrame(true);
    layout->addWidget(w.monitor, 1);

    // Layers, the top one first, with [up][down][remove].
    QHBoxLayout* layerRow = new QHBoxLayout;
    layerRow->setSpacing(2);
    w.layers = new QListWidget(box);
    KvGui::styleList(w.layers);
    w.layers->setFixedHeight( kSwitcherLayerRows * (w.layers->fontMetrics().height() + 6) + 4 );
    w.layers->setToolTip( (bus == ::ProgramOutput::ePreview) ? tr("Layers on Preview, the top one first. Add them with the + of the input boxes.")
                                                           : tr("Layers on air, the top one first.") );
    w.layers->setProperty("bus", int(bus));
    layerRow->addWidget(w.layers, 1);
    QVBoxLayout* layerButtons = new QVBoxLayout;
    layerButtons->setSpacing(2);
    w.up = smallButton( QString::fromUtf8("\xE2\x96\xB2"), tr("Move the layer up"), box ); // black up-pointing triangle
    w.down = smallButton( QString::fromUtf8("\xE2\x96\xBC"), tr("Move the layer down"), box );
    w.remove = smallButton( QString::fromUtf8("\xE2\x9C\x95"), (bus == ::ProgramOutput::ePreview) ? tr("Remove the layer from Preview")
                                                                                              : tr("Take the layer off air"), box ); // multiplication x
    QToolButton* layerTools[] = { w.up, w.down, w.remove };
    for (int i = 0; i < 3; ++i) {
        layerTools[i]->setProperty("bus", int(bus));
        layerButtons->addWidget(layerTools[i]);
    }
    layerButtons->addStretch();
    layerRow->addLayout(layerButtons);
    layout->addLayout(layerRow);

    // Timeline, then [|<>][play][pause][stop][<>|]  [Cue][Continue][+cue]  time
    w.timeline = new BusTimeline(box);
    w.timeline->setProperty("bus", int(bus));
    w.timeline->setToolTip( tr("Drag to scrub the layers. Diamonds are cue points, where Cue and Play stop: "
                               "drag to move, double-click to add or delete, right-click for more.") );
    layout->addWidget(w.timeline);

    QHBoxLayout* transport = new QHBoxLayout;
    transport->setSpacing(2);
    struct Button { const char* name; const char* text; const char* tip; };
    const Button buttons[] = {
        { "previous", "\xE2\x97\x86\xE2\x97\x80", QT_TR_NOOP("Previous cue point") },   // diamond, left triangle
        { "play", "\xE2\x96\xB6", QT_TR_NOOP("Play the layers, continuing past cue points") },
        { "pause", "\xE2\x9D\x9A\xE2\x9D\x9A", QT_TR_NOOP("Pause the layers") },
        { "stop", "\xE2\x96\xA0", QT_TR_NOOP("Stop the layers (they are cleared)") },
        { "next", "\xE2\x96\xB6\xE2\x97\x86", QT_TR_NOOP("Next cue point") },
        { "cue", QT_TR_NOOP("Cue"), QT_TR_NOOP("Play the layers from the start to their first cue point") },
        { "continue", QT_TR_NOOP("Continue"), QT_TR_NOOP("Play the layers on to their next cue point") },
        { "addCue", "+\xE2\x97\x86", QT_TR_NOOP("Add a cue point at the playhead to every layer") },
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        const QString name = QString::fromUtf8(buttons[i].name);
        const bool textButton = (name == QString::fromUtf8("cue")) || (name == QString::fromUtf8("continue"));
        QToolButton* button = smallButton( textButton ? tr(buttons[i].text) : QString::fromUtf8(buttons[i].text), tr(buttons[i].tip), box );
        button->setObjectName(name);
        button->setProperty("bus", int(bus));
        QObject::connect( button, SIGNAL(clicked()), this, SLOT(onTransportClicked()) );
        transport->addWidget(button);
        if ( (name == QString::fromUtf8("next")) || (name == QString::fromUtf8("continue")) ) {
            transport->addSpacing(8);
        }
    }
    transport->addStretch();
    w.time = new QLabel(box);
    w.time->setTextFormat(Qt::PlainText);
    transport->addWidget(w.time);
    layout->addLayout(transport);

    w.status = KvGui::secondaryLabel(box);
    w.status->setMinimumWidth(1);
    layout->addWidget(w.status);

    QObject::connect( w.layers, SIGNAL(itemSelectionChanged()), this, SLOT(onLayerSelectionChanged()) );
    QObject::connect( w.up, SIGNAL(clicked()), this, SLOT(onLayerUpClicked()) );
    QObject::connect( w.down, SIGNAL(clicked()), this, SLOT(onLayerDownClicked()) );
    QObject::connect( w.remove, SIGNAL(clicked()), this, SLOT(onLayerRemoveClicked()) );
    QObject::connect( w.timeline, SIGNAL(seekRequested(double)), this, SLOT(onTimelineSeek(double)) );
    QObject::connect( w.timeline, SIGNAL(markerAdded(double)), this, SLOT(onMarkerAdded(double)) );
    QObject::connect( w.timeline, SIGNAL(markerMoved(double,double)), this, SLOT(onMarkerMoved(double,double)) );
    QObject::connect( w.timeline, SIGNAL(markerRemoved(double)), this, SLOT(onMarkerRemoved(double)) );

    return box;
}

QWidget*
SwitcherPanel::createTransitions()
{
    QWidget* transitions = new QWidget(this);
    QVBoxLayout* column = new QVBoxLayout(transitions);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);

    // Output format of both buses.
    _formatCombo = new QComboBox(transitions);
    _formatCombo->setToolTip( tr("Size of Preview and Program (and of the Program NDI source)") );
    const QSize presets[] = { QSize(1920, 1080), QSize(1280, 720), QSize(3840, 2160), QSize(1080, 1920), QSize(720, 576) };
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
        _formatCombo->addItem( formatName( presets[i].width(), presets[i].height() ), presets[i] );
    }
    _formatCombo->addItem( tr("Custom..."), QSize() );
    _fpsCombo = new QComboBox(transitions);
    _fpsCombo->setToolTip( tr("Frame rate of the Program NDI source") );
    const double rates[] = { 23.976, 24, 25, 29.97, 30, 50, 59.94, 60 };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        _fpsCombo->addItem( tr("%1 fps").arg(rates[i]), rates[i] );
    }

    _cutButton = new QPushButton(tr("Cut"), transitions);
    _cutButton->setToolTip( tr("Put Preview on air at once. The layers on Program go to Preview.") );
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

    column->addWidget(_formatCombo);
    column->addWidget(_fpsCombo);
    column->addSpacing(8);
    column->addWidget(_cutButton);
    column->addWidget(_fadeButton);
    column->addWidget(_durationSpin);
    column->addSpacing(6);
    column->addWidget(_fadeToBlackButton);
    column->addSpacing(6);
    column->addWidget(_tbar, 1, Qt::AlignHCenter);
    transitions->setFixedWidth( qMax( 110, qMax( _formatCombo->sizeHint().width(), _durationSpin->sizeHint().width() ) + 4 ) );

    QObject::connect( _formatCombo, SIGNAL(activated(int)), this, SLOT(onFormatChosen(int)) );
    QObject::connect( _fpsCombo, SIGNAL(activated(int)), this, SLOT(onFpsChosen(int)) );
    QObject::connect( _cutButton, SIGNAL(clicked()), this, SLOT(onCutClicked()) );
    QObject::connect( _fadeButton, SIGNAL(clicked()), this, SLOT(onFadeClicked()) );
    QObject::connect( _fadeToBlackButton, SIGNAL(clicked()), this, SLOT(onFadeToBlackClicked()) );
    QObject::connect( _tbar, SIGNAL(sliderMoved(int)), this, SLOT(onTBarMoved(int)) );
    QObject::connect( _tbar, SIGNAL(sliderReleased()), this, SLOT(onTBarReleased()) );
    QObject::connect( _durationSpin, SIGNAL(valueChanged(int)), this, SLOT(onDurationChanged(int)) );

    return transitions;
}

SwitcherPanel::BusWidgets&
SwitcherPanel::busOf(QObject* sender)
{
    const int bus = sender ? sender->property("bus").toInt() : 0;

    return _buses[ (bus == ::ProgramOutput::eProgram) ? ::ProgramOutput::eProgram : ::ProgramOutput::ePreview ];
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

QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> >
SwitcherPanel::channels(::ProgramOutput::Bus bus) const
{
    QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > out;

    if (!_program) {
        return out;
    }
    const ::ProgramOutput::Layers layers = _program->layers(bus);
    for (int i = 0; i < layers.size(); ++i) {
        ::PlayoutChannel* channel = _ndi ? _ndi->channel(layers.at(i).sceneId, layers.at(i).item) : 0;
        out << qMakePair(layers.at(i), channel);
    }

    return out;
}

::ProgramOutput::Source
SwitcherPanel::selectedLayer(const BusWidgets& w) const
{
    const QList<QListWidgetItem*> selected = w.layers->selectedItems();

    if ( selected.isEmpty() ) {
        return ::ProgramOutput::Source();
    }

    return ::ProgramOutput::Source( selected.front()->data(Qt::UserRole).toString(), selected.front()->data(Qt::UserRole + 1).toString() );
}

void
SwitcherPanel::refreshLayerList(BusWidgets& w)
{
    const ::ProgramOutput::Source keep = selectedLayer(w);
    const ::ProgramOutput::Layers layers = _program ? _program->layers(w.bus) : ::ProgramOutput::Layers();
    const bool blocked = w.layers->blockSignals(true);

    w.layers->clear();
    // Top layer first, numbered from the bottom as they stack.
    for (int i = layers.size() - 1; i >= 0; --i) {
        const QString label = sourceLabel( layers.at(i) );
        QListWidgetItem* item = new QListWidgetItem( QString::fromUtf8("%1   %2").arg(i + 1).arg(label) );
        item->setData( Qt::UserRole, layers.at(i).sceneId );
        item->setData( Qt::UserRole + 1, layers.at(i).item );
        item->setToolTip(label);
        w.layers->addItem(item);
        if (layers.at(i) == keep) {
            item->setSelected(true);
        }
    }
    w.layers->blockSignals(blocked);

    // Title: the top layer, and how many are under it.
    const QString base = (w.bus == ::ProgramOutput::ePreview) ? tr("Preview") : tr("Program");
    if ( layers.isEmpty() ) {
        w.title->setText(base);
    } else if (layers.size() == 1) {
        w.title->setText( tr("%1: %2").arg(base).arg( sourceLabel( layers.last() ) ) );
    } else {
        w.title->setText( tr("%1: %2 (+%3)").arg(base).arg( sourceLabel( layers.last() ) ).arg(layers.size() - 1) );
    }

    // Layers that cannot show: their scene's NDI output is off.
    QStringList off;
    const QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > list = channels(w.bus);
    for (int i = 0; i < list.size(); ++i) {
        if (!list.at(i).second) {
            off << sourceLabel(list.at(i).first);
        }
    }
    if (w.bus == ::ProgramOutput::ePreview) {
        w.status->setText( off.isEmpty() ? QString() : tr("Not shown (NDI output of the scene off): %1").arg( off.join( QString::fromUtf8(", ") ) ) );
    }
}

void
SwitcherPanel::refreshTimeline(BusWidgets& w)
{
    // The bus runs as long as its longest layer; the playhead follows it.
    const QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > list = channels(w.bus);
    double duration = 0;
    double position = 0;
    double fps = 0;
    QList<double> markers;

    for (int i = 0; i < list.size(); ++i) {
        ::PlayoutChannel* channel = list.at(i).second;
        if (!channel) {
            continue;
        }
        const ::PlayoutChannel::Status st = channel->status();
        if ( (st.state != ::PlayoutChannel::eLive) && (st.duration() > duration) ) {
            duration = st.duration();
            position = st.seconds();
            fps = st.fps;
        }
        // Cue points as saved in the scene (also those of layers not loaded yet).
        ::Scene scene;
        if ( _scenes && _scenes->scene(list.at(i).first.sceneId, &scene) ) {
            const QList<double> points = scene.cuePoints(list.at(i).first.item);
            for (int p = 0; p < points.size(); ++p) {
                bool known = false;
                for (int m = 0; m < markers.size() && !known; ++m) {
                    known = std::fabs(markers.at(m) - points.at(p)) < 0.001;
                }
                if (!known) {
                    markers << points.at(p);
                }
            }
        }
    }
    w.timeline->setRange(duration, fps);
    w.timeline->setPosition( qMin(position, duration) );
    w.timeline->setMarkers(markers);
    w.time->setText( (duration > 0) ? QString::fromUtf8("%1 / %2").arg( formatTime( qMin(position, duration) ) ).arg( formatTime(duration) )
                                    : QString() );
}

void
SwitcherPanel::onBusChanged()
{
    // Labels of the layers now on the buses (scene renames included).
    _labels.clear();
    for (int b = 0; b < 2; ++b) {
        const ::ProgramOutput::Layers layers = _program ? _program->layers( ::ProgramOutput::Bus(b) ) : ::ProgramOutput::Layers();
        for (int i = 0; i < layers.size(); ++i) {
            _labels.insert( layerKey( layers.at(i) ), sourceLabel( layers.at(i) ) );
        }
    }
    for (int b = 0; b < 2; ++b) {
        refreshLayerList(_buses[b]);
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
    const bool hasPreview = _program && !_program->layers(::ProgramOutput::ePreview).isEmpty();
    const bool hasProgram = _program && !_program->layers(::ProgramOutput::eProgram).isEmpty();
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

    for (int b = 0; b < 2; ++b) {
        BusWidgets& w = _buses[b];
        const ::ProgramOutput::Source selected = selectedLayer(w);
        const ::ProgramOutput::Layers layers = _program ? _program->layers(w.bus) : ::ProgramOutput::Layers();
        const int index = layers.indexOf(selected);
        w.up->setEnabled( index >= 0 && index < layers.size() - 1 );
        w.down->setEnabled(index > 0);
        w.remove->setEnabled(index >= 0);
    }
}

void
SwitcherPanel::onRefreshTimer()
{
    if ( !isVisible() || !_program ) {
        return;
    }

    BusWidgets& preview = _buses[::ProgramOutput::ePreview];
    BusWidgets& program = _buses[::ProgramOutput::eProgram];

    preview.monitor->setImage( _program->layers(::ProgramOutput::ePreview).isEmpty() ? QImage()
                               : _program->previewImage( preview.monitor->width() ) );
    program.monitor->setImage( _program->layers(::ProgramOutput::eProgram).isEmpty() && !_program->inTransition() ? QImage()
                               : _program->outputPreview( program.monitor->width() ) );
    program.monitor->setBadge( _program->inTransition() ? tr("Fade %1%").arg( int(_program->transitionPosition() * 100 + 0.5) )
                               : QString() );
    refreshTimeline(preview);
    refreshTimeline(program);
    refreshLayerBoxes();

    const QString error = _program->outputError();
    if ( !error.isEmpty() ) {
        program.status->setText( tr("NDI source %1 not available: %2").arg( _program->sourceName() ).arg(error) );
    } else {
        const int connections = _program->connections();
        program.status->setText( (connections < 0) ? tr("NDI source: %1").arg( _program->sourceName() )
                                 : (connections == 1) ? tr("NDI source: %1  \xC2\xB7  1 receiver").arg( _program->sourceName() )
                                 : tr("NDI source: %1  \xC2\xB7  %2 receivers").arg( _program->sourceName() ).arg(connections) );
    }
    program.status->setToolTip( program.status->text() );
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

void
SwitcherPanel::applyFormat(int width,
                           int height,
                           double fps)
{
    if ( (width <= 0) || (height <= 0) ) {
        width = 1920;
        height = 1080;
    }
    if (fps <= 0) {
        fps = 30;
    }
    if (_program) {
        _program->setFormat(width, height, fps);
    }
    for (int b = 0; b < 2; ++b) {
        _buses[b].monitor->setAspect(width, height);
    }
    _layerMonitor->setCanvas(width, height);

    // Show it in the combos (a custom size gets its own entry).
    int index = _formatCombo->findData( QSize(width, height) );
    if (index < 0) {
        _formatCombo->insertItem( _formatCombo->count() - 1, formatName(width, height), QSize(width, height) );
        index = _formatCombo->findData( QSize(width, height) );
    }
    _formatCombo->setCurrentIndex(index);
    int fpsIndex = 0;
    for (int i = 0; i < _fpsCombo->count(); ++i) {
        if (std::fabs(_fpsCombo->itemData(i).toDouble() - fps) < 0.001) {
            fpsIndex = i;
        }
    }
    _fpsCombo->setCurrentIndex(fpsIndex);

    QSettings settings;
    settings.setValue(QString::fromUtf8(kSwitcherSettingsWidth), width);
    settings.setValue(QString::fromUtf8(kSwitcherSettingsHeight), height);
    settings.setValue(QString::fromUtf8(kSwitcherSettingsFps), fps);
}

void
SwitcherPanel::onFormatChosen(int index)
{
    QSize size = _formatCombo->itemData(index).toSize();
    const double fps = _fpsCombo->itemData( _fpsCombo->currentIndex() ).toDouble();

    if ( !size.isValid() ) {
        // Custom...: ask for the size.
        QDialog dialog(this);
        dialog.setWindowTitle( tr("Output Format") );
        QFormLayout* form = new QFormLayout(&dialog);
        QSpinBox* widthSpin = new QSpinBox(&dialog);
        QSpinBox* heightSpin = new QSpinBox(&dialog);
        widthSpin->setRange(16, 8192);
        heightSpin->setRange(16, 8192);
        widthSpin->setValue( _program ? _program->width() : 1920 );
        heightSpin->setValue( _program ? _program->height() : 1080 );
        form->addRow(tr("Width:"), widthSpin);
        form->addRow(tr("Height:"), heightSpin);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        form->addRow(buttons);
        QObject::connect( buttons, SIGNAL(accepted()), &dialog, SLOT(accept()) );
        QObject::connect( buttons, SIGNAL(rejected()), &dialog, SLOT(reject()) );
        if (dialog.exec() != QDialog::Accepted) {
            applyFormat(_program ? _program->width() : 1920, _program ? _program->height() : 1080, fps); // back to the current one
            return;
        }
        size = QSize( widthSpin->value(), heightSpin->value() );
    }
    applyFormat(size.width(), size.height(), fps);
}

void
SwitcherPanel::onFpsChosen(int index)
{
    applyFormat( _program ? _program->width() : 1920, _program ? _program->height() : 1080, _fpsCombo->itemData(index).toDouble() );
}

// ----- Layers -----

void
SwitcherPanel::onLayerSelectionChanged()
{
    BusWidgets& w = busOf( sender() );

    if (w.bus == ::ProgramOutput::ePreview) {
        const ::ProgramOutput::Source source = selectedLayer(w);
        _layerMonitor->setSelected( source.isEmpty() ? QString() : layerKey(source) );
    }
    refreshButtons();
}

// ----- Placing Preview layers -----

QString
SwitcherPanel::layerKey(const ::ProgramOutput::Source& source)
{
    return source.sceneId + QLatin1Char('|') + source.item;
}

::ProgramOutput::Source
SwitcherPanel::previewLayer(const QString& key) const
{
    const ::ProgramOutput::Layers layers = _program ? _program->layers(::ProgramOutput::ePreview) : ::ProgramOutput::Layers();

    for (int i = 0; i < layers.size(); ++i) {
        if (layerKey( layers.at(i) ) == key) {
            return layers.at(i);
        }
    }

    return ::ProgramOutput::Source();
}

void
SwitcherPanel::refreshLayerBoxes()
{
    QList<LayerMonitor::Box> boxes;
    const QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > list = channels(::ProgramOutput::ePreview);

    for (int i = 0; i < list.size(); ++i) {
        LayerMonitor::Box box;
        box.key = layerKey(list.at(i).first);
        box.label = _labels.value(box.key);
        box.placement = _program->placement(::ProgramOutput::ePreview, list.at(i).first);
        // Its frame's size, to fit it as the mixer does.
        QByteArray frame;
        int width = 0, height = 0;
        if ( list.at(i).second && list.at(i).second->currentFrame(&frame, &width, &height) ) {
            box.size = QSize(width, height);
        }
        boxes << box;
    }
    _layerMonitor->setBoxes(boxes);
}

void
SwitcherPanel::onMonitorLayerSelected(const QString& key)
{
    BusWidgets& w = _buses[::ProgramOutput::ePreview];

    for (int i = 0; i < w.layers->count(); ++i) {
        QListWidgetItem* item = w.layers->item(i);
        const ::ProgramOutput::Source source( item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString() );
        item->setSelected(layerKey(source) == key);
    }
}

void
SwitcherPanel::onPlacementChanging(const QString& key,
                                   const LayerPlacement& placement)
{
    const ::ProgramOutput::Source source = previewLayer(key);

    if ( _program && !source.isEmpty() ) {
        _program->setPlacement(::ProgramOutput::ePreview, source, placement);
    }
}

void
SwitcherPanel::onPlacementChanged(const QString& key,
                                  const LayerPlacement& placement)
{
    const ::ProgramOutput::Source source = previewLayer(key);

    if ( !_program || source.isEmpty() ) {
        return;
    }
    _program->setPlacement(::ProgramOutput::ePreview, source, placement);
    // Where it goes next time it is layered.
    if (_scenes) {
        _scenes->setLayerPlacement(source.sceneId, source.item, placement);
    }
}

void
SwitcherPanel::onLayerUpClicked()
{
    BusWidgets& w = busOf( sender() );

    if (_program) {
        _program->moveLayer( w.bus, selectedLayer(w), 1 );
    }
}

void
SwitcherPanel::onLayerDownClicked()
{
    BusWidgets& w = busOf( sender() );

    if (_program) {
        _program->moveLayer( w.bus, selectedLayer(w), -1 );
    }
}

void
SwitcherPanel::onLayerRemoveClicked()
{
    BusWidgets& w = busOf( sender() );

    if (_program) {
        _program->removeLayer( w.bus, selectedLayer(w) );
    }
}

// ----- Timelines -----

void
SwitcherPanel::onTransportClicked()
{
    QObject* button = sender();

    if (!button) {
        return;
    }
    BusWidgets& w = busOf(button);
    const QString action = button->objectName();

    if ( action == QString::fromUtf8("previous") || action == QString::fromUtf8("next") ) {
        const double to = ( action == QString::fromUtf8("previous") ) ? w.timeline->previousMarker() : w.timeline->nextMarker();
        if (to >= 0) {
            onTimelineSeek(to); // sender: the button, of the same bus
        }

        return;
    }
    if ( action == QString::fromUtf8("addCue") ) {
        editCuePoints( w.bus, -1, w.timeline->position() );

        return;
    }

    const QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > list = channels(w.bus);
    for (int i = 0; i < list.size(); ++i) {
        ::PlayoutChannel* channel = list.at(i).second;
        if (!channel) {
            continue;
        }
        if ( action == QString::fromUtf8("play") ) {
            channel->play();
        } else if ( action == QString::fromUtf8("pause") ) {
            channel->pause();
        } else if ( action == QString::fromUtf8("stop") ) {
            channel->stop();
        } else if ( action == QString::fromUtf8("cue") ) {
            channel->cue();
        } else if ( ( action == QString::fromUtf8("continue") ) && (channel->status().state == ::PlayoutChannel::ePaused) ) {
            channel->play();
        }
    }
}

void
SwitcherPanel::onTimelineSeek(double seconds)
{
    const QList<QPair< ::ProgramOutput::Source, ::PlayoutChannel*> > list = channels( busOf( sender() ).bus );

    // Each layer clamps to its own length.
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).second) {
            list.at(i).second->seek(seconds);
        }
    }
}

void
SwitcherPanel::onMarkerAdded(double seconds)
{
    editCuePoints(busOf( sender() ).bus, -1, seconds);
}

void
SwitcherPanel::onMarkerMoved(double from,
                             double to)
{
    editCuePoints(busOf( sender() ).bus, from, to);
}

void
SwitcherPanel::onMarkerRemoved(double seconds)
{
    editCuePoints(busOf( sender() ).bus, seconds, -1);
}

void
SwitcherPanel::editCuePoints(::ProgramOutput::Bus bus,
                             double from,
                             double to)
{
    if (!_program || !_scenes) {
        return;
    }

    // Points within half a frame are the same one.
    const BusWidgets& w = _buses[bus];
    const double tolerance = 0.5 / ( (w.timeline->fps() > 0) ? w.timeline->fps() : 25 );
    const ::ProgramOutput::Layers layers = _program->layers(bus);

    for (int i = 0; i < layers.size(); ++i) {
        ::Scene scene;
        if ( !_scenes->scene(layers.at(i).sceneId, &scene) ) {
            continue;
        }
        QList<double> points = scene.cuePoints(layers.at(i).item);
        bool found = false;
        if (from >= 0) {
            for (int p = points.size() - 1; p >= 0; --p) {
                if (std::fabs(points.at(p) - from) < tolerance) {
                    points.removeAt(p);
                    found = true;
                }
            }
        }
        // Moving: only the layers that had it. Adding: every layer, once.
        bool present = false;
        for (int p = 0; p < points.size() && !present; ++p) {
            present = std::fabs(points.at(p) - to) < tolerance;
        }
        if ( (to >= 0) && ( (from < 0) || found ) && !present ) {
            points << to;
        }
        _scenes->setNdiCuePoints(layers.at(i).sceneId, layers.at(i).item, points);
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_SwitcherPanel.cpp"
