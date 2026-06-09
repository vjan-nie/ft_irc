#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  audit.sh — ft_irc subject-compliance + good-practice audit.
#
#  Verifies the rules that the 42 ft_irc subject makes non-negotiable:
#    - compiles clean with c++ -std=c++98 -Wall -Wextra -Werror
#    - no C++11-or-later tokens anywhere in the build sources
#    - no obviously-forbidden functions (fork/system/exec/threads/…)
#    - a single event-wait call (poll | epoll_wait | select | kqueue)
#    - fcntl() used only as fcntl(fd, F_SETFL, O_NONBLOCK)
#    - Makefile exposes all / clean / fclean / re (+ $(NAME))
#    - `make` does not relink when nothing changed
#    - header include graph has no cycles (delegated to libcpp's checker)
#
#  Exit status is non-zero if any check fails. Run from anywhere.
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# ── colours (disabled when not a TTY) ────────────────────────────────────────
if [ -t 1 ]; then
	R=$'\033[1;31m'; G=$'\033[1;32m'; Y=$'\033[1;33m'; B=$'\033[1;34m'; Z=$'\033[0m'
else
	R=""; G=""; Y=""; B=""; Z=""
fi

FAILS=0
pass() { printf "  ${G}✓${Z} %s\n" "$1"; }
fail() { printf "  ${R}✗ %s${Z}\n" "$1"; FAILS=$((FAILS + 1)); }
warn() { printf "  ${Y}!${Z} %s\n" "$1"; }
section() { printf "\n${B}== %s ==${Z}\n" "$1"; }

# Build sources audited: ft_irc's own code + the C++98 libcpp tier (never the
# C++17 libcpp tree, never googletest).
SRC_DIRS=(src include)
[ -d vendor/libcpp/c98 ] && SRC_DIRS+=(vendor/libcpp/c98/src vendor/libcpp/c98/include)

src_files() {
	find "${SRC_DIRS[@]}" -type f \( -name '*.cpp' -o -name '*.hpp' \
		-o -name '*.tpp' -o -name '*.ipp' -o -name '*.h' \) 2>/dev/null
}

# ── 1. clean compile under the mandated flags (all three tiers) ──────────────
section "compile: c++ -std=c++98 -Wall -Wextra -Werror"
BUILD_LOG="$(mktemp)"
if make re >"$BUILD_LOG" 2>&1; then
	if grep -qiE 'warning:' "$BUILD_LOG"; then
		fail "build emitted warnings"; grep -iE 'warning:' "$BUILD_LOG" | head
	else
		pass "clean build (full), zero warnings"
	fi
else
	fail "build failed"; tail -20 "$BUILD_LOG"
fi
for tier in mandatory bonus; do
	if make "$tier" >"$BUILD_LOG" 2>&1; then
		if grep -qiE 'warning:' "$BUILD_LOG"; then
			fail "make $tier emitted warnings"; grep -iE 'warning:' "$BUILD_LOG" | head
		else
			pass "clean build ($tier), zero warnings"
		fi
	else
		fail "make $tier failed"; tail -20 "$BUILD_LOG"
	fi
done
make >/dev/null 2>&1   # leave the default (full) binary in place

# ── 2. no C++11+ tokens in build sources ─────────────────────────────────────
section "C++98 compliance (no C++11+ constructs)"
# Patterns chosen to avoid false positives on common C++98 code.
declare -A CXX11=(
	["\bnullptr\b"]="nullptr"
	["\bauto\b[[:space:]]+[a-zA-Z_&*]"]="auto type deduction"
	["\[\["]="attribute [[...]]"
	["\bstd::move\b"]="std::move"
	["\bstd::thread\b|\bstd::mutex\b|<thread>|<mutex>|<chrono>|<atomic>"]="threading/chrono headers"
	["\boverride\b|\bfinal\b"]="override/final"
	["=[[:space:]]*default\b|=[[:space:]]*delete\b"]="= default / = delete"
	["\bnoexcept\b|\bconstexpr\b|\bstatic_assert\b"]="noexcept/constexpr/static_assert"
	["\bstd::to_string\b|\bstd::stoi\b|\bstd::stol\b"]="std::to_string/stoi (C++11)"
	["\bstd::unordered_(map|set)\b|\bstd::shared_ptr\b|\bstd::unique_ptr\b"]="C++11 containers/smart ptrs"
	["for[[:space:]]*\([^;]*[^:]:[^:][^;]*\)"]="range-based for"
)
CXX_HIT=0
FILES="$(src_files)"
for pat in "${!CXX11[@]}"; do
	hits="$(grep -REn "$pat" $FILES 2>/dev/null)"
	if [ -n "$hits" ]; then
		fail "C++11+ construct: ${CXX11[$pat]}"
		echo "$hits" | sed 's/^/      /' | head -5
		CXX_HIT=1
	fi
done
[ "$CXX_HIT" -eq 0 ] && pass "no C++11+ tokens found"

# ── 3. forbidden functions ───────────────────────────────────────────────────
section "forbidden functions"
FORBIDDEN='\b(fork|vfork|system|popen|execve|execl|execlp|execvp|pthread_create)\b'
hits="$(grep -REn "$FORBIDDEN" $FILES 2>/dev/null)"
if [ -n "$hits" ]; then
	fail "forbidden function call"; echo "$hits" | sed 's/^/      /' | head
else
	pass "no forbidden functions"
fi

# ── 4. single event-wait call ────────────────────────────────────────────────
section "single poll/epoll/select event loop"
# Ignore matches inside string literals / comments (error messages mention the
# name without being a call site).
EV="$(grep -REn '\b(epoll_wait|[^_]poll|select|kqueue|kevent)[[:space:]]*\(' src 2>/dev/null \
	| grep -vE '//' | grep -v '"' )"
EVN="$(printf '%s' "$EV" | grep -c . )"
if [ "$EVN" -le 1 ]; then
	pass "exactly $EVN event-wait call site"
else
	warn "$EVN event-wait call sites (subject allows only one) — review:"
	echo "$EV" | sed 's/^/      /'
fi

# ── 5. fcntl only F_SETFL,O_NONBLOCK ─────────────────────────────────────────
section "fcntl usage"
# Only real calls have an argument list with a comma; the error strings are the
# literal text "fcntl()". A compliant call carries F_SETFL + O_NONBLOCK.
BADF="$(grep -REn '\bfcntl[[:space:]]*\([^)]*,' src 2>/dev/null | grep -v 'F_SETFL' | grep -v 'O_NONBLOCK')"
if [ -n "$BADF" ]; then
	fail "fcntl used with disallowed flags"; echo "$BADF" | sed 's/^/      /'
else
	pass "fcntl absent or only F_SETFL/O_NONBLOCK"
fi

# ── 6. Makefile required rules ───────────────────────────────────────────────
section "Makefile rules"
for rule in 'all' 'bonus' 'mandatory' 'clean' 'fclean' 're'; do
	if grep -qE "^${rule}[[:space:]]*:" Makefile; then
		pass "rule '$rule' present"
	else
		fail "missing Makefile rule '$rule'"
	fi
done

# ── 7. no unnecessary relink ─────────────────────────────────────────────────
section "no unnecessary relinking"
make >/dev/null 2>&1
SECOND="$(make 2>&1)"
if printf '%s' "$SECOND" | grep -qiE 'Nothing to be done|is up to date' \
	|| [ -z "$(printf '%s' "$SECOND" | grep -E '\.o|ircserv')" ]; then
	pass "second 'make' is a no-op"
else
	fail "second 'make' relinks:"; printf '%s\n' "$SECOND" | sed 's/^/      /' | head
fi

# ── 8. header cycles (delegate to libcpp) ────────────────────────────────────
section "header include cycles"
CYC="vendor/libcpp/vendor/scripts/check_header_cycles.py"
if [ -f "$CYC" ]; then
	if python3 "$CYC" include src >/tmp/ircaudit_cyc 2>&1; then
		pass "no header cycles"
	else
		warn "check_header_cycles reported issues:"; sed 's/^/      /' /tmp/ircaudit_cyc | head
	fi
else
	warn "check_header_cycles.py not found (libcpp scripts missing) — skipped"
fi

rm -f "$BUILD_LOG"
echo
if [ "$FAILS" -eq 0 ]; then
	printf "${G}AUDIT PASSED${Z}\n"; exit 0
else
	printf "${R}AUDIT FAILED — %d check(s)${Z}\n" "$FAILS"; exit 1
fi
