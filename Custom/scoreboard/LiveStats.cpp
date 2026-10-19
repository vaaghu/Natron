#include "LiveStats.h"

#include "ScoreboardCommon.h"

#include "../state/KvValue.h"

#include <algorithm>
#include <cmath>

namespace
{
QString s(const char *text)
{
  return QString::fromUtf8(text);
}

bool isSet(const QVariant &value)
{
  return value.isValid() && !value.isNull() && !(value.userType() == QMetaType::QString && value.toString().isEmpty());
}

double num(const QVariant &value, double defaultValue = 0)
{
  if (!isSet(value))
  {
    return defaultValue;
  }
  bool ok = false;
  const double n = value.toDouble(&ok);
  return ok ? n : defaultValue;
}

QString str(const QVariant &value)
{
  return isSet(value) ? value.toString().trimmed() : QString();
}

bool flag(const QVariant &value)
{
  return num(value) != 0;
}

// An image URL in a string, or anywhere in a map / list (LiveStats gives
// photos and logos as {"url": ...}, or per size: {"S1": {"url": ...}}).
QString findUrl(const QVariant &value)
{
  if (value.userType() == QMetaType::QString)
  {
    const QString text = value.toString().trimmed();
    return text.startsWith(s("http://")) || text.startsWith(s("https://")) || text.startsWith(s("//")) ? text
                                                                                                       : QString();
  }
  if (value.userType() == QMetaType::QVariantMap)
  {
    const QVariantMap map = value.toMap();
    const QString url = findUrl(map.value(s("url")));
    if (!url.isEmpty())
    {
      return url;
    }
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
    {
      const QString found = findUrl(it.value());
      if (!found.isEmpty())
      {
        return found;
      }
    }
  }
  if (value.userType() == QMetaType::QVariantList)
  {
    const QVariantList list = value.toList();
    for (int i = 0; i < list.size(); ++i)
    {
      const QString found = findUrl(list.at(i));
      if (!found.isEmpty())
      {
        return found;
      }
    }
  }
  return QString();
}

QString fullName(const QString &first, const QString &family)
{
  if (first.isEmpty())
  {
    return family;
  }
  if (family.isEmpty())
  {
    return first;
  }
  return first + s(" ") + family;
}

// "MM:SS:CC" (TV feed), "MM:SS", "MM:SS.t", "SS.t" -> centiseconds; -1 if
// not a clock.
qint64 parseClock(const QVariant &value)
{
  const QString text = str(value);
  if (text.isEmpty())
  {
    return -1;
  }
  const QStringList parts = text.split(QLatin1Char(':'));
  bool ok = true;
  double minutes = 0;
  double seconds = 0;
  double centiseconds = 0;
  if (parts.size() == 3)
  {
    bool ok1 = false, ok2 = false, ok3 = false;
    minutes = parts.at(0).toDouble(&ok1);
    seconds = parts.at(1).toDouble(&ok2);
    centiseconds = parts.at(2).toDouble(&ok3);
    ok = ok1 && ok2 && ok3;
  }
  else if (parts.size() == 2)
  {
    bool ok1 = false, ok2 = false;
    minutes = parts.at(0).toDouble(&ok1);
    seconds = parts.at(1).toDouble(&ok2);
    ok = ok1 && ok2;
  }
  else
  {
    seconds = parts.at(0).toDouble(&ok);
  }
  if (!ok || minutes < 0 || seconds < 0 || centiseconds < 0)
  {
    return -1;
  }
  return static_cast<qint64>(std::floor((minutes * 60 + seconds) * 100 + centiseconds + 0.5));
}

QString clockText(qint64 cs)
{
  const int minutes = static_cast<int>(cs / 6000);
  const int seconds = static_cast<int>((cs / 100) % 60);
  const int tenths = static_cast<int>((cs / 10) % 10);
  return Scoreboard::clockText(minutes, seconds, tenths);
}

QString shotClockText(qint64 cs)
{
  return Scoreboard::shotClockText(static_cast<int>(cs / 100), static_cast<int>((cs / 10) % 10));
}

// Minutes played: decimal minutes (TV feed) or "MM:SS" (data.json, which
// can send 199:60) -> "MM:SS".
QString minutesText(const QVariant &value)
{
  qint64 seconds = 0;
  const QString text = str(value);
  if (text.contains(QLatin1Char(':')))
  {
    const QStringList parts = text.split(QLatin1Char(':'));
    seconds = static_cast<qint64>(parts.at(0).toDouble()) * 60 + static_cast<qint64>(parts.at(1).toDouble());
  }
  else
  {
    seconds = static_cast<qint64>(std::floor(num(value) * 60 + 0.5));
  }
  if (seconds < 0)
  {
    seconds = 0;
  }
  return Scoreboard::twoDigits(static_cast<int>(seconds / 60)) + s(":") +
         Scoreboard::twoDigits(static_cast<int>(seconds % 60));
}

// LiveStats stat -> key field. Players and teams.
struct Stat
{
  const char *source;
  const char *name;
};

const Stat kStats[] = {
    {"sPoints", "points"},
    {"sReboundsTotal", "rebounds"},
    {"sReboundsOffensive", "reboundsOffensive"},
    {"sReboundsDefensive", "reboundsDefensive"},
    {"sAssists", "assists"},
    {"sSteals", "steals"},
    {"sBlocks", "blocks"},
    {"sBlocksReceived", "blocksReceived"},
    {"sTurnovers", "turnovers"},
    {"sFoulsPersonal", "fouls"},
    {"sFoulsTechnical", "foulsTechnical"},
    {"sFoulsOn", "foulsOn"},
    {"sFieldGoalsMade", "fgMade"},
    {"sFieldGoalsAttempted", "fgAttempted"},
    {"sTwoPointersMade", "twoMade"},
    {"sTwoPointersAttempted", "twoAttempted"},
    {"sThreePointersMade", "threeMade"},
    {"sThreePointersAttempted", "threeAttempted"},
    {"sFreeThrowsMade", "ftMade"},
    {"sFreeThrowsAttempted", "ftAttempted"},
    {"sPointsInThePaint", "pointsInPaint"},
    {"sPointsFastBreak", "fastBreakPoints"},
    {"sPointsSecondChance", "secondChancePoints"},
    {"sPointsFromTurnovers", "pointsFromTurnovers"},
};

// Teams only.
const Stat kTeamStats[] = {
    {"sBenchPoints", "benchPoints"},
    {"sBiggestLead", "biggestLead"},
    {"sBiggestScoringRun", "biggestRun"},
    {"sLeadChanges", "leadChanges"},
    {"sTimesScoreLevel", "timesTied"},
    {"sReboundsTeam", "teamRebounds"},
    {"sTurnoversTeam", "teamTurnovers"},
    {"sFoulsTeam", "foulsTeam"},
};

// Percentages, computed from made / attempted (whole percent).
struct Percentage
{
  const char *name;
  const char *made;
  const char *attempted;
};

const Percentage kPercentages[] = {
    {"fgPct", "sFieldGoalsMade", "sFieldGoalsAttempted"},
    {"twoPct", "sTwoPointersMade", "sTwoPointersAttempted"},
    {"threePct", "sThreePointersMade", "sThreePointersAttempted"},
    {"ftPct", "sFreeThrowsMade", "sFreeThrowsAttempted"},
};

// Leaders: key field -> LiveStats stat.
const Stat kLeaders[] = {
    {"sPoints", "points"},
    {"sReboundsTotal", "rebounds"},
    {"sAssists", "assists"},
    {"sSteals", "steals"},
    {"sBlocks", "blocks"},
};

template <typename T, int N>
int arraySize(const T (&)[N])
{
  return N;
}

double stat(const QVariantMap &stats, const char *source)
{
  return num(stats.value(s(source)));
}

// The specification's efficiency, for data without sEfficiency.
double efficiency(const QVariantMap &stats)
{
  if (isSet(stats.value(s("sEfficiency"))))
  {
    return stat(stats, "sEfficiency");
  }
  const double positive = stat(stats, "sPoints") + stat(stats, "sAssists") + stat(stats, "sBlocks") +
                          stat(stats, "sSteals") + stat(stats, "sFoulsOn") + stat(stats, "sReboundsTotal");
  const double missed = (stat(stats, "sTwoPointersAttempted") - stat(stats, "sTwoPointersMade")) +
                        (stat(stats, "sThreePointersAttempted") - stat(stats, "sThreePointersMade")) +
                        (stat(stats, "sFreeThrowsAttempted") - stat(stats, "sFreeThrowsMade"));
  const double negative = stat(stats, "sTurnovers") + stat(stats, "sBlocksReceived") + stat(stats, "sFoulsPersonal") +
                          stat(stats, "sFoulsTechnical") + missed;
  return positive - negative;
}

double plusMinus(const QVariantMap &stats)
{
  if (isSet(stats.value(s("sPlusMinusPoints"))))
  {
    return stat(stats, "sPlusMinusPoints");
  }
  return stat(stats, "sPlus") - stat(stats, "sMinus");
}

void addStats(const QString &prefix, const QVariantMap &stats, QVariantMap *values)
{
  for (int i = 0; i < arraySize(kStats); ++i)
  {
    values->insert(prefix + s(kStats[i].name), Kv::makeNumber(stat(stats, kStats[i].source)));
  }
  for (int i = 0; i < arraySize(kPercentages); ++i)
  {
    const double attempted = stat(stats, kPercentages[i].attempted);
    const double made = stat(stats, kPercentages[i].made);
    values->insert(prefix + s(kPercentages[i].name),
                   Kv::makeNumber(attempted > 0 ? std::floor(100 * made / attempted + 0.5) : 0));
  }
  values->insert(prefix + s("efficiency"), Kv::makeNumber(efficiency(stats)));
}

// The s* fields of a LiveStats object (sPoints, ...); tot_s* for data.json
// team totals.
QVariantMap statsOf(const QVariantMap &object, const QString &prefix = QString())
{
  QVariantMap stats;
  for (QVariantMap::const_iterator it = object.constBegin(); it != object.constEnd(); ++it)
  {
    if (!it.key().startsWith(prefix))
    {
      continue;
    }
    const QString name = it.key().mid(prefix.size());
    if (name.size() > 1 && name.at(0) == QLatin1Char('s') && name.at(1).isUpper())
    {
      stats.insert(name, it.value());
    }
  }
  return stats;
}

// Shirt numbers in order: 4, 5, 10, 00.
bool numberLess(const QString &a, const QString &b)
{
  bool okA = false, okB = false;
  const int na = a.toInt(&okA);
  const int nb = b.toInt(&okB);
  if (okA && okB && na != nb)
  {
    return na < nb;
  }
  if (okA != okB)
  {
    return okA;
  }
  return a < b;
}

QString typeName(const QString &type)
{
  static const char *names[][2] = {
      {"te", "teams"},       {"box", "boxscore"},         {"st", "status"},    {"ac", "action"},
      {"pbp", "playbyplay"}, {"mi", "matchinformation"}, {"se", "setup"},     {"sd", "standings"},
      {"sc", "schedule"},    {"of", "officials"},         {"su", "summary"},   {"cs", "competitionstatistics"},
  };
  const QString lower = type.toLower();
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
  {
    if (lower == s(names[i][0]))
    {
      return s(names[i][1]);
    }
  }
  return lower;
}
} // namespace

LiveStatsModel::LiveStatsModel()
{
  clear();
}

void LiveStatsModel::clear()
{
  m_teams.clear();
  m_homeTeam = 1;
  m_regularPeriods = 4;
  m_hasClock = false;
  m_clockCs = 0;
  m_shotClockCs = -1;
  m_clockSetMs = 0;
  m_clockRunning = false;
  m_period = 0;
  m_periodType.clear();
  m_possession = 0;
  m_possessionArrow = 0;
  m_hasPossession = false;
  m_match.clear();
  m_standings.clear();
  m_schedule.clear();
  m_lastAction = Action();
  m_lastScore = Action();
  m_lastMessageId = 0;
}

bool LiveStatsModel::applyTvMessage(const QVariantMap &message, qint64 nowMs, QString *type, QString *error)
{
  const QString name = typeName(str(message.value(s("type"))));
  if (name.isEmpty())
  {
    *error = s("not a LiveStats message (no \"type\")");
    return false;
  }
  *type = name;
  m_lastMessageId = std::max(m_lastMessageId, static_cast<qlonglong>(num(message.value(s("messageId")))));

  if (name == s("teams"))
  {
    applyTeams(message);
  }
  else if (name == s("boxscore"))
  {
    applyBoxscore(message);
  }
  else if (name == s("status"))
  {
    applyStatus(message, nowMs);
  }
  else if (name == s("action"))
  {
    applyAction(message, nowMs);
  }
  else if (name == s("playbyplay"))
  {
    applyPlayByPlay(message.value(s("actions")).toList(), false);
  }
  else if (name == s("matchinformation"))
  {
    applyMatchInformation(message);
  }
  else if (name == s("setup"))
  {
    const QVariantMap periods = message.value(s("periods")).toMap();
    if (num(periods.value(s("number"))) > 0)
    {
      m_regularPeriods = static_cast<int>(num(periods.value(s("number"))));
    }
    if (isSet(periods.value(s("length"))))
    {
      m_match.insert(Scoreboard::prefix() + s("match.periodLength"), Kv::makeNumber(num(periods.value(s("length")))));
    }
  }
  else if (name == s("standings"))
  {
    applyStandings(message);
  }
  else if (name == s("schedule"))
  {
    applySchedule(message);
  }
  // ping, officials, summary, ...: nothing to show.
  return true;
}

bool LiveStatsModel::applyDataJson(const QVariantMap &root, qint64 nowMs, QString *error)
{
  const QVariantMap teams = root.value(s("tm")).toMap();
  if (teams.isEmpty())
  {
    *error = s("not a LiveStats data.json (no \"tm\")");
    return false;
  }

  const QHash<QString, QVariant> images = m_images;
  clear();
  m_images = images;

  // Home is team 1.
  for (int team = 1; team <= 2; ++team)
  {
    applyDataJsonTeam(team, teams.value(QString::number(team)).toMap());
  }

  m_periodType = str(root.value(s("periodType")));
  if (flag(root.value(s("inOT"))))
  {
    m_periodType = s("OVERTIME");
  }
  m_period = periodNumber(static_cast<int>(num(root.value(s("period")))), m_periodType);
  setClock(root.value(s("clock")), root.value(s("shotClock")), nowMs);
  m_clockRunning = flag(root.value(s("clockRunning")));
  if (isSet(root.value(s("possession"))))
  {
    m_hasPossession = true;
    m_possession = static_cast<int>(num(root.value(s("possession"))));
  }
  m_possessionArrow = static_cast<int>(num(root.value(s("possessionArrow"))));
  if (isSet(root.value(s("status"))))
  {
    m_match.insert(Scoreboard::prefix() + s("match.status"), Kv::makeText(str(root.value(s("status")))));
  }

  applyPlayByPlay(root.value(s("pbp")).toList(), true);
  applyOtherMatches(root.value(s("othermatches")).toList());
  return true;
}

void LiveStatsModel::applyDataJsonTeam(int number, const QVariantMap &data)
{
  if (data.isEmpty())
  {
    return;
  }
  Team &team = m_teams[number];
  team.name = str(data.value(s("name")));
  team.shortName = str(data.value(s("shortName")));
  team.code = str(data.value(s("code")));
  team.coach = str(data.value(s("coach")));
  team.logoUrl = findUrl(data.value(s("logoT")));
  if (team.logoUrl.isEmpty())
  {
    team.logoUrl = findUrl(data.value(s("logoS")));
  }
  if (team.logoUrl.isEmpty())
  {
    team.logoUrl = findUrl(data.value(s("logo")));
  }
  if (isSet(data.value(s("score"))))
  {
    team.hasScore = true;
    team.score = num(data.value(s("score")));
  }
  team.fouls = num(data.value(s("fouls")), -1);
  team.timeouts = num(data.value(s("timeouts")), -1);
  team.stats = statsOf(data, s("tot_"));

  // p1_score, p2_score, ...
  for (QVariantMap::const_iterator it = data.constBegin(); it != data.constEnd(); ++it)
  {
    if (it.key().startsWith(QLatin1Char('p')) && it.key().endsWith(s("_score")))
    {
      bool ok = false;
      const int period = it.key().mid(1, it.key().size() - 7).toInt(&ok);
      if (ok && period > 0)
      {
        team.periods.insert(period, num(it.value()));
      }
    }
  }

  const QVariantMap players = data.value(s("pl")).toMap();
  for (QVariantMap::const_iterator it = players.constBegin(); it != players.constEnd(); ++it)
  {
    const QVariantMap p = it.value().toMap();
    Player player;
    player.pno = it.key();
    player.number = str(p.value(s("shirtNumber")));
    player.firstName = str(p.value(s("firstName")));
    player.familyName = str(p.value(s("familyName")));
    player.name = str(p.value(s("name")));
    if (player.name.isEmpty())
    {
      player.name = fullName(player.firstName, player.familyName);
    }
    player.scoreboardName = str(p.value(s("scoreboardName")));
    player.position = str(p.value(s("playingPosition")));
    player.starter = flag(p.value(s("starter")));
    player.captain = flag(p.value(s("captain")));
    player.onCourt = flag(p.value(s("active")));
    player.photoUrl = findUrl(p.value(s("photoT")));
    if (player.photoUrl.isEmpty())
    {
      player.photoUrl = findUrl(p.value(s("photoS")));
    }
    if (player.photoUrl.isEmpty())
    {
      player.photoUrl = findUrl(p.value(s("photo")));
    }
    player.stats = statsOf(p);
    team.players << player;
  }
}

void LiveStatsModel::applyOtherMatches(const QVariantList &matches)
{
  m_schedule.clear();
  const QString prefix = Scoreboard::prefix() + s("schedule.");
  int n = 0;
  for (int i = 0; i < matches.size(); ++i)
  {
    const QVariantMap match = matches.at(i).toMap();
    if (match.isEmpty())
    {
      continue;
    }
    const QString base = prefix + QString::number(++n) + s(".");
    m_schedule.insert(base + s("home"), Kv::makeText(str(match.value(s("team1Name")))));
    m_schedule.insert(base + s("away"), Kv::makeText(str(match.value(s("team2Name")))));
    m_schedule.insert(base + s("homeCode"),
                      Kv::makeText(str(match.value(s("team1")).toMap().value(s("teamCode")))));
    m_schedule.insert(base + s("awayCode"),
                      Kv::makeText(str(match.value(s("team2")).toMap().value(s("teamCode")))));
    m_schedule.insert(base + s("homeScore"), Kv::makeText(str(match.value(s("team1Score")))));
    m_schedule.insert(base + s("awayScore"), Kv::makeText(str(match.value(s("team2Score")))));
    m_schedule.insert(base + s("time"), Kv::makeText(str(match.value(s("matchTime")))));
    // In progress: period and clock.
    const int period = periodNumber(static_cast<int>(num(match.value(s("period")))), str(match.value(s("periodType"))));
    QString status = Scoreboard::periodText(period);
    const qint64 clock = parseClock(match.value(s("clock")));
    if (!status.isEmpty() && clock >= 0)
    {
      status += s(" ") + clockText(clock);
    }
    m_schedule.insert(base + s("status"), Kv::makeText(status));
  }
  m_schedule.insert(prefix + s("count"), Kv::makeNumber(n));
}

void LiveStatsModel::applyTeams(const QVariantMap &message)
{
  const QVariantList teams = message.value(s("teams")).toList();
  for (int i = 0; i < teams.size(); ++i)
  {
    const QVariantMap data = teams.at(i).toMap();
    const int number = static_cast<int>(num(data.value(s("teamNumber"))));
    if (number != 1 && number != 2)
    {
      continue;
    }
    Team &team = m_teams[number];
    const QVariantMap detail = data.value(s("detail")).toMap();
    team.name = str(detail.value(s("teamName")));
    team.shortName = str(detail.value(s("teamNickname")));
    team.code = str(detail.value(s("teamCode")));
    team.logoUrl = findUrl(detail.value(s("logo")));
    if (team.logoUrl.isEmpty())
    {
      team.logoUrl = findUrl(data.value(s("logo")));
    }
    if (flag(detail.value(s("isHomeCompetitor"))))
    {
      m_homeTeam = number;
    }
    const QVariant coach = data.value(s("coach"));
    team.coach = coach.userType() == QMetaType::QVariantMap ? fullName(str(coach.toMap().value(s("firstName"))),
                                                          str(coach.toMap().value(s("familyName"))))
                                               : str(coach);

    // Keep the stats of players already known (a boxscore can come first).
    QHash<QString, QVariantMap> stats;
    for (int j = 0; j < team.players.size(); ++j)
    {
      stats.insert(team.players.at(j).pno, team.players.at(j).stats);
    }
    team.players.clear();

    const QVariantList players = data.value(s("players")).toList();
    for (int j = 0; j < players.size(); ++j)
    {
      const QVariantMap p = players.at(j).toMap();
      Player player;
      player.pno = str(p.value(s("pno")));
      player.number = str(p.value(s("shirtNumber")));
      player.firstName = str(p.value(s("firstName")));
      player.familyName = str(p.value(s("familyName")));
      player.name = str(p.value(s("TVName")));
      if (player.name.isEmpty())
      {
        player.name = fullName(str(p.value(s("firstNameTV"))), str(p.value(s("familyNameTV"))));
      }
      if (player.name.isEmpty())
      {
        player.name = fullName(player.firstName, player.familyName);
      }
      player.scoreboardName = str(p.value(s("scoreboardName")));
      player.position = str(p.value(s("playingPosition")));
      player.starter = flag(p.value(s("starter")));
      player.captain = flag(p.value(s("captain")));
      player.onCourt = flag(p.value(s("active")));
      player.photoUrl = findUrl(p.value(s("photo")));
      player.stats = stats.value(player.pno);
      team.players << player;
    }
  }
}

void LiveStatsModel::applyBoxscore(const QVariantMap &message)
{
  const QVariantList teams = message.value(s("teams")).toList();
  for (int i = 0; i < teams.size(); ++i)
  {
    const QVariantMap data = teams.at(i).toMap();
    const int number = static_cast<int>(num(data.value(s("teamNumber"))));
    if (number != 1 && number != 2)
    {
      continue;
    }
    Team &team = m_teams[number];
    const QVariantMap total = data.value(s("total")).toMap();
    team.stats = statsOf(total.value(s("team")).toMap());

    const QVariantList players = total.value(s("players")).toList();
    for (int j = 0; j < players.size(); ++j)
    {
      const QVariantMap p = players.at(j).toMap();
      const QString pno = str(p.value(s("pno")));
      Player *known = player(number, pno);
      if (!known && !pno.isEmpty())
      {
        // Before the teams message: kept (without a shirt number, so not
        // shown) until it names the player.
        Player unknown;
        unknown.pno = pno;
        team.players << unknown;
        known = &team.players.last();
      }
      if (known)
      {
        known->stats = statsOf(p);
      }
    }

    const QVariantList periods = data.value(s("periods")).toList();
    team.periods.clear();
    for (int j = 0; j < periods.size(); ++j)
    {
      const QVariantMap period = periods.at(j).toMap();
      const int n = periodNumber(static_cast<int>(num(period.value(s("period")))), str(period.value(s("periodType"))));
      if (n > 0)
      {
        team.periods.insert(n, stat(period.value(s("team")).toMap(), "sPoints"));
      }
    }
  }
}

void LiveStatsModel::applyStatus(const QVariantMap &message, qint64 nowMs)
{
  const QString prefix = Scoreboard::prefix();
  if (isSet(message.value(s("status"))))
  {
    m_match.insert(prefix + s("match.status"), Kv::makeText(str(message.value(s("status")))));
  }
  if (isSet(message.value(s("periodStatus"))))
  {
    m_match.insert(prefix + s("match.periodStatus"), Kv::makeText(str(message.value(s("periodStatus")))));
  }

  // "period": {"current": 2, "periodType": "REGULAR"}, or a number.
  const QVariant period = message.value(s("period"));
  if (period.userType() == QMetaType::QVariantMap)
  {
    m_periodType = str(period.toMap().value(s("periodType")));
    m_period = periodNumber(static_cast<int>(num(period.toMap().value(s("current")))), m_periodType);
  }
  else if (isSet(period))
  {
    m_periodType = str(message.value(s("periodType")));
    m_period = periodNumber(static_cast<int>(num(period)), m_periodType);
  }

  setClock(message.value(s("clock")), message.value(s("shotClock")), nowMs);
  if (isSet(message.value(s("clockRunning"))))
  {
    setClockRunning(flag(message.value(s("clockRunning"))), nowMs);
  }
  if (isSet(message.value(s("possession"))))
  {
    m_hasPossession = true;
    m_possession = static_cast<int>(num(message.value(s("possession"))));
  }
  if (isSet(message.value(s("possessionArrow"))))
  {
    m_possessionArrow = static_cast<int>(num(message.value(s("possessionArrow"))));
  }

  const QVariantList scores = message.value(s("scores")).toList();
  for (int i = 0; i < scores.size(); ++i)
  {
    const QVariantMap score = scores.at(i).toMap();
    const int number = static_cast<int>(num(score.value(s("teamNumber"))));
    if (number != 1 && number != 2)
    {
      continue;
    }
    Team &team = m_teams[number];
    if (isSet(score.value(s("score"))))
    {
      team.hasScore = true;
      team.score = num(score.value(s("score")));
    }
    if (isSet(score.value(s("timeoutsRemaining"))))
    {
      team.timeouts = num(score.value(s("timeoutsRemaining")));
    }
    // Team fouls of the period, as on the scoreboard.
    if (isSet(score.value(s("teamFouls"))))
    {
      team.fouls = num(score.value(s("teamFouls")));
    }
    else if (isSet(score.value(s("fouls"))))
    {
      team.fouls = num(score.value(s("fouls")));
    }
  }
}

void LiveStatsModel::applyAction(const QVariantMap &message, qint64 nowMs)
{
  const QString type = str(message.value(s("actionType"))).toLower();
  const QString subType = str(message.value(s("subType"))).toLower();

  if (type == s("clock"))
  {
    setClock(message.value(s("clock")), message.value(s("shotClock")), nowMs);
    if (subType == s("start") || subType == s("stop"))
    {
      setClockRunning(subType == s("start"), nowMs);
    }
    return;
  }
  if (type == s("shotclock"))
  {
    setClock(QVariant(), message.value(s("shotClock")), nowMs);
    return;
  }

  // Scores after the action.
  for (int team = 1; team <= 2; ++team)
  {
    const QVariant score = message.value(s("score") + QString::number(team));
    if (isSet(score))
    {
      m_teams[team].hasScore = true;
      m_teams[team].score = num(score);
    }
  }

  if (!isSportAction(type) || isSet(message.value(s("deleted"))))
  {
    return;
  }

  // Substitutions change who is on court before the next boxscore.
  if (type == s("substitution"))
  {
    Player *p = player(static_cast<int>(num(message.value(s("teamNumber")))), str(message.value(s("pno"))));
    if (p)
    {
      p->onCourt = subType == s("in");
    }
  }

  const Action a = action(message, false);
  if (a.number >= m_lastAction.number)
  {
    m_lastAction = a;
  }
  if (isScore(a) && a.number >= m_lastScore.number)
  {
    m_lastScore = a;
  }
}

void LiveStatsModel::applyPlayByPlay(const QVariantList &actions, bool dataJson)
{
  m_lastAction = Action();
  m_lastScore = Action();
  for (int i = 0; i < actions.size(); ++i)
  {
    const QVariantMap entry = actions.at(i).toMap();
    if (!isSportAction(str(entry.value(s("actionType"))).toLower()) || isSet(entry.value(s("deleted"))))
    {
      continue;
    }
    const Action a = action(entry, dataJson);
    if (a.number >= m_lastAction.number)
    {
      m_lastAction = a;
    }
    if (isScore(a) && a.number >= m_lastScore.number)
    {
      m_lastScore = a;
    }
  }
}

void LiveStatsModel::applyMatchInformation(const QVariantMap &message)
{
  const QString prefix = Scoreboard::prefix() + s("match.");
  const QVariantMap match = message.value(s("match")).toMap();
  const QVariantMap competition = message.value(s("competition")).toMap();
  const QVariantMap venue = message.value(s("venue")).toMap();
  m_match.insert(prefix + s("name"), Kv::makeText(str(match.value(s("matchName")))));
  m_match.insert(prefix + s("time"), Kv::makeText(str(match.value(s("matchTime")))));
  QString round = str(match.value(s("roundDescription")));
  if (round.isEmpty())
  {
    round = str(match.value(s("phaseName")));
  }
  m_match.insert(prefix + s("round"), Kv::makeText(round));
  m_match.insert(prefix + s("competition"), Kv::makeText(str(competition.value(s("competitionName")))));
  m_match.insert(prefix + s("venue"), Kv::makeText(str(venue.value(s("venueName")))));
}

void LiveStatsModel::applyStandings(const QVariantMap &message)
{
  QVariantList rows = message.value(s("standings")).toList();
  std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
    return num(a.toMap().value(s("position")), 1e9) < num(b.toMap().value(s("position")), 1e9);
  });

  m_standings.clear();
  const QString prefix = Scoreboard::prefix() + s("standings.");
  for (int i = 0; i < rows.size(); ++i)
  {
    const QVariantMap row = rows.at(i).toMap();
    const QString base = prefix + QString::number(i + 1) + s(".");
    m_standings.insert(base + s("position"), Kv::makeNumber(num(row.value(s("position")), i + 1)));
    m_standings.insert(base + s("team"), Kv::makeText(str(row.value(s("teamName")))));
    m_standings.insert(base + s("code"), Kv::makeText(str(row.value(s("teamCode")))));
    m_standings.insert(base + s("played"), Kv::makeNumber(num(row.value(s("played")))));
    m_standings.insert(base + s("won"), Kv::makeNumber(num(row.value(s("won")))));
    m_standings.insert(base + s("lost"), Kv::makeNumber(num(row.value(s("lost")))));
    m_standings.insert(base + s("points"), Kv::makeNumber(num(row.value(s("standingPoints")))));
  }
  m_standings.insert(prefix + s("count"), Kv::makeNumber(rows.size()));
}

void LiveStatsModel::applySchedule(const QVariantMap &message)
{
  const QVariantList matches = message.value(s("matches")).toList();
  m_schedule.clear();
  const QString prefix = Scoreboard::prefix() + s("schedule.");
  for (int i = 0; i < matches.size(); ++i)
  {
    const QVariantMap match = matches.at(i).toMap();
    const QVariantList competitors = match.value(s("competitors")).toList();
    QVariantMap home = competitors.value(0).toMap();
    QVariantMap away = competitors.value(1).toMap();
    if (flag(away.value(s("isHomeCompetitor"))))
    {
      std::swap(home, away);
    }
    const QString base = prefix + QString::number(i + 1) + s(".");
    m_schedule.insert(base + s("home"), Kv::makeText(str(home.value(s("teamName")))));
    m_schedule.insert(base + s("away"), Kv::makeText(str(away.value(s("teamName")))));
    m_schedule.insert(base + s("homeCode"), Kv::makeText(str(home.value(s("teamCode")))));
    m_schedule.insert(base + s("awayCode"), Kv::makeText(str(away.value(s("teamCode")))));
    m_schedule.insert(base + s("homeScore"), Kv::makeText(str(home.value(s("scoreString")))));
    m_schedule.insert(base + s("awayScore"), Kv::makeText(str(away.value(s("scoreString")))));
    m_schedule.insert(base + s("time"), Kv::makeText(str(match.value(s("matchTime")))));
    m_schedule.insert(base + s("status"), Kv::makeText(str(match.value(s("matchStatus")))));
  }
  m_schedule.insert(prefix + s("count"), Kv::makeNumber(matches.size()));
}

void LiveStatsModel::runClockTo(qint64 nowMs)
{
  if (m_clockRunning && m_hasClock)
  {
    const qint64 elapsed = std::max<qint64>(0, (nowMs - m_clockSetMs) / 10);
    m_clockCs = std::max<qint64>(0, m_clockCs - elapsed);
    if (m_shotClockCs >= 0)
    {
      m_shotClockCs = std::max<qint64>(0, m_shotClockCs - elapsed);
    }
  }
  m_clockSetMs = nowMs;
}

void LiveStatsModel::setClock(const QVariant &clock, const QVariant &shotClock, qint64 nowMs)
{
  runClockTo(nowMs);
  const qint64 cs = parseClock(clock);
  if (cs >= 0)
  {
    m_hasClock = true;
    m_clockCs = cs;
  }
  // Not given: it keeps running from where it was.
  if (isSet(shotClock))
  {
    m_shotClockCs = parseClock(shotClock);
  }
}

void LiveStatsModel::setClockRunning(bool running, qint64 nowMs)
{
  runClockTo(nowMs);
  m_clockRunning = running;
}

QString LiveStatsModel::side(int team) const
{
  if (team != 1 && team != 2)
  {
    return QString();
  }
  return team == m_homeTeam ? s("home") : s("away");
}

int LiveStatsModel::periodNumber(int period, const QString &periodType) const
{
  if (period > 0 && periodType.toUpper() == s("OVERTIME") && period <= m_regularPeriods)
  {
    return m_regularPeriods + period;
  }
  return period;
}

LiveStatsModel::Player *LiveStatsModel::player(int team, const QString &pno)
{
  if (!m_teams.contains(team) || pno.isEmpty())
  {
    return nullptr;
  }
  QList<Player> &players = m_teams[team].players;
  for (int i = 0; i < players.size(); ++i)
  {
    if (players.at(i).pno == pno)
    {
      return &players[i];
    }
  }
  return nullptr;
}

LiveStatsModel::Action LiveStatsModel::action(const QVariantMap &entry, bool dataJson) const
{
  Action a;
  a.number = static_cast<qlonglong>(num(entry.value(s("actionNumber"))));
  a.type = str(entry.value(s("actionType"))).toLower();
  a.subType = str(entry.value(s("subType"))).toLower();
  a.success = flag(entry.value(s("success")));
  a.team = static_cast<int>(num(entry.value(dataJson ? s("tno") : s("teamNumber"))));
  const qint64 clock = parseClock(entry.value(dataJson ? s("gt") : s("clock")));
  if (clock >= 0)
  {
    a.clock = clockText(clock);
  }
  a.period = periodNumber(static_cast<int>(num(entry.value(s("period")))), str(entry.value(s("periodType"))));

  if (dataJson)
  {
    a.shirtNumber = str(entry.value(s("shirtNumber")));
    a.name = str(entry.value(s("scoreboardName")));
    if (a.name.isEmpty())
    {
      a.name = str(entry.value(s("player")));
    }
    if (a.name.isEmpty())
    {
      a.name = fullName(str(entry.value(s("firstName"))), str(entry.value(s("familyName"))));
    }
  }
  else if (m_teams.contains(a.team))
  {
    const QString pno = str(entry.value(s("pno")));
    const QList<Player> &players = m_teams[a.team].players;
    for (int i = 0; i < players.size(); ++i)
    {
      if (!pno.isEmpty() && players.at(i).pno == pno)
      {
        a.shirtNumber = players.at(i).number;
        a.name = players.at(i).scoreboardName.isEmpty() ? players.at(i).name : players.at(i).scoreboardName;
      }
    }
  }
  return a;
}

bool LiveStatsModel::isSportAction(const QString &type)
{
  static const char *types[] = {"2pt",      "3pt",          "freethrow", "jumpball", "assist", "block",
                                "rebound",  "foul",         "timeout",   "turnover", "steal",  "substitution",
                                "foulon",   "headcoachchallenge"};
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
  {
    if (type == s(types[i]))
    {
      return true;
    }
  }
  return false;
}

bool LiveStatsModel::isScore(const Action &action)
{
  return action.success && (action.type == s("2pt") || action.type == s("3pt") || action.type == s("freethrow"));
}

QString LiveStatsModel::actionText(const Action &action) const
{
  QString label;
  if (action.type == s("2pt") || action.type == s("3pt"))
  {
    label = action.type.toUpper() + (action.success ? s(" made") : s(" missed"));
  }
  else if (action.type == s("freethrow"))
  {
    label = action.success ? s("Free throw made") : s("Free throw missed");
  }
  else if (action.type == s("rebound"))
  {
    label = action.subType == s("offensive")   ? s("Offensive rebound")
            : action.subType == s("defensive") ? s("Defensive rebound")
                                               : s("Rebound");
  }
  else if (action.type == s("substitution"))
  {
    label = action.subType == s("out") ? s("Substitution out") : s("Substitution in");
  }
  else if (action.type == s("jumpball"))
  {
    label = s("Jump ball");
  }
  else if (action.type == s("headcoachchallenge"))
  {
    label = s("Coach's challenge");
  }
  else if (action.type == s("foulon"))
  {
    label = s("Fouled");
  }
  else if (!action.type.isEmpty())
  {
    label = action.type.at(0).toUpper() + action.type.mid(1);
  }

  QString who;
  if (!action.shirtNumber.isEmpty())
  {
    who = s("#") + action.shirtNumber;
    if (!action.name.isEmpty())
    {
      who += s(" ") + action.name;
    }
  }
  else if (!action.name.isEmpty())
  {
    who = action.name;
  }
  else if (m_teams.contains(action.team))
  {
    const Team &team = m_teams[action.team];
    who = team.code.isEmpty() ? team.name : team.code;
  }
  return who.isEmpty() ? label : who + s(" ") + label;
}

QVariant LiveStatsModel::image(const QString &url) const
{
  return url.isEmpty() ? QVariant() : m_images.value(url);
}

QStringList LiveStatsModel::imageUrls() const
{
  QStringList urls;
  for (QMap<int, Team>::const_iterator it = m_teams.constBegin(); it != m_teams.constEnd(); ++it)
  {
    if (!it.value().logoUrl.isEmpty())
    {
      urls << it.value().logoUrl;
    }
    for (int i = 0; i < it.value().players.size(); ++i)
    {
      if (!it.value().players.at(i).photoUrl.isEmpty())
      {
        urls << it.value().players.at(i).photoUrl;
      }
    }
  }
  urls.removeDuplicates();
  return urls;
}

void LiveStatsModel::setImage(const QString &url, const QVariant &image)
{
  m_images.insert(url, image);
}

qlonglong LiveStatsModel::lastMessageId() const
{
  return m_lastMessageId;
}

bool LiveStatsModel::clockRunning() const
{
  return m_hasClock && m_clockRunning;
}

bool LiveStatsModel::isGameStateKey(const QString &key)
{
  static const char *keys[] = {"period",     "periodNumber",   "clock",          "clockRunning",
                               "shotClock",  "possession",     "home.score",     "away.score",
                               "home.fouls", "away.fouls",     "home.timeouts",  "away.timeouts"};
  if (!key.startsWith(Scoreboard::prefix()))
  {
    return false;
  }
  const QString name = key.mid(Scoreboard::prefix().size());
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
  {
    if (name == s(keys[i]))
    {
      return true;
    }
  }
  return false;
}

QStringList LiveStatsModel::playerStatNames()
{
  QStringList names;
  for (int i = 0; i < arraySize(kStats); ++i)
  {
    names << s(kStats[i].name);
  }
  for (int i = 0; i < arraySize(kPercentages); ++i)
  {
    names << s(kPercentages[i].name);
  }
  names << s("efficiency") << s("plusMinus") << s("minutes");
  return names;
}

QVariantMap LiveStatsModel::clockValues(qint64 nowMs) const
{
  QVariantMap values;
  if (!m_hasClock)
  {
    return values;
  }
  const QString prefix = Scoreboard::prefix();
  const qint64 elapsed = m_clockRunning ? std::max<qint64>(0, (nowMs - m_clockSetMs) / 10) : 0;
  values.insert(prefix + s("clock"), Kv::makeText(clockText(std::max<qint64>(0, m_clockCs - elapsed))));
  values.insert(prefix + s("shotClock"),
                Kv::makeText(m_shotClockCs < 0 ? QString()
                                               : shotClockText(std::max<qint64>(0, m_shotClockCs - elapsed))));
  values.insert(prefix + s("clockRunning"), Kv::makeBool(m_clockRunning));
  return values;
}

QVariantMap LiveStatsModel::values(qint64 nowMs) const
{
  const QString prefix = Scoreboard::prefix();
  QVariantMap values = m_match;
  for (QVariantMap::const_iterator it = m_standings.constBegin(); it != m_standings.constEnd(); ++it)
  {
    values.insert(it.key(), it.value());
  }
  for (QVariantMap::const_iterator it = m_schedule.constBegin(); it != m_schedule.constEnd(); ++it)
  {
    values.insert(it.key(), it.value());
  }

  const QVariantMap clock = clockValues(nowMs);
  for (QVariantMap::const_iterator it = clock.constBegin(); it != clock.constEnd(); ++it)
  {
    values.insert(it.key(), it.value());
  }
  if (m_period > 0)
  {
    values.insert(prefix + s("period"), Kv::makeText(Scoreboard::periodText(m_period)));
    values.insert(prefix + s("periodNumber"), Kv::makeNumber(m_period));
    values.insert(prefix + s("periodType"), Kv::makeText(m_periodType));
  }
  if (m_hasPossession)
  {
    values.insert(prefix + s("possession"), Kv::makeText(side(m_possession)));
  }
  if (m_hasPossession || m_possessionArrow != 0)
  {
    values.insert(prefix + s("possessionArrow"), Kv::makeText(side(m_possessionArrow)));
  }

  for (QMap<int, Team>::const_iterator it = m_teams.constBegin(); it != m_teams.constEnd(); ++it)
  {
    addTeamValues(it.key(), &values);
  }
  if (!m_teams.isEmpty())
  {
    addLeaders(&values);
  }

  if (m_lastAction.number >= 0)
  {
    const QString base = prefix + s("lastAction.");
    values.insert(base + s("text"), Kv::makeText(actionText(m_lastAction)));
    values.insert(base + s("type"), Kv::makeText(m_lastAction.type));
    values.insert(base + s("team"), Kv::makeText(side(m_lastAction.team)));
    values.insert(base + s("number"), Kv::makeText(m_lastAction.shirtNumber));
    values.insert(base + s("name"), Kv::makeText(m_lastAction.name));
    values.insert(base + s("clock"), Kv::makeText(m_lastAction.clock));
    values.insert(base + s("period"), Kv::makeText(Scoreboard::periodText(m_lastAction.period)));
  }
  if (m_lastScore.number >= 0)
  {
    const QString base = prefix + s("lastScore.");
    const int points = m_lastScore.type == s("3pt") ? 3 : m_lastScore.type == s("2pt") ? 2 : 1;
    values.insert(base + s("text"), Kv::makeText(actionText(m_lastScore)));
    values.insert(base + s("team"), Kv::makeText(side(m_lastScore.team)));
    values.insert(base + s("number"), Kv::makeText(m_lastScore.shirtNumber));
    values.insert(base + s("name"), Kv::makeText(m_lastScore.name));
    values.insert(base + s("points"), Kv::makeNumber(points));
  }
  return values;
}

namespace
{
template <typename Player>
QList<const Player *> rosterOrder(const QList<Player> &players)
{
  QList<const Player *> order;
  for (int i = 0; i < players.size(); ++i)
  {
    if (!players.at(i).number.isEmpty())
    {
      order << &players.at(i);
    }
  }
  std::stable_sort(order.begin(), order.end(), [](const Player *a, const Player *b) {
    if (a->starter != b->starter)
    {
      return a->starter;
    }
    return numberLess(a->number, b->number);
  });
  return order;
}
} // namespace

void LiveStatsModel::addTeamValues(int number, QVariantMap *values) const
{
  const QString t = side(number);
  if (t.isEmpty())
  {
    return;
  }
  const Team &team = m_teams[number];
  const QString base = Scoreboard::prefix() + t + s(".");

  values->insert(base + s("name"), Kv::makeText(team.name));
  values->insert(base + s("shortName"), Kv::makeText(team.shortName));
  values->insert(base + s("code"), Kv::makeText(team.code));
  values->insert(base + s("coach"), Kv::makeText(team.coach));
  const QVariant logo = image(team.logoUrl);
  if (logo.isValid())
  {
    values->insert(base + s("logo"), logo);
  }

  if (team.hasScore)
  {
    values->insert(base + s("score"), Kv::makeNumber(team.score));
  }
  else if (team.stats.contains(s("sPoints")))
  {
    values->insert(base + s("score"), Kv::makeNumber(stat(team.stats, "sPoints")));
  }
  if (team.fouls >= 0)
  {
    values->insert(base + s("fouls"), Kv::makeNumber(team.fouls));
  }
  if (team.timeouts >= 0)
  {
    values->insert(base + s("timeouts"), Kv::makeNumber(team.timeouts));
  }
  for (QMap<int, double>::const_iterator it = team.periods.constBegin(); it != team.periods.constEnd(); ++it)
  {
    values->insert(base + s("period.") + QString::number(it.key()) + s(".score"), Kv::makeNumber(it.value()));
  }

  const QString stats = base + s("stats.");
  addStats(stats, team.stats, values);
  for (int i = 0; i < arraySize(kTeamStats); ++i)
  {
    values->insert(stats + s(kTeamStats[i].name), Kv::makeNumber(stat(team.stats, kTeamStats[i].source)));
  }

  const QList<const Player *> roster = rosterOrder(team.players);
  int onCourt = 0;
  for (int i = 0; i < roster.size(); ++i)
  {
    const Player &p = *roster.at(i);
    addPlayerValues(base + s("player.") + p.number + s("."), p, values);
    addPlayerValues(base + s("roster.") + QString::number(i + 1) + s("."), p, values);
    if (p.onCourt)
    {
      addPlayerValues(base + s("onCourt.") + QString::number(++onCourt) + s("."), p, values);
    }
  }
  values->insert(base + s("roster.count"), Kv::makeNumber(roster.size()));
  values->insert(base + s("onCourt.count"), Kv::makeNumber(onCourt));
}

void LiveStatsModel::addPlayerValues(const QString &prefix, const Player &p, QVariantMap *values) const
{
  values->insert(prefix + s("number"), Kv::makeText(p.number));
  values->insert(prefix + s("name"), Kv::makeText(p.name));
  values->insert(prefix + s("firstName"), Kv::makeText(p.firstName));
  values->insert(prefix + s("familyName"), Kv::makeText(p.familyName));
  values->insert(prefix + s("scoreboardName"), Kv::makeText(p.scoreboardName.isEmpty() ? p.name : p.scoreboardName));
  values->insert(prefix + s("position"), Kv::makeText(p.position));
  values->insert(prefix + s("starter"), Kv::makeBool(p.starter));
  values->insert(prefix + s("captain"), Kv::makeBool(p.captain));
  values->insert(prefix + s("onCourt"), Kv::makeBool(p.onCourt));
  const QVariant photo = image(p.photoUrl);
  if (photo.isValid())
  {
    values->insert(prefix + s("photo"), photo);
  }
  addStats(prefix, p.stats, values);
  values->insert(prefix + s("plusMinus"), Kv::makeNumber(plusMinus(p.stats)));
  values->insert(prefix + s("minutes"), Kv::makeText(minutesText(p.stats.value(s("sMinutes")))));
}

void LiveStatsModel::addLeaders(QVariantMap *values) const
{
  const QString prefix = Scoreboard::prefix();
  for (int l = 0; l < arraySize(kLeaders); ++l)
  {
    const Player *best = nullptr;
    int bestTeam = 0;
    for (QMap<int, Team>::const_iterator it = m_teams.constBegin(); it != m_teams.constEnd(); ++it)
    {
      const QString t = side(it.key());
      if (t.isEmpty())
      {
        continue;
      }
      const QList<const Player *> roster = rosterOrder(it.value().players);
      const Player *teamBest = nullptr;
      for (int i = 0; i < roster.size(); ++i)
      {
        if (stat(roster.at(i)->stats, kLeaders[l].source) > 0 &&
            (!teamBest || stat(roster.at(i)->stats, kLeaders[l].source) > stat(teamBest->stats, kLeaders[l].source)))
        {
          teamBest = roster.at(i);
        }
      }
      const QString base = prefix + t + s(".leader.") + s(kLeaders[l].name) + s(".");
      values->insert(base + s("name"), Kv::makeText(teamBest ? teamBest->name : QString()));
      values->insert(base + s("number"), Kv::makeText(teamBest ? teamBest->number : QString()));
      values->insert(base + s("value"), Kv::makeNumber(teamBest ? stat(teamBest->stats, kLeaders[l].source) : 0));

      // Game leader: home wins a tie.
      if (teamBest && (!best || stat(teamBest->stats, kLeaders[l].source) > stat(best->stats, kLeaders[l].source) ||
                       (stat(teamBest->stats, kLeaders[l].source) == stat(best->stats, kLeaders[l].source) &&
                        t == s("home"))))
      {
        best = teamBest;
        bestTeam = it.key();
      }
    }
    const QString base = prefix + s("leader.") + s(kLeaders[l].name) + s(".");
    values->insert(base + s("team"), Kv::makeText(best ? side(bestTeam) : QString()));
    values->insert(base + s("name"), Kv::makeText(best ? best->name : QString()));
    values->insert(base + s("number"), Kv::makeText(best ? best->number : QString()));
    values->insert(base + s("value"), Kv::makeNumber(best ? stat(best->stats, kLeaders[l].source) : 0));
  }
}
