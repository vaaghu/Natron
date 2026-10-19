#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

// FIBA LiveStats (Genius Sports) game data as state store keys: rosters,
// the player box score, team stats, match information, leaders, the last
// action, standings and the schedule. No I/O: LiveStatsFeed reads the data
// and gives it to the model, so the model can be tested on its own.
//
// Two inputs, the same keys:
//   - TV feed messages (in-venue TCP, port 7677): JSON objects with a "type"
//     (teams, boxscore, status, action, playbyplay, matchinformation, setup,
//     standings, schedule, ping). Players are referred to by "pno", their
//     number in the match; the teams message maps it to the shirt number.
//   - The online data.json (fibalivestats.dcd.shared.geniussports.com/data/
//     <match id>/data.json): the whole game in one object, polled.
//
// Keys (prefix scoreboard., <t> is home or away):
//   match.name, match.status, match.periodStatus, match.competition,
//   match.round, match.venue, match.time, match.periodLength, periodType,
//   possessionArrow
//   game state, also written by the Ultra Score console (see
//   isGameStateKey): period, periodNumber, clock, clockRunning, shotClock,
//   possession, <t>.score, <t>.fouls, <t>.timeouts (timeouts left)
//   <t>.name, <t>.shortName, <t>.code, <t>.coach, <t>.logo (image),
//   <t>.period.<n>.score, <t>.stats.<stat>
//   <t>.player.<shirt no>.<field>: number, name, firstName, familyName,
//     scoreboardName, position, starter, captain, onCourt, photo (image) and
//     the stats (see playerStatNames)
//   <t>.roster.count, <t>.roster.<1..n>.<field>: the players, starters
//     first, then by shirt number
//   <t>.onCourt.count, <t>.onCourt.<1..5>.<field>: the players on court
//   <t>.leader.<stat>.name / number / value, leader.<stat>.team / name /
//     number / value (stat: points, rebounds, assists, steals, blocks)
//   lastAction.text / type / team / number / name / clock / period,
//   lastScore.text / team / number / name / points
//   standings.count, standings.<i>.position / team / code / played / won /
//     lost / points
//   schedule.count, schedule.<i>.home / away / homeCode / awayCode /
//     homeScore / awayScore / time / status
//
// Images: photos and logos come as URLs (imageUrls()). Once downloaded,
// setImage() gives the model the image value to write.
class LiveStatsModel
{
public:
  LiveStatsModel();

  void clear();

  // Adds one TV feed message to the model. Returns false and sets *error if
  // it is not a LiveStats message. *type is the message type, in full
  // ("teams", "boxscore", ...). Unknown types and keys are ignored, as the
  // feed's specification asks. nowMs: when it arrived (runs the clocks).
  bool applyTvMessage(const QVariantMap &message, qint64 nowMs, QString *type, QString *error);

  // Replaces the model with a data.json. Returns false and sets *error if
  // it is not one.
  bool applyDataJson(const QVariantMap &root, qint64 nowMs, QString *error);

  // Every key -> typed value (see KvValue.h), with the clocks run on to
  // nowMs.
  QVariantMap values(qint64 nowMs) const;

  // Only the clock keys, for updating them between messages.
  QVariantMap clockValues(qint64 nowMs) const;
  bool clockRunning() const;

  // Highest messageId received (TV feed), 0 before any.
  qlonglong lastMessageId() const;
  // Back to 0: the feed restarted and numbers its messages from 1 again.
  void resetMessageId();

  // Photo and logo URLs of the current data.
  QStringList imageUrls() const;
  void setImage(const QString &url, const QVariant &image);

  // Keys the Ultra Score console writes too.
  static bool isGameStateKey(const QString &key);

  // Player stat field names, as in the keys.
  static QStringList playerStatNames();

private:
  struct Player
  {
    QString pno;
    QString number;
    QString name;
    QString firstName;
    QString familyName;
    QString scoreboardName;
    QString position;
    QString photoUrl;
    bool starter;
    bool captain;
    bool onCourt;
    QVariantMap stats; // LiveStats name (sPoints, ...) -> value

    Player()
        : starter(false),
          captain(false),
          onCourt(false)
    {
    }
  };

  struct Team
  {
    QString name;
    QString shortName;
    QString code;
    QString coach;
    QString logoUrl;
    QList<Player> players;
    QVariantMap stats;          // LiveStats name -> value
    QMap<int, double> periods;  // period number -> points
    bool hasScore;
    double score;
    double fouls;
    double timeouts;

    Team()
        : hasScore(false),
          score(0),
          fouls(-1),
          timeouts(-1)
    {
    }
  };

  // A sport action, for lastAction / lastScore.
  struct Action
  {
    qlonglong number;
    int team; // 1, 2, 0: none
    QString shirtNumber;
    QString name;
    QString type;
    QString subType;
    bool success;
    QString clock;
    int period;

    Action()
        : number(-1),
          team(0),
          success(false),
          period(0)
    {
    }
  };

  void applyTeams(const QVariantMap &message);
  void applyBoxscore(const QVariantMap &message);
  void applyStatus(const QVariantMap &message, qint64 nowMs);
  void applyAction(const QVariantMap &action, qint64 nowMs);
  void applyPlayByPlay(const QVariantList &actions, bool dataJson);
  void applyMatchInformation(const QVariantMap &message);
  void applyStandings(const QVariantMap &message);
  void applySchedule(const QVariantMap &message);
  void applyDataJsonTeam(int number, const QVariantMap &team);
  void applyOtherMatches(const QVariantList &matches);

  // Team 1 or 2 -> "home" / "away".
  QString side(int team) const;
  int periodNumber(int period, const QString &periodType) const;
  Player *player(int team, const QString &pno);
  Action action(const QVariantMap &entry, bool dataJson) const;
  static bool isSportAction(const QString &type);
  static bool isScore(const Action &action);
  QString actionText(const Action &action) const;

  // The clocks as they are at nowMs, then counting from there.
  void runClockTo(qint64 nowMs);
  // Sets what is given (an invalid / empty value keeps that clock).
  void setClock(const QVariant &clock, const QVariant &shotClock, qint64 nowMs);
  void setClockRunning(bool running, qint64 nowMs);

  void addTeamValues(int team, QVariantMap *values) const;
  void addPlayerValues(const QString &prefix, const Player &player, QVariantMap *values) const;
  void addLeaders(QVariantMap *values) const;
  QVariant image(const QString &url) const;

  QMap<int, Team> m_teams; // 1, 2
  int m_homeTeam;
  int m_regularPeriods;

  // Game state.
  bool m_hasClock;
  qint64 m_clockCs; // centiseconds when last set
  qint64 m_shotClockCs;  // -1: off
  qint64 m_clockSetMs;
  bool m_clockRunning;
  int m_period;
  QString m_periodType;
  int m_possession; // team, 0: none
  int m_possessionArrow;
  bool m_hasPossession;

  QVariantMap m_match; // key -> value (match.*)
  QVariantMap m_standings;
  QVariantMap m_schedule;
  Action m_lastAction;
  Action m_lastScore;
  qlonglong m_lastMessageId;
  QHash<QString, QVariant> m_images; // URL -> image value
};
