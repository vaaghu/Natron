#pragma once

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

  // Output file paths of the Write nodes, as entered ([Project] resolved).
  // May contain frame patterns such as ### or %04d.
  QStringList outputs;

  ProjectInfo()
      : ok(false)
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
