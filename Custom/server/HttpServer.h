#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class StateStore;

// Extra routes served by other parts of the application (e.g. /ndi).
class HttpRouteHandler
{
public:
  virtual ~HttpRouteHandler() {}

  // Returns false if the path is not handled. Otherwise sets the status
  // code and the JSON response body.
  virtual bool handleHttpRequest(const QByteArray &method,
                                 const QByteArray &path,
                                 const QByteArray &body,
                                 int *status,
                                 QByteArray *response) = 0;
};

class HttpServer : public QObject
{
  Q_OBJECT

public:
  explicit HttpServer(
      unsigned short port,
      StateStore &store,
      QObject *parent = nullptr);
  ~HttpServer();

  // Starts listening on localhost. If the port is busy (e.g. another Natron
  // is running), keeps retrying every few seconds until it is free.
  void start();

  // Not owned; must outlive the server or be removed.
  void addRouteHandler(HttpRouteHandler *handler);
  void removeRouteHandler(HttpRouteHandler *handler);

  bool isListening() const;
  unsigned short port() const;

  // Why the last attempt to listen failed (empty when listening).
  QString lastError() const;

Q_SIGNALS:
  // Listening state or lastError() changed.
  void statusChanged();

private Q_SLOTS:
  void tryListen();
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
  void handleGetState(QTcpSocket *socket);

  static void sendResponse(
      QTcpSocket *socket,
      int status,
      const QByteArray &reason,
      const QByteArray &contentType,
      const QByteArray &body);

  QTcpServer m_server;
  QTimer m_retryTimer;
  QString m_lastError;
  unsigned short m_port;
  StateStore &m_store;
  QHash<QTcpSocket *, QByteArray> m_buffers;
  QList<HttpRouteHandler *> m_handlers;
};
