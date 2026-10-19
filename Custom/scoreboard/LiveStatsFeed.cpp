#include "LiveStatsFeed.h"

#include "ScoreboardCommon.h"
#include "ScoreboardFeed.h"

#include "../state/Json.h"
#include "../state/KvValue.h"
#include "../state/StateStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QVariantList>
#include <QtDebug>

#include <algorithm>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

// The TV feed's message types (all but officials, summary and competition
// statistics, which nothing shows).
const char kTypes[] = "se,ac,mi,te,st,box,pbp,sd,sc";

const int kReconnectMs = 3000;
const int kConnectTimeoutMs = 10000;
// The feed sends a ping every 10 seconds when there is nothing else.
const int kSilenceMs = 30000;
const int kConsoleQuietMs = 5000;
const int kClockTickMs = 100;
// A message is never this large: the stream is not LiveStats.
const int kMaxMessageBytes = 64 * 1024 * 1024;

qint64 now()
{
  return QDateTime::currentMSecsSinceEpoch();
}
} // namespace

LiveStatsFeed::LiveStatsFeed(StateStore *store, ScoreboardFeed *console, QObject *parent)
    : QObject(parent),
      m_store(store),
      m_console(console),
      m_port(0),
      m_connected(false),
      m_everConnected(false),
      m_requestedFrom(0),
      m_baseUrl(s("https://fibalivestats.dcd.shared.geniussports.com")),
      m_pollReply(nullptr),
      m_messages(0),
      m_rejected(0)
{
  connect(&m_socket, SIGNAL(connected()), this, SLOT(onConnected()));
  connect(&m_socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
  connect(&m_socket, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
  connect(&m_socket, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this, SLOT(onSocketError()));

  m_reconnectTimer.setSingleShot(true);
  connect(&m_reconnectTimer, SIGNAL(timeout()), this, SLOT(onReconnect()));
  m_watchdog.setInterval(1000);
  connect(&m_watchdog, SIGNAL(timeout()), this, SLOT(onWatchdog()));
  connect(&m_pollTimer, SIGNAL(timeout()), this, SLOT(onPoll()));
  m_clockTimer.setInterval(kClockTickMs);
  connect(&m_clockTimer, SIGNAL(timeout()), this, SLOT(onClockTick()));
}

LiveStatsFeed::~LiveStatsFeed()
{
  // Not stop(): the console may be gone already.
  m_mode.clear();
  m_socket.abort();
  if (m_pollReply)
  {
    QNetworkReply *reply = m_pollReply;
    m_pollReply = nullptr;
    reply->abort();
  }
}

void LiveStatsFeed::connectTo(const QString &host, unsigned short port)
{
  stop();
  m_mode = s("tv");
  m_host = host;
  m_port = port;
  m_everConnected = false;
  m_watchdog.start();
  onReconnect();
  Q_EMIT statusChanged();
}

void LiveStatsFeed::pollMatch(const QString &match, int intervalMs)
{
  stop();
  m_mode = s("online");
  const QString trimmed = match.trimmed();
  m_url = trimmed.startsWith(s("http://")) || trimmed.startsWith(s("https://"))
              ? QUrl(trimmed)
              : QUrl(m_baseUrl + s("/data/") + trimmed + s("/data.json"));
  m_lastBody.clear();
  m_pollTimer.start(std::max(500, intervalMs));
  onPoll();
  Q_EMIT statusChanged();
}

void LiveStatsFeed::stop()
{
  m_mode.clear();
  m_reconnectTimer.stop();
  m_watchdog.stop();
  m_pollTimer.stop();
  m_clockTimer.stop();
  m_socket.abort();
  m_buffer.clear();
  if (m_pollReply)
  {
    QNetworkReply *reply = m_pollReply;
    m_pollReply = nullptr;
    reply->abort();
  }
  m_connected = false;
  // The console's player data is all there is now.
  if (m_console)
  {
    m_console->setSkipPlayerKeys(false);
  }
  Q_EMIT statusChanged();
}

void LiveStatsFeed::reset()
{
  m_model.clear();
  const bool console = consoleActive();
  for (QSet<QString>::const_iterator it = m_written.constBegin(); it != m_written.constEnd(); ++it)
  {
    if (!(console && LiveStatsModel::isGameStateKey(*it)) && m_store->has(*it))
    {
      m_store->remove(*it);
    }
  }
  m_written.clear();
  m_lastBody.clear();
  m_imagesRequested.clear();
  m_clockTimer.stop();
  if (m_console)
  {
    m_console->setSkipPlayerKeys(false);
  }
}

QString LiveStatsFeed::mode() const
{
  return m_mode;
}

bool LiveStatsFeed::isConnected() const
{
  return m_connected;
}

QString LiveStatsFeed::lastError() const
{
  return m_lastError;
}

void LiveStatsFeed::setBaseUrl(const QString &url)
{
  m_baseUrl = url;
  while (m_baseUrl.endsWith(QLatin1Char('/')))
  {
    m_baseUrl.chop(1);
  }
}

QString LiveStatsFeed::baseUrl() const
{
  return m_baseUrl;
}

void LiveStatsFeed::setImageDirectory(const QString &path)
{
  m_imageDirectory = path;
}

QString LiveStatsFeed::imageDirectory() const
{
  return m_imageDirectory;
}

bool LiveStatsFeed::consoleActive() const
{
  if (!m_console)
  {
    return false;
  }
  const QDateTime last = m_console->lastPacketTime();
  return last.isValid() && last.msecsTo(QDateTime::currentDateTime()) < kConsoleQuietMs;
}

bool LiveStatsFeed::receiveMessage(const QVariantMap &message, const QString &sender, QVariantMap *result)
{
  QString type;
  QString error;
  if (!m_model.applyTvMessage(message, now(), &type, &error))
  {
    ++m_rejected;
    m_lastRejection = error;
    if (result)
    {
      result->insert(s("error"), error);
    }
    return false;
  }

  ++m_messages;
  m_lastMessage = QDateTime::currentDateTime();
  m_lastMessageType = type;
  m_lastSender = sender;

  // Pings change nothing.
  const QStringList changed = type == s("ping") ? QStringList() : publish();
  if (result)
  {
    result->insert(s("type"), type);
    result->insert(s("changed"), changed);
  }
  return true;
}

bool LiveStatsFeed::receiveDataJson(const QVariantMap &root, const QString &sender, QVariantMap *result)
{
  QString error;
  if (!m_model.applyDataJson(root, now(), &error))
  {
    ++m_rejected;
    m_lastRejection = error;
    if (result)
    {
      result->insert(s("error"), error);
    }
    return false;
  }

  ++m_messages;
  m_lastMessage = QDateTime::currentDateTime();
  m_lastMessageType = s("dataJson");
  m_lastSender = sender;

  const QStringList changed = publish();
  if (result)
  {
    result->insert(s("type"), m_lastMessageType);
    result->insert(s("changed"), changed);
  }
  return true;
}

QStringList LiveStatsFeed::publish()
{
  // First: images already downloaded are in the values.
  requestImages();

  const QVariantMap values = m_model.values(now());
  const bool console = consoleActive();

  QVariantMap toWrite;
  QSet<QString> current;
  bool hasPlayers = false;
  for (QVariantMap::const_iterator it = values.constBegin(); it != values.constEnd(); ++it)
  {
    current.insert(it.key());
    hasPlayers = hasPlayers || it.key().contains(s(".player."));
    if (console && LiveStatsModel::isGameStateKey(it.key()))
    {
      continue;
    }
    toWrite.insert(it.key(), it.value());
  }

  QStringList changed = Scoreboard::applyChanged(m_store, toWrite);

  // Players who left the roster, a shorter table, ... The game state keys
  // stay: the console may write them.
  for (QSet<QString>::const_iterator it = m_written.constBegin(); it != m_written.constEnd(); ++it)
  {
    if (!current.contains(*it) && !LiveStatsModel::isGameStateKey(*it) && m_store->has(*it))
    {
      m_store->remove(*it);
      changed << *it;
    }
  }
  m_written = current;

  if (m_console)
  {
    m_console->setSkipPlayerKeys(hasPlayers);
  }

  if (m_model.clockRunning())
  {
    if (!m_clockTimer.isActive())
    {
      m_clockTimer.start();
    }
  }
  else
  {
    m_clockTimer.stop();
  }
  return changed;
}

void LiveStatsFeed::onClockTick()
{
  if (!m_model.clockRunning())
  {
    m_clockTimer.stop();
    return;
  }
  if (!consoleActive())
  {
    Scoreboard::applyChanged(m_store, m_model.clockValues(now()));
  }
}

// TV feed --------------------------------------------------------------------

void LiveStatsFeed::onReconnect()
{
  if (m_mode != s("tv") || m_socket.state() != QAbstractSocket::UnconnectedState)
  {
    return;
  }
  m_buffer.clear();
  m_lastReceived = QDateTime::currentDateTime(); // start of the attempt
  m_socket.connectToHost(m_host, m_port);
}

void LiveStatsFeed::onConnected()
{
  m_connected = true;
  m_lastError.clear();
  m_lastReceived = QDateTime::currentDateTime();

  // Within 5 seconds, or the feed closes the connection.
  QVariantMap parameters;
  parameters.insert(s("type"), s("parameters"));
  parameters.insert(s("types"), s(kTypes));
  // The whole play-by-play once, then the actions one by one.
  parameters.insert(s("playbyplayOnConnect"), 1);
  m_requestedFrom = 0;
  if (m_everConnected && m_model.lastMessageId() > 0)
  {
    // The messages missed while disconnected.
    m_requestedFrom = m_model.lastMessageId() + 1;
    parameters.insert(s("fromMessageId"), m_requestedFrom);
  }
  m_everConnected = true;
  m_socket.write(Json::serialize(parameters) + "\r\n");
  Q_EMIT statusChanged();
}

void LiveStatsFeed::onReadyRead()
{
  m_buffer += m_socket.readAll();
  m_lastReceived = QDateTime::currentDateTime();
  const QString sender = m_host + s(":") + QString::number(m_port);

  // One message per line (CRLF).
  int start = 0;
  for (int end = m_buffer.indexOf('\n', start); end >= 0; end = m_buffer.indexOf('\n', start))
  {
    const QByteArray line = m_buffer.mid(start, end - start).trimmed();
    start = end + 1;
    if (line.isEmpty())
    {
      continue;
    }
    bool ok = false;
    QString error;
    const QVariant message = Json::parse(line, &ok, &error);
    if (!ok || message.userType() != QMetaType::QVariantMap)
    {
      ++m_rejected;
      m_lastRejection = s("not a JSON object: ") + (ok ? s("not an object") : error);
      continue;
    }

    // The first numbered message after asking for the missed ones: lower
    // than asked, LiveStats restarted and numbers from 1 again, so it sent
    // nothing of what was missed. Reconnect and ask for everything.
    const qlonglong id = message.toMap().value(s("messageId")).toLongLong();
    if (m_requestedFrom > 0 && id > 0)
    {
      if (id < m_requestedFrom)
      {
        qWarning() << "LiveStats feed:" << m_host << m_port << ": restarted (message" << id << "after"
                   << m_requestedFrom - 1 << "), reading everything again";
        m_model.resetMessageId();
        m_everConnected = false;
        m_requestedFrom = 0;
        m_buffer.clear();
        m_socket.abort();
        onReconnect();
        return;
      }
      m_requestedFrom = 0;
    }
    receiveMessage(message.toMap(), sender, nullptr);
  }
  m_buffer.remove(0, start);

  if (m_buffer.size() > kMaxMessageBytes)
  {
    m_buffer.clear();
    m_lastError = s("message too large: not a LiveStats TV feed?");
    m_socket.abort();
  }
}

void LiveStatsFeed::onDisconnected()
{
  const bool wasConnected = m_connected;
  m_connected = false;
  if (m_mode == s("tv"))
  {
    m_reconnectTimer.start(kReconnectMs);
  }
  if (wasConnected)
  {
    Q_EMIT statusChanged();
  }
}

void LiveStatsFeed::onSocketError()
{
  if (m_mode != s("tv"))
  {
    return;
  }
  const QString error = m_socket.errorString();
  if (error != m_lastError)
  {
    m_lastError = error;
    // qWarning rather than qDebug: release builds compile qDebug out.
    qWarning() << "LiveStats feed:" << m_host << m_port << ":" << error;
    Q_EMIT statusChanged();
  }
  if (m_socket.state() == QAbstractSocket::UnconnectedState)
  {
    m_connected = false;
    m_reconnectTimer.start(kReconnectMs);
  }
}

void LiveStatsFeed::onWatchdog()
{
  if (m_mode != s("tv"))
  {
    return;
  }
  const qint64 quiet = m_lastReceived.msecsTo(QDateTime::currentDateTime());
  const QAbstractSocket::SocketState state = m_socket.state();
  if (state == QAbstractSocket::ConnectedState && quiet > kSilenceMs)
  {
    m_lastError = s("no data for 30 seconds");
  }
  else if ((state == QAbstractSocket::HostLookupState || state == QAbstractSocket::ConnectingState) &&
           quiet > kConnectTimeoutMs)
  {
    m_lastError = s("connection timed out");
  }
  else
  {
    return;
  }
  qWarning() << "LiveStats feed:" << m_host << m_port << ":" << m_lastError;
  m_socket.abort();
  onDisconnected();
  Q_EMIT statusChanged();
}

// Online ---------------------------------------------------------------------

void LiveStatsFeed::onPoll()
{
  if (m_mode != s("online") || m_pollReply)
  {
    return;
  }
  QNetworkRequest request(m_url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  m_pollReply = m_network.get(request);
  connect(m_pollReply, SIGNAL(finished()), this, SLOT(onPollFinished()));
}

void LiveStatsFeed::onPollFinished()
{
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  if (!reply)
  {
    return;
  }
  reply->deleteLater();
  if (reply != m_pollReply)
  {
    return; // aborted by stop()
  }
  m_pollReply = nullptr;

  if (reply->error() != QNetworkReply::NoError)
  {
    const QString error = reply->errorString();
    const bool changed = m_connected || error != m_lastError;
    m_connected = false;
    m_lastError = error;
    if (changed)
    {
      qWarning() << "LiveStats feed:" << m_url.toString() << ":" << error;
      Q_EMIT statusChanged();
    }
    return;
  }

  if (!m_connected || !m_lastError.isEmpty())
  {
    m_connected = true;
    m_lastError.clear();
    Q_EMIT statusChanged();
  }

  const QByteArray body = reply->readAll();
  if (body == m_lastBody)
  {
    return;
  }
  m_lastBody = body;

  bool ok = false;
  QString error;
  const QVariant root = Json::parse(body, &ok, &error);
  if (!ok || root.userType() != QMetaType::QVariantMap)
  {
    ++m_rejected;
    m_lastRejection = s("not a JSON object: ") + (ok ? s("not an object") : error);
    return;
  }
  receiveDataJson(root.toMap(), m_url.toString(), nullptr);
}

// Images ---------------------------------------------------------------------

QString LiveStatsFeed::imageFile(const QString &url) const
{
  // Without the query: photo URLs are signed, and change, for the same photo.
  QUrl key(url);
  key.setQuery(QString());
  key.setFragment(QString());
  const QByteArray hash = QCryptographicHash::hash(key.toString().toUtf8(), QCryptographicHash::Sha1).toHex();

  QString suffix = QFileInfo(key.path()).suffix().toLower();
  static const QStringList known = QStringList() << s("png") << s("jpg") << s("jpeg") << s("gif") << s("webp")
                                                 << s("bmp");
  if (!known.contains(suffix))
  {
    suffix = s("png");
  }
  return QDir(m_imageDirectory).absoluteFilePath(QString::fromLatin1(hash) + s(".") + suffix);
}

void LiveStatsFeed::requestImages()
{
  if (m_imageDirectory.isEmpty())
  {
    return;
  }
  const QStringList urls = m_model.imageUrls();
  for (int i = 0; i < urls.size(); ++i)
  {
    const QString &url = urls.at(i);
    if (m_imagesRequested.contains(url))
    {
      continue;
    }
    m_imagesRequested.insert(url);

    const QString path = imageFile(url);
    if (QFile::exists(path))
    {
      m_model.setImage(url, Kv::makeImage(path));
      continue;
    }

    QNetworkRequest request(QUrl(url.startsWith(s("//")) ? s("https:") + url : url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    reply->setProperty("liveStatsUrl", url);
    connect(reply, SIGNAL(finished()), this, SLOT(onImageFinished()));
  }
}

void LiveStatsFeed::onImageFinished()
{
  QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
  if (!reply)
  {
    return;
  }
  reply->deleteLater();
  const QString url = reply->property("liveStatsUrl").toString();
  // Not retried (until reset): a broken URL would be fetched with every
  // message.
  if (reply->error() != QNetworkReply::NoError)
  {
    qWarning() << "LiveStats feed: cannot download" << url << ":" << reply->errorString();
    return;
  }
  const QByteArray data = reply->readAll();
  if (data.isEmpty())
  {
    return;
  }

  const QString path = imageFile(url);
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
  {
    qWarning() << "LiveStats feed: cannot write" << path << ":" << file.errorString();
    return;
  }
  m_model.setImage(url, Kv::makeImage(path));
  publish();
}

// HTTP -----------------------------------------------------------------------

QVariantMap LiveStatsFeed::status() const
{
  QVariantMap result;
  result.insert(s("mode"), m_mode);
  result.insert(s("connected"), m_connected);
  result.insert(s("host"), m_host);
  result.insert(s("port"), static_cast<int>(m_port));
  result.insert(s("url"), m_url.toString());
  result.insert(s("interval"), m_pollTimer.interval() / 1000.0);
  result.insert(s("error"), m_lastError);
  result.insert(s("messages"), m_messages);
  result.insert(s("rejected"), m_rejected);
  result.insert(s("lastRejection"), m_lastRejection);
  result.insert(s("lastMessage"), m_lastMessage.isValid() ? m_lastMessage.toString(Qt::ISODate) : QString());
  result.insert(s("lastMessageType"), m_lastMessageType);
  result.insert(s("lastMessageId"), m_model.lastMessageId());
  result.insert(s("lastSender"), m_lastSender);
  result.insert(s("consoleActive"), consoleActive());
  result.insert(s("imageDirectory"), m_imageDirectory);
  return result;
}

bool LiveStatsFeed::handleHttpRequest(const QByteArray &method, const QByteArray &path, const QByteArray &body,
                                      int *status, QByteArray *response)
{
  if (path != "/livestats")
  {
    return false;
  }

  if (method == "GET")
  {
    QVariantMap values;
    for (QSet<QString>::const_iterator it = m_written.constBegin(); it != m_written.constEnd(); ++it)
    {
      if (m_store->has(*it))
      {
        values.insert(*it, m_store->get(*it));
      }
    }
    QVariantMap result = this->status();
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
  if (!ok)
  {
    *status = 400;
    *response = "{\"error\":\"body: a JSON object (see GET / for the fields)\"}";
    return true;
  }

  // Settings.
  if (request.value(s("stop")).toBool() || request.value(s("reset")).toBool() || request.contains(s("host")) ||
      request.contains(s("matchId")) || request.contains(s("url")))
  {
    if (request.value(s("stop")).toBool())
    {
      stop();
    }
    if (request.value(s("reset")).toBool())
    {
      reset();
    }
    if (request.contains(s("host")))
    {
      const int port = request.value(s("port"), 7677).toInt();
      if (port <= 0 || port > 65535 || request.value(s("host")).toString().trimmed().isEmpty())
      {
        *status = 400;
        *response = "{\"error\":\"host: a name or address, port: 1-65535\"}";
        return true;
      }
      connectTo(request.value(s("host")).toString().trimmed(), static_cast<unsigned short>(port));
    }
    else if (request.contains(s("matchId")) || request.contains(s("url")))
    {
      const QString match = request.contains(s("url")) ? request.value(s("url")).toString()
                                                        : request.value(s("matchId")).toString();
      const double interval = request.value(s("interval"), 2).toDouble();
      if (match.trimmed().isEmpty() || interval <= 0)
      {
        *status = 400;
        *response = "{\"error\":\"matchId or url, interval: seconds\"}";
        return true;
      }
      pollMatch(match, static_cast<int>(interval * 1000));
    }
    *status = 200;
    *response = Json::serialize(this->status());
    return true;
  }

  // Data by hand.
  if (request.contains(s("dataJson")))
  {
    QVariantMap result;
    const bool accepted = receiveDataJson(request.value(s("dataJson")).toMap(), s("http"), &result);
    *status = accepted ? 200 : 400;
    *response = Json::serialize(result);
    return true;
  }

  QVariantList messages = request.value(s("messages")).toList();
  if (request.contains(s("message")))
  {
    messages.prepend(request.value(s("message")));
  }
  if (messages.isEmpty())
  {
    *status = 400;
    *response = "{\"error\":\"body: {host, port}, {matchId, interval}, {stop}, {reset}, {message}, "
                "{messages: [...]} or {dataJson}\"}";
    return true;
  }
  // Check every message first: a bad one rejects the whole request.
  for (int i = 0; i < messages.size(); ++i)
  {
    if (messages.at(i).userType() != QMetaType::QVariantMap)
    {
      QVariantMap result;
      result.insert(s("error"), s("message %1 is not an object").arg(i));
      *status = 400;
      *response = Json::serialize(result);
      return true;
    }
  }

  QVariantList results;
  bool allOk = true;
  for (int i = 0; i < messages.size(); ++i)
  {
    QVariantMap result;
    allOk = receiveMessage(messages.at(i).toMap(), s("http"), &result) && allOk;
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
