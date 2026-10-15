#include "TestUtil.h"

#include "Custom/state/KvValue.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <limits>

#include "gtest/gtest.h"

using TestUtil::writeImage;

namespace
{
QVariantMap typed(const char *type, const QVariant &value)
{
  QVariantMap map;
  map.insert(QString::fromUtf8("type"), QString::fromUtf8(type));
  map.insert(QString::fromUtf8("value"), value);
  return map;
}

QVariant imageInput(const QString &path)
{
  QVariantMap map;
  map.insert(QString::fromUtf8("type"), QString::fromUtf8("image"));
  map.insert(QString::fromUtf8("path"), path);
  return map;
}

QVariant normalizeOk(const QVariant &input, bool strict = true)
{
  QString error;
  const QVariant value = Kv::normalize(input, strict, &error);
  EXPECT_TRUE(value.isValid()) << error.toStdString();
  EXPECT_TRUE(error.isEmpty());
  return value;
}

QString normalizeError(const QVariant &input)
{
  QString error;
  EXPECT_FALSE(Kv::normalize(input, true, &error).isValid());
  EXPECT_FALSE(error.isEmpty());
  return error;
}
}

TEST(KvValue, TypeNamesRoundTrip)
{
  const Kv::Type types[] = {Kv::eTypeText, Kv::eTypeImage, Kv::eTypeNumber, Kv::eTypeBool, Kv::eTypeColor};
  for (int i = 0; i < 5; ++i)
  {
    EXPECT_EQ(types[i], Kv::typeFromName(Kv::typeName(types[i])));
  }
  EXPECT_EQ(QString::fromUtf8("text"), Kv::typeName(Kv::eTypeText));
  EXPECT_EQ(QString::fromUtf8("color"), Kv::typeName(Kv::eTypeColor));
  EXPECT_TRUE(Kv::typeName(Kv::eTypeInvalid).isEmpty());
  EXPECT_EQ(Kv::eTypeInvalid, Kv::typeFromName(QString::fromUtf8("Text")));
  EXPECT_EQ(Kv::eTypeInvalid, Kv::typeFromName(QString()));
}

TEST(KvValue, TypeOf)
{
  EXPECT_EQ(Kv::eTypeInvalid, Kv::typeOf(QVariant()));
  EXPECT_EQ(Kv::eTypeInvalid, Kv::typeOf(QString::fromUtf8("text")));
  EXPECT_EQ(Kv::eTypeInvalid, Kv::typeOf(QVariantMap()));
  EXPECT_EQ(Kv::eTypeText, Kv::typeOf(Kv::makeText(QString::fromUtf8("a"))));
  EXPECT_EQ(Kv::eTypeNumber, Kv::typeOf(Kv::makeNumber(1)));
  EXPECT_EQ(Kv::eTypeBool, Kv::typeOf(Kv::makeBool(true)));
  EXPECT_EQ(Kv::eTypeColor, Kv::typeOf(Kv::makeColor(QString::fromUtf8("#000000"))));
  EXPECT_EQ(Kv::eTypeImage, Kv::typeOf(Kv::makeImage(QString::fromUtf8("/nonexistent/x.png"))));
}

TEST(KvValue, MakeValues)
{
  EXPECT_EQ(QVariant(typed("text", QString::fromUtf8("COPPER"))), Kv::makeText(QString::fromUtf8("COPPER")));
  EXPECT_EQ(QVariant(typed("number", 42.5)), Kv::makeNumber(42.5));
  EXPECT_EQ(QVariant(typed("bool", true)), Kv::makeBool(true));
}

TEST(KvValue, ParseColor)
{
  double rgba[4];
  ASSERT_TRUE(Kv::parseColor(QString::fromUtf8("#ff8000"), rgba));
  EXPECT_DOUBLE_EQ(1.0, rgba[0]);
  EXPECT_DOUBLE_EQ(128 / 255.0, rgba[1]);
  EXPECT_DOUBLE_EQ(0.0, rgba[2]);
  EXPECT_DOUBLE_EQ(1.0, rgba[3]);

  ASSERT_TRUE(Kv::parseColor(QString::fromUtf8("  00ff0080 "), rgba));
  EXPECT_DOUBLE_EQ(0.0, rgba[0]);
  EXPECT_DOUBLE_EQ(1.0, rgba[1]);
  EXPECT_DOUBLE_EQ(128 / 255.0, rgba[3]);

  EXPECT_FALSE(Kv::parseColor(QString::fromUtf8("#fff"), rgba));
  EXPECT_FALSE(Kv::parseColor(QString::fromUtf8("#ff80001"), rgba));
  EXPECT_FALSE(Kv::parseColor(QString::fromUtf8("#zz8000"), rgba));
  EXPECT_FALSE(Kv::parseColor(QString(), rgba));
}

TEST(KvValue, MakeColorNormalizes)
{
  EXPECT_EQ(QVariant(typed("color", QString::fromUtf8("#ff8800"))), Kv::makeColor(QString::fromUtf8("FF8800")));
  EXPECT_EQ(QVariant(typed("color", QString::fromUtf8("#ff880080"))), Kv::makeColor(QString::fromUtf8(" #FF880080 ")));
  EXPECT_FALSE(Kv::makeColor(QString::fromUtf8("orange")).isValid());
}

TEST(KvValue, LinearColor)
{
  double rgba[4];
  ASSERT_TRUE(Kv::linearColor(Kv::makeColor(QString::fromUtf8("#ffffff")), rgba));
  EXPECT_NEAR(1.0, rgba[0], 1e-9);
  ASSERT_TRUE(Kv::linearColor(Kv::makeColor(QString::fromUtf8("#000000")), rgba));
  EXPECT_DOUBLE_EQ(0.0, rgba[0]);
  // Linear segment below 0.04045.
  ASSERT_TRUE(Kv::linearColor(Kv::makeColor(QString::fromUtf8("#0a0a0a")), rgba));
  EXPECT_NEAR(10 / 255.0 / 12.92, rgba[0], 1e-9);
  // Power segment; alpha is not converted.
  ASSERT_TRUE(Kv::linearColor(Kv::makeColor(QString::fromUtf8("#80808080")), rgba));
  EXPECT_NEAR(0.2159, rgba[0], 1e-3);
  EXPECT_NEAR(0.2159, rgba[2], 1e-3);
  EXPECT_DOUBLE_EQ(128 / 255.0, rgba[3]);

  EXPECT_FALSE(Kv::linearColor(Kv::makeText(QString::fromUtf8("#ffffff")), rgba));
  EXPECT_FALSE(Kv::linearColor(QVariant(typed("color", QString::fromUtf8("bad"))), rgba));
}

TEST(KvValue, FitsBinding)
{
  const QVariant image = Kv::makeImage(QString::fromUtf8("/nonexistent/x.png"));
  EXPECT_TRUE(Kv::fitsBinding(image, QString::fromUtf8("image")));
  EXPECT_FALSE(Kv::fitsBinding(Kv::makeText(QString::fromUtf8("a")), QString::fromUtf8("image")));
  EXPECT_FALSE(Kv::fitsBinding(image, QString::fromUtf8("text")));
  EXPECT_TRUE(Kv::fitsBinding(Kv::makeText(QString::fromUtf8("a")), QString::fromUtf8("text")));
  EXPECT_TRUE(Kv::fitsBinding(Kv::makeNumber(1), QString::fromUtf8("text")));
  EXPECT_TRUE(Kv::fitsBinding(Kv::makeBool(false), QString::fromUtf8("text")));
  EXPECT_TRUE(Kv::fitsBinding(Kv::makeColor(QString::fromUtf8("#123456")), QString::fromUtf8("text")));
  EXPECT_FALSE(Kv::fitsBinding(QString::fromUtf8("plain"), QString::fromUtf8("text")));
}

TEST(KvValue, NormalizePlainValuesBecomeText)
{
  EXPECT_EQ(Kv::makeText(QString()), normalizeOk(QVariant()));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("COPPER")), normalizeOk(QString::fromUtf8("COPPER")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("true")), normalizeOk(true));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("42")), normalizeOk(qlonglong(42)));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("0.5")), normalizeOk(0.5));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("[1,\"a\"]")), normalizeOk(QVariantList() << 1 << QString::fromUtf8("a")));
}

TEST(KvValue, NormalizeText)
{
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("x")), normalizeOk(typed("text", QString::fromUtf8("x"))));
  EXPECT_EQ(Kv::makeText(QString()), normalizeOk(typed("text", QVariant())));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("42")), normalizeOk(typed("text", qlonglong(42))));
  QVariantMap inner;
  inner.insert(QString::fromUtf8("a"), 1);
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("{\"a\":1}")), normalizeOk(typed("text", inner)));
}

TEST(KvValue, NormalizeNumber)
{
  EXPECT_EQ(Kv::makeNumber(42.5), normalizeOk(typed("number", 42.5)));
  EXPECT_EQ(Kv::makeNumber(3.5), normalizeOk(typed("number", QString::fromUtf8("3.5"))));
  EXPECT_EQ(QString::fromUtf8("a number needs a numeric \"value\""), normalizeError(typed("number", QString::fromUtf8("abc"))));
  normalizeError(typed("number", QVariant()));
  normalizeError(typed("number", std::numeric_limits<double>::quiet_NaN()));
  normalizeError(typed("number", std::numeric_limits<double>::infinity()));
}

TEST(KvValue, NormalizeBool)
{
  EXPECT_EQ(Kv::makeBool(true), normalizeOk(typed("bool", true)));
  EXPECT_EQ(Kv::makeBool(false), normalizeOk(typed("bool", false)));
  const char *on[] = {"true", "On", " yes ", "1"};
  const char *off[] = {"false", "OFF", "no", "0"};
  for (int i = 0; i < 4; ++i)
  {
    EXPECT_EQ(Kv::makeBool(true), normalizeOk(typed("bool", QString::fromUtf8(on[i])))) << on[i];
    EXPECT_EQ(Kv::makeBool(false), normalizeOk(typed("bool", QString::fromUtf8(off[i])))) << off[i];
  }
  EXPECT_EQ(Kv::makeBool(true), normalizeOk(typed("bool", qlonglong(1))));
  normalizeError(typed("bool", QString::fromUtf8("maybe")));
}

TEST(KvValue, NormalizeColor)
{
  EXPECT_EQ(Kv::makeColor(QString::fromUtf8("#ff8800")), normalizeOk(typed("color", QString::fromUtf8("FF8800"))));
  normalizeError(typed("color", QString::fromUtf8("red")));
}

TEST(KvValue, NormalizeImage)
{
  const QVariant value = normalizeOk(imageInput(QString::fromUtf8("  /nonexistent/p1.png ")));
  EXPECT_EQ(Kv::eTypeImage, Kv::typeOf(value));
  EXPECT_EQ(QString::fromUtf8("/nonexistent/p1.png"), Kv::imagePath(value));
  EXPECT_EQ(QString::fromUtf8("an image needs a \"path\""), normalizeError(imageInput(QString::fromUtf8("  "))));
}

TEST(KvValue, NormalizeUnknownObjects)
{
  QVariantMap untyped;
  untyped.insert(QString::fromUtf8("a"), 1);
  EXPECT_EQ(QString::fromUtf8("objects need a \"type\": text, number, bool, color or image"), normalizeError(untyped));
  EXPECT_TRUE(normalizeError(typed("video", 1)).startsWith(QString::fromUtf8("unknown type \"video\"")));

  // Not strict (older saved values): kept as their JSON text.
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("{\"a\":1}")), normalizeOk(untyped, false));
  // The error pointer is optional.
  EXPECT_FALSE(Kv::normalize(untyped, true, nullptr).isValid());
}

TEST(KvValue, RefreshImageReadsFileDetails)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("p1.png"));
  ASSERT_TRUE(writeImage(path, 4, 2));

  QVariant value = Kv::makeImage(path);
  const Kv::ImageInfo info = Kv::imageInfo(value);
  EXPECT_EQ(path, info.path);
  EXPECT_EQ(QString::fromUtf8("p1.png"), info.name);
  EXPECT_EQ(4, info.width);
  EXPECT_EQ(2, info.height);
  EXPECT_EQ(QString::fromUtf8("png"), info.format);
  EXPECT_TRUE(info.exists);

  // Nothing changed: false.
  EXPECT_FALSE(Kv::refreshImage(&value));

  ASSERT_TRUE(writeImage(path, 8, 8));
  EXPECT_TRUE(Kv::refreshImage(&value));
  EXPECT_EQ(8, Kv::imageInfo(value).width);

  QFile::remove(path);
  EXPECT_TRUE(Kv::refreshImage(&value));
  EXPECT_FALSE(Kv::imageInfo(value).exists);
  EXPECT_EQ(0, Kv::imageInfo(value).width);
  EXPECT_EQ(QString::fromUtf8("png"), Kv::imageInfo(value).format);
}

TEST(KvValue, RefreshImageIgnoresOtherValues)
{
  QVariant text = Kv::makeText(QString::fromUtf8("a"));
  EXPECT_FALSE(Kv::refreshImage(&text));
  EXPECT_FALSE(Kv::refreshImage(nullptr));
}

TEST(KvValue, UnreadableImageUsesSuffixAsFormat)
{
  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("frame.EXR"));
  ASSERT_TRUE(TestUtil::writeFile(path, "not an image"));
  const Kv::ImageInfo info = Kv::imageInfo(Kv::makeImage(path));
  EXPECT_TRUE(info.exists);
  EXPECT_EQ(0, info.width);
  EXPECT_EQ(QString::fromUtf8("exr"), info.format);
}

#ifdef Q_OS_UNIX
TEST(KvValue, FfprobeFallbackForFormatsQtCannotRead)
{
  QTemporaryDir dir;
  const QString image = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("frame.exr"));
  const QString ffprobe = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("ffprobe"));
  ASSERT_TRUE(TestUtil::writeFile(image, "not an image"));
  ASSERT_TRUE(TestUtil::writeScript(ffprobe, "echo 'EXR,64,32'"));

  Kv::setFfprobePath(ffprobe);
  const Kv::ImageInfo info = Kv::imageInfo(Kv::makeImage(image));
  Kv::setFfprobePath(QString());

  EXPECT_EQ(64, info.width);
  EXPECT_EQ(32, info.height);
  EXPECT_EQ(QString::fromUtf8("exr"), info.format);
}
#endif

TEST(KvValue, TextValue)
{
  EXPECT_EQ(QString::fromUtf8("COPPER"), Kv::textValue(Kv::makeText(QString::fromUtf8("COPPER"))));
  EXPECT_EQ(QString::fromUtf8("42"), Kv::textValue(Kv::makeNumber(42)));
  EXPECT_EQ(QString::fromUtf8("0.1"), Kv::textValue(Kv::makeNumber(0.1)));
  EXPECT_EQ(QString::fromUtf8("-2.5"), Kv::textValue(Kv::makeNumber(-2.5)));
  EXPECT_EQ(QString::fromUtf8("true"), Kv::textValue(Kv::makeBool(true)));
  EXPECT_EQ(QString::fromUtf8("false"), Kv::textValue(Kv::makeBool(false)));
  EXPECT_EQ(QString::fromUtf8("#ff8800"), Kv::textValue(Kv::makeColor(QString::fromUtf8("#FF8800"))));
  EXPECT_EQ(QString::fromUtf8("/nonexistent/x.png"), Kv::textValue(Kv::makeImage(QString::fromUtf8("/nonexistent/x.png"))));
  EXPECT_EQ(QString::fromUtf8("plain"), Kv::textValue(QString::fromUtf8("plain")));
}

TEST(KvValue, ImagePathOnlyForImages)
{
  EXPECT_TRUE(Kv::imagePath(Kv::makeText(QString::fromUtf8("/a.png"))).isEmpty());
  EXPECT_FALSE(Kv::imageInfo(Kv::makeText(QString::fromUtf8("/a.png"))).exists);
  EXPECT_TRUE(Kv::imageInfo(Kv::makeText(QString::fromUtf8("/a.png"))).path.isEmpty());
}

TEST(KvValue, DisplayTextAndDescribe)
{
  EXPECT_EQ(QString::fromUtf8("On"), Kv::displayText(Kv::makeBool(true)));
  EXPECT_EQ(QString::fromUtf8("Off"), Kv::displayText(Kv::makeBool(false)));
  EXPECT_EQ(QString::fromUtf8("3"), Kv::displayText(Kv::makeNumber(3)));
  EXPECT_EQ(QString::fromUtf8("x"), Kv::describe(Kv::makeText(QString::fromUtf8("x"))));

  const QVariant missing = Kv::makeImage(QString::fromUtf8("/nonexistent/p1.png"));
  EXPECT_EQ(QString::fromUtf8("p1.png (missing)"), Kv::displayText(missing));
  EXPECT_EQ(QString::fromUtf8("p1.png  file not found\n/nonexistent/p1.png"), Kv::describe(missing));

  QTemporaryDir dir;
  const QString path = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("p2.png"));
  ASSERT_TRUE(writeImage(path, 4, 2));
  const QVariant image = Kv::makeImage(path);
  EXPECT_EQ(QString::fromUtf8("p2.png"), Kv::displayText(image));
  EXPECT_EQ(QString::fromUtf8("p2.png  4x2 PNG\n") + path, Kv::describe(image));

  const QString junk = QDir(dir.path()).absoluteFilePath(QString::fromUtf8("j.dpx"));
  ASSERT_TRUE(TestUtil::writeFile(junk, "junk"));
  EXPECT_EQ(QString::fromUtf8("j.dpx  DPX\n") + junk, Kv::describe(Kv::makeImage(junk)));
}

TEST(KvValue, ImageDifferences)
{
  QTemporaryDir dir;
  const QDir d(dir.path());
  const QString wide = d.absoluteFilePath(QString::fromUtf8("wide.png"));
  const QString wider = d.absoluteFilePath(QString::fromUtf8("wider.png"));
  const QString square = d.absoluteFilePath(QString::fromUtf8("square.png"));
  const QString bmp = d.absoluteFilePath(QString::fromUtf8("wide.bmp"));
  ASSERT_TRUE(writeImage(wide, 4, 2));
  ASSERT_TRUE(writeImage(wider, 8, 4));
  ASSERT_TRUE(writeImage(square, 4, 4));
  ASSERT_TRUE(writeImage(bmp, 4, 2));

  EXPECT_TRUE(Kv::imageDifferences(Kv::makeImage(wide), Kv::makeImage(wide)).isEmpty());
  EXPECT_EQ(QStringList() << QString::fromUtf8("size 4x2 -> 8x4"),
            Kv::imageDifferences(Kv::makeImage(wide), Kv::makeImage(wider)));
  EXPECT_EQ(QStringList() << QString::fromUtf8("size 4x2 -> 4x4") << QString::fromUtf8("aspect ratio 2.00 -> 1.00"),
            Kv::imageDifferences(Kv::makeImage(wide), Kv::makeImage(square)));
  EXPECT_EQ(QStringList() << QString::fromUtf8("format PNG -> BMP"),
            Kv::imageDifferences(Kv::makeImage(wide), Kv::makeImage(bmp)));
  EXPECT_EQ(QStringList() << QString::fromUtf8("file not found: /nonexistent/a.png"),
            Kv::imageDifferences(Kv::makeImage(wide), Kv::makeImage(QString::fromUtf8("/nonexistent/a.png"))));
  // Nothing to compare against.
  EXPECT_TRUE(Kv::imageDifferences(Kv::makeImage(QString::fromUtf8("/nonexistent/a.png")), Kv::makeImage(wide)).isEmpty());
}
