#pragma once

#include "FrameSink.h"

#include <QString>

// NDI output, loaded at runtime from the installed NDI Runtime (libndi).
// Natron does not link or ship the NDI SDK (it is proprietary and Natron
// is GPL): if the runtime is not installed, NDI is simply unavailable.
// Get it from https://ndi.video/tools/ (NDI Tools / NDI Runtime).
namespace Ndi
{
// Loads the runtime once. Returns false (and why) if it is not available.
bool load(QString *error = nullptr);
bool isAvailable();
QString libraryPath(); // the library that was loaded

// Sink sending to an NDI source named after open()'s name.
FrameSink *createSink();
}
