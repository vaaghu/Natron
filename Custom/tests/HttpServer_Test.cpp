#include "TestUtil.h"

#include "Custom/server/HttpServer.h"
#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"
#include "Custom/state/StateStore.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantMap>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
struct Response
{
  int status;
  QByteArray headers;
  QByteArray body;

  Response()
      : status(0)
  {
  }

  QVariantMap json() const { return Json::parse(body, nullptr).toMap(); }
};

Response parseResponse(const QByteArray &raw)
{
  Response r;
  const int end = raw.indexOf("\r\n\r\n");
  r.headers = end < 0 ? raw : raw.left(end);
  r.body = end < 0 ? QByteArray() : raw.mid(end + 4);
  const QList<QByteArray> first = r.headers.split('\n').first().split(' ');
  r.status = first.size() > 1 ? first.at(1).toInt() : 0;
  return r;
}

// Sends raw bytes and reads until the server closes the connection.
Response send(quint16 port, const QByteArray &request)
{
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, port);
  if (!waitFor([&]() { return socket.state() == QAbstractSocket::ConnectedState; }))
  {
    return Response();
  }
  socket.write(request);
  QByteArray raw;
  waitFor([&]() {
    raw += socket.readAll();
    return socket.state() == QAbstractSocket::UnconnectedState;
  });
  raw += socket.readAll();
  return parseResponse(raw);
}

QByteArray request(const QByteArray &method, const QByteArray &path, const QByteArray &body = QByteArray(),
                   const QByteArray &extraHeaders = QByteArray())
{
  QByteArray r = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n" + extraHeaders;
  if (!body.isNull())
  {
    r += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
  }
  return r + "\r\n" + body;
}

class EchoHandler : public HttpRouteHandler
{
public:
  bool handleHttpRequest(const QByteArray &method, const QByteArray &path, const QByteArray &body, int *status,
                         QByteArray *response)
  {
    if (path != "/echo")
    {
      return false;
    }
    *status = method == "POST" ? 200 : 405;
    *response = "{\"method\":\"" + method + "\",\"body\":" + (body.isEmpty() ? QByteArray("null") : body) + "}";
    return true;
  }
};

class HttpServerTest : public ::testing::Test
{
protected:
  void SetUp()
  {
    m_port = freeTcpPort();
    m_server = new HttpServer(m_port, m_store);
  }

  void TearDown() { delete m_server; }

  void start()
  {
    m_server->start();
    ASSERT_TRUE(m_server->isListening()) << m_server->lastError().toStdString();
  }

  Response get(const QByteArray &path, const QByteArray &headers = QByteArray())
  {
    return send(m_port, request("GET", path, QByteArray(), headers));
  }

  Response post(const QByteArray &path, const QByteArray &body, const QByteArray &headers = QByteArray())
  {
    return send(m_port, request("POST", path, body, headers));
  }

  StateStore m_store;
  quint16 m_port;
  HttpServer *m_server;
};
}

TEST_F(HttpServerTest, Basics)
{
  EXPECT_EQ(m_port, m_server->port());
  EXPECT_FALSE(m_server->isListening());
  int statusChanged = 0;
  QObject::connect(m_server, &HttpServer::statusChanged, [&]() { ++statusChanged; });
  start();
  EXPECT_TRUE(m_server->lastError().isEmpty());
  EXPECT_EQ(1, statusChanged);
}

TEST_F(HttpServerTest, HealthAndApiReference)
{
  start();
  const Response health = get("/health");
  EXPECT_EQ(200, health.status);
  EXPECT_EQ(QByteArray("ok"), health.body);
  EXPECT_TRUE(health.headers.contains("Access-Control-Allow-Origin: *"));
  EXPECT_TRUE(health.headers.contains("Content-Length: 2"));

  const Response api = get("/");
  EXPECT_EQ(200, api.status);
  EXPECT_TRUE(api.headers.contains("Content-Type: text/html; charset=utf-8"));
  EXPECT_TRUE(api.body.contains("Natron dashboard API"));
  EXPECT_EQ(200, get("/api").status);
}

TEST_F(HttpServerTest, CorsPreflight)
{
  start();
  const Response r = send(m_port, request("OPTIONS", "/state"));
  EXPECT_EQ(204, r.status);
  EXPECT_TRUE(r.headers.contains("Access-Control-Allow-Methods: GET, POST, OPTIONS"));
  EXPECT_TRUE(r.body.isEmpty());
}

TEST_F(HttpServerTest, PostStateSetsTypedValues)
{
  start();
  const Response r = post("/state", "{\"name\":\"COPPER\",\"count\":3,\"score\":{\"type\":\"number\",\"value\":42.5},"
                                    "\"on\":{\"type\":\"bool\",\"value\":true},\"tint\":{\"type\":\"color\",\"value\":\"#FF8800\"}}");
  EXPECT_EQ(200, r.status);
  EXPECT_EQ(QByteArray("{\"saved\":5}"), r.body);
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("COPPER")), m_store.get(QString::fromUtf8("name")));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("3")), m_store.get(QString::fromUtf8("count")));
  EXPECT_EQ(Kv::makeNumber(42.5), m_store.get(QString::fromUtf8("score")));
  EXPECT_EQ(Kv::makeBool(true), m_store.get(QString::fromUtf8("on")));
  EXPECT_EQ(Kv::makeColor(QString::fromUtf8("#ff8800")), m_store.get(QString::fromUtf8("tint")));
}

TEST_F(HttpServerTest, PostStateRejectsTheWholeRequestOnABadValue)
{
  start();
  const Response r = post("/state", "{\"good\":\"x\",\"bad\":{\"type\":\"number\",\"value\":\"abc\"}}");
  EXPECT_EQ(400, r.status);
  EXPECT_EQ(QString::fromUtf8("invalid values, nothing saved"), r.json().value(QString::fromUtf8("error")).toString());
  EXPECT_TRUE(r.json().value(QString::fromUtf8("keys")).toMap().contains(QString::fromUtf8("bad")));
  EXPECT_FALSE(m_store.has(QString::fromUtf8("good")));
}

TEST_F(HttpServerTest, PostStateNeedsAnObject)
{
  start();
  EXPECT_EQ(400, post("/state", "[1,2]").status);
  EXPECT_EQ(400, post("/state", "not json").status);
  EXPECT_EQ(400, post("/state", "").status);
}

TEST_F(HttpServerTest, GetState)
{
  m_store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  m_store.set(QString::fromUtf8("b"), Kv::makeBool(false));
  start();
  const Response r = get("/state");
  EXPECT_EQ(200, r.status);
  EXPECT_TRUE(r.headers.contains("Content-Type: application/json"));
  EXPECT_EQ(Kv::makeText(QString::fromUtf8("1")), r.json().value(QString::fromUtf8("a")));
  EXPECT_EQ(Kv::makeBool(false), r.json().value(QString::fromUtf8("b")));
}

TEST_F(HttpServerTest, UnknownRoutesAndMethods)
{
  start();
  EXPECT_EQ(404, get("/nope").status);
  EXPECT_EQ(405, send(m_port, request("PUT", "/state", "{}")).status);
  EXPECT_EQ(404, send(m_port, "GARBAGE\r\n\r\n").status);
}

TEST_F(HttpServerTest, RouteHandlers)
{
  EchoHandler echo;
  m_server->addRouteHandler(&echo);
  m_server->addRouteHandler(&echo); // once
  m_server->addRouteHandler(nullptr);
  start();

  Response r = post("/echo", "{\"x\":1}");
  EXPECT_EQ(200, r.status);
  EXPECT_EQ(QByteArray("{\"method\":\"POST\",\"body\":{\"x\":1}}"), r.body);
  EXPECT_TRUE(r.headers.contains("Content-Type: application/json"));
  r = get("/echo");
  EXPECT_EQ(405, r.status);
  EXPECT_TRUE(r.headers.startsWith("HTTP/1.1 405 Method Not Allowed"));

  m_server->removeRouteHandler(&echo);
  EXPECT_EQ(404, post("/echo", "{}").status);
}

TEST_F(HttpServerTest, RequestQueryIsNotPartOfTheRoute)
{
  start();
  EXPECT_EQ(200, get("/state?x=1").status);
}

TEST_F(HttpServerTest, BodyArrivingLater)
{
  start();
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, m_port);
  ASSERT_TRUE(waitFor([&]() { return socket.state() == QAbstractSocket::ConnectedState; }));
  const QByteArray body = "{\"late\":\"yes\"}";
  socket.write("POST /state HTTP/1.1\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n");
  processEventsFor(100);
  EXPECT_FALSE(m_store.has(QString::fromUtf8("late")));
  socket.write(body);
  ASSERT_TRUE(waitFor([&]() { return m_store.has(QString::fromUtf8("late")); }));
}

TEST_F(HttpServerTest, TooLargeRequest)
{
  start();
  const Response r = send(m_port, "POST /state HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n{}");
  EXPECT_EQ(413, r.status);
}

TEST_F(HttpServerTest, TokenRequired)
{
  m_server->setToken(QString::fromUtf8("  s3cret x  "));
  start();

  // Open without the token.
  EXPECT_EQ(200, get("/health").status);
  EXPECT_EQ(200, get("/").status);

  const Response denied = get("/state");
  EXPECT_EQ(401, denied.status);
  EXPECT_TRUE(denied.headers.contains("WWW-Authenticate: Bearer"));
  EXPECT_EQ(401, get("/state", "Authorization: Bearer wrong\r\n").status);
  EXPECT_EQ(401, get("/nope").status); // before routing

  EXPECT_EQ(200, get("/state", "Authorization: Bearer s3cret x\r\n").status);
  EXPECT_EQ(200, get("/state", "X-Natron-Token: s3cret x\r\n").status);
  EXPECT_EQ(200, get("/state?token=s3cret%20x").status);
  EXPECT_EQ(200, get("/state?a=1&token=s3cret%20x").status);
  EXPECT_EQ(401, get("/state?token=s3cret").status);
}

TEST_F(HttpServerTest, NetworkAddressNeedsAToken)
{
  m_server->setListenAddress(QHostAddress::Any);
  m_server->start();
  EXPECT_FALSE(m_server->isListening());
  EXPECT_TRUE(m_server->lastError().contains(QString::fromUtf8("NATRON_HTTP_TOKEN")));
}

TEST_F(HttpServerTest, RetriesWhileThePortIsBusy)
{
  QTcpServer blocker;
  ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, m_port));
  m_server->start();
  EXPECT_FALSE(m_server->isListening());
  EXPECT_FALSE(m_server->lastError().isEmpty());

  blocker.close();
  ASSERT_TRUE(waitFor([&]() { return m_server->isListening(); }, 8000));
  EXPECT_TRUE(m_server->lastError().isEmpty());
}

TEST_F(HttpServerTest, EventStream)
{
  m_store.set(QString::fromUtf8("a"), QString::fromUtf8("1"));
  start();

  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, m_port);
  ASSERT_TRUE(waitFor([&]() { return socket.state() == QAbstractSocket::ConnectedState; }));
  socket.write("GET /events HTTP/1.1\r\n\r\n");
  QByteArray received;
  ASSERT_TRUE(waitFor([&]() {
    received += socket.readAll();
    return received.contains("event: state\n");
  }));
  EXPECT_TRUE(received.startsWith("HTTP/1.1 200 OK\r\n"));
  EXPECT_TRUE(received.contains("Content-Type: text/event-stream"));
  EXPECT_TRUE(received.contains("data: {\"a\":{\"type\":\"text\",\"value\":\"1\"}}\n\n"));

  m_store.set(QString::fromUtf8("b"), QString::fromUtf8("2"));
  ASSERT_TRUE(waitFor([&]() {
    received += socket.readAll();
    return received.contains("event: keys\n");
  }));
  EXPECT_TRUE(received.contains("event: value\ndata: {\"key\":\"b\",\"value\":{\"type\":\"text\",\"value\":\"2\"}}\n\n"));
  EXPECT_TRUE(received.contains("event: keys\ndata: {\"keys\":["));

  m_store.remove(QString::fromUtf8("a"));
  ASSERT_TRUE(waitFor([&]() {
    received += socket.readAll();
    return received.contains("{\"key\":\"a\",\"value\":null}");
  }));

  // Still open; closing it is fine.
  EXPECT_EQ(QAbstractSocket::ConnectedState, socket.state());
  socket.disconnectFromHost();
  processEventsFor(50);
  m_store.set(QString::fromUtf8("c"), QString::fromUtf8("3"));
}
