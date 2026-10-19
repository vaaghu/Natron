#!/usr/bin/env python3
"""
FIBA LiveStats simulator: plays a game by itself and serves it the way
LiveStats does, to test Natron's LiveStats feed (LiveStatsFeed) without a
LiveStats computer or a live match. Python 3, standard library only.

  simulate_livestats.py tv        the TV feed: a TCP server (port 7677) that
                                  waits for the parameters message, then sends
                                  teams, status, boxscore, actions, ... one
                                  JSON message per line
  simulate_livestats.py online    the online data.json: an HTTP server (port
                                  8080) serving /data/<any id>/data.json

Options: --listen (address, default 127.0.0.1; 0.0.0.0 for other machines),
--port, --speed (game time multiplier, default 1).

Start Natron with, for the TV feed:
  NATRON_LIVESTATS_HOST=127.0.0.1 Natron
or for the online data:
  NATRON_LIVESTATS_URL=http://127.0.0.1:8080 NATRON_LIVESTATS_MATCH=1 Natron
(or POST {"host": "127.0.0.1"} / {"url": "http://127.0.0.1:8080/data/1/data.json"}
to http://localhost:3000/livestats while it runs).

Check what Natron read: curl http://localhost:3000/livestats
"""

import argparse
import http.server
import json
import random
import socket
import socketserver
import sys
import threading
import time

TYPES = "se,ac,mi,te,st,box,pbp,sd,sc"
PERIOD_TENTHS = 10 * 600
STATS = (
    "sPoints sFieldGoalsMade sFieldGoalsAttempted sTwoPointersMade sTwoPointersAttempted "
    "sThreePointersMade sThreePointersAttempted sFreeThrowsMade sFreeThrowsAttempted "
    "sReboundsOffensive sReboundsDefensive sReboundsTotal sAssists sSteals sTurnovers sBlocks "
    "sFoulsPersonal sFoulsOn sMinutes sPlusMinusPoints"
).split()

ROSTERS = {
    1: ("Home City", "City", "HOM", [
        ("4", "Alex", "Rivers", "G"), ("7", "Sam", "Smith", "G"), ("11", "Jordan", "Lee", "F"),
        ("14", "Chris", "Moreau", "F"), ("21", "Dan", "Okafor", "C"), ("3", "Leo", "Brandt", "G"),
        ("23", "Max", "Ivanov", "F")]),
    2: ("Visitors BC", "Visitors", "VIS", [
        ("5", "Tom", "Novak", "G"), ("8", "Ray", "Costa", "G"), ("9", "Ian", "Fischer", "F"),
        ("12", "Ben", "Adeyemi", "F"), ("33", "Kai", "Larsen", "C"), ("15", "Eli", "Duarte", "G"),
        ("20", "Noah", "Kim", "F")]),
}


def clock_text(tenths):
    """MM:SS:CC, as the TV feed writes clocks."""
    tenths = max(0, int(tenths))
    return "%02d:%02d:%02d" % (tenths // 600, (tenths // 10) % 60, (tenths % 10) * 10)


class Game:
    def __init__(self):
        self.lock = threading.Lock()
        self.message_id = 0
        self.period = 1
        self.clock = PERIOD_TENTHS
        self.shot = 240
        self.running = False
        self.status = "INPROGRESS"
        self.possession = 1
        self.timeouts = {1: 2, 2: 2}
        self.team_fouls = {1: 0, 2: 0}
        self.period_points = {1: {}, 2: {}}
        self.players = {}
        for team, (_, _, _, roster) in ROSTERS.items():
            self.players[team] = []
            for pno, (shirt, first, family, position) in enumerate(roster, 1):
                self.players[team].append({
                    "pno": pno, "shirtNumber": shirt, "firstName": first, "familyName": family,
                    "scoreboardName": "%s. %s" % (first[0], family.upper()), "playingPosition": position,
                    "starter": 1 if pno <= 5 else 0, "active": 1 if pno <= 5 else 0,
                    "stats": {name: 0 for name in STATS},
                })
        self.actions = []
        self.next_event = 8.0  # game seconds until the next event
        self.listeners = []  # called with each new TV feed message

    # Scores --------------------------------------------------------------

    def score(self, team):
        return sum(p["stats"]["sPoints"] for p in self.players[team])

    def on_court(self, team):
        return [p for p in self.players[team] if p["active"]]

    # Messages ------------------------------------------------------------

    def message(self, kind, body):
        self.message_id += 1
        body = dict(body)
        body["type"] = kind
        body["messageId"] = self.message_id
        return body

    def teams_message(self):
        teams = []
        for team, (name, nickname, code, _) in ROSTERS.items():
            teams.append({
                "teamNumber": team,
                "detail": {"teamName": name, "teamNickname": nickname, "teamCode": code,
                           "isHomeCompetitor": 1 if team == 1 else 0},
                "coach": {"firstName": "Coach", "familyName": code.title()},
                "players": [{k: v for k, v in p.items() if k != "stats"} for p in self.players[team]],
            })
        return self.message("teams", {"teams": teams})

    def status_message(self):
        return self.message("status", {
            "status": self.status,
            "period": {"current": min(self.period, 4) if self.period <= 4 else self.period - 4,
                       "periodType": "REGULAR" if self.period <= 4 else "OVERTIME"},
            "periodStatus": "STARTED" if self.running else "PENDING",
            "clock": clock_text(self.clock),
            "shotClock": clock_text(self.shot),
            "clockRunning": 1 if self.running else 0,
            "possession": self.possession,
            "scores": [{"teamNumber": t, "score": self.score(t), "timeoutsRemaining": self.timeouts[t],
                        "teamFouls": self.team_fouls[t]} for t in (1, 2)],
        })

    def boxscore_message(self):
        teams = []
        for team in (1, 2):
            total = {name: sum(p["stats"][name] for p in self.players[team]) for name in STATS}
            total["sBenchPoints"] = sum(p["stats"]["sPoints"] for p in self.players[team] if not p["starter"])
            teams.append({
                "teamNumber": team,
                "total": {"team": total,
                          "players": [dict(p["stats"], pno=p["pno"]) for p in self.players[team]]},
                "periods": [{"period": n, "periodType": "REGULAR", "team": {"sPoints": points}}
                            for n, points in sorted(self.period_points[team].items())],
            })
        return self.message("boxscore", {"teams": teams})

    def action_message(self, action):
        return self.message("action", action)

    def connect_messages(self):
        """What the feed sends to a new client."""
        return [
            self.message("matchinformation", {
                "match": {"matchName": "Simulated game", "matchTime": time.strftime("%Y-%m-%d %H:%M:00"),
                          "roundDescription": "Round 1"},
                "competition": {"competitionName": "Natron Cup"},
                "venue": {"venueName": "Test Arena"}}),
            self.message("setup", {"periods": {"number": 4, "length": 10}}),
            self.teams_message(),
            self.status_message(),
            self.boxscore_message(),
            self.message("playbyplay", {"actions": self.actions}),
            self.message("standings", {"standings": [
                {"teamName": "Home City", "teamCode": "HOM", "position": 1, "played": 4, "won": 3, "lost": 1,
                 "standingPoints": 7},
                {"teamName": "Visitors BC", "teamCode": "VIS", "position": 2, "played": 4, "won": 2, "lost": 2,
                 "standingPoints": 6}]}),
            self.message("schedule", {"matches": [
                {"matchTime": "2026-10-26 19:30:00", "matchStatus": "SCHEDULED", "competitors": [
                    {"teamName": "Visitors BC", "teamCode": "VIS", "scoreString": "", "isHomeCompetitor": 1},
                    {"teamName": "Home City", "teamCode": "HOM", "scoreString": "", "isHomeCompetitor": 0}]}]}),
        ]

    def data_json(self):
        """The whole game, as the online data.json."""
        tm = {}
        for team, (name, nickname, code, _) in ROSTERS.items():
            entry = {"name": name, "shortName": nickname, "code": code, "coach": "Coach " + code.title(),
                     "score": self.score(team), "fouls": self.team_fouls[team], "timeouts": self.timeouts[team]}
            for n, points in self.period_points[team].items():
                entry["p%d_score" % n] = points
            for stat in STATS:
                entry["tot_" + stat] = sum(p["stats"][stat] for p in self.players[team])
            entry["pl"] = {}
            for p in self.players[team]:
                player = {k: v for k, v in p.items() if k != "stats"}
                player.update(p["stats"])
                player["name"] = p["firstName"] + " " + p["familyName"]
                minutes = int(p["stats"]["sMinutes"] * 60)
                player["sMinutes"] = "%d:%02d" % (minutes // 60, minutes % 60)
                entry["pl"][str(team * 100 + p["pno"])] = player
            tm[str(team)] = entry
        pbp = []
        for a in reversed(self.actions):  # newest first, like the real one
            p = self.player(a["teamNumber"], a.get("pno"))
            pbp.append({"actionNumber": a["actionNumber"], "tno": a["teamNumber"], "pno": a.get("pno", 0),
                        "period": a["period"], "periodType": a["periodType"], "gt": a["clock"][:5],
                        "actionType": a["actionType"], "subType": a["subType"], "success": a["success"],
                        "s1": a["score1"], "s2": a["score2"],
                        "shirtNumber": p["shirtNumber"] if p else "",
                        "scoreboardName": p["scoreboardName"] if p else ""})
        return {"clock": clock_text(self.clock)[:5], "period": self.period, "periodType": "REGULAR",
                "inOT": 0, "clockRunning": 1 if self.running else 0, "tm": tm, "pbp": pbp}

    # Game ----------------------------------------------------------------

    def player(self, team, pno):
        for p in self.players.get(team, []):
            if p["pno"] == pno:
                return p
        return None

    def act(self, team, player, kind, sub="", success=0):
        action = {"actionNumber": len(self.actions) + 1, "teamNumber": team,
                  "period": self.period, "periodType": "REGULAR", "clock": clock_text(self.clock),
                  "actionType": kind, "subType": sub, "success": success, "qualifiers": [],
                  "score1": str(self.score(1)), "score2": str(self.score(2))}
        if player:
            action["pno"] = player["pno"]
        self.actions.append(action)
        return [self.action_message(action)]

    def add_points(self, team, player, points):
        player["stats"]["sPoints"] += points
        self.period_points[team][self.period] = self.period_points[team].get(self.period, 0) + points
        for p in self.on_court(team):
            p["stats"]["sPlusMinusPoints"] += points
        for p in self.on_court(3 - team):
            p["stats"]["sPlusMinusPoints"] -= points

    def event(self):
        """One random play. Returns the TV feed messages it makes."""
        team = self.possession
        other = 3 - team
        shooter = random.choice(self.on_court(team))
        s = shooter["stats"]
        messages = []
        roll = random.random()
        if roll < 0.62:
            three = random.random() < 0.35
            made = random.random() < (0.36 if three else 0.52)
            s["sFieldGoalsAttempted"] += 1
            s["sThreePointersAttempted" if three else "sTwoPointersAttempted"] += 1
            if made:
                s["sFieldGoalsMade"] += 1
                s["sThreePointersMade" if three else "sTwoPointersMade"] += 1
                self.add_points(team, shooter, 3 if three else 2)
            messages += self.act(team, shooter, "3pt" if three else "2pt", "jumpshot", 1 if made else 0)
            if made:
                if random.random() < 0.6:
                    passer = random.choice([p for p in self.on_court(team) if p is not shooter])
                    passer["stats"]["sAssists"] += 1
                    messages += self.act(team, passer, "assist")
                self.change_possession()
            else:
                offensive = random.random() < 0.28
                rebounder = random.choice(self.on_court(team if offensive else other))
                rebounder["stats"]["sReboundsOffensive" if offensive else "sReboundsDefensive"] += 1
                rebounder["stats"]["sReboundsTotal"] += 1
                messages += self.act(team if offensive else other, rebounder, "rebound",
                                     "offensive" if offensive else "defensive")
                self.shot = 140 if offensive else 240
                if not offensive:
                    self.possession = other
        elif roll < 0.75:
            s["sTurnovers"] += 1
            messages += self.act(team, shooter, "turnover", "badpass")
            stealer = random.choice(self.on_court(other))
            stealer["stats"]["sSteals"] += 1
            messages += self.act(other, stealer, "steal")
            self.change_possession()
        elif roll < 0.93:
            fouler = random.choice(self.on_court(other))
            fouler["stats"]["sFoulsPersonal"] += 1
            s["sFoulsOn"] += 1
            self.team_fouls[other] += 1
            self.running = False
            messages += self.act(other, fouler, "foul", "personal")
            messages += self.act(0, None, "clock", "stop")
            for _ in range(2):
                made = random.random() < 0.75
                s["sFreeThrowsAttempted"] += 1
                if made:
                    s["sFreeThrowsMade"] += 1
                    self.add_points(team, shooter, 1)
                messages += self.act(team, shooter, "freethrow", "", 1 if made else 0)
            self.change_possession()
            self.running = True
            messages += self.act(0, None, "clock", "start")
        elif roll < 0.97:
            # Substitution.
            out = random.choice(self.on_court(team))
            bench = [p for p in self.players[team] if not p["active"]]
            if bench:
                inn = random.choice(bench)
                out["active"], inn["active"] = 0, 1
                messages += self.act(team, out, "substitution", "out")
                messages += self.act(team, inn, "substitution", "in")
        elif self.timeouts[team] > 0:
            self.timeouts[team] -= 1
            self.running = False
            messages += self.act(team, None, "timeout", "full")
            messages += self.act(0, None, "clock", "stop")
        messages.append(self.status_message())
        messages.append(self.boxscore_message())
        return messages

    def change_possession(self):
        self.possession = 3 - self.possession
        self.shot = 240

    def tick(self, tenths):
        """Game time goes on. Returns the TV feed messages it makes."""
        messages = []
        with self.lock:
            if self.status == "FINISHED":
                return messages
            if not self.running:
                # Restart after a stop (timeouts, period breaks, free throws).
                self.next_event -= tenths / 10.0
                if self.next_event <= 0:
                    self.running = True
                    self.next_event = random.uniform(6, 20)
                    messages += self.act(0, None, "clock", "start")
                    messages.append(self.status_message())
                return messages
            self.clock = max(0, self.clock - tenths)
            self.shot = max(0, self.shot - tenths)
            for team in (1, 2):
                for p in self.on_court(team):
                    p["stats"]["sMinutes"] = round(p["stats"]["sMinutes"] + tenths / 600.0, 3)
            if self.clock == 0:
                self.running = False
                messages += self.act(0, None, "clock", "stop")
                if self.period >= 4 and self.score(1) != self.score(2):
                    self.status = "FINISHED"
                else:
                    self.period += 1
                    self.clock = PERIOD_TENTHS if self.period <= 4 else 5 * 600
                    self.shot = 240
                    self.team_fouls = {1: 0, 2: 0}
                    self.next_event = 15
                messages.append(self.status_message())
                return messages
            if self.shot == 0:
                self.change_possession()
            self.next_event -= tenths / 10.0
            if self.next_event <= 0:
                self.next_event = random.uniform(6, 20)
                messages += self.event()
        return messages


def run_game(game, speed, stop):
    """Game time at speed x real time; each message goes to the listeners."""
    last = time.time()
    while not stop.is_set():
        time.sleep(0.1)
        now = time.time()
        tenths = int(round((now - last) * 10 * speed))
        if tenths <= 0:
            continue
        last = now
        for message in game.tick(tenths):
            for listener in list(game.listeners):
                listener(message)


def line(message):
    return (json.dumps(message, separators=(",", ":")) + "\r\n").encode("utf-8")


def serve_tv(game, args, stop):
    """The TV feed: one client at a time, like the LiveStats computer."""
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.listen, args.port))
    server.listen(5)
    print("TV feed on %s:%d (Ctrl+C to stop)" % (args.listen, args.port))
    history = []  # every message sent, for fromMessageId

    def keep(message):
        history.append(message)

    game.listeners.append(keep)
    while not stop.is_set():
        client, address = server.accept()
        print("client %s:%d" % address)
        client.settimeout(5)
        try:
            request = b""
            while b"\n" not in request:
                chunk = client.recv(4096)
                if not chunk:
                    raise ConnectionError("closed before the parameters message")
                request += chunk
        except (socket.timeout, ConnectionError) as error:
            print("  no parameters message (%s): closed" % error)
            client.close()
            continue
        parameters = json.loads(request.split(b"\n")[0].decode("utf-8"))
        print("  parameters:", parameters)
        types = set((parameters.get("types") or TYPES).split(","))
        codes = {"matchinformation": "mi", "setup": "se", "teams": "te", "status": "st", "boxscore": "box",
                 "playbyplay": "pbp", "standings": "sd", "schedule": "sc", "action": "ac"}

        def wanted(message):
            code = codes.get(message["type"])
            return code is None or code in types

        client.settimeout(None)
        lock = threading.Lock()
        alive = [True]

        def send(message):
            if not alive[0] or not wanted(message):
                return
            with lock:
                try:
                    client.sendall(line(message))
                except OSError:
                    alive[0] = False

        with game.lock:
            if "fromMessageId" in parameters:
                start = int(parameters["fromMessageId"])
                backlog = [m for m in history if m["messageId"] >= start]
                print("  resending %d messages from %d" % (len(backlog), start))
            else:
                backlog = game.connect_messages()
            for message in backlog:
                send(message)
            game.listeners.append(send)

        last_ping = time.time()
        while alive[0] and not stop.is_set():
            time.sleep(0.5)
            if time.time() - last_ping >= 10:
                last_ping = time.time()
                send({"type": "ping", "timestamp": time.strftime("%Y-%m-%d %H:%M:%S:00")})
        game.listeners.remove(send)
        client.close()
        print("  client gone")


def serve_online(game, args):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if not (self.path.startswith("/data/") and self.path.split("?")[0].endswith("/data.json")):
                self.send_error(404)
                return
            with game.lock:
                body = json.dumps(game.data_json()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_):
            pass

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    server = socketserver.ThreadingTCPServer((args.listen, args.port), Handler)
    print("data.json on http://%s:%d/data/<any id>/data.json (Ctrl+C to stop)" % (args.listen, args.port))
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["tv", "online"])
    parser.add_argument("--listen", default="127.0.0.1", help="address to listen on")
    parser.add_argument("--port", type=int, help="default: 7677 (tv), 8080 (online)")
    parser.add_argument("--speed", type=float, default=1.0, help="game time multiplier")
    args = parser.parse_args()
    if args.port is None:
        args.port = 7677 if args.mode == "tv" else 8080

    game = Game()
    stop = threading.Event()
    threading.Thread(target=run_game, args=(game, args.speed, stop), daemon=True).start()
    try:
        if args.mode == "tv":
            serve_tv(game, args, stop)
        else:
            serve_online(game, args)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
    return 0


if __name__ == "__main__":
    sys.exit(main())
