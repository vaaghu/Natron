#pragma once

#include "LiveStats.h"

#include "../server/HttpServer.h"

#include <QByteArray>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class QNetworkReply;
class ScoreboardFeed;
class StateStore;

// FIBA LiveStats game data into the state store (keys: see LiveStats.h),
// from one of:
//   - the TV feed of the LiveStats computer in the venue: a TCP connection
//     (port 7677) on which it sends one JSON message per line. Reconnects by
//     itself, and asks for the messages missed meanwhile (or for everything
//     when LiveStats restarted and numbers its messages from 1 again).
//   - the online data.json of a match, polled every few seconds.
//
// The Ultra Score console (ScoreboardFeed) keeps the clock and the score
// while it sends packets: the game state keys are only written when it has
// been silent for 5 seconds. While LiveStats has players, the console no
// longer writes the player keys.
//
// Photos and logos are downloaded into imageDirectory() and set as image
// values.
//
// Also serves /livestats over HTTP (see handleHttpRequest).
class LiveStatsFeed : public QObject, public HttpRouteHandler
{
  Q_OBJECT

public:
  // console may be null.
  LiveStatsFeed(StateStore *store, ScoreboardFeed *console, QObject *parent = nullptr);
  ~LiveStatsFeed();

  // TV feed: connects to host:port (and keeps reconnecting).
  void connectTo(const QString &host, unsigned short port);

  // Online: polls a match's data.json every intervalMs. match: the match id
  // (with baseUrl()) or the whole URL of a data.json.
  void pollMatch(const QString &match, int intervalMs);

  // Stops reading (the keys stay as they are).
  void stop();

  // Forgets the game: removes the keys this feed wrote.
  void reset();

  // "tv", "online" or empty when stopped.
  QString mode() const;
  bool isConnected() const;
  QString lastError() const;

  // Where the online data is: <baseUrl>/data/<match id>/data.json.
  void setBaseUrl(const QString &url);
  QString baseUrl() const;

  void setImageDirectory(const QString &path);
  QString imageDirectory() const;

  // Whether the console sent a packet in the last 5 seconds.
  bool consoleActive() const;

  // Adds one TV feed message / a whole data.json, as if it was received.
  // Returns false if it was rejected. result (optional): type, changed
  // keys or the error.
  bool receiveMessage(const QVariantMap &message, const QString &sender, QVariantMap *result);
  bool receiveDataJson(const QVariantMap &root, const QString &sender, QVariantMap *result);

  // GET /livestats: {mode, connected, host, port, url, interval, messages,
  //                  rejected, lastRejection, lastError, lastMessage,
  //                  lastMessageType, lastSender, consoleActive, values}
  // POST /livestats:
  //   {"host": "192.168.1.30", "port": 7677}    TV feed
  //   {"matchId": "2345678", "interval": 2}     online (or "url")
  //   {"stop": true}, {"reset": true}
  //   {"message": {...}} / {"messages": [...]}  TV feed messages by hand
  //   {"dataJson": {...}}                       a data.json by hand
  bool handleHttpRequest(const QByteArray &method,
                         const QByteArray &path,
                         const QByteArray &body,
                         int *status,
                         QByteArray *response);

Q_SIGNALS:
  // Mode, connection or lastError() changed.
  void statusChanged();

private Q_SLOTS:
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onSocketError();
  void onReconnect();
  void onWatchdog();
  void onPoll();
  void onPollFinished();
  void onImageFinished();
  void onClockTick();

private:
  // Writes the model's values; removes the keys it no longer has.
  QStringList publish();
  void requestImages();
  QString imageFile(const QString &url) const;
  QVariantMap status() const;

  StateStore *m_store;
  ScoreboardFeed *m_console;
  LiveStatsModel m_model;
  QSet<QString> m_written; // keys this feed set

  QString m_mode;
  QString m_lastError;

  // TV feed.
  QTcpSocket m_socket;
  QString m_host;
  unsigned short m_port;
  QByteArray m_buffer;
  bool m_connected;
  bool m_everConnected;
  // fromMessageId asked for on this connection, until the first message
  // with an id: a lower one means LiveStats restarted. 0: none.
  qlonglong m_requestedFrom;
  QTimer m_reconnectTimer;
  QTimer m_watchdog;
  QDateTime m_lastReceived;

  // Online.
  QNetworkAccessManager m_network;
  QString m_baseUrl;
  QUrl m_url;
  QTimer m_pollTimer;
  QNetworkReply *m_pollReply;
  QByteArray m_lastBody;

  // Images: cache key (URL without query) -> its download.
  QString m_imageDirectory;
  QSet<QString> m_imagesRequested;

  QTimer m_clockTimer;

  qint64 m_messages;
  qint64 m_rejected;
  QString m_lastRejection;
  QDateTime m_lastMessage;
  QString m_lastMessageType;
  QString m_lastSender;
};
