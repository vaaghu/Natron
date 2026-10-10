#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
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
  // Before start(). With a token, every request (except GET / and
  // GET /health) needs "Authorization: Bearer <token>", an
  // "X-Natron-Token: <token>" header or "?token=<token>". Listening on an
  // address other than this machine (default: localhost) needs a token.
  void setToken(const QString &token);
  void setListenAddress(const QHostAddress &address);

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
  void onStoreValueChanged(const QString &key);
  void onStoreKeysChanged();
  void onKeepAlive();

private:
  // Returns true once a full request (headers + body) is in buffer.
  static bool parseRequest(
      const QByteArray &buffer,
      QByteArray &method,
      QByteArray &path,
      QHash<QByteArray, QByteArray> &headers, // lower-case names
      QByteArray &body,
      bool &tooLarge);

  void handleRequest(
      QTcpSocket *socket,
      const QByteArray &method,
      const QByteArray &path,
      const QHash<QByteArray, QByteArray> &headers,
      const QByteArray &body);

  bool authorized(const QByteArray &query, const QHash<QByteArray, QByteArray> &headers) const;

  // GET /events: server-sent events stream of the store changes.
  void openEventStream(QTcpSocket *socket);
  void sendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &data);
  void broadcastEvent(const QByteArray &event, const QByteArray &data);
  QByteArray stateJson() const;

  // GET /: the API reference.
  static QByteArray apiReference();

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
  QString m_token;
  QHostAddress m_address;
  QList<QTcpSocket *> m_eventClients;
  QTimer m_keepAliveTimer; // comment line to /events clients, so proxies keep them open
};
