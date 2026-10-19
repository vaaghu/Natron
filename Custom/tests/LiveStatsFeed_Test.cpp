#include "TestUtil.h"

#include "Custom/scoreboard/LiveStatsFeed.h"
#include "Custom/scoreboard/ScoreboardFeed.h"
#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
const char kTeams[] =
    "{\"type\":\"teams\",\"messageId\":1,\"teams\":["
    "{\"teamNumber\":1,\"detail\":{\"teamName\":\"Home City\",\"teamCode\":\"HOM\",\"isHomeCompetitor\":1},"
    "\"players\":[{\"pno\":1,\"shirtNumber\":\"7\",\"firstName\":\"Sam\",\"familyName\":\"Smith\",\"active\":1},"
    "{\"pno\":2,\"shirtNumber\":\"8\",\"firstName\":\"Ed\",\"familyName\":\"Eight\"}]},"
    "{\"teamNumber\":2,\"detail\":{\"teamName\":\"Visitors\",\"teamCode\":\"VIS\"},\"players\":[]}]}";

const char kStatus[] =
    "{\"type\":\"status\",\"messageId\":2,\"status\":\"INPROGRESS\","
    "\"period\":{\"current\":1,\"periodType\":\"REGULAR\"},\"clock\":\"09:00:00\",\"clockRunning\":0,"
    "\"scores\":[{\"teamNumber\":1,\"score\":5},{\"teamNumber\":2,\"score\":3}]}";

QVariantMap json(const char *text)
{
  bool ok = false;
  return Json::parse(QByteArray(text), &ok).toMap();
}

// An Ultra Score general data packet: Q1, 05:00, score 40 - 38.
QByteArray consolePacket()
{
  const int values[] = {0xFF, 0xFE, 0x01, 0x01, 0x00, 0x00, 0x12, 1, 0, 5, 0, 0, 0xFF, 0xFF, 0xFF,
                        0, 0, 40, 38, 0, 0, 0, 0, 0, 0};
  QByteArray p(25, '\0');
  for (int i = 0; i < 25; ++i)
  {
    p[i] = char(values[i]);
  }
  return p;
}

// One HTTP response per connection, with the current body.
class FakeHttp : public QObject
{
public:
  QByteArray body;
  int requests = 0;
  QTcpServer server;

  FakeHttp()
  {
    server.listen(QHostAddress::LocalHost);
    QObject::connect(&server, &QTcpServer::newConnection, [this]() {
      QTcpSocket *socket = server.nextPendingConnection();
      QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        if (!socket->readAll().contains("\r\n\r\n"))
        {
          return;
        }
        ++requests;
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
  }

  QString url(const char *path) const
  {
    return QString::fromUtf8("http://127.0.0.1:") + QString::number(server.serverPort()) + QString::fromUtf8(path);
  }
};

class LiveStatsFeedTest : public ::testing::Test
{
protected:
  LiveStatsFeedTest()
      : m_console(&m_store),
        m_feed(&m_store, &m_console)
  {
  }

  QString text(const char *key) const
  {
    return Kv::textValue(m_store.get(QString::fromUtf8("scoreboard.") + QString::fromUtf8(key)));
  }

  bool has(const char *key) const
  {
    return m_store.has(QString::fromUtf8("scoreboard.") + QString::fromUtf8(key));
  }

  void message(const char *text)
  {
    QVariantMap result;
    EXPECT_TRUE(m_feed.receiveMessage(json(text), QString::fromUtf8("test"), &result))
        << result.value(QString::fromUtf8("error")).toString().toStdString();
  }

  void handle(const char *method, const QByteArray &body, int *status, QVariantMap *response)
  {
    QByteArray out;
    ASSERT_TRUE(m_feed.handleHttpRequest(QByteArray(method), QByteArray("/livestats"), body, status, &out));
    bool ok = false;
    *response = Json::parse(out, &ok).toMap();
    EXPECT_TRUE(ok) << out.constData();
  }

  StateStore m_store;
  ScoreboardFeed m_console;
  LiveStatsFeed m_feed;
};
} // namespace

TEST_F(LiveStatsFeedTest, WritesAndRemovesKeys)
{
  message(kTeams);
  message(kStatus);
  EXPECT_EQ(QString::fromUtf8("Sam Smith"), text("home.player.7.name"));
  EXPECT_EQ(QString::fromUtf8("Ed Eight"), text("home.player.8.name"));
  EXPECT_EQ(QString::fromUtf8("09:00"), text("clock"));
  EXPECT_EQ(QString::fromUtf8("5"), text("home.score"));

  // #8 leaves the roster: its keys go.
  QByteArray teams(kTeams);
  teams.replace(",{\"pno\":2,\"shirtNumber\":\"8\",\"firstName\":\"Ed\",\"familyName\":\"Eight\"}", "");
  message(teams.constData());
  EXPECT_FALSE(has("home.player.8.name"));
  EXPECT_FALSE(has("home.roster.2.name"));
  EXPECT_TRUE(has("home.player.7.name"));

  // Reset: every key goes.
  m_feed.reset();
  EXPECT_FALSE(has("home.player.7.name"));
  EXPECT_FALSE(has("clock"));
}

TEST_F(LiveStatsFeedTest, ConsoleKeepsTheGameState)
{
  int status = 0;
  QByteArray out;
  QVariantMap packet;
  QStringList hex;
  const QByteArray bytes = consolePacket();
  for (int i = 0; i < bytes.size(); ++i)
  {
    hex << QString::fromLatin1(bytes.mid(i, 1).toHex());
  }
  packet.insert(QString::fromUtf8("packet"), hex.join(QLatin1Char(' ')));
  ASSERT_TRUE(m_console.handleHttpRequest("POST", "/scoreboard", Json::serialize(packet), &status, &out));
  ASSERT_EQ(200, status) << out.constData();
  EXPECT_TRUE(m_feed.consoleActive());

  message(kTeams);
  message(kStatus);
  // The console's clock and score stay; LiveStats adds what it doesn't have.
  EXPECT_EQ(QString::fromUtf8("05:00"), text("clock"));
  EXPECT_EQ(QString::fromUtf8("40"), text("home.score"));
  EXPECT_EQ(QString::fromUtf8("INPROGRESS"), text("match.status"));
  EXPECT_EQ(QString::fromUtf8("Sam Smith"), text("home.player.7.name"));

  // LiveStats has players: the console's player data is left out.
  const char players[] = "FF FE 02 01 00 00 00 37 00 00 63 05 FD FC"; // #7: 99 points
  packet.insert(QString::fromUtf8("packet"), QString::fromUtf8(players));
  ASSERT_TRUE(m_console.handleHttpRequest("POST", "/scoreboard", Json::serialize(packet), &status, &out));
  EXPECT_EQ(QString::fromUtf8("0"), text("home.player.7.points"));

  // Stopped: the console writes them again.
  m_feed.stop();
  ASSERT_TRUE(m_console.handleHttpRequest("POST", "/scoreboard", Json::serialize(packet), &status, &out));
  EXPECT_EQ(QString::fromUtf8("99"), text("home.player.7.points"));
}

TEST_F(LiveStatsFeedTest, TvFeedOverTcp)
{
  QTcpServer server;
  ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
  QTcpSocket *client = nullptr;
  QByteArray received;
  QObject::connect(&server, &QTcpServer::newConnection, [&]() {
    client = server.nextPendingConnection();
    QObject::connect(client, &QTcpSocket::readyRead, [&]() { received += client->readAll(); });
  });

  int statusChanged = 0;
  QObject::connect(&m_feed, &LiveStatsFeed::statusChanged, [&]() { ++statusChanged; });
  m_feed.connectTo(QString::fromUtf8("127.0.0.1"), server.serverPort());
  EXPECT_EQ(QString::fromUtf8("tv"), m_feed.mode());

  // The parameters message first, on one line.
  ASSERT_TRUE(waitFor([&]() { return received.endsWith("\r\n"); }));
  EXPECT_TRUE(m_feed.isConnected());
  bool ok = false;
  const QVariantMap parameters = Json::parse(received.trimmed(), &ok).toMap();
  ASSERT_TRUE(ok) << received.constData();
  EXPECT_EQ(QString::fromUtf8("parameters"), parameters.value(QString::fromUtf8("type")).toString());
  EXPECT_TRUE(parameters.value(QString::fromUtf8("types")).toString().contains(QString::fromUtf8("box")));
  EXPECT_FALSE(parameters.contains(QString::fromUtf8("fromMessageId")));

  // Messages split anywhere, a bad line in between.
  const QByteArray stream = QByteArray(kTeams) + "\r\nnot json\r\n" + kStatus + "\r\n";
  client->write(stream.left(40));
  client->flush();
  processEventsFor(50);
  client->write(stream.mid(40));
  ASSERT_TRUE(waitFor([&]() { return has("clock"); }));
  EXPECT_EQ(QString::fromUtf8("Sam Smith"), text("home.player.7.name"));

  int status = 0;
  QVariantMap response;
  handle("GET", QByteArray(), &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_EQ(QString::fromUtf8("tv"), response.value(QString::fromUtf8("mode")).toString());
  EXPECT_TRUE(response.value(QString::fromUtf8("connected")).toBool());
  EXPECT_EQ(2, response.value(QString::fromUtf8("messages")).toInt());
  EXPECT_EQ(1, response.value(QString::fromUtf8("rejected")).toInt());
  EXPECT_EQ(2, response.value(QString::fromUtf8("lastMessageId")).toInt());
  EXPECT_TRUE(response.value(QString::fromUtf8("values")).toMap().contains(QString::fromUtf8("scoreboard.home.name")));

  // Disconnected: reconnects and asks for what it missed.
  received.clear();
  client->disconnectFromHost();
  ASSERT_TRUE(waitFor([&]() { return received.endsWith("\r\n"); }, 10000));
  const QVariantMap again = Json::parse(received.trimmed(), &ok).toMap();
  EXPECT_EQ(3, again.value(QString::fromUtf8("fromMessageId")).toInt());
  EXPECT_GE(statusChanged, 3);

  m_feed.stop();
  EXPECT_TRUE(m_feed.mode().isEmpty());
  EXPECT_FALSE(m_feed.isConnected());
}

TEST_F(LiveStatsFeedTest, TvFeedRestarted)
{
  QTcpServer server;
  ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
  QList<QTcpSocket *> clients;
  QList<QByteArray> parameters; // the first line of each connection
  QObject::connect(&server, &QTcpServer::newConnection, [&]() {
    QTcpSocket *client = server.nextPendingConnection();
    clients << client;
    parameters << QByteArray();
    const int i = parameters.size() - 1;
    QObject::connect(client, &QTcpSocket::readyRead, [&, client, i]() { parameters[i] += client->readAll(); });
  });
  auto fromMessageId = [&](int i) {
    bool ok = false;
    const QVariantMap p = Json::parse(parameters.at(i).trimmed(), &ok).toMap();
    return p.contains(QString::fromUtf8("fromMessageId")) ? p.value(QString::fromUtf8("fromMessageId")).toInt() : -1;
  };

  m_feed.connectTo(QString::fromUtf8("127.0.0.1"), server.serverPort());
  ASSERT_TRUE(waitFor([&]() { return !parameters.isEmpty() && parameters.at(0).endsWith("\r\n"); }));
  EXPECT_EQ(-1, fromMessageId(0));
  QByteArray teams(kTeams);
  teams.replace("\"messageId\":1", "\"messageId\":40");
  clients.at(0)->write(teams + "\r\n");
  ASSERT_TRUE(waitFor([&]() { return has("home.name"); }));

  // LiveStats restarts: it can't resend from 41, and numbers from 1 again.
  clients.at(0)->disconnectFromHost();
  ASSERT_TRUE(waitFor([&]() { return parameters.size() == 2 && parameters.at(1).endsWith("\r\n"); }, 10000));
  EXPECT_EQ(41, fromMessageId(1));
  clients.at(1)->write(QByteArray(kStatus) + "\r\n"); // messageId 2

  // So the feed reconnects at once and asks for everything.
  ASSERT_TRUE(waitFor([&]() { return parameters.size() == 3 && parameters.at(2).endsWith("\r\n"); }, 2000));
  EXPECT_EQ(-1, fromMessageId(2));
  int code = 0;
  QVariantMap response;
  handle("GET", QByteArray(), &code, &response);
  EXPECT_EQ(0, response.value(QString::fromUtf8("lastMessageId")).toInt());
  EXPECT_FALSE(has("clock")); // the message of the restarted feed was not read

  clients.at(2)->write(QByteArray(kStatus) + "\r\n");
  ASSERT_TRUE(waitFor([&]() { return has("clock"); }));
  EXPECT_EQ(QString::fromUtf8("Home City"), text("home.name")); // kept meanwhile

  // A normal resend (ids from the one asked for) is read as usual.
  clients.at(2)->disconnectFromHost();
  ASSERT_TRUE(waitFor([&]() { return parameters.size() == 4 && parameters.at(3).endsWith("\r\n"); }, 10000));
  EXPECT_EQ(3, fromMessageId(3));
  QByteArray status(kStatus);
  status.replace("\"messageId\":2", "\"messageId\":3").replace("\"score\":5", "\"score\":7");
  clients.at(3)->write(status + "\r\n");
  ASSERT_TRUE(waitFor([&]() { return text("home.score") == QString::fromUtf8("7"); }));
  processEventsFor(200);
  EXPECT_EQ(4, parameters.size());
}

TEST_F(LiveStatsFeedTest, ReconnectsAfterRefused)
{
  const quint16 port = freeTcpPort();
  m_feed.connectTo(QString::fromUtf8("127.0.0.1"), port);
  ASSERT_TRUE(waitFor([&]() { return !m_feed.lastError().isEmpty(); }));
  EXPECT_FALSE(m_feed.isConnected());

  QTcpServer server;
  ASSERT_TRUE(server.listen(QHostAddress::LocalHost, port));
  EXPECT_TRUE(waitFor([&]() { return m_feed.isConnected(); }, 10000));
  EXPECT_TRUE(m_feed.lastError().isEmpty());
}

TEST_F(LiveStatsFeedTest, PollsDataJson)
{
  FakeHttp http;
  http.body = "{\"clock\":\"04:00\",\"period\":2,\"tm\":{\"1\":{\"name\":\"Home City\",\"score\":10,"
              "\"pl\":{\"1\":{\"shirtNumber\":\"5\",\"name\":\"Five\",\"sPoints\":10}}},"
              "\"2\":{\"name\":\"Visitors\",\"score\":8}}}";
  m_feed.pollMatch(http.url("/data/123/data.json"), 100);
  EXPECT_EQ(QString::fromUtf8("online"), m_feed.mode());
  ASSERT_TRUE(waitFor([&]() { return has("home.player.5.points"); }));
  EXPECT_TRUE(m_feed.isConnected());
  EXPECT_EQ(QString::fromUtf8("10"), text("home.player.5.points"));
  EXPECT_EQ(QString::fromUtf8("Q2"), text("period"));

  http.body.replace("\"sPoints\":10", "\"sPoints\":12");
  ASSERT_TRUE(waitFor([&]() { return text("home.player.5.points") == QString::fromUtf8("12"); }));

  m_feed.stop();
  const int requests = http.requests;
  processEventsFor(300);
  EXPECT_EQ(requests, http.requests);
}

TEST_F(LiveStatsFeedTest, MatchIdUsesTheBaseUrl)
{
  FakeHttp http;
  http.body = "{\"tm\":{\"1\":{\"name\":\"Base\"}}}";
  m_feed.setBaseUrl(http.url("/"));
  int status = 0;
  QVariantMap response;
  handle("POST", "{\"matchId\": \"777\", \"interval\": 0.5}", &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_EQ(http.url("/data/777/data.json"), response.value(QString::fromUtf8("url")).toString());
  ASSERT_TRUE(waitFor([&]() { return has("home.name"); }));
  EXPECT_EQ(QString::fromUtf8("Base"), text("home.name"));
}

TEST_F(LiveStatsFeedTest, DownloadsImages)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString png = dir.filePath(QString::fromUtf8("source.png"));
  ASSERT_TRUE(writeImage(png, 4, 3));

  FakeHttp http;
  http.body = readFile(png);
  m_feed.setImageDirectory(dir.filePath(QString::fromUtf8("images")));

  const QString photo = http.url("/photo.png?signature=1");
  const QByteArray teams =
      "{\"type\":\"teams\",\"teams\":[{\"teamNumber\":1,\"detail\":{\"teamName\":\"H\"},\"players\":"
      "[{\"pno\":1,\"shirtNumber\":\"7\",\"photo\":\"" +
      photo.toUtf8() + "\"}]}]}";
  message(teams.constData());
  ASSERT_TRUE(waitFor([&]() { return has("home.player.7.photo"); }));
  const Kv::ImageInfo info = Kv::imageInfo(m_store.get(QString::fromUtf8("scoreboard.home.player.7.photo")));
  EXPECT_TRUE(info.exists);
  EXPECT_EQ(4, info.width);
  EXPECT_TRUE(info.path.startsWith(dir.filePath(QString::fromUtf8("images"))));
  EXPECT_TRUE(has("home.roster.1.photo"));

  // A new signature for the same photo: from the cache, no download.
  m_feed.reset();
  const int requests = http.requests;
  QByteArray resigned = teams;
  resigned.replace("signature=1", "signature=2");
  message(resigned.constData());
  EXPECT_TRUE(has("home.player.7.photo"));
  EXPECT_EQ(requests, http.requests);
}

TEST_F(LiveStatsFeedTest, HttpMessages)
{
  int status = 0;
  QVariantMap response;
  handle("POST", QByteArray("{\"messages\": [") + kTeams + "," + kStatus + "]}", &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_EQ(2, response.value(QString::fromUtf8("results")).toList().size());
  EXPECT_EQ(QString::fromUtf8("Home City"), text("home.name"));

  handle("POST", "{\"message\": {\"type\": \"ping\"}}", &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_EQ(QString::fromUtf8("ping"), response.value(QString::fromUtf8("type")).toString());

  handle("POST", "{\"message\": {\"no\": \"type\"}}", &status, &response);
  EXPECT_EQ(400, status);
  EXPECT_FALSE(response.value(QString::fromUtf8("error")).toString().isEmpty());

  handle("POST", "{\"messages\": [{\"type\": \"ping\"}, 3]}", &status, &response);
  EXPECT_EQ(400, status);

  handle("POST", "{\"dataJson\": {\"tm\": {\"1\": {\"name\": \"By hand\"}}}}", &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_EQ(QString::fromUtf8("By hand"), text("home.name"));

  handle("POST", "{}", &status, &response);
  EXPECT_EQ(400, status);

  handle("POST", "{\"host\": \"\"}", &status, &response);
  EXPECT_EQ(400, status);

  handle("POST", "{\"reset\": true}", &status, &response);
  EXPECT_EQ(200, status);
  EXPECT_FALSE(has("home.name"));

  QByteArray out;
  EXPECT_FALSE(m_feed.handleHttpRequest("GET", "/other", QByteArray(), &status, &out));
  EXPECT_TRUE(m_feed.handleHttpRequest("DELETE", "/livestats", QByteArray(), &status, &out));
  EXPECT_EQ(405, status);
}
