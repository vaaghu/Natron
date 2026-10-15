#include "TestUtil.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QMutexLocker>
#include <QTcpServer>
#include <QThread>
#include <QUdpSocket>

namespace TestUtil
{
bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
  QElapsedTimer timer;
  timer.start();
  while (!condition())
  {
    if (timer.elapsed() > timeoutMs)
    {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  return true;
}

void processEventsFor(int ms)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
}

bool writeFile(const QString &path, const QByteArray &data)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeScript(const QString &path, const QByteArray &body)
{
  if (!writeFile(path, "#!/bin/sh\n" + body + "\n"))
  {
    return false;
  }
  return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

bool writeImage(const QString &path, int width, int height, QRgb color, const char *format)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image.save(path, format);
}

Node textNode(const QString &name, const QString &stateKey, const QString &label)
{
  Node n;
  n.pluginId = QString::fromUtf8("net.fxarena.openfx.Text");
  n.name = name;
  n.label = label;
  n.stateKey = stateKey;
  return n;
}

Node readNode(const QString &name, const QString &stateKey, const QString &label)
{
  Node n;
  n.pluginId = QString::fromUtf8("fr.inria.built-in.Read");
  n.name = name;
  n.label = label;
  n.stateKey = stateKey;
  return n;
}

Node writeNode(const QString &name, const QString &filename, const QString &pluginId)
{
  Node n;
  n.pluginId = pluginId.isEmpty() ? QString::fromUtf8("fr.inria.built-in.Write") : pluginId;
  n.name = name;
  n.filename = filename;
  return n;
}

Node groupNode(const QString &name, const QList<Node> &children)
{
  Node n;
  n.pluginId = QString::fromUtf8("fr.inria.built-in.Group");
  n.name = name;
  n.children = children;
  return n;
}

namespace
{
QByteArray escaped(const QString &text)
{
  QString s = text;
  s.replace(QLatin1Char('&'), QString::fromUtf8("&amp;"));
  s.replace(QLatin1Char('<'), QString::fromUtf8("&lt;"));
  s.replace(QLatin1Char('>'), QString::fromUtf8("&gt;"));
  return s.toUtf8();
}

void appendNode(QByteArray &xml, const Node &node)
{
  xml += "<item>\n";
  if (!node.label.isEmpty())
  {
    xml += "<Plugin_label>" + escaped(node.label) + "</Plugin_label>\n";
  }
  xml += "<Plugin_script_name>" + escaped(node.name) + "</Plugin_script_name>\n";
  xml += "<Plugin_id>" + escaped(node.pluginId) + "</Plugin_id>\n";
  xml += "<Knobs>\n";
  if (!node.filename.isNull())
  {
    xml += "<item><Name>filename</Name><Value>" + escaped(node.filename) + "</Value></item>\n";
  }
  if (!node.stateKey.isNull())
  {
    xml += "<item><Name>natronStateKey</Name><Value>" + escaped(node.stateKey) + "</Value></item>\n";
  }
  xml += "</Knobs>\n";
  if (!node.children.isEmpty())
  {
    xml += "<Children>\n";
    for (int i = 0; i < node.children.size(); ++i)
    {
      appendNode(xml, node.children.at(i));
    }
    xml += "</Children>\n";
  }
  xml += "</item>\n";
}
}

QByteArray projectXml(const QList<Node> &nodes, double fps)
{
  QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<boost_serialization>\n<Project>\n<NodesCollection>\n";
  for (int i = 0; i < nodes.size(); ++i)
  {
    appendNode(xml, nodes.at(i));
  }
  xml += "</NodesCollection>\n";
  if (fps > 0)
  {
    xml += "<ProjectKnobsCount>1</ProjectKnobsCount>\n";
    xml += "<item><Name>frameRate</Name><Value>" + QByteArray::number(fps) + "</Value></item>\n";
  }
  xml += "</Project>\n</boost_serialization>\n";
  return xml;
}

bool writeProject(const QString &path, const QList<Node> &nodes, double fps)
{
  return writeFile(path, projectXml(nodes, fps));
}

quint16 freeTcpPort()
{
  QTcpServer server;
  server.listen(QHostAddress::LocalHost, 0);
  const quint16 port = server.serverPort();
  server.close();
  return port;
}

quint16 freeUdpPort()
{
  QUdpSocket socket;
  socket.bind(QHostAddress(QHostAddress::LocalHost), 0);
  const quint16 port = socket.localPort();
  socket.close();
  return port;
}
}

namespace
{
QMutex &sinkMutex()
{
  static QMutex mutex;
  return mutex;
}

QHash<QString, FakeSinkRecord> &sinkRecords()
{
  static QHash<QString, FakeSinkRecord> records;
  return records;
}

class FakeSink : public FrameSink
{
public:
  FakeSink()
      : m_clocked(false)
  {
  }

  bool open(const QString &name, bool alpha, QString *error)
  {
    if (name.startsWith(QString::fromUtf8("fail")))
    {
      if (error)
      {
        *error = QString::fromUtf8("boom");
      }
      return false;
    }
    m_name = name;
    QMutexLocker locker(&sinkMutex());
    FakeSinkRecord &record = sinkRecords()[name];
    ++record.opened;
    record.alpha = alpha;
    record.clocked = m_clocked;
    return true;
  }

  void sendFrame(const QByteArray &bgra, int width, int height, int fpsNum, int fpsDen)
  {
    QMutexLocker locker(&sinkMutex());
    FakeSinkRecord &record = sinkRecords()[m_name];
    ++record.frames;
    record.lastFrame = bgra;
    record.width = width;
    record.height = height;
    record.fpsNum = fpsNum;
    record.fpsDen = fpsDen;
  }

  bool isClocked() const
  {
    return m_clocked;
  }

  void setClocked(bool clocked)
  {
    m_clocked = clocked;
  }

  int connectionCount()
  {
    return kFakeSinkConnections;
  }

private:
  QString m_name;
  bool m_clocked;
};
}

FrameSink *createFakeSink()
{
  return new FakeSink;
}

FakeSinkRecord fakeSinkRecord(const QString &name)
{
  QMutexLocker locker(&sinkMutex());
  return sinkRecords().value(name);
}
