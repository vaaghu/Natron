#pragma once

#include "../server/HttpServer.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUdpSocket>
#include <QVariantMap>

class StateStore;

// Live data of an Ultra Score scoreboard console: the "Data Out" UDP packets
// it broadcasts are decoded into state store keys, so Text nodes bound to
// them show the score, clock, fouls, ... (and live NDI scenes re-render).
//
// Packets: FF FE <id high> 01 <system> <length 2> <body> FD FC (or the same
// with every byte inverted). Keys written (only when their value changes):
//
//   General data (01 01):
//     scoreboard.period            text    Q1..Q4, OT, 2OT, ...
//     scoreboard.periodNumber      number
//     scoreboard.clock             text    MM:SS, SS.t in the last minute
//     scoreboard.clockRunning      bool
//     scoreboard.shotClock         text    seconds, S.t in the last 5
//     scoreboard.timeout           bool    a timeout is being timed
//     scoreboard.timeoutSeconds    number
//     scoreboard.possession        text    home, away or empty
//     scoreboard.home.score        number  (and scoreboard.away.*)
//     scoreboard.home.fouls        number
//     scoreboard.home.timeouts     number
//   Player data (02 01 home, 03 01 away), per shirt number:
//     scoreboard.home.player.<no>.points   number
//     scoreboard.home.player.<no>.fouls    number
//   Players on court (06 01 home, 07 01 away):
//     scoreboard.home.player.<no>.onCourt  bool
//
// Also serves /scoreboard over HTTP (see handleHttpRequest): GET for the
// feed's status and values, POST to feed packets by hand as if they came
// over UDP.
class ScoreboardFeed : public QObject, public HttpRouteHandler
{
  Q_OBJECT

public:
  explicit ScoreboardFeed(StateStore *store, QObject *parent = nullptr);
  ~ScoreboardFeed();

  // Listens for the console's packets on every interface (broadcasts
  // included). Port 0 stops listening.
  void start(unsigned short port);

  bool isListening() const;
  unsigned short port() const;

  // Why listening failed (empty when listening).
  QString lastError() const;

  // Decodes one packet into key -> typed value (see KvValue.h). Returns an
  // empty map and sets *error if it is not an Ultra Score packet; *type is
  // "general", "players" or "onCourt".
  static QVariantMap decode(const QByteArray &packet, QString *type, QString *error);

  // Sets the keys whose value differs from the store's. Returns those keys.
  QStringList apply(const QVariantMap &values);

  // GET /scoreboard: {listening, port, error, packets, rejected, lastPacket,
  //                   lastSender, values}
  // POST /scoreboard: {"packet": "FF FE 01 01 ..."} or {"packets": [...]},
  //                   hex with or without spaces.
  bool handleHttpRequest(const QByteArray &method,
                         const QByteArray &path,
                         const QByteArray &body,
                         int *status,
                         QByteArray *response);

Q_SIGNALS:
  // Listening state or lastError() changed.
  void statusChanged();

private Q_SLOTS:
  void onReadyRead();

private:
  // Decodes and applies one packet. Returns false if it was rejected.
  bool receive(const QByteArray &packet, const QString &sender, QVariantMap *result);

  StateStore *m_store;
  QUdpSocket m_socket;
  unsigned short m_port;
  QString m_lastError;
  qint64 m_packets;
  qint64 m_rejected;
  QString m_lastRejection;
  QDateTime m_lastPacket;
  QString m_lastSender;
};
