#include "TestUtil.h"

#include "Custom/scoreboard/LiveStats.h"
#include "Custom/state/Json.h"
#include "Custom/state/KvValue.h"

#include <QVariantList>
#include <QVariantMap>

#include "gtest/gtest.h"

using namespace TestUtil;

namespace
{
QVariantMap json(const char *text)
{
  bool ok = false;
  QString error;
  const QVariant value = Json::parse(QByteArray(text), &ok, &error);
  EXPECT_TRUE(ok) << error.toStdString();
  return value.toMap();
}

void tv(LiveStatsModel *model, const char *message, qint64 nowMs = 0)
{
  QString type;
  QString error;
  EXPECT_TRUE(model->applyTvMessage(json(message), nowMs, &type, &error)) << error.toStdString();
}

QString text(const QVariantMap &values, const char *key)
{
  const QString name = QString::fromUtf8("scoreboard.") + QString::fromUtf8(key);
  EXPECT_TRUE(values.contains(name)) << key;
  return Kv::textValue(values.value(name));
}

bool has(const QVariantMap &values, const char *key)
{
  return values.contains(QString::fromUtf8("scoreboard.") + QString::fromUtf8(key));
}

// Team 2 is the home team; players 1-3 of team 2, 1 of team 1.
const char kTeams[] =
    "{\"type\":\"teams\",\"messageId\":3,\"teams\":["
    "{\"teamNumber\":1,\"detail\":{\"teamName\":\"Visitors BC\",\"teamNickname\":\"Visitors\",\"teamCode\":\"VIS\","
    "\"isHomeCompetitor\":0},\"coach\":{\"firstName\":\"Ann\",\"familyName\":\"Coach\"},"
    "\"players\":[{\"pno\":1,\"shirtNumber\":\"9\",\"firstName\":\"Vic\",\"familyName\":\"Away\","
    "\"scoreboardName\":\"V. AWAY\",\"starter\":1,\"active\":1}]},"
    "{\"teamNumber\":2,\"detail\":{\"teamName\":\"Home City\",\"teamNickname\":\"City\",\"teamCode\":\"HOM\","
    "\"isHomeCompetitor\":1,\"logo\":{\"S1\":{\"url\":\"https://img.example/logo.png\"}}},"
    "\"players\":["
    "{\"pno\":1,\"shirtNumber\":\"12\",\"firstName\":\"Bob\",\"familyName\":\"Bench\",\"starter\":0,\"active\":0},"
    "{\"pno\":2,\"shirtNumber\":\"7\",\"firstName\":\"Sam\",\"familyName\":\"Smith\",\"TVName\":\"Sammy Smith\","
    "\"scoreboardName\":\"S. SMITH\",\"playingPosition\":\"G\",\"starter\":1,\"captain\":1,\"active\":1,"
    "\"photo\":\"https://img.example/7.png\"},"
    "{\"pno\":3,\"shirtNumber\":\"4\",\"firstName\":\"Al\",\"familyName\":\"Four\",\"starter\":1,\"active\":1}"
    "]}]}";

const char kBoxscore[] =
    "{\"type\":\"boxscore\",\"messageId\":5,\"teams\":["
    "{\"teamNumber\":2,\"total\":{"
    "\"team\":{\"sPoints\":21,\"sReboundsTotal\":10,\"sFieldGoalsMade\":8,\"sFieldGoalsAttempted\":16,"
    "\"sBenchPoints\":2,\"sBiggestLead\":6},"
    "\"players\":["
    "{\"pno\":2,\"sPoints\":15,\"sReboundsTotal\":3,\"sAssists\":4,\"sFieldGoalsMade\":6,\"sFieldGoalsAttempted\":9,"
    "\"sThreePointersMade\":1,\"sThreePointersAttempted\":3,\"sFoulsPersonal\":2,\"sMinutes\":12.5,"
    "\"sPlusMinusPoints\":5,\"sEfficiency\":17},"
    "{\"pno\":3,\"sPoints\":4,\"sReboundsTotal\":7,\"sAssists\":1},"
    "{\"pno\":1,\"sPoints\":2}]},"
    "\"periods\":[{\"period\":1,\"periodType\":\"REGULAR\",\"team\":{\"sPoints\":12}},"
    "{\"period\":2,\"periodType\":\"REGULAR\",\"team\":{\"sPoints\":9}},"
    "{\"period\":1,\"periodType\":\"OVERTIME\",\"team\":{\"sPoints\":0}}]},"
    "{\"teamNumber\":1,\"total\":{\"team\":{\"sPoints\":18},"
    "\"players\":[{\"pno\":1,\"sPoints\":18,\"sReboundsTotal\":7,\"sSteals\":2}]}}]}";
} // namespace

TEST(LiveStats, TeamsAndRoster)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("Home City"), text(v, "home.name"));
  EXPECT_EQ(QString::fromUtf8("City"), text(v, "home.shortName"));
  EXPECT_EQ(QString::fromUtf8("HOM"), text(v, "home.code"));
  EXPECT_EQ(QString::fromUtf8("VIS"), text(v, "away.code"));
  EXPECT_EQ(QString::fromUtf8("Ann Coach"), text(v, "away.coach"));

  EXPECT_EQ(QString::fromUtf8("Sammy Smith"), text(v, "home.player.7.name"));
  EXPECT_EQ(QString::fromUtf8("S. SMITH"), text(v, "home.player.7.scoreboardName"));
  EXPECT_EQ(QString::fromUtf8("Smith"), text(v, "home.player.7.familyName"));
  EXPECT_EQ(QString::fromUtf8("G"), text(v, "home.player.7.position"));
  EXPECT_EQ(Kv::makeBool(true), v.value(QString::fromUtf8("scoreboard.home.player.7.captain")));
  EXPECT_EQ(QString::fromUtf8("Bob Bench"), text(v, "home.player.12.name"));
  EXPECT_EQ(QString::fromUtf8("V. AWAY"), text(v, "away.player.9.scoreboardName"));

  // Starters first, then by shirt number.
  EXPECT_EQ(QString::fromUtf8("3"), text(v, "home.roster.count"));
  EXPECT_EQ(QString::fromUtf8("4"), text(v, "home.roster.1.number"));
  EXPECT_EQ(QString::fromUtf8("7"), text(v, "home.roster.2.number"));
  EXPECT_EQ(QString::fromUtf8("12"), text(v, "home.roster.3.number"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.onCourt.count"));
  EXPECT_EQ(QString::fromUtf8("Sammy Smith"), text(v, "home.onCourt.2.name"));
  EXPECT_FALSE(has(v, "home.onCourt.3.name"));

  // Stats before any boxscore.
  EXPECT_EQ(QString::fromUtf8("0"), text(v, "home.player.7.points"));
  EXPECT_EQ(QString::fromUtf8("00:00"), text(v, "home.player.7.minutes"));

  // No game state yet: the console's keys are left alone.
  EXPECT_FALSE(has(v, "clock"));
  EXPECT_FALSE(has(v, "period"));
  EXPECT_FALSE(has(v, "home.score"));
  EXPECT_EQ(3, model.lastMessageId());
}

TEST(LiveStats, Boxscore)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  tv(&model, kBoxscore);
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("15"), text(v, "home.player.7.points"));
  EXPECT_EQ(QString::fromUtf8("4"), text(v, "home.player.7.assists"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.player.7.fouls"));
  EXPECT_EQ(QString::fromUtf8("67"), text(v, "home.player.7.fgPct"));
  EXPECT_EQ(QString::fromUtf8("33"), text(v, "home.player.7.threePct"));
  EXPECT_EQ(QString::fromUtf8("0"), text(v, "home.player.7.ftPct"));
  EXPECT_EQ(QString::fromUtf8("12:30"), text(v, "home.player.7.minutes"));
  EXPECT_EQ(QString::fromUtf8("5"), text(v, "home.player.7.plusMinus"));
  EXPECT_EQ(QString::fromUtf8("17"), text(v, "home.player.7.efficiency"));
  EXPECT_EQ(QString::fromUtf8("15"), text(v, "home.roster.2.points"));
  EXPECT_EQ(QString::fromUtf8("18"), text(v, "away.player.9.points"));

  EXPECT_EQ(QString::fromUtf8("21"), text(v, "home.stats.points"));
  EXPECT_EQ(QString::fromUtf8("50"), text(v, "home.stats.fgPct"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.stats.benchPoints"));
  EXPECT_EQ(QString::fromUtf8("6"), text(v, "home.stats.biggestLead"));
  EXPECT_EQ(QString::fromUtf8("12"), text(v, "home.period.1.score"));
  EXPECT_EQ(QString::fromUtf8("9"), text(v, "home.period.2.score"));
  EXPECT_EQ(QString::fromUtf8("0"), text(v, "home.period.5.score"));
  // No status yet: the score comes from the boxscore.
  EXPECT_EQ(QString::fromUtf8("21"), text(v, "home.score"));
}

TEST(LiveStats, BoxscoreBeforeTeams)
{
  LiveStatsModel model;
  tv(&model, kBoxscore);
  tv(&model, kTeams);
  EXPECT_EQ(QString::fromUtf8("15"), text(model.values(0), "home.player.7.points"));
}

TEST(LiveStats, Leaders)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  tv(&model, kBoxscore);
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("Sammy Smith"), text(v, "home.leader.points.name"));
  EXPECT_EQ(QString::fromUtf8("7"), text(v, "home.leader.points.number"));
  EXPECT_EQ(QString::fromUtf8("15"), text(v, "home.leader.points.value"));
  EXPECT_EQ(QString::fromUtf8("4"), text(v, "home.leader.rebounds.number"));
  // Nobody has a block.
  EXPECT_EQ(QString::fromUtf8(""), text(v, "home.leader.blocks.name"));
  EXPECT_EQ(QString::fromUtf8("0"), text(v, "home.leader.blocks.value"));

  EXPECT_EQ(QString::fromUtf8("away"), text(v, "leader.points.team"));
  EXPECT_EQ(QString::fromUtf8("9"), text(v, "leader.points.number"));
  EXPECT_EQ(QString::fromUtf8("18"), text(v, "leader.points.value"));
  // 7 rebounds each: home wins the tie.
  EXPECT_EQ(QString::fromUtf8("home"), text(v, "leader.rebounds.team"));
}

TEST(LiveStats, Status)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  tv(&model,
     "{\"type\":\"status\",\"status\":\"INPROGRESS\",\"periodStatus\":\"STARTED\","
     "\"period\":{\"current\":2,\"periodType\":\"REGULAR\"},\"clock\":\"05:07:00\",\"shotClock\":\"00:14:00\","
     "\"clockRunning\":0,\"possession\":2,\"possessionArrow\":1,"
     "\"scores\":[{\"teamNumber\":1,\"score\":48,\"timeoutsRemaining\":1,\"fouls\":9,\"teamFouls\":4},"
     "{\"teamNumber\":2,\"score\":51,\"timeoutsRemaining\":2,\"fouls\":7,\"teamFouls\":3}]}");
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("INPROGRESS"), text(v, "match.status"));
  EXPECT_EQ(QString::fromUtf8("STARTED"), text(v, "match.periodStatus"));
  EXPECT_EQ(QString::fromUtf8("Q2"), text(v, "period"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "periodNumber"));
  EXPECT_EQ(QString::fromUtf8("05:07"), text(v, "clock"));
  EXPECT_EQ(QString::fromUtf8("14"), text(v, "shotClock"));
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.clockRunning")));
  EXPECT_EQ(QString::fromUtf8("home"), text(v, "possession"));
  EXPECT_EQ(QString::fromUtf8("away"), text(v, "possessionArrow"));
  EXPECT_EQ(QString::fromUtf8("51"), text(v, "home.score"));
  EXPECT_EQ(QString::fromUtf8("48"), text(v, "away.score"));
  EXPECT_EQ(QString::fromUtf8("3"), text(v, "home.fouls"));
  EXPECT_EQ(QString::fromUtf8("1"), text(v, "away.timeouts"));
}

TEST(LiveStats, Overtime)
{
  LiveStatsModel model;
  tv(&model, "{\"type\":\"status\",\"period\":{\"current\":2,\"periodType\":\"OVERTIME\"},\"clock\":\"04:00:00\"}");
  EXPECT_EQ(QString::fromUtf8("2OT"), text(model.values(0), "period"));
  EXPECT_EQ(QString::fromUtf8("6"), text(model.values(0), "periodNumber"));
}

TEST(LiveStats, RunningClock)
{
  LiveStatsModel model;
  tv(&model, "{\"type\":\"status\",\"clock\":\"01:00:50\",\"shotClock\":\"00:06:00\",\"clockRunning\":1}", 1000);
  EXPECT_TRUE(model.clockRunning());

  // 1.6 s later: 00:58.9, shot clock 4.4.
  QVariantMap v = model.clockValues(2600);
  EXPECT_EQ(QString::fromUtf8("58.9"), text(v, "clock"));
  EXPECT_EQ(QString::fromUtf8("4.4"), text(v, "shotClock"));

  // Stopped 2 s after the start without a clock: it stays where it was.
  tv(&model, "{\"type\":\"action\",\"actionType\":\"clock\",\"subType\":\"stop\"}", 3000);
  EXPECT_FALSE(model.clockRunning());
  v = model.clockValues(60000);
  EXPECT_EQ(QString::fromUtf8("58.5"), text(v, "clock"));
  EXPECT_EQ(QString::fromUtf8("4.0"), text(v, "shotClock"));

  // It never goes below zero.
  tv(&model, "{\"type\":\"action\",\"actionType\":\"clock\",\"subType\":\"start\"}", 60000);
  EXPECT_EQ(QString::fromUtf8("00.0"), text(model.clockValues(200000), "clock"));
  EXPECT_EQ(QString::fromUtf8("0.0"), text(model.clockValues(200000), "shotClock"));
}

TEST(LiveStats, Actions)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  tv(&model,
     "{\"type\":\"action\",\"actionNumber\":10,\"teamNumber\":2,\"pno\":2,\"period\":2,\"periodType\":\"REGULAR\","
     "\"clock\":\"05:07:00\",\"actionType\":\"3pt\",\"subType\":\"jumpshot\",\"success\":1,"
     "\"score1\":\"48\",\"score2\":\"54\"}");
  QVariantMap v = model.values(0);
  EXPECT_EQ(QString::fromUtf8("#7 S. SMITH 3PT made"), text(v, "lastAction.text"));
  EXPECT_EQ(QString::fromUtf8("3pt"), text(v, "lastAction.type"));
  EXPECT_EQ(QString::fromUtf8("home"), text(v, "lastAction.team"));
  EXPECT_EQ(QString::fromUtf8("05:07"), text(v, "lastAction.clock"));
  EXPECT_EQ(QString::fromUtf8("Q2"), text(v, "lastAction.period"));
  EXPECT_EQ(QString::fromUtf8("3"), text(v, "lastScore.points"));
  EXPECT_EQ(QString::fromUtf8("54"), text(v, "home.score"));
  EXPECT_EQ(QString::fromUtf8("48"), text(v, "away.score"));

  // A missed shot changes the last action, not the last score.
  tv(&model,
     "{\"type\":\"action\",\"actionNumber\":11,\"teamNumber\":1,\"pno\":1,\"actionType\":\"2pt\",\"success\":0}");
  v = model.values(0);
  EXPECT_EQ(QString::fromUtf8("#9 V. AWAY 2PT missed"), text(v, "lastAction.text"));
  EXPECT_EQ(QString::fromUtf8("#7 S. SMITH 3PT made"), text(v, "lastScore.text"));

  // A team action.
  tv(&model, "{\"type\":\"action\",\"actionNumber\":12,\"teamNumber\":1,\"actionType\":\"timeout\",\"subType\":\"full\"}");
  EXPECT_EQ(QString::fromUtf8("VIS Timeout"), text(model.values(0), "lastAction.text"));

  // Substitution: on court before the next boxscore.
  tv(&model,
     "{\"type\":\"action\",\"actionNumber\":13,\"teamNumber\":2,\"pno\":1,\"actionType\":\"substitution\","
     "\"subType\":\"in\"}");
  tv(&model,
     "{\"type\":\"action\",\"actionNumber\":14,\"teamNumber\":2,\"pno\":3,\"actionType\":\"substitution\","
     "\"subType\":\"out\"}");
  v = model.values(0);
  EXPECT_EQ(Kv::makeBool(true), v.value(QString::fromUtf8("scoreboard.home.player.12.onCourt")));
  EXPECT_EQ(Kv::makeBool(false), v.value(QString::fromUtf8("scoreboard.home.player.4.onCourt")));
  EXPECT_EQ(QString::fromUtf8("7"), text(v, "home.onCourt.1.number"));
  EXPECT_EQ(QString::fromUtf8("12"), text(v, "home.onCourt.2.number"));
  EXPECT_EQ(QString::fromUtf8("#4 Al Four Substitution out"), text(v, "lastAction.text"));
}

TEST(LiveStats, PlayByPlay)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  tv(&model,
     "{\"type\":\"playbyplay\",\"actions\":["
     "{\"actionNumber\":1,\"teamNumber\":2,\"pno\":2,\"actionType\":\"2pt\",\"success\":1},"
     "{\"actionNumber\":3,\"teamNumber\":2,\"pno\":3,\"actionType\":\"rebound\",\"subType\":\"defensive\"},"
     "{\"actionNumber\":2,\"teamNumber\":1,\"pno\":1,\"actionType\":\"freethrow\",\"success\":1},"
     "{\"actionNumber\":4,\"teamNumber\":1,\"pno\":1,\"actionType\":\"3pt\",\"success\":1,"
     "\"deleted\":\"2026-10-19 12:00:00\"}]}");
  const QVariantMap v = model.values(0);
  EXPECT_EQ(QString::fromUtf8("#4 Al Four Defensive rebound"), text(v, "lastAction.text"));
  EXPECT_EQ(QString::fromUtf8("#9 V. AWAY Free throw made"), text(v, "lastScore.text"));
  EXPECT_EQ(QString::fromUtf8("1"), text(v, "lastScore.points"));
}

TEST(LiveStats, MatchStandingsSchedule)
{
  LiveStatsModel model;
  tv(&model,
     "{\"type\":\"matchinformation\",\"match\":{\"matchName\":\"Game 1\",\"matchTime\":\"2026-10-19 19:30:00\","
     "\"roundDescription\":\"Final\"},\"competition\":{\"competitionName\":\"Cup\"},"
     "\"venue\":{\"venueName\":\"Arena\"}}");
  tv(&model,
     "{\"type\":\"standings\",\"standings\":["
     "{\"teamName\":\"B\",\"teamCode\":\"BBB\",\"position\":2,\"played\":3,\"won\":1,\"lost\":2,\"standingPoints\":4},"
     "{\"teamName\":\"A\",\"teamCode\":\"AAA\",\"position\":1,\"played\":3,\"won\":3,\"lost\":0,\"standingPoints\":6}]}");
  tv(&model,
     "{\"type\":\"schedule\",\"matches\":[{\"matchTime\":\"2026-10-20 18:00:00\",\"matchStatus\":\"SCHEDULED\","
     "\"competitors\":[{\"teamName\":\"Away\",\"teamCode\":\"AWY\",\"scoreString\":\"\",\"isHomeCompetitor\":0},"
     "{\"teamName\":\"Home\",\"teamCode\":\"HME\",\"scoreString\":\"\",\"isHomeCompetitor\":1}]}]}");
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("Game 1"), text(v, "match.name"));
  EXPECT_EQ(QString::fromUtf8("Final"), text(v, "match.round"));
  EXPECT_EQ(QString::fromUtf8("Cup"), text(v, "match.competition"));
  EXPECT_EQ(QString::fromUtf8("Arena"), text(v, "match.venue"));

  EXPECT_EQ(QString::fromUtf8("2"), text(v, "standings.count"));
  EXPECT_EQ(QString::fromUtf8("A"), text(v, "standings.1.team"));
  EXPECT_EQ(QString::fromUtf8("6"), text(v, "standings.1.points"));
  EXPECT_EQ(QString::fromUtf8("BBB"), text(v, "standings.2.code"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "standings.2.lost"));

  EXPECT_EQ(QString::fromUtf8("1"), text(v, "schedule.count"));
  EXPECT_EQ(QString::fromUtf8("Home"), text(v, "schedule.1.home"));
  EXPECT_EQ(QString::fromUtf8("AWY"), text(v, "schedule.1.awayCode"));
  EXPECT_EQ(QString::fromUtf8("SCHEDULED"), text(v, "schedule.1.status"));

  // A shorter table replaces the old one.
  tv(&model, "{\"type\":\"standings\",\"standings\":[{\"teamName\":\"C\",\"position\":1}]}");
  EXPECT_FALSE(has(model.values(0), "standings.2.team"));
}

TEST(LiveStats, Images)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  QStringList urls = model.imageUrls();
  urls.sort();
  EXPECT_EQ(QStringList() << QString::fromUtf8("https://img.example/7.png")
                          << QString::fromUtf8("https://img.example/logo.png"),
            urls);
  EXPECT_FALSE(has(model.values(0), "home.logo"));
  EXPECT_FALSE(has(model.values(0), "home.player.7.photo"));

  const QVariant image = Kv::makeImage(QString::fromUtf8("/tmp/does-not-exist.png"));
  model.setImage(QString::fromUtf8("https://img.example/7.png"), image);
  const QVariantMap v = model.values(0);
  EXPECT_EQ(image, v.value(QString::fromUtf8("scoreboard.home.player.7.photo")));
  EXPECT_EQ(image, v.value(QString::fromUtf8("scoreboard.home.roster.2.photo")));
}

TEST(LiveStats, RemovedPlayer)
{
  LiveStatsModel model;
  tv(&model, kTeams);
  EXPECT_TRUE(has(model.values(0), "home.player.12.name"));

  QByteArray teams(kTeams);
  teams.replace("\"shirtNumber\":\"12\"", "\"shirtNumber\":\"\"");
  tv(&model, teams.constData());
  const QVariantMap v = model.values(0);
  EXPECT_FALSE(has(v, "home.player.12.name"));
  EXPECT_FALSE(has(v, "home.roster.3.name"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.roster.count"));
}

TEST(LiveStats, Rejected)
{
  LiveStatsModel model;
  QString type;
  QString error;
  EXPECT_FALSE(model.applyTvMessage(json("{\"teams\":[]}"), 0, &type, &error));
  EXPECT_FALSE(error.isEmpty());

  // Unknown types are accepted and ignored.
  error.clear();
  EXPECT_TRUE(model.applyTvMessage(json("{\"type\":\"somethingnew\",\"x\":1}"), 0, &type, &error));
  EXPECT_EQ(QString::fromUtf8("somethingnew"), type);
  EXPECT_TRUE(model.values(0).isEmpty());

  error.clear();
  EXPECT_FALSE(model.applyDataJson(json("{\"clock\":\"00:00\"}"), 0, &error));
  EXPECT_FALSE(error.isEmpty());
}

TEST(LiveStats, GameStateKeys)
{
  EXPECT_TRUE(LiveStatsModel::isGameStateKey(QString::fromUtf8("scoreboard.clock")));
  EXPECT_TRUE(LiveStatsModel::isGameStateKey(QString::fromUtf8("scoreboard.away.score")));
  EXPECT_FALSE(LiveStatsModel::isGameStateKey(QString::fromUtf8("scoreboard.home.player.7.points")));
  EXPECT_FALSE(LiveStatsModel::isGameStateKey(QString::fromUtf8("scoreboard.home.name")));
  EXPECT_FALSE(LiveStatsModel::isGameStateKey(QString::fromUtf8("clock")));
}

TEST(LiveStats, DataJson)
{
  const char data[] =
      "{\"clock\":\"03:21\",\"period\":3,\"periodType\":\"REGULAR\",\"inOT\":0,"
      "\"tm\":{"
      "\"1\":{\"name\":\"Home City\",\"shortName\":\"City\",\"code\":\"HOM\",\"coach\":\"Ann Coach\","
      "\"logoT\":{\"url\":\"https://img.example/h.png\",\"size\":\"T1\"},"
      "\"score\":60,\"fouls\":2,\"timeouts\":1,\"p1_score\":20,\"p2_score\":22,\"p3_score\":18,"
      "\"tot_sPoints\":60,\"tot_sAssists\":14,\"tot_sFreeThrowsMade\":3,\"tot_sFreeThrowsAttempted\":4,"
      "\"pl\":{"
      "\"11\":{\"shirtNumber\":\"7\",\"name\":\"Sammy Smith\",\"firstName\":\"Sam\",\"familyName\":\"Smith\","
      "\"scoreboardName\":\"S. SMITH\",\"playingPosition\":\"G\",\"starter\":1,\"active\":1,"
      "\"sPoints\":22,\"sReboundsTotal\":5,\"sAssists\":6,\"sMinutes\":\"24:60\","
      "\"sTwoPointersMade\":8,\"sTwoPointersAttempted\":10,\"sFreeThrowsMade\":3,\"sFreeThrowsAttempted\":4,"
      "\"sTurnovers\":2,\"sFoulsPersonal\":1},"
      "\"12\":{\"shirtNumber\":\"10\",\"firstName\":\"Ten\",\"familyName\":\"Bench\",\"starter\":0,\"active\":0,"
      "\"sPoints\":4}}},"
      "\"2\":{\"name\":\"Visitors BC\",\"code\":\"VIS\",\"score\":55,"
      "\"pl\":{\"21\":{\"shirtNumber\":\"9\",\"name\":\"Vic Away\",\"starter\":1,\"active\":1,\"sPoints\":25}}}},"
      "\"pbp\":["
      "{\"actionNumber\":40,\"tno\":2,\"shirtNumber\":\"9\",\"scoreboardName\":\"V. AWAY\",\"actionType\":\"steal\","
      "\"gt\":\"03:21\",\"period\":3,\"periodType\":\"REGULAR\"},"
      "{\"actionNumber\":39,\"tno\":1,\"shirtNumber\":\"7\",\"scoreboardName\":\"S. SMITH\",\"actionType\":\"2pt\","
      "\"success\":1,\"gt\":\"03:30\",\"period\":3,\"periodType\":\"REGULAR\"}],"
      "\"othermatches\":[{\"team1Name\":\"A\",\"team2Name\":\"B\",\"team1Score\":10,\"team2Score\":12,"
      "\"team1\":{\"teamCode\":\"AAA\"},\"team2\":{\"teamCode\":\"BBB\"},\"period\":1,\"periodType\":\"REGULAR\","
      "\"clock\":\"02:00\"}]}";

  LiveStatsModel model;
  QString error;
  ASSERT_TRUE(model.applyDataJson(json(data), 0, &error)) << error.toStdString();
  const QVariantMap v = model.values(0);

  EXPECT_EQ(QString::fromUtf8("03:21"), text(v, "clock"));
  EXPECT_EQ(QString::fromUtf8("Q3"), text(v, "period"));
  EXPECT_EQ(QString::fromUtf8("Home City"), text(v, "home.name"));
  EXPECT_EQ(QString::fromUtf8("Ann Coach"), text(v, "home.coach"));
  EXPECT_EQ(QString::fromUtf8("60"), text(v, "home.score"));
  EXPECT_EQ(QString::fromUtf8("55"), text(v, "away.score"));
  EXPECT_EQ(QString::fromUtf8("2"), text(v, "home.fouls"));
  EXPECT_EQ(QString::fromUtf8("1"), text(v, "home.timeouts"));
  EXPECT_FALSE(has(v, "away.fouls"));
  EXPECT_EQ(QString::fromUtf8("22"), text(v, "home.period.2.score"));
  EXPECT_EQ(QString::fromUtf8("14"), text(v, "home.stats.assists"));
  EXPECT_EQ(QString::fromUtf8("75"), text(v, "home.stats.ftPct"));

  EXPECT_EQ(QString::fromUtf8("Sammy Smith"), text(v, "home.player.7.name"));
  EXPECT_EQ(QString::fromUtf8("22"), text(v, "home.player.7.points"));
  EXPECT_EQ(QString::fromUtf8("25:00"), text(v, "home.player.7.minutes"));
  EXPECT_EQ(QString::fromUtf8("80"), text(v, "home.player.7.twoPct"));
  // No sEfficiency: 22+6+5 - (2+1+2 missed 2PT+1 missed FT).
  EXPECT_EQ(QString::fromUtf8("27"), text(v, "home.player.7.efficiency"));
  EXPECT_EQ(QString::fromUtf8("Ten Bench"), text(v, "home.roster.2.name"));
  EXPECT_EQ(QString::fromUtf8("1"), text(v, "home.onCourt.count"));

  EXPECT_EQ(QString::fromUtf8("away"), text(v, "leader.points.team"));
  EXPECT_EQ(QString::fromUtf8("Vic Away"), text(v, "leader.points.name"));

  EXPECT_EQ(QString::fromUtf8("#9 V. AWAY Steal"), text(v, "lastAction.text"));
  EXPECT_EQ(QString::fromUtf8("away"), text(v, "lastAction.team"));
  EXPECT_EQ(QString::fromUtf8("#7 S. SMITH 2PT made"), text(v, "lastScore.text"));

  EXPECT_EQ(QString::fromUtf8("1"), text(v, "schedule.count"));
  EXPECT_EQ(QString::fromUtf8("A"), text(v, "schedule.1.home"));
  EXPECT_EQ(QString::fromUtf8("12"), text(v, "schedule.1.awayScore"));
  EXPECT_EQ(QString::fromUtf8("Q1 02:00"), text(v, "schedule.1.status"));

  QStringList urls = model.imageUrls();
  EXPECT_EQ(QStringList() << QString::fromUtf8("https://img.example/h.png"), urls);
}
