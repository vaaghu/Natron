# FIBA scoreboard feed (Ultra Score)

Natron reads the live game data straight from the arena's scoreboard
console. The score, game clock, shot clock, fouls, timeouts, possession and
player stats then update the graphics by themselves, with no one typing them
in during the game.

```
Ultra Score console ──UDP :2800──► Natron ──► state store keys (scoreboard.*)
                                              ├─► Text nodes bound to the keys
                                              ├─► live NDI scenes (re-rendered)
                                              └─► HTTP API: /state, /events, /scoreboard
```

The console broadcasts binary "Data Out" packets on the venue network.
Natron decodes them into keys of its state store, the same store the
dashboard and `POST /state` write to. Anything bound to a key updates when
the console changes it.

- [Quick start](#quick-start)
- [Setting up the network](#setting-up-the-network)
- [Showing the data in a graphic](#showing-the-data-in-a-graphic)
- [Key reference](#key-reference)
- [Testing without a console](#testing-without-a-console)
- [HTTP API](#http-api)
- [Troubleshooting](#troubleshooting)
- [Packet reference](#packet-reference)
- [Limitations](#limitations)
- [Source files](#source-files)

## Quick start

1. Start Natron. The feed starts with it and listens on UDP port 2800.
2. Connect the Natron machine to the same network as the scoreboard console,
   and set the console's Data Out to UDP port 2800 (broadcast, or this
   machine's IP address).
3. Check that packets arrive:
   ```bash
   curl http://localhost:3000/scoreboard
   ```
   `"listening": true` and a growing `"packets"` count mean the console's
   data is being received. `"values"` shows what was decoded.
4. In a project, bind a Text node to `scoreboard.home.score` (see
   [Showing the data in a graphic](#showing-the-data-in-a-graphic)).

No console at hand? Run the simulator instead of step 2:
`python3 Custom/scoreboard/simulate_ultrascore.py play`.

## Setting up the network

| Setting | Default | Change it with |
|---|---|---|
| UDP port the feed listens on | `2800` | `NATRON_SCOREBOARD_PORT=<port>` when starting Natron |
| Turn the feed off | on | `NATRON_SCOREBOARD_PORT=0` |
| HTTP API port (status, testing) | `3000` | `NATRON_HTTP_PORT=<port>` |

- **Interfaces:** the feed listens on every network interface, so it
  receives the console's broadcasts and packets sent directly to the
  machine's IP address.
- **Firewall:** let UDP port 2800 in on the Natron machine. On Linux with
  ufw: `sudo ufw allow 2800/udp`. On Windows, allow Natron through Windows
  Defender Firewall for private networks.
- **Docker:** publish the port with `-p 2800:2800/udp`. Broadcasts don't
  cross Docker's bridge network: either send to the host's IP address, or
  run the container with `--network host`.
- **Sharing the port:** the port is opened in shared mode, so another
  program on the same machine (for example the FIBA web graphics backend)
  can listen to the console at the same time.
- **Security:** anyone who can send UDP to the machine can change the
  score, a limit of the console's protocol. Keep the console and Natron on
  the production network, not on a guest or public one.

## Showing the data in a graphic

Keys are bound per node, in the **State** tab of a Text node's properties
panel:

1. Open the Text node's properties and go to the **State** tab.
2. In **Key**, type or select a key, for example `scoreboard.home.score`.
3. The node's text is replaced with the key's value now and every time it
   changes. **Value** shows what the node currently receives. "Key not in
   store yet" just means no packet has set it yet.

Numbers show as plain integers (`42`), and on/off values as `true` /
`false`.

Examples:

| Graphic element | Key |
|---|---|
| Home / away score | `scoreboard.home.score`, `scoreboard.away.score` |
| Game clock | `scoreboard.clock` |
| Quarter | `scoreboard.period` |
| Shot clock | `scoreboard.shotClock` |
| Team fouls | `scoreboard.home.fouls`, `scoreboard.away.fouls` |
| Points of home #7 | `scoreboard.home.player.7.points` |

**Scenes:**

- Live NDI scenes re-render when a key used by one of their projects
  changes. Only projects that use the changed key are re-rendered, and
  changes arriving close together are grouped into one render.
- The scene's key renaming (the "Uses key" table) works here too. A
  template bound to `scoreboard.home.player.7.points` can read another
  player's key in another scene.

**Clock updates:** the console sends the clock about 10 times a second, but
`scoreboard.clock` only changes when the text shown changes: once a second,
and every tenth in the last minute. A scene that shows the clock re-renders
at that rate, so keep clock graphics light, or use them in scenes that
aren't live-rendered.

## Key reference

A key is written only when its value changes. Team keys exist for `home` and
`away`. On the console, home is team A and away is team B.

### Game

| Key | Type | Value |
|---|---|---|
| `scoreboard.period` | text | `Q1` … `Q4`, then `OT`, `2OT`, `3OT`, … (empty before the game) |
| `scoreboard.periodNumber` | number | 1–4, 5 = first overtime, … |
| `scoreboard.clock` | text | `MM:SS` (`09:55`); in the last minute `SS.t` (`45.3`); empty when the console blanks it |
| `scoreboard.clockRunning` | on/off | the game clock is running |
| `scoreboard.shotClock` | text | whole seconds (`24`, `14`); in the last 5 seconds `S.t` (`4.2`); empty when off |
| `scoreboard.timeout` | on/off | a timeout is being timed |
| `scoreboard.timeoutSeconds` | number | seconds left in the timeout, 0 when none |
| `scoreboard.possession` | text | `home`, `away`, or empty |

### Teams

| Key | Type | Value |
|---|---|---|
| `scoreboard.home.score` | number | points |
| `scoreboard.home.fouls` | number | team fouls (as counted by the console) |
| `scoreboard.home.timeouts` | number | the console's timeout count. Check your console's setting: some send timeouts taken, others timeouts left |

### Players

Players are identified by **shirt number** (as entered on the console), not
by name. Match the numbers with the team roster.

| Key | Type | Value |
|---|---|---|
| `scoreboard.home.player.<no>.points` | number | points of the player wearing `<no>` |
| `scoreboard.home.player.<no>.fouls` | number | personal fouls |
| `scoreboard.home.player.<no>.onCourt` | on/off | the player is on the court |

Player keys appear once the console sends player data. Some consoles only
send it if it is turned on in their Data Out settings.

## Testing without a console

Three tools come with the feed, all in `Custom/scoreboard/`:

| Tool | Sends | Use it to test |
|---|---|---|
| `scoreboard_console.sh` | real UDP packets, broadcast to the LAN; you run the game from a keyboard menu | a game by hand, like the operator at the console |
| `simulate_ultrascore.py` | real UDP packets, like the console | the whole path: network, firewall, decoding, graphics |
| `UltraScore.postman_collection.json` | the same packets over HTTP (`POST /scoreboard`) | decoding and graphics from Postman (Postman can't send UDP) |

### Keyboard console (bash, UDP broadcast)

A menu in the terminal that stands in for the console: you change the
values and run the clocks with single keys. It broadcasts the packets like
the real console: general data 10 times a second, and player data once a
second when players are entered.

```bash
Custom/scoreboard/scoreboard_console.sh                 # broadcast to 255.255.255.255:2800
Custom/scoreboard/scoreboard_console.sh -H 127.0.0.1    # this machine only
Custom/scoreboard/scoreboard_console.sh -H 192.168.1.20 -p 2800 -r 10
```

Requirements:
- bash 4.4 or newer, and python3 or socat. Plain bash can't send
  broadcasts, so one of them sends the datagrams.
- On macOS, the system bash is 3.2: install a newer one
  (`brew install bash`).

```
ULTRA SCORE CONSOLE  UDP -> 255.255.255.255:2800  (python3, 10 pkt/s)  sent: 412

   HOME     5  <-   Q1  09:57      AWAY   2
   fouls 1   timeouts 2    shot 14   RUNNING       fouls 0   timeouts 1
```

| Key | Does |
|---|---|
| `space` | start / stop the game clock (also ends a timeout) |
| `c` | set the game clock: `09:55`, `0:45.3` or `45.3` |
| `n` / `b` | next period (resets the clock to 10:00, or 5:00 in overtime) / previous period |
| `r` / `e` | shot clock to 24 / 14 |
| `s` / `o` | set the shot clock (`4.2`) / turn it off and on |
| `1` `2` `3` / `4` | home +1 +2 +3 / −1 |
| `7` `8` `9` / `0` | away +1 +2 +3 / −1 |
| `f` / `F`, `g` / `G` | home / away team foul +1 (stops the clock) / −1 |
| `t` / `y` | home / away timeout: 60 s, one timeout fewer, clock stopped |
| `T` / `Y` | home / away one timeout more |
| `p` | switch possession |
| `h` / `a` | type the home / away score |
| `k` | player stats: `h 7 12 2 y` = home #7, 12 points, 2 fouls, on court; `h 7 -` removes the player |
| `l` | list the players entered |
| `x` / `q` | new game / quit |

Behaviour:
- **Clocks:** the game clock and shot clock run in real time. They stop at
  0, and the timeout timer counts down by itself.
- **Baskets:** a basket of 2 or 3 gives the ball to the other team and
  resets the shot clock to 24.
- **Third quarter:** team fouls reset, and each team gets 3 timeouts.
- **Overtime:** team fouls reset.
- **Typing a value:** no packets are sent while you type at a prompt
  (`c`, `s`, `h`, `a`, `k`). The clocks keep their time and catch up
  afterwards.
- **Send errors:** shown at the bottom of the screen, for example "Network
  is unreachable" when the machine has no network for the broadcast.

### Console simulator (UDP)

Python 3, nothing to install. It sends the score and clock packet 10 times a
second and the player packets once a second, like the console.

```bash
# A game that plays itself: running clock, baskets, fouls, timeouts,
# substitutions and periods. --speed 10 fast-forwards it.
python3 Custom/scoreboard/simulate_ultrascore.py play

# You type the events while the clock runs.
python3 Custom/scoreboard/simulate_ultrascore.py interactive

# One raw packet.
python3 Custom/scoreboard/simulate_ultrascore.py send FF FE 01 01 01 12 00 02 11 09 37 05 14 07 FF 00 00 0F 07 03 02 01 02 01 00 FD FC
```

Commands in interactive mode (team `h` = home, `a` = away):

| Command | Does |
|---|---|
| `h2` `h3` `h1` / `a2` `a3` `a1` | basket or free throw |
| `h-1` / `a-1` | score correction |
| `hf` / `af` | team foul (stops the clock) |
| `ht` / `at` | timeout, 60 seconds |
| `hs` / `as` | substitution |
| `p` | switch possession |
| `start` / `stop` | game clock |
| `n` | next period |
| `clock 0:45.3` | set the game clock |
| `shot 4.2` | set the shot clock |
| `raw FF FE …` | send a raw packet |
| `q` | quit |

Where to send:

| Option | Sends to |
|---|---|
| (none) | `127.0.0.1:2800`, this machine |
| `--host 192.168.1.20` | another machine, or a Docker container |
| `--broadcast` | `255.255.255.255`, the whole local network, like a console |
| `--port 2900` | another port (start Natron with `NATRON_SCOREBOARD_PORT=2900`) |
| `--rate 5` | fewer general packets per second |

### Postman collection

Import `Custom/scoreboard/UltraScore.postman_collection.json` in Postman.

| Folder | Contains |
|---|---|
| Feed | health check, feed status and values, all state keys |
| Game | Tip-off (reset), Home/Away +1/+2/+3, score correction, team fouls, clock −1 s, last minute, shot clock 4.2, start/stop clock, timeout, possession, next period |
| Players | player points and fouls, players on court (edit the lists in the request's Pre-request script) |
| Raw packets | a fixed packet, the same packet inverted, several packets at once, and a bad packet (expected 400) |

- **How the Game requests work:** they keep the game in collection
  variables (`homeScore`, `minutes`, `period`, …) and build a real packet
  from them. Start with **Tip-off**, then send events in any order.
- **Running clock:** run **Clock −1 s** in the Collection Runner with a
  1000 ms delay.
- **Tests:** every request checks that Natron decoded what was sent.
- **Settings:** set `baseUrl` if Natron's HTTP API isn't on
  `http://localhost:3000`, and `token` if Natron was started with
  `NATRON_HTTP_TOKEN`.

## HTTP API

Natron's HTTP API (port 3000) has one endpoint for the feed. The reference
page at `http://localhost:3000/` lists all endpoints. With
`NATRON_HTTP_TOKEN` set, requests need `Authorization: Bearer <token>`.

### `GET /scoreboard`

The feed's status and the current `scoreboard.*` values.

```json
{
  "listening": true,
  "port": 2800,
  "error": "",
  "packets": 5321,
  "rejected": 0,
  "lastRejection": "",
  "lastPacket": "2026-10-14T19:32:05",
  "lastSender": "192.168.1.50:50123",
  "values": {
    "scoreboard.home.score": {"type": "number", "value": 15},
    "scoreboard.clock": {"type": "text", "value": "09:55"},
    "...": "..."
  }
}
```

| Field | Meaning |
|---|---|
| `listening` / `error` | whether the UDP port is open, and why not |
| `packets` / `rejected` | packets decoded / packets that were not Ultra Score data |
| `lastRejection` | why the last rejected packet was rejected |
| `lastPacket` / `lastSender` | when the last packet came, and from which address (`http` when sent through `POST /scoreboard`) |

### `POST /scoreboard`

Decodes packets given in hex, exactly as if they came over UDP. Spaces,
`-`, `:` and `,` between bytes are allowed.

```bash
curl -X POST http://localhost:3000/scoreboard \
     -H 'Content-Type: application/json' \
     -d '{"packet": "FF FE 01 01 01 12 00 02 11 09 37 05 14 07 FF 00 00 0F 07 03 02 01 02 01 00 FD FC"}'
```

```json
{"type": "general", "changed": ["scoreboard.home.score", "..."], "values": {"...": "..."}}
```

- **Several packets:** `{"packets": ["FF FE …", "FF FE …"]}` decodes them in
  order and answers `{"results": [...]}`.
- **Errors:** a packet that isn't valid hex rejects the whole request. A
  packet that isn't Ultra Score data answers 400 with the reason.

### Following the score live

The feed's keys are ordinary state keys, so `GET /state` returns them, and
`GET /events` (server-sent events) streams each change as it happens:

```js
const events = new EventSource('http://localhost:3000/events');
events.addEventListener('value', (e) => {
  const { key, value } = JSON.parse(e.data);
  if (key.startsWith('scoreboard.')) console.log(key, value.value);
});
```

## Troubleshooting

| Symptom | Check |
|---|---|
| `GET /scoreboard` doesn't answer | Natron isn't running, or its HTTP API is on another port (`NATRON_HTTP_PORT`) |
| `"listening": false` with an `error` | another program holds UDP 2800 without sharing it. Close it, or use `NATRON_SCOREBOARD_PORT` and set the console to that port |
| `"packets": 0` | the packets don't reach the machine. Check the firewall (UDP 2800 in), that the console and Natron are on the same network or VLAN, the console's Data Out port, and Docker networking. Try `simulate_ultrascore.py send …` on the Natron machine itself to rule out Natron |
| `"rejected"` grows | something else sends to port 2800, or the console is set to another protocol. `lastRejection` says why |
| Values in `/scoreboard` but the graphic doesn't change | the Text node is bound to another key (State tab: compare **Key** with the key in `values`), or the scene renames that key. For NDI scenes, check that the scene is live |
| Score stuck after the console was corrected | Natron only follows the console. Correct it on the console |
| Home and away swapped | the console's team A is `home`. Swap the bindings in the graphic, or the teams on the console |

## Packet reference

Ultra Score Data Out packets, as decoded by the feed. Multi-byte length is
little-endian. A packet with every byte inverted (starting `00 01`) is
accepted too.

```
FF FE  <id> 01  <system>  <length lo> <length hi>  <body …>  FD FC
 0  1    2   3      4          5           6        7 …      foot
```

| Packet id (byte 2) | Content | Status |
|---|---|---|
| `01` | general data | decoded |
| `02` / `03` | player data, home / away | decoded |
| `04` / `05` | penalties | ignored (not used in basketball) |
| `06` / `07` | players on court, home / away | decoded |

### General data (`FF FE 01 01`, 27 bytes)

| Byte | Content | Key |
|---|---|---|
| 7 | period: 1–4, 5 = OT, 6 = 2OT, … | `period`, `periodNumber` |
| 8 | status, bit 0: clock running | `clockRunning` |
| 9, 10, 11 | game clock: minutes, seconds, tenths (all `FF`: blank) | `clock` |
| 12, 13 | shot clock: seconds, tenths (`FF`: off) | `shotClock` |
| 14 | timeout timer, seconds (`FF`: none) | `timeout`, `timeoutSeconds` |
| 15, 16 | reserved | |
| 17, 18 | score home, away | `home.score`, `away.score` |
| 19, 20 | team fouls home, away | `home.fouls`, `away.fouls` |
| 21, 22 | timeouts home, away | `home.timeouts`, `away.timeouts` |
| 23, 24 | possession home, away (1 = has the ball) | `possession` |

`FF` in bytes 17–22 means the console doesn't send that value, and the key
is left as it was.

### Player data (`FF FE 02 01` home, `03 01` away)

The body has 20 entries of 5 bytes: shirt number (3 ASCII bytes, NUL
padded, e.g. `37 00 00` = "7"), points, fouls. Entries with an empty number
are unused.

### Players on court (`FF FE 06 01` home, `07 01` away)

The body has 20 entries of 4 bytes: shirt number (3 ASCII bytes), then
`01` when the player is on court.

## Limitations

- **Timeout team:** the protocol doesn't say which team called a timeout,
  so there's no key for it. The timeout counter that went down tells you.
- **Player names:** players come by shirt number only. Names, photos and
  rosters come from elsewhere (for example the FIBA LiveStats JSON or
  values set through `POST /state`).
- **Unsupported data:**
  - penalty packets and other consoles' protocols
  - JSON sent over UDP
  - encrypted packets that need the vendor's decoder (CKazoBridge)
- **Byte layout:** no official protocol document is available here. The
  layout follows the FIBA project's parser and simulator. Check
  `scoreboard.home.timeouts` against your console, as its meaning
  (taken / left) depends on the console.
- **Saved values:** `scoreboard.*` keys are saved in the dashboard's
  `state.json` like other keys. After a restart they show the last game's
  values until the console sends new ones.

## Source files

| File | What |
|---|---|
| `ScoreboardFeed.h/.cpp` | UDP listener, packet decoder, `/scoreboard` HTTP route |
| `scoreboard_console.sh` | keyboard console (bash, UDP broadcast) |
| `simulate_ultrascore.py` | console simulator (UDP) |
| `UltraScore.postman_collection.json` | Postman collection (HTTP) |
| `Gui/GuiApplicationManager.cpp` | starts the feed (`NATRON_SCOREBOARD_PORT`) and adds its HTTP route |
