#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  memcheck.sh — run ircserv under valgrind to check for leaks.
#
#  Usage: scripts/memcheck.sh [port] [password]
#  Builds the binary, launches it under valgrind, then you connect a client
#  (nc / HexChat) and Ctrl-C to trigger clean shutdown; valgrind prints the
#  leak summary. Wraps the flags libcpp's valgrind_check.sh uses.
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${1:-6667}"
PASS="${2:-pass}"

if ! command -v valgrind >/dev/null 2>&1; then
	echo "valgrind not installed — 'sudo apt install valgrind'"; exit 1
fi

make >/dev/null || { echo "build failed"; exit 1; }

echo "Running ircserv on port $PORT under valgrind."
echo "Connect a client, then press Ctrl-C here for clean shutdown + leak report."
exec valgrind \
	--leak-check=full \
	--show-leak-kinds=all \
	--track-origins=yes \
	--errors-for-leak-kinds=definite \
	./ircserv "$PORT" "$PASS"
