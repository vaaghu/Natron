#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class StateStore;

class HttpServer : public QObject
{
  Q_OBJECT

public:
  explicit HttpServer(
      unsigned short port,
      StateStore &store,
      QObject *parent = nullptr);
  ~HttpServer();

  void start();

private Q_SLOTS:
  void onNewConnection();
  void onReadyRead();
  void onDisconnected();

private:
  // Returns true once a full request (headers + body) is in buffer.
  static bool parseRequest(
      const QByteArray &buffer,
      QByteArray &method,
      QByteArray &path,
      QByteArray &body,
      bool &tooLarge);

  void handleRequest(
      QTcpSocket *socket,
      const QByteArray &method,
      const QByteArray &path,
      const QByteArray &body);

  void handleSetState(QTcpSocket *socket, const QByteArray &body);

  static void sendResponse(
      QTcpSocket *socket,
      int status,
      const QByteArray &reason,
      const QByteArray &contentType,
      const QByteArray &body);

  QTcpServer m_server;
  unsigned short m_port;
  StateStore &m_store;
  QHash<QTcpSocket *, QByteArray> m_buffers;
};
