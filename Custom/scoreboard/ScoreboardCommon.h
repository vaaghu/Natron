#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

class StateStore;

// Shared by the scoreboard feeds (Ultra Score console, FIBA LiveStats): the
// way they write game values, so both show the same text for the same game.
namespace Scoreboard
{
// "scoreboard."
QString prefix();

// 1-4: Q1..Q4, 5: OT, 6: 2OT, ... Empty for 0 (before the game).
QString periodText(int period);

// 7 -> "07".
QString twoDigits(int n);

// Game clock as shown: MM:SS, SS.t in the last minute.
QString clockText(int minutes, int seconds, int tenths);

// Shot clock as shown: whole seconds, S.t in the last 5.
QString shotClockText(int seconds, int tenths);

// Sets the keys whose value differs from the store's (compared as shown, so
// a saved integer equals the same double). Returns those keys.
QStringList applyChanged(StateStore *store, const QVariantMap &values);
}
