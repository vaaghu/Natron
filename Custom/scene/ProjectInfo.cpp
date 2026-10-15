#include "ProjectInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>

namespace
{
QString unescapeXml(const QString &text)
{
  if (!text.contains(QLatin1Char('&')))
  {
    return text;
  }

  QString out;
  out.reserve(text.size());

  for (int i = 0; i < text.size(); ++i)
  {
    const QChar c = text.at(i);
    const int end = (c == QLatin1Char('&')) ? text.indexOf(QLatin1Char(';'), i) : -1;

    if (end < 0)
    {
      out.append(c);
      continue;
    }

    const QString entity = text.mid(i + 1, end - i - 1);
    bool known = true;

    if (entity == QString::fromUtf8("lt"))
    {
      out.append(QLatin1Char('<'));
    }
    else if (entity == QString::fromUtf8("gt"))
    {
      out.append(QLatin1Char('>'));
    }
    else if (entity == QString::fromUtf8("amp"))
    {
      out.append(QLatin1Char('&'));
    }
    else if (entity == QString::fromUtf8("quot"))
    {
      out.append(QLatin1Char('"'));
    }
    else if (entity == QString::fromUtf8("apos"))
    {
      out.append(QLatin1Char('\''));
    }
    else if (entity.startsWith(QLatin1Char('#')))
    {
      bool ok = false;
      const uint code = entity.startsWith(QString::fromUtf8("#x"))
                            ? entity.mid(2).toUInt(&ok, 16)
                            : entity.mid(1).toUInt(&ok, 10);
      if (ok && code > 0 && code < 0x10000)
      {
        out.append(QChar(ushort(code)));
      }
      else
      {
        known = false;
      }
    }
    else
    {
      known = false;
    }

    if (known)
    {
      i = end;
    }
    else
    {
      out.append(c);
    }
  }

  return out;
}

// Value of the knob named knobName inside text, i.e. the first
// <Value>...</Value> after <Name>knobName</Name>. Starts searching at *from
// and moves *from past the match.
bool nextKnobValue(const QString &text, const QString &knobName, int *from, QString *value)
{
  const QString nameTag = QString::fromUtf8("<Name>%1</Name>").arg(knobName);
  const int namePos = text.indexOf(nameTag, *from);

  if (namePos < 0)
  {
    return false;
  }

  const QString open = QString::fromUtf8("<Value>");
  const QString close = QString::fromUtf8("</Value>");
  const int valuePos = text.indexOf(open, namePos);
  const int valueEnd = (valuePos < 0) ? -1 : text.indexOf(close, valuePos);

  // The value must belong to this knob, i.e. come before the next knob.
  const int nextName = text.indexOf(QString::fromUtf8("<Name>"), namePos + nameTag.size());

  *from = namePos + nameTag.size();

  if (valuePos < 0 || valueEnd < 0 || (nextName >= 0 && valuePos > nextName))
  {
    value->clear();
    return true;
  }

  *value = unescapeXml(text.mid(valuePos + open.size(), valueEnd - valuePos - open.size()));
  return true;
}

// Full script names of the project's nodes ("Group1.Write1" for a node inside
// Group1, as NatronRenderer -w and Python take them), in the order their
// <Plugin_script_name> appears: a group's children are serialized inside its
// own <item>.
QStringList nodePaths(const QString &text)
{
  QStringList paths;
  QList<QPair<int, QString> > open; // element depth of each enclosing node, its name
  QXmlStreamReader xml(text);
  int depth = 0;

  while (!xml.atEnd() && !xml.hasError())
  {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (token == QXmlStreamReader::StartElement)
    {
      ++depth;
      if (xml.name() == QLatin1String("Plugin_script_name"))
      {
        const QString name = xml.readElementText().trimmed(); // consumes the end tag
        --depth;
        open << qMakePair(depth, name); // the node is the enclosing <item>
        QStringList path;
        for (int i = 0; i < open.size(); ++i)
        {
          path << open.at(i).second;
        }
        paths << path.join(QString::fromUtf8("."));
      }
    }
    else if (token == QXmlStreamReader::EndElement)
    {
      if (!open.isEmpty() && open.last().first == depth)
      {
        open.removeLast();
      }
      --depth;
    }
  }
  return paths;
}

bool isWritePlugin(const QString &pluginId)
{
  // Built-in Write meta node and the actual writer plug-ins it wraps.
  return pluginId == QString::fromUtf8("fr.inria.built-in.Write") ||
         pluginId.startsWith(QString::fromUtf8("fr.inria.openfx.Write")) ||
         pluginId.startsWith(QString::fromUtf8("fr.inria.built-in.Write"));
}
}

ProjectInfo readProjectInfo(const QString &projectFilePath)
{
  ProjectInfo info;

  QFile file(projectFilePath);
  if (!file.open(QIODevice::ReadOnly))
  {
    info.error = file.errorString();
    return info;
  }

  const QString text = QString::fromUtf8(file.readAll());
  const QString projectDir = QFileInfo(projectFilePath).absolutePath();
  const QString pluginTag = QString::fromUtf8("<Plugin_id>");
  const QString nameTag = QString::fromUtf8("<Plugin_script_name>");

  // Node paths, matched to the <Plugin_script_name> tags by their order.
  const QStringList paths = nodePaths(text);
  QList<int> namePositions;
  for (int at = text.indexOf(nameTag); at >= 0; at = text.indexOf(nameTag, at + 1))
  {
    namePositions << at;
  }
  const QString pluginEnd = QString::fromUtf8("</Plugin_id>");
  const QString labelTag = QString::fromUtf8("<Plugin_label>");
  int previousIdEnd = 0;

  // Each node starts with its <Plugin_id>; its parameters follow until the
  // next node.
  int pos = text.indexOf(pluginTag);
  while (pos >= 0)
  {
    const int idEnd = text.indexOf(pluginEnd, pos);
    if (idEnd < 0)
    {
      break;
    }

    const QString pluginId = text.mid(pos + pluginTag.size(), idEnd - pos - pluginTag.size()).trimmed();
    const int next = text.indexOf(pluginTag, idEnd);
    const QString node = text.mid(idEnd, next < 0 ? -1 : next - idEnd);

    // The node's script name and label come just before its <Plugin_id>;
    // inside a group, the full path (Group1.Write1) is used.
    const int nameStart = text.lastIndexOf(nameTag, pos);
    const int nameEnd = (nameStart < previousIdEnd) ? -1 : text.indexOf(QString::fromUtf8("</Plugin_script_name>"), nameStart);
    QString name = (nameEnd < 0) ? QString()
                                 : unescapeXml(text.mid(nameStart + nameTag.size(), nameEnd - nameStart - nameTag.size()).trimmed());
    const int nameIndex = namePositions.indexOf(nameStart);
    if (!name.isEmpty() && nameIndex >= 0 && nameIndex < paths.size() && paths.at(nameIndex).endsWith(name))
    {
      name = paths.at(nameIndex);
    }
    const int labelStart = text.lastIndexOf(labelTag, pos);
    const int labelEnd = (labelStart < previousIdEnd) ? -1 : text.indexOf(QString::fromUtf8("</Plugin_label>"), labelStart);
    const QString label = (labelEnd < 0) ? QString()
                                         : unescapeXml(text.mid(labelStart + labelTag.size(), labelEnd - labelStart - labelTag.size()).trimmed());
    previousIdEnd = idEnd;

    // Text nodes bind text, Read nodes bind an image file.
    const QString boundType = (pluginId == QString::fromUtf8("fr.inria.built-in.Read")) ? QString::fromUtf8("image")
                                                                                       : QString::fromUtf8("text");
    int from = 0;
    QString value;
    while (nextKnobValue(node, QString::fromUtf8(kProjectInfoStateKeyParam), &from, &value))
    {
      value = value.trimmed();
      if (value.isEmpty())
      {
        continue;
      }
      if (!name.isEmpty() || !label.isEmpty())
      {
        ProjectInfo::BoundNode bound;
        bound.label = label.isEmpty() ? name : label;
        bound.name = name;
        info.keyNodes[value] << bound;
      }
      if (!info.stateKeys.contains(value))
      {
        info.stateKeys << value;
        info.keyTypes.insert(value, boundType);
      }
      else if (!info.keyTypes.value(value).split(QLatin1Char(',')).contains(boundType))
      {
        info.keyTypes[value] += QLatin1Char(',') + boundType;
      }
    }

    if (isWritePlugin(pluginId))
    {
      const QString& writer = name;
      if (!writer.isEmpty() && !info.writers.contains(writer))
      {
        info.writers << writer;
      }

      from = 0;
      if (nextKnobValue(node, QString::fromUtf8("filename"), &from, &value))
      {
        value = value.trimmed();
        value.replace(QString::fromUtf8("[Project]"), projectDir);
        // Relative to the project; Windows drive paths (C:/...) are absolute
        // even when read on another OS.
        const bool windowsAbsolute = QRegularExpression(QString::fromUtf8("^[A-Za-z]:[/\\\\]")).match(value).hasMatch();
        if (!value.isEmpty() && !windowsAbsolute && QFileInfo(value).isRelative())
        {
          value = QDir(projectDir).absoluteFilePath(value);
        }
        if (!value.isEmpty() && !info.outputs.contains(value))
        {
          info.outputs << value;
        }
        if (!writer.isEmpty() && !value.isEmpty())
        {
          info.writerOutputs.insert(writer, value);
        }
      }
    }

    pos = next;
  }

  // Project settings follow the nodes; the frame rate is only saved when it
  // differs from the default.
  const int projectKnobs = text.indexOf(QString::fromUtf8("<ProjectKnobsCount>"));
  if (projectKnobs >= 0)
  {
    int from = projectKnobs;
    QString value;
    if (nextKnobValue(text, QString::fromUtf8("frameRate"), &from, &value))
    {
      bool ok = false;
      const double fps = value.trimmed().toDouble(&ok);
      if (ok && fps > 0)
      {
        info.fps = fps;
      }
    }
  }

  info.ok = true;
  return info;
}

namespace
{
// Frame number placeholders of a sequence file name: ### or %04d.
const QRegularExpression &frameToken()
{
  static const QRegularExpression token(QString::fromUtf8("#+|%0?\\d*d"));
  return token;
}

// Anchored regex matching the whole file name, with each frame placeholder
// replaced by numberRegex and the rest matched literally.
QRegularExpression sequenceFileRegex(const QString &fileNamePattern, const QString &numberRegex)
{
  QString regex;
  int last = 0;
  QRegularExpressionMatchIterator it = frameToken().globalMatch(fileNamePattern);
  while (it.hasNext())
  {
    const QRegularExpressionMatch m = it.next();
    regex += QRegularExpression::escape(fileNamePattern.mid(last, m.capturedStart() - last));
    regex += numberRegex;
    last = m.capturedEnd();
  }
  regex += QRegularExpression::escape(fileNamePattern.mid(last));
  return QRegularExpression(QRegularExpression::anchoredPattern(regex));
}

// Regex matching the frame number part of a sequence file name; the number
// is captured.
QRegularExpression sequenceRegex(const QString &fileNamePattern)
{
  return sequenceFileRegex(fileNamePattern, QString::fromUtf8("(-?\\d+)"));
}
}

bool isSequencePattern(const QString &outputPattern)
{
  const QString name = QFileInfo(outputPattern).fileName();
  return frameToken().match(name).hasMatch();
}

QList<int> existingSequenceFrames(const QString &outputPattern)
{
  QList<int> frames;
  const QFileInfo fi(outputPattern);
  const QRegularExpression match = sequenceRegex(fi.fileName());

  const QStringList files = QDir(fi.absolutePath()).entryList(QDir::Files);
  for (int i = 0; i < files.size(); ++i)
  {
    const QRegularExpressionMatch m = match.match(files.at(i));
    if (m.hasMatch())
    {
      frames << m.captured(1).toInt();
    }
  }
  std::sort(frames.begin(), frames.end());
  return frames;
}

QString sequenceFrameFile(const QString &outputPattern, int frame)
{
  const QFileInfo fi(outputPattern);
  QString name = fi.fileName();

  const QRegularExpressionMatch hashes = QRegularExpression(QString::fromUtf8("#+")).match(name);
  if (hashes.hasMatch())
  {
    const int width = hashes.capturedLength();
    name.replace(hashes.capturedStart(), width, QString::fromUtf8("%1").arg(frame, width, 10, QLatin1Char('0')));
  }
  else
  {
    const QRegularExpressionMatch printfFrame = QRegularExpression(QString::fromUtf8("%0?(\\d*)d")).match(name);
    if (printfFrame.hasMatch())
    {
      const int width = printfFrame.captured(1).toInt();
      name.replace(printfFrame.capturedStart(), printfFrame.capturedLength(), QString::fromUtf8("%1").arg(frame, width, 10, QLatin1Char('0')));
    }
  }
  return QDir(fi.absolutePath()).absoluteFilePath(name);
}

QString findExistingOutputFile(const QString &outputPattern)
{
  if (outputPattern.isEmpty())
  {
    return QString();
  }

  // ### / %04d frame patterns: pick the lowest-numbered existing frame.
  const QFileInfo fi(outputPattern);
  QString name = fi.fileName();

  if (!frameToken().match(name).hasMatch())
  {
    return QFile::exists(outputPattern) ? outputPattern : QString();
  }

  const QRegularExpression match = sequenceFileRegex(name, QString::fromUtf8("-?\\d+"));

  QStringList candidates;
  const QStringList files = QDir(fi.absolutePath()).entryList(QDir::Files, QDir::Name);
  for (int i = 0; i < files.size(); ++i)
  {
    if (match.match(files.at(i)).hasMatch())
    {
      candidates << files.at(i);
    }
  }

  if (candidates.isEmpty())
  {
    return QString();
  }

  return QDir(fi.absolutePath()).absoluteFilePath(candidates.first());
}
