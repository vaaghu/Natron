#include "ScoreboardFeed.h"

#include "../state/Json.h"
#include "../state/KvValue.h"
#include "../state/StateStore.h"

#include <QHostAddress>
#include <QVariantList>
#include <QtDebug>

#include <cctype>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

const char kPrefix[] = "scoreboard.";

// A byte of the packet as an unsigned number.
int at(const QByteArray &packet, int i)
{
  return static_cast<unsigned char>(packet.at(i));
}

QString periodText(int period)
{
  if (period <= 0)
  {
    return QString();
  }
  if (period <= 4)
  {
    return s("Q") + QString::number(period);
  }
  if (period == 5)
  {
    return s("OT");
  }
  return QString::number(period - 4) + s("OT");
}

QString twoDigits(int n)
{
  return QString::number(n).rightJustified(2, QLatin1Char('0'));
}

// Shirt number of a player entry: 3 ASCII bytes, NUL padded. Empty if unused
// or not a plain number / name.
QString shirtNumber(const QByteArray &packet, int offset)
{
  QByteArray raw = packet.mid(offset, 3);
  const int nul = raw.indexOf('\0');
  if (nul >= 0)
  {
    raw.truncate(nul);
  }
  const QString number = QString::fromLatin1(raw.constData(), raw.size()).trimmed();
  for (int i = 0; i < number.size(); ++i)
  {
    if (!number.at(i).isLetterOrNumber() || number.at(i).unicode() > 127)
    {
      return QString();
    }
  }
  return number;
}

// End of the player entries: before the FD FC foot when there is one.
int bodyEnd(const QByteArray &packet)
{
  const int n = packet.size();
  if (n >= 2 && at(packet, n - 2) == 0xFD && at(packet, n - 1) == 0xFC)
  {
    return n - 2;
  }
  return n;
}

void decodeGeneral(const QByteArray &p, QVariantMap *values)
{
  const QString home = s(kPrefix) + s("home.");
  const QString away = s(kPrefix) + s("away.");

  const int period = at(p, 7);
  values->insert(s(kPrefix) + s("period"), Kv::makeText(periodText(period)));
  values->insert(s(kPrefix) + s("periodNumber"), Kv::makeNumber(period));
  values->insert(s(kPrefix) + s("clockRunning"), Kv::makeBool((at(p, 8) & 0x01) != 0));

  // Match timer: minutes, seconds, tenths (all FF: not shown).
  const int minutes = at(p, 9);
  const int seconds = at(p, 10);
  const int tenths = at(p, 11);
  QString clock;
  if (minutes != 0xFF || seconds != 0xFF)
  {
    if (minutes > 0)
    {
      clock = twoDigits(minutes) + s(":") + twoDigits(seconds);
    }
    else
    {
      clock = twoDigits(seconds) + s(".") + QString::number(tenths == 0xFF ? 0 : tenths);
    }
  }
  values->insert(s(kPrefix) + s("clock"), Kv::makeText(clock));

  // Shot clock: seconds, tenths (FF: off). Tenths only in the last 5 seconds.
  const int shotSeconds = at(p, 12);
  const int shotTenths = at(p, 13) == 0xFF ? 0 : at(p, 13);
  QString shotClock;
  if (shotSeconds != 0xFF)
  {
    shotClock = (shotSeconds * 10 + shotTenths > 50)
                    ? QString::number(shotSeconds)
                    : QString::number(shotSeconds) + s(".") + QString::number(shotTenths);
  }
  values->insert(s(kPrefix) + s("shotClock"), Kv::makeText(shotClock));

  // Timeout timer in seconds (FF: no timeout).
  const int timeout = at(p, 14);
  const bool timeoutActive = timeout != 0xFF && timeout > 0;
  values->insert(s(kPrefix) + s("timeout"), Kv::makeBool(timeoutActive));
  values->insert(s(kPrefix) + s("timeoutSeconds"), Kv::makeNumber(timeoutActive ? timeout : 0));

  // Team counts; FF means the console does not send that one.
  const char *counts[] = {"score", "fouls", "timeouts"};
  for (int i = 0; i < 3; ++i)
  {
    const int homeValue = at(p, 17 + 2 * i);
    const int awayValue = at(p, 18 + 2 * i);
    if (homeValue != 0xFF)
    {
      values->insert(home + s(counts[i]), Kv::makeNumber(homeValue));
    }
    if (awayValue != 0xFF)
    {
      values->insert(away + s(counts[i]), Kv::makeNumber(awayValue));
    }
  }

  QString possession;
  if (at(p, 23) == 1)
  {
    possession = s("home");
  }
  else if (at(p, 24) == 1)
  {
    possession = s("away");
  }
  values->insert(s(kPrefix) + s("possession"), Kv::makeText(possession));
}

// Player data: 20 entries of number (3) + points + fouls.
void decodePlayers(const QByteArray &p, const QString &team, QVariantMap *values)
{
  const int end = bodyEnd(p);
  for (int offset = 7; offset + 5 <= end; offset += 5)
  {
    const QString number = shirtNumber(p, offset);
    if (number.isEmpty())
    {
      continue;
    }
    const QString player = s(kPrefix) + team + s(".player.") + number + s(".");
    values->insert(player + s("points"), Kv::makeNumber(at(p, offset + 3)));
    values->insert(player + s("fouls"), Kv::makeNumber(at(p, offset + 4)));
  }
}

// Players on court: 20 entries of number (3) + state (1: on court).
void decodeOnCourt(const QByteArray &p, const QString &team, QVariantMap *values)
{
  const int end = bodyEnd(p);
  for (int offset = 7; offset + 4 <= end; offset += 4)
  {
    const QString number = shirtNumber(p, offset);
    if (number.isEmpty())
    {
      continue;
    }
    values->insert(s(kPrefix) + team + s(".player.") + number + s(".onCourt"),
                   Kv::makeBool(at(p, offset + 3) == 1));
  }
}

// "FF FE 01 01", "fffe0101", "FF-FE-01-01" -> bytes. False if not hex.
bool parseHex(const QString &text, QByteArray *bytes)
{
  QByteArray hex;
  for (int i = 0; i < text.size(); ++i)
  {
    const QChar c = text.at(i);
    if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char(':') || c == QLatin1Char(','))
    {
      continue;
    }
    if (!std::isxdigit(static_cast<unsigned char>(c.toLatin1())))
    {
      return false;
    }
    hex += c.toLatin1();
  }
  if (hex.isEmpty() || hex.size() % 2 != 0)
  {
    return false;
  }
  *bytes = QByteArray::fromHex(hex);
  return true;
}
} // namespace

ScoreboardFeed::ScoreboardFeed(StateStore *store, QObject *parent)
    : QObject(parent),
      m_store(store),
      m_port(0),
      m_packets(0),
      m_rejected(0)
{
  connect(&m_socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
}

ScoreboardFeed::~ScoreboardFeed()
{
}

void ScoreboardFeed::start(unsigned short port)
{
  m_socket.close();
  m_port = port;
  m_lastError.clear();

  if (port != 0)
  {
    const QHostAddress any(QHostAddress::AnyIPv4);
    // Shared, so another program can listen to the console as well.
    if (!m_socket.bind(any, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
      m_lastError = m_socket.errorString();
      // qWarning rather than qDebug: release builds compile qDebug out.
      qWarning() << "Scoreboard feed: cannot listen on UDP port" << port << ":" << m_lastError;
    }
  }
  Q_EMIT statusChanged();
}

bool ScoreboardFeed::isListening() const
{
  return m_socket.state() == QAbstractSocket::BoundState;
}

unsigned short ScoreboardFeed::port() const
{
  return m_port;
}

QString ScoreboardFeed::lastError() const
{
  return m_lastError;
}

QVariantMap ScoreboardFeed::decode(const QByteArray &input, QString *type, QString *error)
{
  QVariantMap values;
  QByteArray p = input;

  // Some consoles send every byte inverted.
  if (p.size() >= 2 && at(p, 0) == 0x00 && at(p, 1) == 0x01)
  {
    for (int i = 0; i < p.size(); ++i)
    {
      p[i] = static_cast<char>(~p.at(i));
    }
  }

  if (p.size() < 7 || at(p, 0) != 0xFF || at(p, 1) != 0xFE)
  {
    *error = s("not an Ultra Score packet (no FF FE header)");
    return values;
  }
  if (at(p, 3) != 0x01)
  {
    *error = s("unknown packet id");
    return values;
  }

  const int id = at(p, 2);
  const QString team = (id % 2 == 0) ? s("home") : s("away");

  if (id == 0x01)
  {
    if (p.size() < 25)
    {
      *error = s("general data packet too short (needs 25 bytes)");
      return values;
    }
    *type = s("general");
    decodeGeneral(p, &values);
  }
  else if (id == 0x02 || id == 0x03)
  {
    *type = s("players");
    decodePlayers(p, team, &values);
  }
  else if (id == 0x06 || id == 0x07)
  {
    *type = s("onCourt");
    decodeOnCourt(p, team, &values);
  }
  else
  {
    // 04 / 05: penalties, not used in basketball.
    *error = s("unsupported packet id");
  }
  return values;
}

QStringList ScoreboardFeed::apply(const QVariantMap &values)
{
  QStringList changed;
  for (QVariantMap::const_iterator it = values.constBegin(); it != values.constEnd(); ++it)
  {
    // Compared as shown: saved numbers come back as integers, not doubles.
    const QVariant current = m_store->get(it.key());
    if (current.isValid() && Kv::typeOf(current) == Kv::typeOf(it.value()) &&
        Kv::textValue(current) == Kv::textValue(it.value()))
    {
      continue;
    }
    m_store->set(it.key(), it.value());
    changed << it.key();
  }
  return changed;
}

bool ScoreboardFeed::receive(const QByteArray &packet, const QString &sender, QVariantMap *result)
{
  QString type;
  QString error;
  const QVariantMap values = decode(packet, &type, &error);

  if (!error.isEmpty())
  {
    ++m_rejected;
    m_lastRejection = error;
    if (result)
    {
      result->insert(s("error"), error);
    }
    return false;
  }

  ++m_packets;
  m_lastPacket = QDateTime::currentDateTime();
  m_lastSender = sender;

  const QStringList changed = apply(values);
  if (result)
  {
    result->insert(s("type"), type);
    result->insert(s("changed"), changed);
    result->insert(s("values"), values);
  }
  return true;
}

void ScoreboardFeed::onReadyRead()
{
  while (m_socket.hasPendingDatagrams())
  {
    QByteArray datagram;
    datagram.resize(static_cast<int>(m_socket.pendingDatagramSize()));
    QHostAddress address;
    quint16 port = 0;
    if (m_socket.readDatagram(datagram.data(), datagram.size(), &address, &port) < 0)
    {
      continue;
    }
    receive(datagram, address.toString() + s(":") + QString::number(port), nullptr);
  }
}

bool ScoreboardFeed::handleHttpRequest(const QByteArray &method, const QByteArray &path, const QByteArray &body,
                                       int *status, QByteArray *response)
{
  if (path != "/scoreboard")
  {
    return false;
  }

  if (method == "GET")
  {
    QVariantMap values;
    const QStringList keys = m_store->keys();
    for (int i = 0; i < keys.size(); ++i)
    {
      if (keys.at(i).startsWith(s(kPrefix)))
      {
        values.insert(keys.at(i), m_store->get(keys.at(i)));
      }
    }

    QVariantMap result;
    result.insert(s("listening"), isListening());
    result.insert(s("port"), static_cast<int>(m_port));
    result.insert(s("error"), m_lastError);
    result.insert(s("packets"), m_packets);
    result.insert(s("rejected"), m_rejected);
    result.insert(s("lastRejection"), m_lastRejection);
    result.insert(s("lastPacket"), m_lastPacket.isValid() ? m_lastPacket.toString(Qt::ISODate) : QString());
    result.insert(s("lastSender"), m_lastSender);
    result.insert(s("values"), values);
    *status = 200;
    *response = Json::serialize(result);
    return true;
  }

  if (method != "POST")
  {
    *status = 405;
    *response = "{\"error\":\"use GET or POST\"}";
    return true;
  }

  bool ok = false;
  const QVariantMap request = Json::parse(body, &ok).toMap();
  QVariantList packets = request.value(s("packets")).toList();
  if (request.contains(s("packet")))
  {
    packets.prepend(request.value(s("packet")));
  }
  if (!ok || packets.isEmpty())
  {
    *status = 400;
    *response = "{\"error\":\"body: {\\\"packet\\\": \\\"FF FE 01 01 ...\\\"} or {\\\"packets\\\": [...]}\"}";
    return true;
  }

  // Check every packet first: a bad one rejects the whole request.
  QList<QByteArray> bytes;
  for (int i = 0; i < packets.size(); ++i)
  {
    QByteArray packet;
    if (!parseHex(packets.at(i).toString(), &packet))
    {
      QVariantMap result;
      result.insert(s("error"), s("packet %1 is not hex").arg(i));
      *status = 400;
      *response = Json::serialize(result);
      return true;
    }
    bytes << packet;
  }

  QVariantList results;
  bool allOk = true;
  for (int i = 0; i < bytes.size(); ++i)
  {
    QVariantMap result;
    allOk = receive(bytes.at(i), s("http"), &result) && allOk;
    results << result;
  }

  *status = allOk ? 200 : 400;
  if (results.size() == 1)
  {
    *response = Json::serialize(results.first());
  }
  else
  {
    QVariantMap result;
    result.insert(s("results"), results);
    *response = Json::serialize(result);
  }
  return true;
}
