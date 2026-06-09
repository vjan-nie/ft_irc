#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  normalize.sh — format ft_irc sources to the project style.
#
#  1. clang-format -i over src/ include/ using the repo .clang-format
#     (tabs, Allman braces, pointer-right, no reflow).
#  2. libcpp's cpp_fmt.py for the safe whitespace fixes clang-format skips
#     (trailing whitespace, final newline) — only if available.
#  3. report header include cycles (non-destructive).
#
#  Use --check to verify formatting without rewriting (CI mode): exits non-zero
#  if any file would change.
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

if [ -t 1 ]; then G=$'\033[1;32m'; R=$'\033[1;31m'; B=$'\033[1;34m'; Z=$'\033[0m'
else G=""; R=""; B=""; Z=""; fi

mapfile -t FILES < <(find src include -type f \( -name '*.cpp' -o -name '*.hpp' \
	-o -name '*.tpp' -o -name '*.ipp' \) 2>/dev/null)

if ! command -v clang-format >/dev/null 2>&1; then
	echo "${R}clang-format not found${Z}"; exit 1
fi

printf "${B}== clang-format ==${Z}\n"
if [ "$CHECK" -eq 1 ]; then
	rc=0
	for f in "${FILES[@]}"; do
		if ! diff -q "$f" <(clang-format "$f") >/dev/null 2>&1; then
			echo "  would reformat: $f"; rc=1
		fi
	done
	[ "$rc" -eq 0 ] && printf "${G}all files already formatted${Z}\n"
	exit "$rc"
fi

clang-format -i "${FILES[@]}"
printf "${G}formatted %d files${Z}\n" "${#FILES[@]}"

CPPFMT="vendor/libcpp/vendor/scripts/cpp_fmt.py"
if [ -f "$CPPFMT" ]; then
	printf "\n${B}== cpp_fmt.py (whitespace) ==${Z}\n"
	python3 "$CPPFMT" "${FILES[@]}" 2>/dev/null || \
		echo "  (cpp_fmt.py reported issues — non-fatal)"
fi

CYC="vendor/libcpp/vendor/scripts/check_header_cycles.py"
if [ -f "$CYC" ]; then
	printf "\n${B}== header cycle report ==${Z}\n"
	python3 "$CYC" include src 2>&1 | sed 's/^/  /' || true
fi
