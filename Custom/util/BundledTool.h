#pragma once

#include <QProcessEnvironment>
#include <QString>

class QProcess;

// Running a tool shipped in Natron's bin folder (ffmpeg, ffprobe).
// Some packagings ship these binaries without an RPATH, so they cannot find
// the libraries in the bundle's lib folder on their own: put that folder
// first in the library search path of the child process.
QProcessEnvironment bundledToolEnvironment(const QString &toolPath);

// Applies bundledToolEnvironment() to a process before start().
void prepareBundledTool(QProcess *process, const QString &toolPath);
