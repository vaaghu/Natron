#include "TestUtil.h"

#include "Custom/state/Json.h"

#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>

#include "gtest/gtest.h"

namespace
{
QVariant parseOk(const QByteArray &text)
{
  bool ok = false;
  QString error;
  const QVariant value = Json::parse(text, &ok, &error);
  EXPECT_TRUE(ok) << text.constData() << ": " << error.toStdString();
  return value;
}

QString parseError(const QByteArray &text)
{
  bool ok = true;
  QString error;
  const QVariant value = Json::parse(text, &ok, &error);
  EXPECT_FALSE(ok) << text.constData();
  EXPECT_FALSE(value.isValid());
  return error;
}
}

TEST(Json, ParsesLiterals)
{
  EXPECT_EQ(QVariant(true), parseOk("true"));
  EXPECT_EQ(QVariant(false), parseOk("false"));
  EXPECT_FALSE(parseOk("null").isValid());
}

TEST(Json, ParsesIntegersAsLongLong)
{
  const QVariant v = parseOk("42");
  EXPECT_EQ(QVariant::LongLong, v.type());
  EXPECT_EQ(42, v.toLongLong());
  EXPECT_EQ(-12, parseOk("-12").toLongLong());
  EXPECT_EQ(0, parseOk("0").toLongLong());
}

TEST(Json, ParsesFractionsAndExponentsAsDouble)
{
  EXPECT_EQ(QVariant::Double, parseOk("-0.5").type());
  EXPECT_DOUBLE_EQ(-0.5, parseOk("-0.5").toDouble());
  EXPECT_DOUBLE_EQ(100000.0, parseOk("1e5").toDouble());
  EXPECT_DOUBLE_EQ(0.025, parseOk("2.5E-2").toDouble());
  EXPECT_DOUBLE_EQ(3.0, parseOk("3e+0").toDouble());
}

TEST(Json, OutOfRangeIntegerFallsBackToDouble)
{
  const QVariant v = parseOk("9223372036854775808");
  EXPECT_EQ(QVariant::Double, v.type());
  EXPECT_DOUBLE_EQ(9223372036854775808.0, v.toDouble());
}

TEST(Json, ParsesStringsAndEscapes)
{
  EXPECT_EQ(QString::fromUtf8("hello"), parseOk("\"hello\"").toString());
  EXPECT_EQ(QString::fromUtf8("a\"b\\c/d\b\f\n\r\t"), parseOk("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"").toString());
  EXPECT_EQ(QString::fromUtf8("\xC3\xA9"), parseOk("\"\\u00e9\"").toString());
  EXPECT_EQ(QString::fromUtf8("\xE2\x82\xAC"), parseOk("\"\\u20AC\"").toString());
  // Raw UTF-8 passes through.
  EXPECT_EQ(QString::fromUtf8("caf\xC3\xA9"), parseOk("\"caf\xC3\xA9\"").toString());
}

TEST(Json, CombinesSurrogatePairs)
{
  EXPECT_EQ(QString::fromUtf8("\xF0\x9F\x98\x80"), parseOk("\"\\ud83d\\ude00\"").toString());
}

TEST(Json, RejectsUnpairedSurrogates)
{
  EXPECT_TRUE(parseError("\"\\ud83d\"").startsWith(QString::fromUtf8("unpaired surrogate")));
  EXPECT_TRUE(parseError("\"\\ud83d\\u0041\"").startsWith(QString::fromUtf8("unpaired surrogate")));
  EXPECT_TRUE(parseError("\"\\ude00\"").startsWith(QString::fromUtf8("unpaired surrogate")));
}

TEST(Json, RejectsBadStrings)
{
  EXPECT_TRUE(parseError("\"abc").startsWith(QString::fromUtf8("unterminated string")));
  EXPECT_TRUE(parseError("\"a\nb\"").startsWith(QString::fromUtf8("control character in string")));
  EXPECT_TRUE(parseError("\"\\x\"").startsWith(QString::fromUtf8("invalid escape")));
  EXPECT_TRUE(parseError("\"\\u12zz\"").startsWith(QString::fromUtf8("invalid \\u escape")));
  EXPECT_TRUE(parseError("\"\\u1").startsWith(QString::fromUtf8("truncated \\u escape")));
}

TEST(Json, ParsesObjectsAndArrays)
{
  const QVariant v = parseOk(" \t\n{ \"a\" : [1, \"x\", null, {\"b\": false}], \"c\": {} } \r\n");
  ASSERT_EQ(QVariant::Map, v.type());
  const QVariantMap map = v.toMap();
  EXPECT_EQ(2, map.size());
  const QVariantList list = map.value(QString::fromUtf8("a")).toList();
  ASSERT_EQ(4, list.size());
  EXPECT_EQ(1, list.at(0).toLongLong());
  EXPECT_EQ(QString::fromUtf8("x"), list.at(1).toString());
  EXPECT_FALSE(list.at(2).isValid());
  EXPECT_EQ(QVariant(false), list.at(3).toMap().value(QString::fromUtf8("b")));
  EXPECT_TRUE(map.value(QString::fromUtf8("c")).toMap().isEmpty());
  EXPECT_TRUE(parseOk("[]").toList().isEmpty());
}

TEST(Json, RejectsMalformedDocuments)
{
  parseError("");
  parseError("   ");
  parseError("[1,]");
  parseError("[1 2]");
  parseError("{1: 2}");
  parseError("{\"a\" 1}");
  parseError("{\"a\": 1,}");
  parseError("-");
  parseError("1.");
  parseError("1e");
  parseError(".5");
  parseError("tru");
  parseError("nul");
  EXPECT_TRUE(parseError("{\"a\": 1").startsWith(QString::fromUtf8("expected ',' or '}'")));
  EXPECT_TRUE(parseError("[1").startsWith(QString::fromUtf8("expected ',' or ']'")));
  EXPECT_TRUE(parseError("{1: 2}").startsWith(QString::fromUtf8("expected string key")));
  EXPECT_TRUE(parseError("{\"a\" 1}").startsWith(QString::fromUtf8("expected ':'")));
}

TEST(Json, ReportsTrailingCharactersWithOffset)
{
  EXPECT_EQ(QString::fromUtf8("unexpected trailing characters at offset 2"), parseError("1 x"));
  parseError("01");
  parseError("{} {}");
}

TEST(Json, LimitsNesting)
{
  parseOk(QByteArray(100, '[') + QByteArray(100, ']'));
  EXPECT_TRUE(parseError(QByteArray(200, '[') + QByteArray(200, ']')).startsWith(QString::fromUtf8("nesting too deep")));
}

TEST(Json, ParseAcceptsNullOkAndError)
{
  // Both out parameters are optional.
  EXPECT_EQ(1, Json::parse("1", nullptr).toLongLong());
  EXPECT_FALSE(Json::parse("x", nullptr).isValid());
}

TEST(Json, SerializesScalars)
{
  EXPECT_EQ(QByteArray("null"), Json::serialize(QVariant()));
  EXPECT_EQ(QByteArray("true"), Json::serialize(QVariant(true)));
  EXPECT_EQ(QByteArray("false"), Json::serialize(QVariant(false)));
  EXPECT_EQ(QByteArray("42"), Json::serialize(QVariant(42)));
  EXPECT_EQ(QByteArray("-7"), Json::serialize(QVariant(qlonglong(-7))));
  EXPECT_EQ(QByteArray("4294967295"), Json::serialize(QVariant(uint(4294967295u))));
  EXPECT_EQ(QByteArray("18446744073709551615"), Json::serialize(QVariant(qulonglong(18446744073709551615ull))));
}

TEST(Json, SerializesDoublesInShortestForm)
{
  EXPECT_EQ(QByteArray("0.1"), Json::serialize(QVariant(0.1)));
  EXPECT_EQ(QByteArray("2"), Json::serialize(QVariant(2.0)));
  EXPECT_EQ(QByteArray("-0.5"), Json::serialize(QVariant(-0.5)));
  const double third = 1.0 / 3.0;
  EXPECT_EQ(third, Json::parse(Json::serialize(QVariant(third)), nullptr).toDouble());
}

TEST(Json, SerializesNonFiniteDoublesAsNull)
{
  EXPECT_EQ(QByteArray("null"), Json::serialize(QVariant(std::numeric_limits<double>::quiet_NaN())));
  EXPECT_EQ(QByteArray("null"), Json::serialize(QVariant(std::numeric_limits<double>::infinity())));
  EXPECT_EQ(QByteArray("null"), Json::serialize(QVariant(-std::numeric_limits<double>::infinity())));
}

TEST(Json, EscapesStrings)
{
  EXPECT_EQ(QByteArray("\"a\\\"b\\\\c\\b\\f\\n\\r\\t\""), Json::serialize(QString::fromUtf8("a\"b\\c\b\f\n\r\t")));
  EXPECT_EQ(QByteArray("\"\\u0001\\u001f\""), Json::serialize(QString::fromUtf8("\x01\x1f")));
  EXPECT_EQ(QByteArray("\"caf\xC3\xA9\""), Json::serialize(QString::fromUtf8("caf\xC3\xA9")));
  EXPECT_EQ(QByteArray("\"a/b\""), Json::serialize(QString::fromUtf8("a/b")));
}

TEST(Json, SerializesContainers)
{
  QVariantMap map;
  map.insert(QString::fromUtf8("b"), 1);
  map.insert(QString::fromUtf8("a"), QVariantList() << true << QVariant() << QString::fromUtf8("x"));
  map.insert(QString::fromUtf8("c"), QVariantMap());
  EXPECT_EQ(QByteArray("{\"a\":[true,null,\"x\"],\"b\":1,\"c\":{}}"), Json::serialize(map));
  EXPECT_EQ(QByteArray("[\"x\",\"y\"]"), Json::serialize(QStringList() << QString::fromUtf8("x") << QString::fromUtf8("y")));
  EXPECT_EQ(QByteArray("[]"), Json::serialize(QVariantList()));
}

TEST(Json, SerializesOtherTypesAsStrings)
{
  EXPECT_EQ(QByteArray("\"http://example.com/a\""), Json::serialize(QUrl(QString::fromUtf8("http://example.com/a"))));
}

TEST(Json, RoundTrips)
{
  const QByteArray text = "{\"list\":[1,2.5,\"s\",null,false,{\"x\":\"\\u0001\"}],\"name\":\"caf\xC3\xA9\"}";
  EXPECT_EQ(text, Json::serialize(Json::parse(text, nullptr)));
}
