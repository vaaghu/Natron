
#include "HttpServer.h"
#include "../state/Json.h"
#include "../state/KvValue.h"
#include "../state/StateStore.h"

#include <QDebug>
#include <QList>
#include <QUrl>
#include <QVariantMap>

namespace
{
// Upper bound on headers + body for a single request.
const int kMaxRequestSize = 1024 * 1024;

// Delay between attempts to listen while the port is busy.
const int kRetryIntervalMs = 3000;

// Comment line sent to /events clients this often.
const int kKeepAliveMs = 15000;

// Headers sent with every response: browsers may call the API from a page.
const char kCorsHeaders[] =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Authorization, Content-Type, X-Natron-Token\r\n";

QByteArray reasonPhrase(int status)
{
  switch (status)
  {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  default:
    return "Error";
  }
}
}

HttpServer::HttpServer(
    unsigned short port,
    StateStore &store,
    QObject *parent)
    : QObject(parent),
      m_port(port),
      m_store(store),
      m_address(QHostAddress::LocalHost)
{
  connect(&m_store, SIGNAL(valueChanged(QString)), this, SLOT(onStoreValueChanged(QString)));
  connect(&m_store, SIGNAL(keysChanged()), this, SLOT(onStoreKeysChanged()));
  m_keepAliveTimer.setInterval(kKeepAliveMs);
  connect(&m_keepAliveTimer, SIGNAL(timeout()), this, SLOT(onKeepAlive()));

  connect(
      &m_server,
      SIGNAL(newConnection()),
      this,
      SLOT(onNewConnection()));

  m_retryTimer.setInterval(kRetryIntervalMs);
  connect(
      &m_retryTimer,
      SIGNAL(timeout()),
      this,
      SLOT(tryListen()));
}

HttpServer::~HttpServer()
{
  if (m_server.isListening())
  {
    m_server.close();
  }
}

void HttpServer::setToken(const QString &token)
{
  m_token = token.trimmed();
}

void HttpServer::setListenAddress(const QHostAddress &address)
{
  m_address = address;
}

void HttpServer::start()
{
  // Reachable from other machines: only with a token.
  if (m_address != QHostAddress::LocalHost && m_address != QHostAddress::LocalHostIPv6 && m_token.isEmpty())
  {
    m_lastError = tr("listening on %1 needs NATRON_HTTP_TOKEN (not started)").arg(m_address.toString());
    qWarning() << "HTTP server:" << m_lastError;
    Q_EMIT statusChanged();
    return;
  }
  tryListen();
}

bool HttpServer::isListening() const
{
  return m_server.isListening();
}

unsigned short HttpServer::port() const
{
  return m_port;
}

QString HttpServer::lastError() const
{
  return m_lastError;
}

void HttpServer::tryListen()
{
  if (m_server.isListening())
  {
    m_retryTimer.stop();
    return;
  }

  // qWarning rather than qDebug: release builds compile qDebug out.
  if (m_server.listen(m_address, m_port))
  {
    m_retryTimer.stop();
    m_lastError.clear();

    qWarning()
        << "HTTP server listening on"
        << m_address.toString() << "port" << m_port
        << (m_token.isEmpty() ? "" : "(token required)");

    Q_EMIT statusChanged();
    return;
  }

  const QString error = m_server.errorString();

  // Log each distinct failure once, not on every retry.
  if (error != m_lastError)
  {
    m_lastError = error;

    qWarning()
        << "Failed to start HTTP server on port"
        << m_port
        << ":"
        << error
        << "- retrying every" << kRetryIntervalMs / 1000 << "s";

    Q_EMIT statusChanged();
  }

  if (!m_retryTimer.isActive())
  {
    m_retryTimer.start();
  }
}

void HttpServer::onNewConnection()
{
  while (m_server.hasPendingConnections())
  {
    QTcpSocket *socket = m_server.nextPendingConnection();

    m_buffers.insert(socket, QByteArray());

    connect(
        socket,
        SIGNAL(readyRead()),
        this,
        SLOT(onReadyRead()));

    connect(
        socket,
        SIGNAL(disconnected()),
        this,
        SLOT(onDisconnected()));
  }
}

void HttpServer::onReadyRead()
{
  QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());

  if (!socket || !m_buffers.contains(socket))
  {
    return;
  }

  QByteArray &buffer = m_buffers[socket];
  buffer.append(socket->readAll());

  QByteArray method;
  QByteArray path;
  QHash<QByteArray, QByteArray> headers;
  QByteArray body;
  bool tooLarge = false;

  if (!parseRequest(buffer, method, path, headers, body, tooLarge))
  {
    if (tooLarge)
    {
      m_buffers.remove(socket);
      sendResponse(socket, 413, "Payload Too Large", "text/plain", "Payload Too Large");
    }

    // Otherwise wait for more data.
    return;
  }

  m_buffers.remove(socket);

  qDebug() << "HTTP request:" << method << path;

  handleRequest(socket, method, path, headers, body);
}

void HttpServer::onDisconnected()
{
  QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());

  if (socket)
  {
    m_buffers.remove(socket);
    m_eventClients.removeAll(socket);
    if (m_eventClients.isEmpty())
    {
      m_keepAliveTimer.stop();
    }
    socket->deleteLater();
  }
}

bool HttpServer::parseRequest(
    const QByteArray &buffer,
    QByteArray &method,
    QByteArray &path,
    QHash<QByteArray, QByteArray> &headers,
    QByteArray &body,
    bool &tooLarge)
{
  tooLarge = buffer.size() > kMaxRequestSize;

  const int headerEnd = buffer.indexOf("\r\n\r\n");

  if (headerEnd < 0)
  {
    return false;
  }

  const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
  const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');

  if (requestLine.size() < 2)
  {
    // Malformed; treat as a request with no route so it gets a 404.
    return true;
  }

  method = requestLine.at(0);
  path = requestLine.at(1);

  int contentLength = 0;

  for (int i = 1; i < lines.size(); ++i)
  {
    const QByteArray line = lines.at(i).trimmed();
    const int colon = line.indexOf(':');

    if (colon <= 0)
    {
      continue;
    }
    const QByteArray name = line.left(colon).trimmed().toLower();
    headers.insert(name, line.mid(colon + 1).trimmed());
    if (name == "content-length")
    {
      contentLength = line.mid(colon + 1).trimmed().toInt();
    }
  }

  const int bodyStart = headerEnd + 4;

  if (contentLength < 0 || bodyStart + contentLength > kMaxRequestSize)
  {
    tooLarge = true;
    return false;
  }

  if (buffer.size() - bodyStart < contentLength)
  {
    return false;
  }

  body = buffer.mid(bodyStart, contentLength);
  tooLarge = false;

  return true;
}

bool HttpServer::authorized(const QByteArray &query, const QHash<QByteArray, QByteArray> &headers) const
{
  if (m_token.isEmpty())
  {
    return true;
  }
  const QByteArray token = m_token.toUtf8();
  if (headers.value("authorization") == "Bearer " + token || headers.value("x-natron-token") == token)
  {
    return true;
  }
  // ?token=... (browsers cannot set headers on an EventSource)
  const QList<QByteArray> params = query.split('&');
  for (int i = 0; i < params.size(); ++i)
  {
    if (params.at(i) == "token=" + QUrl::toPercentEncoding(m_token) || params.at(i) == "token=" + token)
    {
      return true;
    }
  }
  return false;
}

void HttpServer::handleRequest(
    QTcpSocket *socket,
    const QByteArray &method,
    const QByteArray &fullPath,
    const QHash<QByteArray, QByteArray> &headers,
    const QByteArray &body)
{
  // Route without the query string (?token=...).
  const int q = fullPath.indexOf('?');
  const QByteArray path = (q < 0) ? fullPath : fullPath.left(q);
  const QByteArray query = (q < 0) ? QByteArray() : fullPath.mid(q + 1);

  if (method == "OPTIONS")
  {
    sendResponse(socket, 204, "No Content", "text/plain", QByteArray()); // CORS preflight
    return;
  }
  if (method == "GET" && (path == "/" || path == "/api"))
  {
    sendResponse(socket, 200, "OK", "text/html; charset=utf-8", apiReference());
    return;
  }
  if (method == "GET" && path == "/health")
  {
    sendResponse(socket, 200, "OK", "text/plain", "ok");
    return;
  }
  if (!authorized(query, headers))
  {
    sendResponse(socket, 401, "Unauthorized", "application/json",
                 "{\"error\":\"missing or wrong token (Authorization: Bearer <token>)\"}");
    return;
  }

  if (method == "GET" && path == "/events")
  {
    openEventStream(socket);
  }
  else if (path == "/state")
  {
    if (method == "POST")
    {
      handleSetState(socket, body);
    }
    else if (method == "GET")
    {
      handleGetState(socket);
    }
    else
    {
      sendResponse(socket, 405, "Method Not Allowed", "text/plain", "Method Not Allowed");
    }
  }
  else
  {
    for (int i = 0; i < m_handlers.size(); ++i)
    {
      int status = 200;
      QByteArray response;
      if (m_handlers.at(i)->handleHttpRequest(method, path, body, &status, &response))
      {
        sendResponse(socket, status, reasonPhrase(status), "application/json", response);
        return;
      }
    }
    sendResponse(socket, 404, "Not Found", "text/plain", "Not Found");
  }
}

void HttpServer::addRouteHandler(HttpRouteHandler *handler)
{
  if (handler && !m_handlers.contains(handler))
  {
    m_handlers << handler;
  }
}

void HttpServer::removeRouteHandler(HttpRouteHandler *handler)
{
  m_handlers.removeAll(handler);
}

// POST /state
// Body: a JSON object of key -> value. A value is
//   "text" or 42 (shorthand for text), {"type":"text","value":"..."},
//   {"type":"number","value":42.5}, {"type":"bool","value":true},
//   {"type":"color","value":"#ff8800"},
//   or {"type":"image","path":"/abs/file.png"} (details are read from the file).
void HttpServer::handleSetState(QTcpSocket *socket, const QByteArray &body)
{
  bool ok = false;
  const QVariant doc = Json::parse(body, &ok);

  if (!ok || doc.type() != QVariant::Map)
  {
    sendResponse(
        socket,
        400,
        "Bad Request",
        "application/json",
        "{\"error\":\"body must be a JSON object\"}");

    return;
  }

  const QVariantMap object = doc.toMap();

  // Validate everything first: a bad value rejects the whole request.
  QVariantMap typed;
  QVariantMap errors;
  for (QVariantMap::const_iterator it = object.constBegin(); it != object.constEnd(); ++it)
  {
    QString error;
    const QVariant value = Kv::normalize(it.value(), true, &error);
    if (!value.isValid())
    {
      errors.insert(it.key(), error);
    }
    else
    {
      typed.insert(it.key(), value);
    }
  }

  if (!errors.isEmpty())
  {
    QVariantMap result;
    result.insert(QString::fromUtf8("error"), QString::fromUtf8("invalid values, nothing saved"));
    result.insert(QString::fromUtf8("keys"), errors);
    sendResponse(socket, 400, "Bad Request", "application/json", Json::serialize(result));
    return;
  }

  for (QVariantMap::const_iterator it = typed.constBegin(); it != typed.constEnd(); ++it)
  {
    m_store.set(it.key(), it.value());
  }

  sendResponse(
      socket,
      200,
      "OK",
      "application/json",
      "{\"saved\":" + QByteArray::number(object.size()) + "}");
}

// GET /state
// All keys with their typed values (images include the details read from
// the file: name, width, height, format, exists).
void HttpServer::handleGetState(QTcpSocket *socket)
{
  sendResponse(socket, 200, "OK", "application/json", stateJson());
}

QByteArray HttpServer::stateJson() const
{
  QVariantMap all;
  const QStringList keys = m_store.keys();
  for (int i = 0; i < keys.size(); ++i)
  {
    all.insert(keys.at(i), m_store.get(keys.at(i)));
  }
  return Json::serialize(all);
}

// GET /events
// A text/event-stream that stays open:
//   event: state  data: all keys and values (on connect)
//   event: value  data: {"key": "...", "value": {...}}  (value null: removed)
//   event: keys   data: {"keys": [...]}                (keys added or removed)
void HttpServer::openEventStream(QTcpSocket *socket)
{
  QByteArray response;
  response += "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: text/event-stream\r\n";
  response += "Cache-Control: no-cache\r\n";
  response += "Connection: keep-alive\r\n";
  response += kCorsHeaders;
  response += "\r\n";
  socket->write(response);

  m_eventClients << socket;
  if (!m_keepAliveTimer.isActive())
  {
    m_keepAliveTimer.start();
  }
  sendEvent(socket, "state", stateJson());
}

void HttpServer::sendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &data)
{
  QByteArray message = "event: " + event + "\n";
  const QList<QByteArray> lines = data.split('\n');
  for (int i = 0; i < lines.size(); ++i)
  {
    message += "data: " + lines.at(i) + "\n";
  }
  message += "\n";
  socket->write(message);
}

void HttpServer::broadcastEvent(const QByteArray &event, const QByteArray &data)
{
  for (int i = 0; i < m_eventClients.size(); ++i)
  {
    sendEvent(m_eventClients.at(i), event, data);
  }
}

void HttpServer::onStoreValueChanged(const QString &key)
{
  if (m_eventClients.isEmpty())
  {
    return;
  }
  QVariantMap change;
  change.insert(QString::fromUtf8("key"), key);
  change.insert(QString::fromUtf8("value"), m_store.has(key) ? m_store.get(key) : QVariant());
  broadcastEvent("value", Json::serialize(change));
}

void HttpServer::onStoreKeysChanged()
{
  if (m_eventClients.isEmpty())
  {
    return;
  }
  QVariantMap keys;
  keys.insert(QString::fromUtf8("keys"), m_store.keys());
  broadcastEvent("keys", Json::serialize(keys));
}

void HttpServer::onKeepAlive()
{
  for (int i = 0; i < m_eventClients.size(); ++i)
  {
    m_eventClients.at(i)->write(": ping\n\n");
  }
}

QByteArray HttpServer::apiReference()
{
  // Kept short; the request formats are those documented in the code above.
  static const char page[] =
      "<!doctype html><html><head><meta charset=\"utf-8\"><title>Natron API</title>"
      "<style>body{font:14px/1.5 sans-serif;max-width:760px;margin:2em auto;padding:0 1em;color:#222}"
      "code,pre{background:#f3f3f3;padding:2px 4px;border-radius:3px}pre{padding:8px;overflow:auto}"
      "h2{margin-top:1.6em;border-bottom:1px solid #ddd}td{padding:2px 12px 2px 0;vertical-align:top}</style>"
      "</head><body><h1>Natron dashboard API</h1>"
      "<p>Values set here update the dashboard, the nodes bound to them and NDI renders.</p>"
      "<h2>Access</h2><p>Without <code>NATRON_HTTP_TOKEN</code> the API answers on this machine only. "
      "With it, send <code>Authorization: Bearer &lt;token&gt;</code> (or <code>X-Natron-Token</code>, "
      "or <code>?token=</code>); <code>NATRON_HTTP_HOST=0.0.0.0</code> then opens it to the network.</p>"
      "<h2>Endpoints</h2><table>"
      "<tr><td><code>GET /health</code></td><td><code>ok</code></td></tr>"
      "<tr><td><code>GET /state</code></td><td>All keys and their typed values.</td></tr>"
      "<tr><td><code>POST /state</code></td><td>Sets keys: a JSON object of key &rarr; value. "
      "Invalid values reject the whole request.</td></tr>"
      "<tr><td><code>GET /events</code></td><td>Server-sent events: <code>state</code> on connect, "
      "then <code>value</code> (<code>{key, value}</code>, value <code>null</code> when removed) "
      "and <code>keys</code> (<code>{keys}</code>).</td></tr>"
      "<tr><td><code>GET /ndi</code></td><td>NDI sources and their state.</td></tr>"
      "<tr><td><code>POST /ndi</code></td><td><code>{scene, project?, writer?, action?, pauseAt?, loop?}</code>; "
      "actions: play, pause, stop, replay, cue, continue.</td></tr></table>"
      "<h2>Values</h2><pre>"
      "\"Text\" or 42                          shorthand for text\n"
      "{\"type\": \"text\",   \"value\": \"COPPER\"}\n"
      "{\"type\": \"number\", \"value\": 42.5}\n"
      "{\"type\": \"bool\",   \"value\": true}\n"
      "{\"type\": \"color\",  \"value\": \"#ff8800\"}       (#rrggbbaa for alpha)\n"
      "{\"type\": \"image\",  \"path\": \"/abs/player.png\"}</pre>"
      "<p>Text nodes take text, numbers and on/off values as their text and colors as their text color; "
      "Read nodes take images.</p>"
      "<h2>Example</h2><pre>curl -X POST http://localhost:3000/state \\\n"
      "     -H 'Content-Type: application/json' \\\n"
      "     -d '{\"playerName\": \"COPPER\", \"score\": {\"type\": \"number\", \"value\": 3}}'\n\n"
      "const events = new EventSource('http://localhost:3000/events');\n"
      "events.addEventListener('value', e =&gt; console.log(JSON.parse(e.data)));</pre>"
      "</body></html>";
  return QByteArray(page);
}

void HttpServer::sendResponse(
    QTcpSocket *socket,
    int status,
    const QByteArray &reason,
    const QByteArray &contentType,
    const QByteArray &body)
{
  QByteArray response;
  response += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
  response += "Content-Type: " + contentType + "\r\n";
  response += kCorsHeaders;
  if (status == 401)
  {
    response += "WWW-Authenticate: Bearer\r\n";
  }
  response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  response += "Connection: close\r\n";
  response += "\r\n";
  response += body;

  socket->write(response);
  socket->disconnectFromHost();
}
