
#include "HttpServer.h"
#include <QDebug>
#include <QStringList>

HttpServer::HttpServer(unsigned short port, QObject *parent)
    : QObject(parent),
      m_port(port)
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

  if (!socket)
  {
    return;
  }

  const QByteArray request = socket->readAll();

  if (request.isEmpty())
  {
    return;
  }

  const QList<QByteArray> lines = request.split('\n');

  if (lines.isEmpty())
  {
    return;
  }

  const QByteArray requestLine = lines.first().trimmed();

  qDebug() << "HTTP request:" << requestLine;

  QByteArray response;

  if (requestLine.startsWith("GET /health "))
  {
    const QByteArray body = "ok";

    response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "ok";
  }
  else
  {
    const QByteArray body = "Not Found";

    response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not Found";
  }

  socket->write(response);
  socket->disconnectFromHost();
}

void HttpServer::onDisconnected()
{
  QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());

  if (socket)
  {
    socket->deleteLater();
  }
}
