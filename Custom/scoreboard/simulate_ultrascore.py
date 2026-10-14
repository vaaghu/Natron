#!/usr/bin/env python3
"""
Ultra Score scoreboard console simulator: sends "Data Out" UDP packets the
way the console does, to test Natron's scoreboard feed (ScoreboardFeed) without
a console. Python 3, standard library only.

  simulate_ultrascore.py play                 a game plays itself: running clock,
                                              baskets, fouls, timeouts, periods
  simulate_ultrascore.py interactive          you type the events (h2, a3, start, ...)
  simulate_ultrascore.py send "FF FE 01 ..."  one raw packet

Options: --host (default 127.0.0.1), --port (2800), --broadcast (send to
255.255.255.255 like a console on the LAN), --rate (general packets per
second, default 10), --speed (game time multiplier for play, default 1).

Check what Natron decoded: curl http://localhost:3000/scoreboard
"""

import argparse
import random
import socket
import sys
import threading
import time


def packet(id_high, body):
    return bytes([0xFF, 0xFE, id_high, 0x01, 0x01, len(body) & 0xFF, len(body) >> 8, *body, 0xFD, 0xFC])


def shirt(number):
    raw = str(number).encode("ascii")[:3]
    return list(raw) + [0] * (3 - len(raw))


class Game:
    def __init__(self):
        self.lock = threading.Lock()
        self.period = 1
        self.tenths_left = 10 * 600  # match clock in tenths of a second
        self.running = False
        self.shot_tenths = 240
        self.timeout_tenths = 0
        self.score = {"home": 0, "away": 0}
        self.fouls = {"home": 0, "away": 0}
        self.timeouts = {"home": 2, "away": 2}
        self.possession = "home"
        self.players = {
            "home": {n: [0, 0] for n in (3, 4, 7, 11, 14, 21, 23)},
            "away": {n: [0, 0] for n in (5, 8, 9, 12, 15, 20, 33)},
        }
        self.on_court = {t: set(list(p)[:5]) for t, p in self.players.items()}

    # One tick of game time (tenths of a second).
    def tick(self, tenths):
        with self.lock:
            if self.timeout_tenths > 0:
                self.timeout_tenths = max(0, self.timeout_tenths - tenths)
                return
            if not self.running:
                return
            self.tenths_left = max(0, self.tenths_left - tenths)
            self.shot_tenths = max(0, self.shot_tenths - tenths)
            if self.tenths_left == 0:
                self.running = False

    def general(self):
        with self.lock:
            minutes, rest = divmod(self.tenths_left, 600)
            seconds, tenths = divmod(rest, 10)
            shot_s, shot_t = divmod(self.shot_tenths, 10)
            timeout = (self.timeout_tenths + 9) // 10 if self.timeout_tenths > 0 else 0xFF
            return packet(0x01, [
                self.period, 0x11 if self.running else 0x10,
                minutes, seconds, tenths,
                shot_s, shot_t,
                timeout, 0x00, 0x00,
                self.score["home"], self.score["away"],
                self.fouls["home"], self.fouls["away"],
                self.timeouts["home"], self.timeouts["away"],
                int(self.possession == "home"), int(self.possession == "away"),
            ])

    def player_packets(self):
        with self.lock:
            out = []
            for team, id_players, id_court in (("home", 0x02, 0x06), ("away", 0x03, 0x07)):
                stats, court = [], []
                for number, (points, fouls) in self.players[team].items():
                    stats += shirt(number) + [points, fouls]
                    court += shirt(number) + [int(number in self.on_court[team])]
                stats += [0] * (100 - len(stats))
                court += [0] * (80 - len(court))
                out += [packet(id_players, stats), packet(id_court, court)]
            return out

    # Events -----------------------------------------------------------------
    def basket(self, team, points, number=None):
        with self.lock:
            self.score[team] = max(0, self.score[team] + points)
            number = number if number is not None else random.choice(sorted(self.on_court[team]))
            if number in self.players[team]:
                self.players[team][number][0] = max(0, self.players[team][number][0] + points)
            if points > 1:
                self.possession = "away" if team == "home" else "home"
                self.shot_tenths = 240
            return number

    def foul(self, team, number=None):
        with self.lock:
            self.fouls[team] += 1
            self.running = False
            number = number if number is not None else random.choice(sorted(self.on_court[team]))
            if number in self.players[team]:
                self.players[team][number][1] += 1
            return number

    def timeout(self, team):
        with self.lock:
            if self.timeouts[team] > 0:
                self.timeouts[team] -= 1
            self.running = False
            self.timeout_tenths = 600

    def substitution(self, team):
        with self.lock:
            bench = sorted(set(self.players[team]) - self.on_court[team])
            if not bench:
                return None
            out_player = random.choice(sorted(self.on_court[team]))
            in_player = random.choice(bench)
            self.on_court[team].discard(out_player)
            self.on_court[team].add(in_player)
            return out_player, in_player

    def next_period(self):
        with self.lock:
            self.period += 1
            self.tenths_left = (5 if self.period > 4 else 10) * 600
            self.running = False
            self.shot_tenths = 240
            if self.period == 3 or self.period > 4:
                self.fouls = {"home": 0, "away": 0}

    def summary(self):
        with self.lock:
            m, rest = divmod(self.tenths_left, 600)
            s = rest // 10
            period = f"Q{self.period}" if self.period <= 4 else f"{self.period - 4}OT"
            state = "running" if self.running else ("TIMEOUT" if self.timeout_tenths else "stopped")
            return (f"{period} {m:02d}:{s:02d} {state:8s}  HOME {self.score['home']:3d} - {self.score['away']:3d} AWAY"
                    f"  fouls {self.fouls['home']}-{self.fouls['away']}  poss {self.possession}")


class Sender:
    def __init__(self, host, port, broadcast):
        self.address = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if broadcast:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    def send(self, data):
        self.sock.sendto(data, self.address)


# Sends the general packet `rate` times a second and the player packets once a
# second, like the console, while advancing the clock.
def stream(game, sender, rate, speed, stop):
    period = 1.0 / rate
    last_players = 0.0
    while not stop.is_set():
        start = time.time()
        game.tick(max(1, round(10 * period * speed)))
        sender.send(game.general())
        if start - last_players >= 1.0:
            for p in game.player_packets():
                sender.send(p)
            last_players = start
        stop.wait(max(0.0, period - (time.time() - start)))


def play(game, sender, args):
    stop = threading.Event()
    threading.Thread(target=stream, args=(game, sender, args.rate, args.speed, stop), daemon=True).start()
    game.running = True
    print("Playing a game; Ctrl+C to stop.")
    try:
        while True:
            time.sleep(max(0.2, 2.0 / args.speed))
            if game.timeout_tenths > 0:
                continue
            if game.tenths_left == 0:
                if game.period >= 4 and game.score["home"] != game.score["away"]:
                    print("Final:", game.summary())
                    break
                game.next_period()
                print("--- next period ---")
                game.running = True
            team = random.choice(["home", "away"])
            roll = random.random()
            if roll < 0.45:
                n = game.basket(team, random.choice([2, 2, 2, 3]))
                event = f"{team} #{n} scores"
            elif roll < 0.6:
                n = game.foul(team)
                game.basket("away" if team == "home" else "home", 1)
                game.running = True
                event = f"{team} #{n} foul, free throw"
            elif roll < 0.63:
                game.timeout(team)
                event = f"{team} timeout"
            elif roll < 0.68:
                swap = game.substitution(team)
                event = f"{team} sub #{swap[0]} -> #{swap[1]}" if swap else "no sub"
            else:
                continue
            print(f"{game.summary()}   {event}")
            if game.timeout_tenths == 0 and not game.running and game.tenths_left > 0:
                game.running = True
    except KeyboardInterrupt:
        pass
    stop.set()


HELP = """Commands (team: h or a):
  h2 h3 h1 a2 a3 a1     basket / free throw     h-1 a-1   score correction
  hf af                 team foul (stops clock)  ht at     timeout (60 s)
  hs as                 substitution             p         switch possession
  start  stop           match clock              n         next period
  clock MM:SS[.t]       set clock                shot S[.t] set shot clock
  raw FF FE ...         send a raw packet        q         quit"""


def interactive(game, sender, args):
    stop = threading.Event()
    threading.Thread(target=stream, args=(game, sender, args.rate, 1.0, stop), daemon=True).start()
    print(HELP)
    teams = {"h": "home", "a": "away"}
    try:
        while True:
            line = input(f"[{game.summary()}] > ").strip()
            if not line:
                continue
            cmd, _, rest = line.partition(" ")
            try:
                if cmd == "q":
                    break
                elif cmd[0] in teams and cmd[1:].lstrip("-").isdigit():
                    game.basket(teams[cmd[0]], int(cmd[1:]))
                elif cmd in ("hf", "af"):
                    game.foul(teams[cmd[0]])
                elif cmd in ("ht", "at"):
                    game.timeout(teams[cmd[0]])
                elif cmd in ("hs", "as"):
                    print(game.substitution(teams[cmd[0]]))
                elif cmd == "p":
                    game.possession = "away" if game.possession == "home" else "home"
                elif cmd == "start":
                    game.timeout_tenths = 0
                    game.running = True
                elif cmd == "stop":
                    game.running = False
                elif cmd == "n":
                    game.next_period()
                elif cmd == "clock":
                    mmss, _, t = rest.partition(".")
                    m, s = (int(x) for x in mmss.split(":"))
                    game.tenths_left = (m * 60 + s) * 10 + int(t or 0)
                elif cmd == "shot":
                    s, _, t = rest.partition(".")
                    game.shot_tenths = int(s) * 10 + int(t or 0)
                elif cmd == "raw":
                    sender.send(bytes.fromhex(rest))
                else:
                    print(HELP)
            except (ValueError, IndexError) as e:
                print("?", e)
    except (KeyboardInterrupt, EOFError):
        print()
    stop.set()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["play", "interactive", "send"])
    parser.add_argument("hex", nargs="*", help="send: the packet bytes in hex")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2800)
    parser.add_argument("--broadcast", action="store_true", help="send to 255.255.255.255")
    parser.add_argument("--rate", type=float, default=10.0, help="general packets per second")
    parser.add_argument("--speed", type=float, default=1.0, help="play: game time multiplier")
    args = parser.parse_args()

    host = "255.255.255.255" if args.broadcast else args.host
    sender = Sender(host, args.port, args.broadcast)
    print(f"Sending Ultra Score packets to {host}:{args.port}")

    if args.mode == "send":
        if not args.hex:
            parser.error("send needs the packet in hex")
        sender.send(bytes.fromhex(" ".join(args.hex)))
        return
    game = Game()
    (play if args.mode == "play" else interactive)(game, sender, args)


if __name__ == "__main__":
    sys.exit(main())
