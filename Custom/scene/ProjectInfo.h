#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// What the dashboard needs to know about a Natron project (.ntp) without
// opening it: read straight from the project XML.
struct ProjectInfo
{
  bool ok;
  QString error;

  // State store keys bound to nodes of the project (see StateBindingTab),
  // in order of appearance, without duplicates.
  QStringList stateKeys;

  // Nodes bound to each key, in order of appearance: label as shown in the
  // node graph, and script name (Group1.Text1 inside a group).
  struct BoundNode
  {
    QString label;
    QString name;
  };
  QHash<QString, QList<BoundNode> > keyNodes;

  // Value type each key is bound for: "text" (Text node) or "image"
  // (Read node). A key bound on both kinds of node is "text,image".
  QHash<QString, QString> keyTypes;

  // Project frame rate (Natron's default 24 when not saved in the file).
  double fps;

  // Output file paths of the Write nodes, as entered ([Project] resolved).
  // May contain frame patterns such as ### or %04d.
  QStringList outputs;

  // Write nodes (script names, as NatronRenderer -w takes them: Group1.Write1
  // for one inside a group) in order of appearance, and the output path of
  // each (same rules as outputs).
  QStringList writers;
  QHash<QString, QString> writerOutputs;

  ProjectInfo()
      : ok(false),
        fps(24.0)
  {
  }
};

// Name of the hidden string parameter holding a node's bound store key.
// Must match the parameter created in Gui/StateBindingTab.cpp.
#define kProjectInfoStateKeyParam "natronStateKey"

ProjectInfo readProjectInfo(const QString &projectFilePath);

// For an output pattern (file.mov, frame_####.png, frame_%04d.exr), the
// first existing file it refers to, or an empty string.
QString findExistingOutputFile(const QString &outputPattern);

// True if the pattern is an image sequence (### or %0Nd frame number).
bool isSequencePattern(const QString &outputPattern);

// Frame numbers of the existing files of a sequence pattern, ascending.
QList<int> existingSequenceFrames(const QString &outputPattern);

// File of a sequence pattern for one frame number.
QString sequenceFrameFile(const QString &outputPattern, int frame);
