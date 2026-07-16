#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  memcheck.sh — run ircserv under valgrind to check for leaks.
#
#  Interactive (default): builds the binary, launches it under valgrind,
#  then you connect a client (nc / HexChat) and Ctrl-C to trigger clean
#  shutdown; valgrind prints the leak summary. Wraps the flags libcpp's
#  valgrind_check.sh uses. Same driving flow as before scripted mode was
#  added, but VG_FLAGS is shared with scripted mode below, so the leak
#  summary you read here now also counts indirect leaks as errors
#  (previously --errors-for-leak-kinds=definite only).
#
#  Scripted (--auto / --scripted): no human required. Drives a fixed set of
#  IRC client sessions over plain bash /dev/tcp sockets — registration, JOIN,
#  PRIVMSG, PART, QUIT, an abrupt disconnect, and — deliberately — two
#  clients still CONNECTED when SIGTERM fires, to exercise ~Server()'s
#  _clients/_channels teardown against live state rather than only
#  post-QUIT bookkeeping. Shuts down the same way the P1 audit's own
#  baseline measurement did: kill -TERM on valgrind's own pid (it forwards
#  the signal to ircserv, which handles it exactly like a normal SIGTERM).
#  Exits with valgrind's own exit code, so this is usable as a pass/fail
#  gate — unlike the interactive mode, which always exits 0 regardless of
#  what the leak summary says.
#
#  NOT covered here, on purpose: the "^Z + flood" high-volume scenario.
#  Memcheck runs ~20-50x slower than native, and a flood large enough to be
#  meaningful takes minutes under instrumentation. That path is already
#  covered by PostMan's in-process leak counter (NoLeakAfterClientChurn in
#  tests/test_robustness.cpp). Don't add it here without re-reading P1's
#  audit (.claude/workflow/tasks/P1-valgrind-harness/01-audit.md, §8.4)
#  first — leaving it out was a deliberate scope call, not an oversight.
#
#  Usage:
#    scripts/memcheck.sh [--auto|--scripted] [--tier=mandatory|bonus|full] [port] [password]
#
#  No flags: same interactive-only driving flow as before scripted mode
#  existed (default tier = full, via plain `make`) — except the leak-kind
#  flags are now shared with scripted mode (see above).
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v valgrind >/dev/null 2>&1; then
	echo "valgrind not installed — 'sudo apt install valgrind'"; exit 1
fi

# ── flags ────────────────────────────────────────────────────────────────
MODE="interactive"
TIER=""
while [ $# -gt 0 ]; do
	case "$1" in
		--auto|--scripted) MODE="auto"; shift ;;
		--tier=*)          TIER="${1#--tier=}"; shift ;;
		--tier)            TIER="$2"; shift 2 ;;
		*) break ;;
	esac
done
[ "$MODE" = "auto" ] && [ -z "$TIER" ] && TIER="mandatory"

PORT="${1:-6667}"
PASS="${2:-pass}"

VG_FLAGS=(--leak-check=full --show-leak-kinds=all --track-origins=yes
          --errors-for-leak-kinds=definite,indirect)
VG_ERR_EXIT=97      # only used in scripted mode — see below
SETUP_FAIL_EXIT=90  # scripted mode only: a wait_for on the welcome/JOIN
                     # echo timed out, or C3's membership in #vgtest
                     # couldn't be confirmed against the server — the run
                     # never verified the "clients alive at SIGTERM"
                     # scenario it claims to exercise. Kept distinct from
                     # 0 (clean) and 97 (leak) so the three outcomes stay
                     # distinguishable: green / leak / setup-unverified.

build_tier() {
	case "$1" in
		mandatory) make mandatory ;;
		bonus)     make bonus ;;
		full|"")   make ;;
		*) echo "unknown tier '$1' (want mandatory|bonus|full)" >&2; return 1 ;;
	esac
}

# ── interactive mode ─────────────────────────────────────────────────────
if [ "$MODE" = "interactive" ]; then
	build_tier "$TIER" >/dev/null || { echo "build failed"; exit 1; }
	echo "Running ircserv on port $PORT under valgrind."
	echo "Connect a client, then press Ctrl-C here for clean shutdown + leak report."
	exec valgrind "${VG_FLAGS[@]}" ./ircserv "$PORT" "$PASS"
fi

# ── scripted mode ────────────────────────────────────────────────────────

# wait_for_listen <port> <max_tries>  — poll-connect instead of a fixed sleep.
wait_for_listen() {
	local port="$1" tries="$2" i fd
	for ((i = 0; i < tries; i++)); do
		if exec {fd}<>"/dev/tcp/127.0.0.1/$port" 2>/dev/null; then
			exec {fd}<&-
			return 0
		fi
		sleep 0.5
	done
	return 1
}

# wait_for <fd> <substring> <timeout-seconds>
# Reads lines off fd until one contains <substring>, or the deadline passes.
# A per-call timeout budget (not a fixed per-read sleep) so this stays
# correct at both native speed and under Memcheck's 20-50x slowdown.
wait_for() {
	local fd="$1" pattern="$2" timeout="$3" line deadline remaining
	deadline=$((SECONDS + timeout))
	while [ "$SECONDS" -lt "$deadline" ]; do
		remaining=$((deadline - SECONDS))
		[ "$remaining" -lt 1 ] && remaining=1
		if IFS= read -r -t "$remaining" line <&"$fd"; then
			[[ "$line" == *"$pattern"* ]] && return 0
		else
			return 1
		fi
	done
	return 1
}

send_line() {  # send_line <fd> <text...>
	local fd="$1"; shift
	printf '%s\r\n' "$*" >&"$fd"
}

open_client() {  # open_client <port> <outvar> — sets outvar to the new fd number
	local port="$1" outvar="$2" fd
	exec {fd}<>"/dev/tcp/127.0.0.1/$port" || return 1
	printf -v "$outvar" '%d' "$fd"
}

# C1 — full graceful lifecycle: registers, joins, talks, parts, quits, then
# closes its own fd. Gone by the time SIGTERM fires.
session_c1_graceful() {
	local port="$1" pass="$2" sockfd
	open_client "$port" sockfd || { echo "  C1: connect failed"; return 1; }
	send_line "$sockfd" "PASS $pass"
	send_line "$sockfd" "NICK vg_c1"
	send_line "$sockfd" "USER vg_c1 0 * :vg c1"
	wait_for "$sockfd" " 001 " 20 || { echo "  C1: no welcome — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	send_line "$sockfd" "JOIN #vgtest"
	wait_for "$sockfd" "JOIN #vgtest" 20 || { echo "  C1: no join echo — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	send_line "$sockfd" "PRIVMSG #vgtest :hello from c1"
	send_line "$sockfd" "PART #vgtest"
	send_line "$sockfd" "QUIT :bye"
	exec {sockfd}<&-
}

# C2 — registers, joins, talks, then the fd is closed WITHOUT a QUIT: an
# abrupt disconnect while still a channel member. Also gone by SIGTERM, but
# via the close/HUP path rather than the QUIT command path.
session_c2_abrupt() {
	local port="$1" pass="$2" sockfd
	open_client "$port" sockfd || { echo "  C2: connect failed"; return 1; }
	send_line "$sockfd" "PASS $pass"
	send_line "$sockfd" "NICK vg_c2"
	send_line "$sockfd" "USER vg_c2 0 * :vg c2"
	wait_for "$sockfd" " 001 " 20 || { echo "  C2: no welcome — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	send_line "$sockfd" "JOIN #vgtest"
	wait_for "$sockfd" "JOIN #vgtest" 20 || { echo "  C2: no join echo — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	send_line "$sockfd" "PRIVMSG #vgtest :hello from c2"
	exec {sockfd}<&-   # no QUIT — simulates a dropped connection
}

# C3 — registers and joins, then the fd is left OPEN. Still alive, still a
# _channels member, when SIGTERM is sent. This is the case the P1 audit
# could not confirm by inspection alone (§7/§8.4).
session_c3_alive_registered() {
	local port="$1" pass="$2" outvar="$3" sockfd
	open_client "$port" sockfd || { echo "  C3: connect failed"; return 1; }
	send_line "$sockfd" "PASS $pass"
	send_line "$sockfd" "NICK vg_c3"
	send_line "$sockfd" "USER vg_c3 0 * :vg c3"
	wait_for "$sockfd" " 001 " 20 || { echo "  C3: no welcome — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	send_line "$sockfd" "JOIN #vgtest"
	wait_for "$sockfd" "JOIN #vgtest" 20 || { echo "  C3: no join echo — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	printf -v "$outvar" '%d' "$sockfd"
}

# C4 — sends NICK only, never completes registration (no USER), fd left
# OPEN. Exercises ~Server()'s _clients teardown against an entry that never
# reached Client::isRegistered().
session_c4_alive_unregistered() {
	local port="$1" outvar="$2" sockfd
	open_client "$port" sockfd || { echo "  C4: connect failed"; return 1; }
	send_line "$sockfd" "NICK vg_c4"
	printf -v "$outvar" '%d' "$sockfd"
}

run_scripted() {
	local tier="$1" port="$2" pass="$3"
	local log rc c3fd c4fd
	# NOT local: an EXIT trap fired by an abrupt `set -u` termination
	# mid-function does not see the function's `local` bindings (verified —
	# a `local` vg_pid reads as unset inside the trap in that specific
	# path, even though a plain `exit` from the same function preserves
	# it). The trap below needs the real value, so vg_pid has to be a
	# plain (script-global) assignment, same as SETUP_FAILURES below.
	vg_pid=""

	# Defense in depth against an anomalous mid-orchestration exit (e.g. an
	# unbound variable under `set -u` in a future edit): whatever happens,
	# don't leave valgrind/ircserv running. Harmless no-op on the normal
	# path, where vg_pid has already been waited on.
	trap 'kill -TERM "${vg_pid:-}" 2>/dev/null' EXIT

	# Counts fatal setup failures (welcome/JOIN wait_for timeouts, failed
	# membership verification) accumulated by the session_* helpers below —
	# they run in this shell, not a subshell, so a plain assignment (no
	# `local`) is visible here. See SETUP_FAIL_EXIT.
	SETUP_FAILURES=0

	echo "== scripted valgrind run: tier=$tier port=$port =="
	build_tier "$tier" >/dev/null || { echo "build failed"; return 1; }

	log="$(mktemp /tmp/ft_irc_memcheck.XXXXXX.log)"
	valgrind "${VG_FLAGS[@]}" --error-exitcode="$VG_ERR_EXIT" \
		./ircserv "$port" "$pass" >"$log" 2>&1 &
	vg_pid=$!

	if ! wait_for_listen "$port" 40; then
		echo "server never started listening on $port — see $log"
		kill -TERM "$vg_pid" 2>/dev/null
		wait "$vg_pid" 2>/dev/null
		return 1
	fi

	session_c1_graceful "$port" "$pass"
	session_c2_abrupt "$port" "$pass"
	session_c3_alive_registered "$port" "$pass" c3fd
	session_c4_alive_unregistered "$port" c4fd

	# Positive verification, against the server, that C3 actually is a
	# member of #vgtest — not inferred from the JOIN echo above. NAMES
	# isn't wired up as a standalone command in this codebase (only
	# auto-sent as part of JOIN's own reply), so WHO #vgtest is the
	# server-side membership query available. Same generous timeout budget
	# as the other wait_for calls, since this also runs under Memcheck.
	if [ -n "${c3fd:-}" ]; then
		send_line "$c3fd" "WHO #vgtest"
		# Every numeric reply to C3 carries its own nick as the target field
		# (sendReply prepends client->getNickname()), including the
		# always-sent RPL_ENDOFWHO — so a bare "vg_c3" match would pass even
		# with an empty/nonexistent channel. Match "#vgtest vg_c3" instead:
		# that's target+" "+username from a genuine RPL_WHOREPLY member
		# line (CommandQuery.cpp), which only appears when the server
		# actually lists vg_c3 as a channel member.
		wait_for "$c3fd" "#vgtest vg_c3" 20 || { echo "  C3: not confirmed as member of #vgtest — setup unverified"; SETUP_FAILURES=$((SETUP_FAILURES + 1)); }
	else
		echo "  C3: no connection to verify membership on — setup unverified"
		SETUP_FAILURES=$((SETUP_FAILURES + 1))
	fi

	# C3/C4 stay connected right up to the signal — no client is fully
	# quiescent, matching the "several clients alive at SIGTERM" scenario.
	kill -TERM "$vg_pid"
	wait "$vg_pid"
	rc=$?

	[ -n "${c3fd:-}" ] && exec {c3fd}<&- 2>/dev/null
	[ -n "${c4fd:-}" ] && exec {c4fd}<&- 2>/dev/null

	echo "-- valgrind summary ($log) --"
	grep -E "definitely lost|indirectly lost|ERROR SUMMARY" "$log" || true

	if [ "$rc" -ne 0 ]; then
		echo "FAIL — tier=$tier, exit=$rc, log=$log"
	elif [ "$SETUP_FAILURES" -gt 0 ]; then
		rc="$SETUP_FAIL_EXIT"
		echo "SETUP UNVERIFIED ($SETUP_FAILURES failure(s)) — tier=$tier, exit=$rc, log=$log"
	else
		echo "PASS — tier=$tier, exit=$rc, log=$log"
	fi
	return "$rc"
}

run_scripted "$TIER" "$PORT" "$PASS"
