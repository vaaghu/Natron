#include "KvValue.h"

#include "Json.h"
#include "../util/BundledTool.h"

#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QSize>
#include <QVariantMap>

#include <cmath>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

QString g_ffprobePath;

// Width/height/format of an image file, without decoding it when possible.
bool probeImage(const QString &path, int *width, int *height, QString *format)
{
  QImageReader reader(path);
  const QSize size = reader.size();
  if (size.isValid())
  {
    *width = size.width();
    *height = size.height();
    *format = QString::fromLatin1(reader.format()).toLower();
    return true;
  }

  // Formats Qt has no plugin for (EXR, DPX...): ask ffprobe.
  if (g_ffprobePath.isEmpty() || !QFileInfo(g_ffprobePath).exists())
  {
    return false;
  }

  QStringList args;
  args << s("-v") << s("error") << s("-select_streams") << s("v:0")
       << s("-show_entries") << s("stream=width,height,codec_name")
       << s("-of") << s("csv=p=0") << path;

  QProcess ffprobe;
  prepareBundledTool(&ffprobe, g_ffprobePath);
  ffprobe.start(g_ffprobePath, args);
  if (!ffprobe.waitForFinished(5000))
  {
    ffprobe.kill();
    ffprobe.waitForFinished(1000);
    return false;
  }

  // "codec,width,height"
  const QStringList parts = QString::fromUtf8(ffprobe.readAllStandardOutput()).trimmed().split(QLatin1Char(','));
  if (parts.size() < 3)
  {
    return false;
  }
  *format = parts.at(0).trimmed().toLower();
  *width = parts.at(1).toInt();
  *height = parts.at(2).toInt();
  return *width > 0 && *height > 0;
}

QVariant imageFromPath(const QString &path)
{
  QVariantMap map;
  map.insert(s("type"), s("image"));
  map.insert(s("path"), path);
  QVariant value(map);
  Kv::refreshImage(&value);
  return value;
}
}

namespace Kv
{
void setFfprobePath(const QString &path)
{
  g_ffprobePath = path;
}

Type typeFromName(const QString &name)
{
  if (name == s("text"))
  {
    return eTypeText;
  }
  if (name == s("image"))
  {
    return eTypeImage;
  }
  if (name == s("number"))
  {
    return eTypeNumber;
  }
  if (name == s("bool"))
  {
    return eTypeBool;
  }
  if (name == s("color"))
  {
    return eTypeColor;
  }
  return eTypeInvalid;
}

QString typeName(Type type)
{
  switch (type)
  {
  case eTypeText:
    return s("text");
  case eTypeImage:
    return s("image");
  case eTypeNumber:
    return s("number");
  case eTypeBool:
    return s("bool");
  case eTypeColor:
    return s("color");
  default:
    return QString();
  }
}

Type typeOf(const QVariant &value)
{
  if (value.type() != QVariant::Map)
  {
    return eTypeInvalid;
  }
  return typeFromName(value.toMap().value(s("type")).toString());
}

QVariant makeText(const QString &text)
{
  QVariantMap map;
  map.insert(s("type"), s("text"));
  map.insert(s("value"), text);
  return map;
}

QVariant makeImage(const QString &path)
{
  return imageFromPath(path);
}

QVariant makeNumber(double number)
{
  QVariantMap map;
  map.insert(s("type"), s("number"));
  map.insert(s("value"), number);
  return map;
}

QVariant makeBool(bool on)
{
  QVariantMap map;
  map.insert(s("type"), s("bool"));
  map.insert(s("value"), on);
  return map;
}

bool parseColor(const QString &hex, double rgba[4])
{
  QString h = hex.trimmed();
  if (h.startsWith(QLatin1Char('#')))
  {
    h.remove(0, 1);
  }
  if (h.size() != 6 && h.size() != 8)
  {
    return false;
  }
  for (int i = 0; i < 4; ++i)
  {
    if (i == 3 && h.size() == 6)
    {
      rgba[3] = 1.0;
      break;
    }
    bool ok = false;
    const int c = h.mid(2 * i, 2).toInt(&ok, 16);
    if (!ok)
    {
      return false;
    }
    rgba[i] = c / 255.0;
  }
  return true;
}

QVariant makeColor(const QString &hex)
{
  double rgba[4];
  if (!parseColor(hex, rgba))
  {
    return QVariant();
  }
  QString h = hex.trimmed().toLower();
  if (!h.startsWith(QLatin1Char('#')))
  {
    h.prepend(QLatin1Char('#'));
  }
  QVariantMap map;
  map.insert(s("type"), s("color"));
  map.insert(s("value"), h);
  return map;
}

bool linearColor(const QVariant &value, double rgba[4])
{
  if (typeOf(value) != eTypeColor || !parseColor(value.toMap().value(s("value")).toString(), rgba))
  {
    return false;
  }
  for (int i = 0; i < 3; ++i)
  {
    const double c = rgba[i];
    rgba[i] = (c <= 0.04045) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  }
  return true;
}

bool fitsBinding(const QVariant &value, const QString &boundType)
{
  const Type type = typeOf(value);
  if (boundType == s("image"))
  {
    return type == eTypeImage;
  }
  return type == eTypeText || type == eTypeNumber || type == eTypeBool || type == eTypeColor;
}

QVariant normalize(const QVariant &input, bool strict, QString *error)
{
  switch (input.type())
  {
  case QVariant::Invalid:
    return makeText(QString());
  case QVariant::Bool:
    return makeText(input.toBool() ? s("true") : s("false"));
  case QVariant::List:
  case QVariant::StringList:
    return makeText(QString::fromUtf8(Json::serialize(input)));
  case QVariant::Map:
    break;
  default:
    // Numbers are written as JSON writes them (42, 0.5).
    if (input.type() == QVariant::String)
    {
      return makeText(input.toString());
    }
    return makeText(QString::fromUtf8(Json::serialize(input)));
  }

  const QVariantMap map = input.toMap();
  const QString type = map.value(s("type")).toString();

  if (type == s("text"))
  {
    const QVariant value = map.value(s("value"));
    if (value.type() == QVariant::Map || value.type() == QVariant::List)
    {
      return makeText(QString::fromUtf8(Json::serialize(value)));
    }
    if (value.type() == QVariant::String || !value.isValid())
    {
      return makeText(value.toString());
    }
    return normalize(value, strict, error);
  }

  if (type == s("number"))
  {
    bool ok = false;
    const double number = map.value(s("value")).toDouble(&ok);
    if (!ok || std::isnan(number) || std::isinf(number))
    {
      if (error)
      {
        *error = s("a number needs a numeric \"value\"");
      }
      return QVariant();
    }
    return makeNumber(number);
  }
  if (type == s("bool"))
  {
    const QVariant v = map.value(s("value"));
    const QString text = v.toString().trimmed().toLower();
    if (v.type() == QVariant::Bool)
    {
      return makeBool(v.toBool());
    }
    if (text == s("true") || text == s("on") || text == s("1") || text == s("yes"))
    {
      return makeBool(true);
    }
    if (text == s("false") || text == s("off") || text == s("0") || text == s("no"))
    {
      return makeBool(false);
    }
    if (error)
    {
      *error = s("an on/off value needs \"value\": true or false");
    }
    return QVariant();
  }
  if (type == s("color"))
  {
    const QVariant color = makeColor(map.value(s("value")).toString());
    if (!color.isValid() && error)
    {
      *error = s("a color needs \"value\": \"#rrggbb\" or \"#rrggbbaa\"");
    }
    return color;
  }
  if (type == s("image"))
  {
    const QString path = map.value(s("path")).toString().trimmed();
    if (path.isEmpty())
    {
      if (error)
      {
        *error = s("an image needs a \"path\"");
      }
      return QVariant();
    }
    return imageFromPath(path);
  }

  if (strict)
  {
    if (error)
    {
      *error = type.isEmpty() ? s("objects need a \"type\": text, number, bool, color or image")
                              : s("unknown type \"%1\" (use text, number, bool, color or image)").arg(type);
    }
    return QVariant();
  }

  // Older saved object values: keep them as their JSON text.
  return makeText(QString::fromUtf8(Json::serialize(input)));
}

bool refreshImage(QVariant *value)
{
  if (!value || typeOf(*value) != eTypeImage)
  {
    return false;
  }

  QVariantMap map = value->toMap();
  const QString path = map.value(s("path")).toString();
  const QFileInfo fi(path);

  int width = 0;
  int height = 0;
  QString format;
  const bool exists = fi.exists() && fi.isFile();
  if (exists && !probeImage(path, &width, &height, &format))
  {
    format = fi.suffix().toLower();
  }
  if (!exists)
  {
    format = fi.suffix().toLower();
  }

  QVariantMap updated = map;
  updated.insert(s("name"), fi.fileName());
  updated.insert(s("exists"), exists);
  updated.insert(s("width"), width);
  updated.insert(s("height"), height);
  updated.insert(s("format"), format);

  if (updated == map)
  {
    return false;
  }
  *value = updated;
  return true;
}

QString textValue(const QVariant &value)
{
  switch (typeOf(value))
  {
  case eTypeText:
    return value.toMap().value(s("value")).toString();
  case eTypeImage:
    return imagePath(value);
  case eTypeNumber:
    return QString::number(value.toMap().value(s("value")).toDouble(), 'g', 15);
  case eTypeBool:
    return value.toMap().value(s("value")).toBool() ? s("true") : s("false");
  case eTypeColor:
    return value.toMap().value(s("value")).toString();
  default:
    return value.toString();
  }
}

QString imagePath(const QVariant &value)
{
  return typeOf(value) == eTypeImage ? value.toMap().value(s("path")).toString() : QString();
}

ImageInfo imageInfo(const QVariant &value)
{
  ImageInfo info;
  if (typeOf(value) != eTypeImage)
  {
    return info;
  }
  const QVariantMap map = value.toMap();
  info.path = map.value(s("path")).toString();
  info.name = map.value(s("name")).toString();
  info.width = map.value(s("width")).toInt();
  info.height = map.value(s("height")).toInt();
  info.format = map.value(s("format")).toString();
  info.exists = map.value(s("exists")).toBool();
  return info;
}

QString displayText(const QVariant &value)
{
  if (typeOf(value) == eTypeBool)
  {
    return value.toMap().value(s("value")).toBool() ? s("On") : s("Off");
  }
  if (typeOf(value) != eTypeImage)
  {
    return textValue(value);
  }

  const ImageInfo info = imageInfo(value);
  const QString name = info.name.isEmpty() ? info.path : info.name;
  return info.exists ? name : s("%1 (missing)").arg(name);
}

QString describe(const QVariant &value)
{
  if (typeOf(value) != eTypeImage)
  {
    return textValue(value);
  }

  const ImageInfo info = imageInfo(value);
  QString details;
  if (!info.exists)
  {
    details = s("file not found");
  }
  else if (info.width > 0)
  {
    details = s("%1x%2 %3").arg(info.width).arg(info.height).arg(info.format.toUpper());
  }
  else
  {
    details = info.format.toUpper();
  }
  return s("%1  %2\n%3").arg(info.name).arg(details).arg(info.path);
}

QStringList imageDifferences(const QVariant &from, const QVariant &to)
{
  QStringList differences;
  const ImageInfo a = imageInfo(from);
  const ImageInfo b = imageInfo(to);

  if (!b.exists)
  {
    differences << s("file not found: %1").arg(b.path);
    return differences;
  }
  if (!a.exists || a.width <= 0 || b.width <= 0)
  {
    // Nothing reliable to compare against.
    if (a.exists && b.exists && a.format != b.format)
    {
      differences << s("format %1 -> %2").arg(a.format.toUpper()).arg(b.format.toUpper());
    }
    return differences;
  }

  if (a.width != b.width || a.height != b.height)
  {
    differences << s("size %1x%2 -> %3x%4").arg(a.width).arg(a.height).arg(b.width).arg(b.height);
  }
  const double ra = double(a.width) / a.height;
  const double rb = double(b.width) / b.height;
  if (std::fabs(ra - rb) > 0.01)
  {
    differences << s("aspect ratio %1 -> %2").arg(ra, 0, 'f', 2).arg(rb, 0, 'f', 2);
  }
  if (a.format != b.format)
  {
    differences << s("format %1 -> %2").arg(a.format.toUpper()).arg(b.format.toUpper());
  }
  return differences;
}
}
