# FIBA LiveStats integration

How Natron reads FIBA LiveStats, why it is built this way, and how it was
tested. To set it up and for every key, see the
[README's LiveStats section](README.md#fiba-livestats-feed).

- [What it does](#what-it-does)
- [Data sources](#data-sources)
- [Design](#design)
- [The console and LiveStats together](#the-console-and-livestats-together)
- [Reconnecting and restarts](#reconnecting-and-restarts)
- [Photos and logos](#photos-and-logos)
- [Files](#files)
- [Verification](#verification)
- [Limits and risks](#limits-and-risks)
- [Possible next steps](#possible-next-steps)

## What it does

The Ultra Score console feed (`ScoreboardFeed`) gives the score, the clocks,
fouls, timeouts and points and fouls per shirt number, with no names. FIBA
LiveStats, the statisticians' software, has everything else. The LiveStats
feed writes it into the same state store, as `scoreboard.*` keys:

| Group | Examples |
|---|---|
| Match | `match.name`, `match.status`, `match.competition`, `match.venue` |
| Game state | `clock`, `shotClock`, `period`, `home.score`, `home.timeouts` |
| Teams | `home.name`, `home.code`, `home.logo`, `home.stats.rebounds`, `home.period.2.score` |
| Players, by shirt number | `home.player.7.name`, `.photo`, `.points`, `.fgPct`, `.minutes`, `.efficiency` |
| Lists | `home.roster.<i>.*`, `home.onCourt.<1-5>.*` |
| Leaders | `home.leader.points.name`, `leader.rebounds.value` |
| Play by play | `lastAction.text` (`#7 S. SMITH 3PT made`), `lastScore.points` |
| Other games | `standings.<i>.team`, `schedule.<i>.home` |

Anything already bound to a key works with these as it does with the
console's keys: Text and Read nodes, live NDI scenes, the dashboard,
`GET /state` and `GET /events`.

## Data sources

| | TV feed | Online data.json |
|---|---|---|
| Where | the LiveStats computer in the venue | `https://fibalivestats.dcd.shared.geniussports.com/data/<match id>/data.json` |
| Transport | TCP, default port 7677, one JSON message per line (CRLF) | HTTPS, polled (default every 2 s) |
| Documented | yes: [Genius Sports TV feed](https://developer.geniussports.com/livestats/tvfeed/index_basketball.html) | no: the format comes from a live match |
| Needs | the TV feed option on in LiveStats, a network path to that computer | internet access, OpenSSL for Qt |
| Players named by | `pno` (number in the match), mapped through the `teams` message | shirt number and names in each entry |
| Has | every message type below | teams, players, team totals, period scores, play by play, other games |

TV feed message types read: `teams`, `boxscore`, `status`, `action`,
`playbyplay`, `matchinformation`, `setup`, `standings`, `schedule`, and
`ping` (counted, nothing to show). Unknown types and fields are ignored, as
the specification asks.

## Design

```
                 ┌──────────────────────── LiveStatsFeed (QObject, /livestats) ─────────────────────────┐
LiveStats TCP ──►│ socket, line splitting, parameters message, reconnect, restart check                 │
data.json ──────►│ polling, unchanged bodies skipped                                                    │
                 │        │                                                                             │
                 │        ▼                                                                             │
                 │  LiveStatsModel (no I/O) ──► values(now) ──► console filter ──► Scoreboard::applyChanged ──► StateStore
                 │        ▲                                          │                                   │
                 │  image downloads ──► setImage()          stale keys removed                           │
                 └──────────────────────────────────────────────────────────────────────────────────────┘
```

- **The model is separate from the I/O.** `LiveStatsModel` turns messages
  into keys and does no networking. So it is tested on its own with plain
  JSON, and both sources share the same key-building code.
- **All the keys are rebuilt from the model.** After each message the model
  produces the whole key set: rosters, mirrors, leaders, the last action.
  `Scoreboard::applyChanged` then writes only the values that differ from
  the store, so bound nodes and scenes re-render only for real changes.
  Rebuilding everything keeps the derived keys (leaders, on-court slots)
  consistent without tracking what each message affects.
- **Stale keys are removed.** The feed remembers the keys it wrote. A key
  the model no longer has (a player who left the roster, a shorter table)
  is removed from the store. Game-state keys are never removed, because the
  console may own them.
- **The model computes some values itself**, the same way for both
  sources:
  - percentages, rounded to whole percent from made / attempted
  - efficiency, by the specification's formula, when it isn't sent
  - minutes, as `MM:SS` from LiveStats' decimal minutes, or normalized from
    data.json's `MM:SS` (it sends `199:60`)
  - leaders, from the player stats
- **Clocks run between messages.** LiveStats sends the clock when it
  changes state, not every tenth of a second. The model stores the clock,
  when it was set and whether it runs. A 100 ms timer in the feed writes
  `clock` and `shotClock` while it runs, and stops at 0.
- **The text formats are shared.** `ScoreboardCommon` holds the period
  text (`Q1`, `OT`, `2OT`), the clock rules (`MM:SS`, then `SS.t` in the
  last minute), the shot clock rules (`S.t` in the last 5 s) and
  `applyChanged`. Both feeds use it, so the same game looks the same
  whichever feed wrote it.
- **Home and away** come from `isHomeCompetitor` on the TV feed (else team
  1 is home). On data.json, team 1 is always home.

## The console and LiveStats together

Both feeds can run at once:

| Keys | Written by |
|---|---|
| `clock`, `shotClock`, `clockRunning`, `period`, `periodNumber`, `possession`, `<t>.score`, `<t>.fouls`, `<t>.timeouts` | the console while it sent a packet in the last 5 s (`LiveStatsFeed::consoleActive`), otherwise LiveStats |
| `<t>.player.*` | LiveStats while it has players (`ScoreboardFeed::setSkipPlayerKeys`), otherwise the console |
| everything else | LiveStats only |

Why: the console is the official clock and score and is the fastest. The
statisticians' box score has names and full stats, and the console's
per-player points could disagree with it. With each key owned by one feed
at a time, the two never overwrite each other.

`<t>.timeouts` is timeouts **left** from LiveStats. From the console it is
whatever the console sends, taken or left depending on its setting.

## Reconnecting and restarts

TV feed connection handling (`LiveStatsFeed`):

1. **On connect:** sends the `parameters` message, which LiveStats requires
   within 5 seconds: `types: se,ac,mi,te,st,box,pbp,sd,sc`,
   `playbyplayOnConnect: 1`.
2. **On a drop**, or after 30 s without data (LiveStats pings every 10 s),
   or a connection attempt hanging for 10 s: reconnects every 3 s.
3. **On reconnect:** asks for what it missed, with
   `fromMessageId: <last id + 1>`.
4. **On a restart:** if the first numbered message after that is **lower**
   than the id asked for, LiveStats was restarted and numbers from 1 again.
   It would never resend the missed messages. The feed drops that message,
   forgets the old id, and reconnects at once without `fromMessageId`, so it
   gets the full state again. The keys stay as they were meanwhile.

The online mode has no connection: each poll is independent. A failed poll
sets `error` and `connected: false` until the next one succeeds.

## Photos and logos

- They are downloaded into `livestats-images/`, next to the dashboard's
  `state.json`, and set as image values (`<t>.logo`, `<t>.player.<no>.photo`
  and the mirrors).
- LiveStats' photo links are signed and expire after a few hours. Files are
  named by the link without its query string, so a re-signed link to the
  same photo finds the file it already has.
- A failed download isn't retried until `{"reset": true}`. Otherwise a
  broken link would be fetched again with every message.

## Files

| File | What |
|---|---|
| `Custom/scoreboard/LiveStats.h/.cpp` | `LiveStatsModel`: messages and data.json into keys |
| `Custom/scoreboard/LiveStatsFeed.h/.cpp` | TCP client, polling, images, the console priority rules, `/livestats` |
| `Custom/scoreboard/ScoreboardCommon.h/.cpp` | period and clock text, `applyChanged`: shared with `ScoreboardFeed` |
| `Custom/scoreboard/ScoreboardFeed.h/.cpp` | adds `lastPacketTime()` and `setSkipPlayerKeys()` |
| `Custom/scoreboard/simulate_livestats.py` | simulator: a game that plays itself, as the TV feed or as data.json |
| `Custom/scoreboard/LiveStats.postman_collection.json` | Postman: start and stop the feed, send messages by hand |
| `Custom/tests/LiveStats_Test.cpp` | model tests (15) |
| `Custom/tests/LiveStatsFeed_Test.cpp` | feed tests (9) |
| `Custom/server/HttpServer.cpp` | `/livestats` on the API reference page |
| `Gui/GuiApplicationManager.cpp`, `GuiApplicationManagerPrivate.h` | starts the feed from `NATRON_LIVESTATS_*`, adds the route, destroys it before the console feed |
| `Custom/CMakeLists.txt`, `Gui/Gui.pro` | the new sources (the qmake build lists them by name) |

Startup settings: `NATRON_LIVESTATS_HOST`, `NATRON_LIVESTATS_PORT`
(default 7677), `NATRON_LIVESTATS_MATCH`, `NATRON_LIVESTATS_INTERVAL`
(default 2 s), `NATRON_LIVESTATS_URL` (another server for data.json, such as
the simulator). Or at runtime with `POST /livestats`.

## Verification

Done on 2026-10-19, branch `livestats-feed`.

### Unit tests

`CustomTests`, built and run with Qt 6 both standalone and from the full
Natron build: **261 / 261 passed**, of which 24 are new:

- **Model:** rosters, roster order and on-court lists; the box score and
  computed percentages, minutes and efficiency; a boxscore that arrives
  before the teams; leaders and ties; status and overtime; the running and
  stopped clock; actions, substitutions, play by play with deleted actions;
  match info, standings and schedule; images; removed players; rejected
  input; data.json.
- **Feed:** writing and removing keys; console priority both ways; the TV
  feed over TCP (messages split across reads, a bad line, reconnecting with
  `fromMessageId`); a restarted LiveStats; reconnecting after a refused
  connection; data.json polling and the base URL; image download and cache;
  the `/livestats` HTTP API.

The existing tests still pass, including `ScoreboardFeed`'s after it moved
to the shared helpers.

### Full build

`scripts/natron-build.sh build --qt6 --tests --committed`: built in 3m14s.
None of the new or changed files produced warnings. **Natron's unit tests:
28 / 28 passed.**

Valgrind reported 5 "uninitialised value" errors during Python startup,
before the tests, in code it couldn't name. Natron's tests don't load the
scoreboard code. The only earlier tested build in the cache was Qt 4, with
0 errors, so they most likely come from the Qt 6 / Python 3.10 SDK. A Qt 6
build without this branch hasn't been checked.

### End to end

The built Natron (Qt 6, offscreen, in the `natron-sdk-qt6` image) against
`simulate_livestats.py tv --speed 10`:

| Check | Result |
|---|---|
| connects and sends `parameters` | yes; 36 messages read, 0 rejected, 1,715 keys |
| all groups present | match, game state, teams, players, roster, leaders, last action, standings, schedule |
| live updates | the clock went from 08:32 to 07:59 in 3 s at 10× speed; score 8–0 to 10–3 |
| console priority | with an Ultra Score packet: clock 05:07, score 51–48 from the console, names from LiveStats. 6 s after the console stopped: LiveStats' clock and score again |
| simulator killed | `connected: false`, "The remote host closed the connection" |
| simulator restarted | reconnected with `fromMessageId: 135`, data flowing again |

The restart detection was added after this run. It is covered by
`LiveStatsFeedTest.TvFeedRestarted` and the second full build, not by an end
to end run.

### Not verified

- A real LiveStats computer: everything ran against the simulator, which
  follows the specification.
- The online mode against the real server end to end: its format was taken
  from a real match, and it is tested with that format in the unit tests and
  a local HTTP server.
- Photo and logo downloads end to end: the simulator sends none. Covered by
  a unit test.
- The installers: not built. Whether they bundle OpenSSL for the online
  mode isn't checked.

## Limits and risks

- **The TV feed is a paid LiveStats option**, and the statistician has to
  enable it.
- **data.json is undocumented.** It can change without notice, and it is a
  few seconds behind. The parser tolerates missing fields, but renamed ones
  would show as empty values.
- **Security:** like the console's UDP, the TV feed isn't authenticated.
  Keep it on the production network.
- **Home on data.json** is always team 1. If that's wrong for a match, swap
  the bindings.
- **Shirt numbers must be unique per team.** Two players with the same
  number share keys; the roster lists still show both.
- **Saved values:** like the console's, the keys are saved in `state.json`.
  After a restart they show the last game until new data comes, or until
  `{"reset": true}`.

## Possible next steps

- An end to end run against a real LiveStats TV feed, and against a live
  online match.
- Dashboard controls for the feed (host or match id, start, stop, status),
  instead of environment variables and `POST /livestats`.
- Check, and if needed bundle, OpenSSL in the Linux and Windows installers.
- Player photos in the simulator, to test images end to end.
