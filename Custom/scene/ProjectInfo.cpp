#include "ProjectInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>

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
  const QString pluginEnd = QString::fromUtf8("</Plugin_id>");

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

    int from = 0;
    QString value;
    while (nextKnobValue(node, QString::fromUtf8(kProjectInfoStateKeyParam), &from, &value))
    {
      value = value.trimmed();
      if (!value.isEmpty() && !info.stateKeys.contains(value))
      {
        info.stateKeys << value;
      }
    }

    if (isWritePlugin(pluginId))
    {
      from = 0;
      if (nextKnobValue(node, QString::fromUtf8("filename"), &from, &value))
      {
        value = value.trimmed();
        value.replace(QString::fromUtf8("[Project]"), projectDir);
        // Relative to the project; Windows drive paths (C:/...) are absolute
        // even when read on another OS.
        const bool windowsAbsolute = QRegExp(QString::fromUtf8("^[A-Za-z]:[/\\\\].*")).exactMatch(value);
        if (!value.isEmpty() && !windowsAbsolute && QFileInfo(value).isRelative())
        {
          value = QDir(projectDir).absoluteFilePath(value);
        }
        if (!value.isEmpty() && !info.outputs.contains(value))
        {
          info.outputs << value;
        }
      }
    }

    pos = next;
  }

  info.ok = true;
  return info;
}

QString findExistingOutputFile(const QString &outputPattern)
{
  if (outputPattern.isEmpty())
  {
    return QString();
  }

  // ### / %04d frame patterns: pick the lowest-numbered existing frame.
  QRegExp hashes(QString::fromUtf8("#+"));
  QRegExp printfFrame(QString::fromUtf8("%0?\\d*d"));
  const QFileInfo fi(outputPattern);
  QString name = fi.fileName();

  if (hashes.indexIn(name) < 0 && printfFrame.indexIn(name) < 0)
  {
    return QFile::exists(outputPattern) ? outputPattern : QString();
  }

  QString regex = QRegExp::escape(name);
  regex.replace(QRegExp(QString::fromUtf8("#+")), QString::fromUtf8("-?\\d+"));
  regex.replace(QRegExp(QString::fromUtf8("%0?\\d*d")), QString::fromUtf8("-?\\d+"));
  const QRegExp match(regex);

  QStringList candidates;
  const QStringList files = QDir(fi.absolutePath()).entryList(QDir::Files, QDir::Name);
  for (int i = 0; i < files.size(); ++i)
  {
    if (match.exactMatch(files.at(i)))
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
