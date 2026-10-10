#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

// Typed values of the state store. Every value is an object with a "type":
//
//   text:   { "type": "text",   "value": "COPPER" }
//   number: { "type": "number", "value": 42.5 }
//   bool:   { "type": "bool",   "value": true }        (shown as On/Off)
//   color:  { "type": "color",  "value": "#ff8800" }   (or #rrggbbaa, sRGB)
//   image:  { "type": "image",  "path": "/assets/p1.png",
//             "name": "p1.png", "width": 1920, "height": 1080,
//             "format": "png", "exists": true }
//
// For images only the path is input; name/size/format/exists are read from
// the file (see refreshImage). Plain values ("COPPER", 42, true) are
// accepted as shorthand for text.
//
// Bindings: Read nodes take images; Text nodes take text, numbers and
// on/off values as their text, and colors as their text color.
namespace Kv
{
enum Type
{
  eTypeInvalid,
  eTypeText,
  eTypeImage,
  eTypeNumber,
  eTypeBool,
  eTypeColor
};

Type typeOf(const QVariant &value);
QString typeName(Type type); // "text", "image", "number", "bool", "color"
Type typeFromName(const QString &name);

QVariant makeText(const QString &text);
QVariant makeImage(const QString &path); // reads the file's details
QVariant makeNumber(double number);
QVariant makeBool(bool on);
// "#rrggbb" or "#rrggbbaa" (# optional); invalid QVariant if not a color.
QVariant makeColor(const QString &hex);

// Parses "#rrggbb" / "#rrggbbaa" into sRGB components 0-1 (alpha 1 if absent).
bool parseColor(const QString &hex, double rgba[4]);
// A color value as linear RGBA, as Natron's color parameters store them.
bool linearColor(const QVariant &value, double rgba[4]);

// Whether a value can be applied to a node bound for boundType ("text" for
// Text nodes, "image" for Read nodes).
bool fitsBinding(const QVariant &value, const QString &boundType);

// Converts input (API, GUI, saved file) to a typed value.
// strict: objects need a valid "type" (API input); otherwise unknown
// objects are kept as their JSON text (older saved values).
// Returns an invalid QVariant and sets *error if the input is rejected.
QVariant normalize(const QVariant &input, bool strict, QString *error);

// Re-reads an image's details from its file. Returns true if they changed.
bool refreshImage(QVariant *value);

// What a bound node receives: the text of a text value (a number as written,
// "true"/"false", a color as #rrggbb), the path of an image value.
QString textValue(const QVariant &value);
QString imagePath(const QVariant &value);

struct ImageInfo
{
  QString path;
  QString name;
  int width;
  int height;
  QString format;
  bool exists;

  ImageInfo()
      : width(0),
        height(0),
        exists(false)
  {
  }
};
ImageInfo imageInfo(const QVariant &value);

// What lists/tables show: the text, or the image's file name.
QString displayText(const QVariant &value);

// Longer description for tooltips: the text, or
// "p1.png  1920x1080 PNG\n/assets/p1.png".
QString describe(const QVariant &value);

// Differences that may change a render when an image key is replaced by
// another (size, aspect ratio, format, missing file). Empty if compatible.
QStringList imageDifferences(const QVariant &from, const QVariant &to);

// Optional fallback for image formats Qt cannot read (EXR, ...).
void setFfprobePath(const QString &path);
}
