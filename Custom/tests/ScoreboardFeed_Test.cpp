#include "TestUtil.h"

#include "Custom/scoreboard/ScoreboardFeed.h"
#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"

#include <QUdpSocket>
#include <QVariantList>
#include <QVariantMap>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
// General data packet (01 01): see ScoreboardFeed.h for the byte layout.
struct General
{
  int period = 2;
  int flags = 0x01; // clock running
  int minutes = 5, seconds = 7, tenths = 0;
  int shotSeconds = 14, shotTenths = 0xFF;
  int timeout = 0xFF;
  int homeScore = 51, awayScore = 48;
  int homeFouls = 3, awayFouls = 4;
  int homeTimeouts = 2, awayTimeouts = 1;
  int homePossession = 1, awayPossession = 0;

  QByteArray bytes() const
  {
    QByteArray p(25, '\0');
    const int values[] = {0xFF, 0xFE, 0x01, 0x01, 0x00, 0x00, 0x12, period, flags, minutes, seconds, tenths,
                          shotSeconds, shotTenths, timeout, 0, 0, homeScore, awayScore, homeFouls, awayFouls,
                          homeTimeouts, awayTimeouts, homePossession, awayPossession};
    for (int i = 0; i < 25; ++i)
    {
      p[i] = char(values[i]);
    }
    return p;
  }
};

QByteArray header(int id)
{
  QByteArray p;
  p.append(char(0xFF)).append(char(0xFE)).append(char(id)).append(char(0x01)).append(char(0)).append(char(0)).append(char(0));
  return p;
}

QByteArray footer()
{
  return QByteArray().append(char(0xFD)).append(char(0xFC));
}

QByteArray shirt(const char *number)
{
  QByteArray s(number);
  s.append(QByteArray(3 - s.size(), '\0'));
  return s;
}

QByteArray inverted(const QByteArray &p)
{
  QByteArray out = p;
  for (int i = 0; i < out.size(); ++i)
  {
    out[i] = char(~out.at(i));
  }
  return out;
}

QVariantMap decodeOk(const QByteArray &packet, QString *type = nullptr)
{
  QString t;
  QString error;
  const QVariantMap values = ScoreboardFeed::decode(packet, &t, &error);
  EXPECT_TRUE(error.isEmpty()) << error.toStdString();
  if (type)
  {
    *type = t;
  }
  return values;
}

QString decodeError(const QByteArray &packet)
{
  QString type;
  QString error;
  EXPECT_TRUE(ScoreboardFeed::decode(packet, &type, &error).isEmpty());
  return error;
}

QString text(const QVariantMap &values, const char *key)
{
  return Kv::textValue(values.value(QString::fromUtf8("scoreboard.") + QString::fromUtf8(key)));
}

// "FF FE 01 ..."
QString hex(const QByteArray &bytes)
{
  QStringList parts;
  for (int i = 0; i < bytes.size(); ++i)
  {
    parts << QString::fromLatin1(bytes.mid(i, 1).toHex()).toUpper();
  }
  return parts.join(QLatin1Char(' '));
}
}

TEST(ScoreboardDecode, GeneralData)
{
  QString type;
  const QVariantMap v = decodeOk(General().bytes(), &type);
  EXPECT_EQ(QString::fromUtf8("general"), type);
  EXPECT_EQ(QString::fromUtf8("Q2"), text(v, "period"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "periodNumber"));
  EXPECT_EQ(Kv::makeBool(true), v.value(QString::fromUtf8("scoreboard.clockRunning")));
  EXPECT_EQ(QString::fromUtf8("05:07"), text(v, "clock"));
  EXPECT_EQ(QString::fromUtf8("14"), text(v, "shotClock"));
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.timeout")));
  EXPECT_EQ(QString::fromUtf8("0"), text(v, "timeoutSeconds"));
  EXPECT_EQ(QString::fromUtf8("home"), text(v, "possession"));
  EXPECT_EQ(Kv::makeNumber(51), v.value(QString::fromUtf8("scoreboard.home.score")));
  EXPECT_EQ(Kv::makeNumber(48), v.value(QString::fromUtf8("scoreboard.away.score")));
  EXPECT_EQ(QString::fromUtf8("3"), text(v, "home.fouls"));
  EXPECT_EQ(QString::fromUtf8("4"), text(v, "away.fouls"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.timeouts"));
  EXPECT_EQ(QString::fromUtf8("1"), text(v, "away.timeouts"));
}

TEST(ScoreboardDecode, Periods)
{
  General g;
  const int periods[] = {0, 1, 4, 5, 6, 7};
  const char *names[] = {"", "Q1", "Q4", "OT", "2OT", "3OT"};
  for (int i = 0; i < 6; ++i)
  {
    g.period = periods[i];
    EXPECT_EQ(QString::fromUtf8(names[i]), text(decodeOk(g.bytes()), "period")) << periods[i];
  }
}

TEST(ScoreboardDecode, Clocks)
{
  General g;
  g.flags = 0;
  g.minutes = 0;
  g.seconds = 42;
  g.tenths = 3;
  QVariantMap v = decodeOk(g.bytes());
  EXPECT_EQ(QString::fromUtf8("42.3"), text(v, "clock")); // last minute: tenths
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.clockRunning")));

  g.tenths = 0xFF;
  EXPECT_EQ(QString::fromUtf8("42.0"), text(decodeOk(g.bytes()), "clock"));

  g.minutes = 0xFF;
  g.seconds = 0xFF;
  EXPECT_EQ(QString(), text(decodeOk(g.bytes()), "clock")); // not shown

  g.shotSeconds = 4;
  g.shotTenths = 7;
  EXPECT_EQ(QString::fromUtf8("4.7"), text(decodeOk(g.bytes()), "shotClock"));
  g.shotSeconds = 5;
  g.shotTenths = 0;
  EXPECT_EQ(QString::fromUtf8("5.0"), text(decodeOk(g.bytes()), "shotClock"));
  g.shotTenths = 1;
  EXPECT_EQ(QString::fromUtf8("5"), text(decodeOk(g.bytes()), "shotClock"));
  g.shotSeconds = 0xFF;
  EXPECT_EQ(QString(), text(decodeOk(g.bytes()), "shotClock"));
}

TEST(ScoreboardDecode, TimeoutPossessionAndMissingCounts)
{
  General g;
  g.timeout = 30;
  g.homePossession = 0;
  g.awayPossession = 1;
  g.homeScore = 0xFF;
  g.awayFouls = 0xFF;
  QVariantMap v = decodeOk(g.bytes());
  EXPECT_EQ(Kv::makeBool(true), v.value(QString::fromUtf8("scoreboard.timeout")));
  EXPECT_EQ(QString::fromUtf8("30"), text(v, "timeoutSeconds"));
  EXPECT_EQ(QString::fromUtf8("away"), text(v, "possession"));
  EXPECT_FALSE(v.contains(QString::fromUtf8("scoreboard.home.score")));
  EXPECT_FALSE(v.contains(QString::fromUtf8("scoreboard.away.fouls")));
  EXPECT_TRUE(v.contains(QString::fromUtf8("scoreboard.away.score")));

  g.timeout = 0;
  g.awayPossession = 0;
  v = decodeOk(g.bytes());
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.timeout")));
  EXPECT_EQ(QString(), text(v, "possession"));
}

TEST(ScoreboardDecode, InvertedPackets)
{
  const QByteArray packet = General().bytes();
  EXPECT_EQ(decodeOk(packet), decodeOk(inverted(packet)));
}

TEST(ScoreboardDecode, PlayerData)
{
  QByteArray p = header(0x02);
  p += shirt("12") + char(7) + char(2);
  p += shirt("") + char(9) + char(9);   // unused
  p += QByteArray("  5") + char(3) + char(1);
  p += shirt("#1") + char(1) + char(1); // not a number
  p += shirt("A1") + char(0) + char(5);
  p += footer();

  QString type;
  const QVariantMap v = decodeOk(p, &type);
  EXPECT_EQ(QString::fromUtf8("players"), type);
  EXPECT_EQ(6, v.size());
  EXPECT_EQ(Kv::makeNumber(7), v.value(QString::fromUtf8("scoreboard.home.player.12.points")));
  EXPECT_EQ(Kv::makeNumber(2), v.value(QString::fromUtf8("scoreboard.home.player.12.fouls")));
  EXPECT_EQ(QString::fromUtf8("3"), text(v, "home.player.5.points"));
  EXPECT_EQ(QString::fromUtf8("5"), text(v, "home.player.A1.fouls"));

  QByteArray away = header(0x03) + shirt("8") + char(10) + char(0);
  EXPECT_EQ(QString::fromUtf8("10"), text(decodeOk(away), "away.player.8.points"));
}

TEST(ScoreboardDecode, PlayersOnCourt)
{
  QByteArray p = header(0x07);
  p += shirt("4") + char(1);
  p += shirt("10") + char(0);
  p += shirt("") + char(1);
  p += footer();

  QString type;
  const QVariantMap v = decodeOk(p, &type);
  EXPECT_EQ(QString::fromUtf8("onCourt"), type);
  EXPECT_EQ(2, v.size());
  EXPECT_EQ(Kv::makeBool(true), v.value(QString::fromUtf8("scoreboard.away.player.4.onCourt")));
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.away.player.10.onCourt")));

  EXPECT_TRUE(decodeOk(header(0x06) + shirt("1") + char(1)).contains(QString::fromUtf8("scoreboard.home.player.1.onCourt")));
}

TEST(ScoreboardDecode, Rejections)
{
  EXPECT_EQ(QString::fromUtf8("not an Ultra Score packet (no FF FE header)"), decodeError(QByteArray("hello world")));
  EXPECT_EQ(QString::fromUtf8("not an Ultra Score packet (no FF FE header)"), decodeError(header(0x01).left(6)));
  QByteArray badId = General().bytes();
  badId[3] = char(0x02);
  EXPECT_EQ(QString::fromUtf8("unknown packet id"), decodeError(badId));
  EXPECT_EQ(QString::fromUtf8("general data packet too short (needs 25 bytes)"), decodeError(General().bytes().left(24)));
  EXPECT_EQ(QString::fromUtf8("unsupported packet id"), decodeError(header(0x04) + footer()));
}

namespace
{
class ScoreboardFeedTest : public ::testing::Test
{
protected:
  ScoreboardFeedTest()
      : m_feed(&m_store)
  {
  }

  bool handle(const QByteArray &method, const QByteArray &body, int *status, QVariantMap *json, const QByteArray &path = "/scoreboard")
  {
    QByteArray response;
    const bool handled = m_feed.handleHttpRequest(method, path, body, status, &response);
    *json = Json::parse(response, nullptr).toMap();
    return handled;
  }

  StateStore m_store;
  ScoreboardFeed m_feed;
};
}

TEST_F(ScoreboardFeedTest, ApplySetsOnlyChangedKeys)
{
  QVariantMap values = decodeOk(General().bytes());
  const QStringList first = m_feed.apply(values);
  EXPECT_EQ(values.size(), first.size());
  EXPECT_EQ(Kv::makeNumber(51), m_store.get(QString::fromUtf8("scoreboard.home.score")));

  EXPECT_TRUE(m_feed.apply(values).isEmpty());

  General g;
  g.homeScore = 53;
  EXPECT_EQ(QStringList() << QString::fromUtf8("scoreboard.home.score"), m_feed.apply(decodeOk(g.bytes())));

  // Same text but another type: replaced.
  m_store.set(QString::fromUtf8("scoreboard.period"), Kv::makeNumber(2));
  QVariantMap period;
  period.insert(QString::fromUtf8("scoreboard.period"), Kv::makeText(QString::fromUtf8("2")));
  EXPECT_EQ(1, m_feed.apply(period).size());
}

TEST_F(ScoreboardFeedTest, NotListeningByDefault)
{
  EXPECT_FALSE(m_feed.isListening());
  EXPECT_EQ(0, m_feed.port());
  EXPECT_TRUE(m_feed.lastError().isEmpty());
}

TEST_F(ScoreboardFeedTest, HttpOtherPathsAreNotHandled)
{
  int status = 0;
  QVariantMap json;
  EXPECT_FALSE(handle("GET", QByteArray(), &status, &json, "/state"));
}

TEST_F(ScoreboardFeedTest, HttpPostOnePacket)
{
  int status = 0;
  QVariantMap json;
  ASSERT_TRUE(handle("POST", "{\"packet\":\"" + hex(General().bytes()).toLatin1() + "\"}", &status, &json));
  EXPECT_EQ(200, status);
  EXPECT_EQ(QString::fromUtf8("general"), json.value(QString::fromUtf8("type")).toString());
  EXPECT_TRUE(json.value(QString::fromUtf8("changed")).toList().contains(QString::fromUtf8("scoreboard.home.score")));
  EXPECT_TRUE(json.value(QString::fromUtf8("values")).toMap().contains(QString::fromUtf8("scoreboard.clock")));
  EXPECT_EQ(QString::fromUtf8("05:07"), Kv::textValue(m_store.get(QString::fromUtf8("scoreboard.clock"))));
}

TEST_F(ScoreboardFeedTest, HttpPostSeveralPacketsInAnyHexStyle)
{
  const QString compact = QString::fromLatin1(General().bytes().toHex());
  const QString dashed = hex(header(0x02) + shirt("9") + char(4) + char(1)).replace(QLatin1Char(' '), QLatin1Char('-'));
  int status = 0;
  QVariantMap json;
  ASSERT_TRUE(handle("POST", "{\"packets\":[\"" + compact.toLatin1() + "\",\"" + dashed.toLatin1() + "\"]}", &status, &json));
  EXPECT_EQ(200, status);
  const QVariantList results = json.value(QString::fromUtf8("results")).toList();
  ASSERT_EQ(2, results.size());
  EXPECT_EQ(QString::fromUtf8("players"), results.at(1).toMap().value(QString::fromUtf8("type")).toString());
  EXPECT_EQ(QString::fromUtf8("4"), Kv::textValue(m_store.get(QString::fromUtf8("scoreboard.home.player.9.points"))));
}

TEST_F(ScoreboardFeedTest, HttpPostRejections)
{
  int status = 0;
  QVariantMap json;
  ASSERT_TRUE(handle("POST", "", &status, &json));
  EXPECT_EQ(400, status);
  ASSERT_TRUE(handle("POST", "{\"packets\":[]}", &status, &json));
  EXPECT_EQ(400, status);

  // A bad packet rejects the whole request, before anything is applied.
  ASSERT_TRUE(handle("POST", "{\"packets\":[\"" + hex(General().bytes()).toLatin1() + "\",\"FF FE 0\"]}", &status, &json));
  EXPECT_EQ(400, status);
  EXPECT_EQ(QString::fromUtf8("packet 1 is not hex"), json.value(QString::fromUtf8("error")).toString());
  EXPECT_FALSE(m_store.has(QString::fromUtf8("scoreboard.clock")));
  ASSERT_TRUE(handle("POST", "{\"packet\":\"ZZ\"}", &status, &json));
  EXPECT_EQ(QString::fromUtf8("packet 0 is not hex"), json.value(QString::fromUtf8("error")).toString());

  // Hex, but not a scoreboard packet.
  ASSERT_TRUE(handle("POST", "{\"packet\":\"01 02 03\"}", &status, &json));
  EXPECT_EQ(400, status);
  EXPECT_TRUE(json.value(QString::fromUtf8("error")).toString().startsWith(QString::fromUtf8("not an Ultra Score packet")));

  ASSERT_TRUE(handle("DELETE", "", &status, &json));
  EXPECT_EQ(405, status);
}

TEST_F(ScoreboardFeedTest, HttpGetStatus)
{
  int status = 0;
  QVariantMap json;
  ASSERT_TRUE(handle("POST", "{\"packet\":\"" + hex(General().bytes()).toLatin1() + "\"}", &status, &json));
  ASSERT_TRUE(handle("POST", "{\"packet\":\"01 02\"}", &status, &json));
  m_store.set(QString::fromUtf8("other"), QString::fromUtf8("not scoreboard"));

  ASSERT_TRUE(handle("GET", QByteArray(), &status, &json));
  EXPECT_EQ(200, status);
  EXPECT_FALSE(json.value(QString::fromUtf8("listening")).toBool());
  EXPECT_EQ(0, json.value(QString::fromUtf8("port")).toInt());
  EXPECT_EQ(1, json.value(QString::fromUtf8("packets")).toInt());
  EXPECT_EQ(1, json.value(QString::fromUtf8("rejected")).toInt());
  EXPECT_TRUE(json.value(QString::fromUtf8("lastRejection")).toString().startsWith(QString::fromUtf8("not an Ultra Score")));
  EXPECT_EQ(QString::fromUtf8("http"), json.value(QString::fromUtf8("lastSender")).toString());
  EXPECT_FALSE(json.value(QString::fromUtf8("lastPacket")).toString().isEmpty());
  const QVariantMap values = json.value(QString::fromUtf8("values")).toMap();
  EXPECT_TRUE(values.contains(QString::fromUtf8("scoreboard.home.score")));
  EXPECT_FALSE(values.contains(QString::fromUtf8("other")));
}

TEST_F(ScoreboardFeedTest, ReceivesUdpPackets)
{
  const quint16 port = freeUdpPort();
  int statusChanged = 0;
  QObject::connect(&m_feed, &ScoreboardFeed::statusChanged, [&]() { ++statusChanged; });
  m_feed.start(port);
  ASSERT_TRUE(m_feed.isListening()) << m_feed.lastError().toStdString();
  EXPECT_EQ(port, m_feed.port());
  EXPECT_EQ(1, statusChanged);

  QUdpSocket console;
  console.writeDatagram(inverted(General().bytes()), QHostAddress(QHostAddress::LocalHost), port);
  console.writeDatagram(QByteArray("noise"), QHostAddress(QHostAddress::LocalHost), port);
  ASSERT_TRUE(waitFor([&]() { return m_store.has(QString::fromUtf8("scoreboard.period")); }));
  EXPECT_EQ(QString::fromUtf8("Q2"), Kv::textValue(m_store.get(QString::fromUtf8("scoreboard.period"))));

  int status = 0;
  QVariantMap json;
  ASSERT_TRUE(waitFor([&]() {
    handle("GET", QByteArray(), &status, &json);
    return json.value(QString::fromUtf8("rejected")).toInt() == 1;
  }));
  EXPECT_TRUE(json.value(QString::fromUtf8("listening")).toBool());
  EXPECT_TRUE(json.value(QString::fromUtf8("lastSender")).toString().startsWith(QString::fromUtf8("127.0.0.1:")));

  m_feed.start(0);
  EXPECT_FALSE(m_feed.isListening());
  EXPECT_EQ(2, statusChanged);
}
