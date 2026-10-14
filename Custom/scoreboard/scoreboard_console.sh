#!/usr/bin/env bash
#
# Scoreboard console: a keyboard menu that plays the part of an Ultra Score
# console. Change the score, fouls, timeouts, possession and player stats,
# run the game and shot clocks, and the packets are broadcast over UDP like
# the real console's Data Out (general data 10 times a second, player data
# once a second).
#
#   scoreboard_console.sh [-H host] [-p port] [-r rate]
#
#   -H host   where to send (default 255.255.255.255: broadcast to the LAN;
#             127.0.0.1 for this machine only)
#   -p port   UDP port (default 2800)
#   -r rate   general packets per second (default 10)
#
# Needs bash 4.4+ and python3 or socat to send the datagrams (plain bash
# cannot broadcast). See README.md in this folder.

set -u

HOST=255.255.255.255
PORT=2800
RATE=10

while getopts "H:p:r:h" opt; do
  case $opt in
    H) HOST=$OPTARG ;;
    p) PORT=$OPTARG ;;
    r) RATE=$OPTARG ;;
    *) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  esac
done

if ((BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4))); then
  echo "Needs bash 4.4 or newer." >&2
  exit 1
fi

# --- Sender ------------------------------------------------------------------
# One datagram per line of hex.

SENDER=""
if command -v python3 >/dev/null 2>&1; then
  coproc SEND {
    exec python3 -I -c '
import socket, sys
host, port = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
error = ""
while True:
    line = sys.stdin.readline()
    if not line:
        break
    try:
        s.sendto(bytes.fromhex(line), (host, port))
    except OSError as e:
        if str(e) != error:
            error = str(e)
            sys.stderr.write("send: " + error + "\n")
' "$HOST" "$PORT" 2>"${TMPDIR:-/tmp}/scoreboard_console.err"
  }
  SENDER=python3
elif command -v socat >/dev/null 2>&1; then
  SENDER=socat
else
  echo "Needs python3 or socat to send UDP broadcasts." >&2
  exit 1
fi

send() # hex bytes, space separated
{
  if [[ $SENDER == python3 ]]; then
    printf '%s\n' "$1" >&"${SEND[1]}"
  else
    printf "$(sed 's/\([0-9A-F][0-9A-F]\) */\\x\1/g' <<<"$1")" |
      socat -u - "UDP-DATAGRAM:$HOST:$PORT,broadcast" 2>/dev/null
  fi
  ((SENT++))
}

# --- Game state --------------------------------------------------------------

declare -A SCORE FOULS TIMEOUTS
declare -A PTS FLS COURT # key: "home 7" / "away 12"

new_game()
{
  PERIOD=1
  CLOCK=6000      # game clock, tenths of a second
  RUNNING=0
  SHOT=240        # shot clock, tenths; -1: off
  TIMEOUT=0       # timeout timer, tenths
  SCORE=([home]=0 [away]=0)
  FOULS=([home]=0 [away]=0)
  TIMEOUTS=([home]=2 [away]=2)
  POSSESSION=home
  PTS=() FLS=() COURT=()
  MESSAGE="New game: Q1 10:00. Space starts the clock."
}

period_length() # tenths
{
  ((PERIOD > 4)) && echo 3000 || echo 6000
}

period_name()
{
  if ((PERIOD <= 4)); then echo "Q$PERIOD"
  elif ((PERIOD == 5)); then echo "OT"
  else echo "$((PERIOD - 4))OT"
  fi
}

clock_text()
{
  if ((CLOCK >= 600)); then
    printf '%02d:%02d' $((CLOCK / 600)) $((CLOCK % 600 / 10))
  else
    printf '%02d.%d' $((CLOCK / 10)) $((CLOCK % 10))
  fi
}

shot_text()
{
  if ((SHOT < 0)); then echo "off"
  elif ((SHOT > 50)); then echo $((SHOT / 10))
  else echo "$((SHOT / 10)).$((SHOT % 10))"
  fi
}

# Advances the clocks by the given tenths.
tick()
{
  local t=$1
  if ((TIMEOUT > 0)); then
    ((TIMEOUT = TIMEOUT > t ? TIMEOUT - t : 0))
    ((TIMEOUT == 0)) && MESSAGE="Timeout over."
  fi
  ((RUNNING)) || return
  ((CLOCK = CLOCK > t ? CLOCK - t : 0))
  ((SHOT > 0)) && ((SHOT = SHOT > t ? SHOT - t : 0))
  if ((CLOCK == 0)); then
    RUNNING=0
    MESSAGE="End of $(period_name). n: next period."
  fi
}

# --- Packets -----------------------------------------------------------------

hex()
{
  local out="" b h
  for b in "$@"; do
    printf -v h '%02X ' $((b & 255))
    out+=$h
  done
  printf '%s' "${out% }"
}

packet() # id, body bytes...
{
  local id=$1
  shift
  echo "$(hex 0xFF 0xFE "$id" 0x01 0x01 $(($# & 255)) $(($# >> 8)) "$@" 0xFD 0xFC)"
}

general_packet()
{
  local shot_s=0xFF shot_t=0xFF timeout=0xFF
  ((SHOT >= 0)) && shot_s=$((SHOT / 10)) shot_t=$((SHOT % 10))
  ((TIMEOUT > 0)) && timeout=$(((TIMEOUT + 9) / 10))
  packet 0x01 \
    "$PERIOD" $((RUNNING ? 0x11 : 0x10)) \
    $((CLOCK / 600)) $((CLOCK % 600 / 10)) $((CLOCK % 10)) \
    "$shot_s" "$shot_t" "$timeout" 0 0 \
    "${SCORE[home]}" "${SCORE[away]}" "${FOULS[home]}" "${FOULS[away]}" \
    "${TIMEOUTS[home]}" "${TIMEOUTS[away]}" \
    $([[ $POSSESSION == home ]] && echo 1 || echo 0) $([[ $POSSESSION == away ]] && echo 1 || echo 0)
}

shirt_bytes() # number -> 3 ASCII bytes, NUL padded
{
  local n=${1:0:3} i
  for ((i = 0; i < 3; i++)); do
    if ((i < ${#n})); then printf '%d ' "'${n:i:1}"; else printf '0 '; fi
  done
}

team_numbers() # team -> its players' numbers, sorted
{
  local key
  for key in "${!PTS[@]}"; do
    [[ $key == "$1 "* ]] && echo "${key#* }"
  done | sort -n | head -20
}

player_packets() # team
{
  local team=$1 stats=() court=() n count=0
  for n in $(team_numbers "$team"); do
    stats+=($(shirt_bytes "$n") "${PTS[$team $n]}" "${FLS[$team $n]}")
    court+=($(shirt_bytes "$n") "${COURT[$team $n]}")
    ((count++))
  done
  for ((; count < 20; count++)); do
    stats+=(0 0 0 0 0)
    court+=(0 0 0 0)
  done
  local id_stats=0x02 id_court=0x06
  [[ $team == away ]] && id_stats=0x03 id_court=0x07
  packet "$id_stats" "${stats[@]}"
  packet "$id_court" "${court[@]}"
}

# --- Screen ------------------------------------------------------------------

B=$(tput bold 2>/dev/null) N=$(tput sgr0 2>/dev/null) D=$(tput dim 2>/dev/null)
EL=$(tput el 2>/dev/null)

draw()
{
  local state=STOPPED
  ((RUNNING)) && state=RUNNING
  ((TIMEOUT > 0)) && state="TIMEOUT $(((TIMEOUT + 9) / 10))s"
  local ball_home="  " ball_away="  "
  [[ $POSSESSION == home ]] && ball_home="<-" || ball_away="->"
  local err=""
  [[ -s ${TMPDIR:-/tmp}/scoreboard_console.err ]] && err=$(tail -1 "${TMPDIR:-/tmp}/scoreboard_console.err")

  tput cup 0 0
  printf '%s\n' "${B}ULTRA SCORE CONSOLE${N}  ${D}UDP -> $HOST:$PORT  ($SENDER, $RATE pkt/s)  sent: $SENT${N}$EL"
  printf '%s\n' "$EL"
  printf '   %s%-6s %3d%s  %s   %s%s  %s%s   %s  %s%3d %-6s%s%s\n' \
    "$B" HOME "${SCORE[home]}" "$N" "$ball_home" "$B" "$(period_name)" "$(clock_text)" "$N" \
    "$ball_away" "$B" "${SCORE[away]}" AWAY "$N" "$EL"
  printf '   fouls %-2d  timeouts %-2d   shot %-4s %-12s  fouls %-2d  timeouts %-2d%s\n' \
    "${FOULS[home]}" "${TIMEOUTS[home]}" "$(shot_text)" "$state" "${FOULS[away]}" "${TIMEOUTS[away]}" "$EL"
  printf '%s\n' "$EL"
  cat <<EOF
 ${B}Clock${N}  space start/stop   c set clock   n next period   b previous period$EL
 ${B}Shot${N}   r reset 24   e reset 14   s set shot clock   o shot clock on/off$EL
 ${B}Home${N}   1 +1  2 +2  3 +3  4 -1   f foul  F foul -1   t timeout  T timeouts +1$EL
 ${B}Away${N}   7 +1  8 +2  9 +3  0 -1   g foul  G foul -1   y timeout  Y timeouts +1$EL
 ${B}Ball${N}   p switch possession$EL
 ${B}Set${N}    h home score   a away score   k player stats   l list players$EL
 ${B}Game${N}   x new game   q quit$EL
$EL
EOF
  printf ' %s%s\n' "$MESSAGE" "$EL"
  [[ -n $err ]] && printf ' %sLast send error: %s%s\n' "$B" "$err" "$N$EL" || printf '%s\n' "$EL"
  tput ed 2>/dev/null
}

# Reads a line at the bottom of the screen (the clocks keep their time).
ask() # prompt -> REPLY
{
  tput cnorm 2>/dev/null
  stty echo 2>/dev/null
  printf '\n %s' "$1"
  REPLY=""
  read -r REPLY
  stty -echo 2>/dev/null
  tput civis 2>/dev/null
  tput clear 2>/dev/null
}

is_number() { [[ $1 =~ ^[0-9]+$ ]]; }

# --- Actions -----------------------------------------------------------------

add_score() # team points
{
  local t=$1
  ((SCORE[$t] += $2))
  ((SCORE[$t] < 0)) && SCORE[$t]=0
  ((SCORE[$t] > 254)) && SCORE[$t]=254
  if (($2 >= 2)); then
    POSSESSION=$([[ $t == home ]] && echo away || echo home)
    ((SHOT >= 0)) && SHOT=240
  fi
  MESSAGE="${t^} $(printf '%+d' "$2")."
}

add_foul() # team delta
{
  ((FOULS[$1] += $2))
  ((FOULS[$1] < 0)) && FOULS[$1]=0
  (($2 > 0)) && RUNNING=0
  MESSAGE="${1^} team fouls: ${FOULS[$1]}."
}

call_timeout() # team
{
  if ((TIMEOUTS[$1] == 0)); then
    MESSAGE="${1^} has no timeouts left (T / Y adds one)."
    return
  fi
  ((TIMEOUTS[$1]--))
  RUNNING=0
  TIMEOUT=600
  MESSAGE="${1^} timeout, 60 s."
}

set_clock()
{
  ask "Game clock (MM:SS, MM:SS.t or SS.t): "
  if [[ $REPLY =~ ^([0-9]{1,2}):([0-5][0-9])(\.([0-9]))?$ ]]; then
    CLOCK=$(((10#${BASH_REMATCH[1]} * 60 + 10#${BASH_REMATCH[2]}) * 10 + ${BASH_REMATCH[4]:-0}))
  elif [[ $REPLY =~ ^([0-9]{1,2})(\.([0-9]))?$ ]]; then
    CLOCK=$((10#${BASH_REMATCH[1]} * 10 + ${BASH_REMATCH[3]:-0}))
  else
    MESSAGE="Not a clock: '$REPLY'."
    return
  fi
  MESSAGE="Clock set to $(clock_text)."
}

set_shot()
{
  ask "Shot clock (S or S.t): "
  if [[ $REPLY =~ ^([0-9]{1,2})(\.([0-9]))?$ ]]; then
    SHOT=$((10#${BASH_REMATCH[1]} * 10 + ${BASH_REMATCH[3]:-0}))
    MESSAGE="Shot clock set to $(shot_text)."
  else
    MESSAGE="Not a shot clock: '$REPLY'."
  fi
}

set_score() # team
{
  ask "${1^} score: "
  if is_number "$REPLY" && ((REPLY <= 254)); then
    SCORE[$1]=$((10#$REPLY))
    MESSAGE="${1^} score set to ${SCORE[$1]}."
  else
    MESSAGE="Not a score: '$REPLY'."
  fi
}

set_player()
{
  ask "Player: team (h/a) number points fouls on-court (y/n), e.g. 'h 7 12 2 y' ('h 7 -' removes): "
  local team num pts fls court
  read -r team num pts fls court <<<"$REPLY"
  case $team in h) team=home ;; a) team=away ;; *) MESSAGE="Team must be h or a."; return ;; esac
  if ! [[ $num =~ ^[0-9A-Za-z]{1,3}$ ]]; then
    MESSAGE="Shirt number: 1 to 3 letters or digits."
    return
  fi
  if [[ $pts == - ]]; then
    unset "PTS[$team $num]" "FLS[$team $num]" "COURT[$team $num]"
    MESSAGE="${team^} #$num removed."
    return
  fi
  if ! is_number "${pts:-0}" || ! is_number "${fls:-0}"; then
    MESSAGE="Points and fouls must be numbers."
    return
  fi
  if [[ -z ${PTS[$team $num]+x} ]] && (($(team_numbers "$team" | wc -l) >= 20)); then
    MESSAGE="${team^} already has 20 players."
    return
  fi
  PTS[$team $num]=$((10#${pts:-0}))
  FLS[$team $num]=$((10#${fls:-0}))
  [[ ${court:-y} == [yY1]* ]] && COURT[$team $num]=1 || COURT[$team $num]=0
  MESSAGE="${team^} #$num: ${PTS[$team $num]} pts, ${FLS[$team $num]} fouls, $([[ ${COURT[$team $num]} == 1 ]] && echo on court || echo bench)."
}

list_players()
{
  local team n line=""
  for team in home away; do
    line+="${team^}:"
    for n in $(team_numbers "$team"); do
      line+=" #$n ${PTS[$team $n]}p/${FLS[$team $n]}f$([[ ${COURT[$team $n]} == 1 ]] && echo '*')"
    done
    line+="   "
  done
  MESSAGE="$line(* on court)"
}

handle_key()
{
  case $1 in
    " ")
      if ((CLOCK == 0)); then MESSAGE="Clock at 0: n for the next period, c to set it."
      else
        ((RUNNING = !RUNNING))
        ((RUNNING)) && TIMEOUT=0
        MESSAGE="Clock $( ((RUNNING)) && echo started || echo stopped)."
      fi ;;
    c) set_clock ;;
    n)
      ((PERIOD++))
      CLOCK=$(period_length) RUNNING=0 TIMEOUT=0
      ((SHOT >= 0)) && SHOT=240
      ((PERIOD == 3 || PERIOD > 4)) && FOULS=([home]=0 [away]=0)
      ((PERIOD == 3)) && TIMEOUTS=([home]=3 [away]=3)
      MESSAGE="$(period_name) ready. Space starts the clock." ;;
    b)
      ((PERIOD > 1)) && ((PERIOD--))
      MESSAGE="Period set back to $(period_name)." ;;
    r) SHOT=240; MESSAGE="Shot clock 24." ;;
    e) SHOT=140; MESSAGE="Shot clock 14." ;;
    s) set_shot ;;
    o)
      if ((SHOT < 0)); then SHOT=240; MESSAGE="Shot clock on."
      else SHOT=-1; MESSAGE="Shot clock off."
      fi ;;
    1) add_score home 1 ;; 2) add_score home 2 ;; 3) add_score home 3 ;; 4) add_score home -1 ;;
    7) add_score away 1 ;; 8) add_score away 2 ;; 9) add_score away 3 ;; 0) add_score away -1 ;;
    f) add_foul home 1 ;; F) add_foul home -1 ;;
    g) add_foul away 1 ;; G) add_foul away -1 ;;
    t) call_timeout home ;; y) call_timeout away ;;
    T) ((TIMEOUTS[home]++)); MESSAGE="Home timeouts: ${TIMEOUTS[home]}." ;;
    Y) ((TIMEOUTS[away]++)); MESSAGE="Away timeouts: ${TIMEOUTS[away]}." ;;
    p)
      POSSESSION=$([[ $POSSESSION == home ]] && echo away || echo home)
      MESSAGE="Possession: $POSSESSION." ;;
    h) set_score home ;;
    a) set_score away ;;
    k) set_player ;;
    l) list_players ;;
    x) new_game ;;
    q) exit 0 ;;
  esac
}

# --- Main loop ---------------------------------------------------------------

now_us()
{
  local t=${EPOCHREALTIME/[.,]/}
  echo $((10#$t))
}

cleanup()
{
  stty "$SAVED_STTY" 2>/dev/null
  tput cnorm 2>/dev/null
  tput clear 2>/dev/null
  [[ -n ${SEND_PID:-} ]] && kill "$SEND_PID" 2>/dev/null
  echo "Scoreboard console stopped ($SENT packets sent)."
}

SENT=0
SAVED_STTY=$(stty -g)
trap cleanup EXIT
trap 'exit 0' INT TERM
stty -echo -icanon
tput civis 2>/dev/null
tput clear 2>/dev/null
: >"${TMPDIR:-/tmp}/scoreboard_console.err"

new_game
PERIOD_US=$((1000000 / RATE))
LAST=$(now_us)
CARRY=0          # microseconds not yet counted as tenths
LAST_PLAYERS=0

while true; do
  NOW=$(now_us)
  ((CARRY += NOW - LAST, LAST = NOW))
  if ((CARRY >= 100000)); then
    tick $((CARRY / 100000))
    ((CARRY %= 100000))
  fi

  send "$(general_packet)"
  if ((NOW - LAST_PLAYERS >= 1000000)) && ((${#PTS[@]} > 0)); then
    LAST_PLAYERS=$NOW
    while read -r p; do send "$p"; done < <(player_packets home; player_packets away)
  fi
  draw

  # Wait for a key until the next packet is due.
  WAIT_US=$((PERIOD_US - ($(now_us) - NOW)))
  ((WAIT_US < 1000)) && WAIT_US=1000
  printf -v WAIT '%d.%06d' $((WAIT_US / 1000000)) $((WAIT_US % 1000000))
  if IFS= read -rsn1 -t "$WAIT" KEY; then
    if [[ $KEY == $'\e' ]]; then
      read -rsn5 -t 0.01 _ # arrow keys and the like: ignored
    else
      handle_key "$KEY"
    fi
  fi
done
