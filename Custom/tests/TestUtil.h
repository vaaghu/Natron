#pragma once

#include "Custom/ndi/FrameSink.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <ostream>

// Readable gtest failure messages for Qt strings.
inline void PrintTo(const QString &s, std::ostream *os)
{
  *os << '"' << s.toStdString() << '"';
}

inline void PrintTo(const QStringList &list, std::ostream *os)
{
  *os << '[';
  for (int i = 0; i < list.size(); ++i)
  {
    *os << (i ? ", \"" : "\"") << list.at(i).toStdString() << '"';
  }
  *os << ']';
}

inline void PrintTo(const QByteArray &bytes, std::ostream *os)
{
  *os << '"' << bytes.constData() << '"';
}

namespace TestUtil
{
// Runs the event loop until condition() is true. False on timeout.
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000);

// Runs the event loop for a while (to check that something does not happen).
void processEventsFor(int ms);

bool writeFile(const QString &path, const QByteArray &data);
QByteArray readFile(const QString &path);

// A /bin/sh script made executable (POSIX only).
bool writeScript(const QString &path, const QByteArray &body);

// A w x h image filled with color; format from the suffix unless given.
bool writeImage(const QString &path, int width, int height, QRgb color = 0xff336699, const char *format = nullptr);

// A node of a minimal Natron project file (.ntp), as readProjectInfo reads it.
struct Node
{
  QString pluginId;
  QString name;  // script name
  QString label;
  QString stateKey; // bound store key (null: none)
  QString filename; // "filename" parameter (null: none)
  QList<Node> children; // nodes inside a group
};

Node textNode(const QString &name, const QString &stateKey, const QString &label = QString());
Node readNode(const QString &name, const QString &stateKey, const QString &label = QString());
Node writeNode(const QString &name, const QString &filename, const QString &pluginId = QString());
Node groupNode(const QString &name, const QList<Node> &children);

// fps <= 0: not saved (Natron's default 24).
QByteArray projectXml(const QList<Node> &nodes, double fps = 0);
bool writeProject(const QString &path, const QList<Node> &nodes, double fps = 0);

// A port free right now on localhost.
quint16 freeTcpPort();
quint16 freeUdpPort();
}

// FrameSink recording what it is given, per source name. Sinks whose name
// starts with "fail" cannot open ("boom"). Thread-safe.
struct FakeSinkRecord
{
  int opened;
  int frames;
  bool alpha;
  bool clocked;
  QByteArray lastFrame;
  int width;
  int height;
  int fpsNum;
  int fpsDen;

  FakeSinkRecord()
      : opened(0),
        frames(0),
        alpha(false),
        clocked(false),
        width(0),
        height(0),
        fpsNum(0),
        fpsDen(0)
  {
  }
};

const int kFakeSinkConnections = 2;

FrameSink *createFakeSink();
FakeSinkRecord fakeSinkRecord(const QString &name);
