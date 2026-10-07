
#include "HttpServer.h"
#include "../state/StateStore.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>

namespace
{
// Upper bound on headers + body for a single request.
const int kMaxRequestSize = 1024 * 1024;
}

HttpServer::HttpServer(
    unsigned short port,
    StateStore &store,
    QObject *parent)
    : QObject(parent),
      m_port(port),
      m_store(store)
{
  connect(
      &m_server,
      &QTcpServer::newConnection,
      this,
      &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
  if (m_server.isListening())
  {
    m_server.close();
  }
}

void HttpServer::start()
{
  if (!m_server.listen(QHostAddress::LocalHost, m_port))
  {
    qCritical()
        << "Failed to start HTTP server on port"
        << m_port
        << ":"
        << m_server.errorString();

    return;
  }

  qDebug()
      << "HTTP server listening on"
      << "http://localhost:" << m_port;
}

void HttpServer::onNewConnection()
{
  while (m_server.hasPendingConnections())
  {
    QTcpSocket *socket = m_server.nextPendingConnection();

    m_buffers.insert(socket, QByteArray());

    connect(
        socket,
        &QTcpSocket::readyRead,
        this,
        &HttpServer::onReadyRead);

    connect(
        socket,
        &QTcpSocket::disconnected,
        this,
        &HttpServer::onDisconnected);
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
  QByteArray body;
  bool tooLarge = false;

  if (!parseRequest(buffer, method, path, body, tooLarge))
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

  handleRequest(socket, method, path, body);
}

void HttpServer::onDisconnected()
{
  QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());

  if (socket)
  {
    m_buffers.remove(socket);
    socket->deleteLater();
  }
}

bool HttpServer::parseRequest(
    const QByteArray &buffer,
    QByteArray &method,
    QByteArray &path,
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

    if (colon > 0 && line.left(colon).trimmed().toLower() == "content-length")
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

void HttpServer::handleRequest(
    QTcpSocket *socket,
    const QByteArray &method,
    const QByteArray &path,
    const QByteArray &body)
{
  if (method == "GET" && path == "/health")
  {
    sendResponse(socket, 200, "OK", "text/plain", "ok");
  }
  else if (path == "/state")
  {
    if (method == "POST")
    {
      handleSetState(socket, body);
    }
    else
    {
      sendResponse(socket, 405, "Method Not Allowed", "text/plain", "Method Not Allowed");
    }
  }
  else
  {
    sendResponse(socket, 404, "Not Found", "text/plain", "Not Found");
  }
}

// POST /state
// Body: a JSON object; every key/value pair is written into the store.
void HttpServer::handleSetState(QTcpSocket *socket, const QByteArray &body)
{
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &error);

  if (error.error != QJsonParseError::NoError || !doc.isObject())
  {
    sendResponse(
        socket,
        400,
        "Bad Request",
        "application/json",
        "{\"error\":\"body must be a JSON object\"}");

    return;
  }

  const QJsonObject object = doc.object();

  for (QJsonObject::const_iterator it = object.constBegin(); it != object.constEnd(); ++it)
  {
    m_store.set(it.key(), it.value().toVariant());
  }

  QJsonObject result;
  result.insert(QStringLiteral("saved"), object.size());

  sendResponse(
      socket,
      200,
      "OK",
      "application/json",
      QJsonDocument(result).toJson(QJsonDocument::Compact));
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
  response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  response += "Connection: close\r\n";
  response += "\r\n";
  response += body;

  socket->write(response);
  socket->disconnectFromHost();
}
