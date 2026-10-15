#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

struct Scene; // SceneStore.h

// The dashboard's scene shortcuts: a key combination per scene puts all its
// inputs on Program (see ProgramOutput::cutTo(Layers)). Key combinations
// are QKeySequence::PortableText. Saved in the application's QSettings as
// scene id -> keys (empty: none); scenes not saved get the default of
// their place in the list: Ctrl+1 to Ctrl+9 for the first nine.
namespace SceneShortcuts
{
const int kDefaults = 9;

// Ctrl+<index + 1> for the first kDefaults places, empty after.
QString defaultShortcut(int index);

// The shortcut of each scene that has one (scene id -> keys): the saved
// ones, else the default of the scene's place, unless another scene uses
// it.
QHash<QString, QString> shortcuts(const QList<Scene> &scenes, const QVariantMap &saved);
QHash<QString, QString> load(const QList<Scene> &scenes); // with the saved ones
QVariantMap saved();
// Every scene given (empty keys: none), so a scene keeps its shortcut when
// the list is reordered; scenes not given (removed) are dropped.
void save(const QVariantMap &shortcuts);

// A key combination that cannot be used: set for two rows (otherRow), or
// already used by another action (otherRow -1, taken: what uses it).
struct Conflict
{
  int row;
  int otherRow;
  QString keys;
  QString taken;
};

// keys: per row (empty: none); taken: other shortcuts (keys -> what they
// do). In row order; a row set like an earlier one conflicts with the
// first of them.
QList<Conflict> conflicts(const QList<QString> &keys, const QHash<QString, QString> &taken);
}
