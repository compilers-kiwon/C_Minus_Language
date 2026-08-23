#!/usr/bin/env bash
#
# End-to-end tests: compile a C- program to an object file, link it against
# the runtime, run it, and compare what it printed.
#
#   tests/exec/<name>.cm   the program
#   tests/exec/<name>.in   standard input (optional)
#   tests/exec/<name>.expected  expected stdout+stderr, then "exit status: N"
#
# Every case runs at -O0 and at -O2 and must produce the same output, which
# catches code generation that only happens to work unoptimized.
#
# Usage:
#   CMC=build/cmc tests/run_exec_tests.sh            # check
#   CMC=build/cmc tests/run_exec_tests.sh --update   # record the .expected files
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
CASES="$HERE/exec"
CMC="${CMC:-$ROOT/build/cmc}"
CMINUS_RT="${CMINUS_RT:-$ROOT/build/libcminus_rt.a}"
CC="${CC:-clang}"

if [[ ! -x "$CMC" ]]; then
  echo "run_exec_tests.sh: cannot execute '$CMC' (set CMC=<path to cmc>)" >&2
  exit 2
fi
if [[ ! -f "$CMINUS_RT" ]]; then
  echo "run_exec_tests.sh: no runtime at '$CMINUS_RT' (set CMINUS_RT=<path>)" >&2
  exit 2
fi
if ! command -v "$CC" > /dev/null; then
  echo "run_exec_tests.sh: no linker driver '$CC' (set CC=<compiler>)" >&2
  exit 2
fi
if [[ ! -d "$CASES" ]]; then
  echo "0 passed, 0 failed"
  exit 0
fi

CMC="$(cd "$(dirname "$CMC")" && pwd)/$(basename "$CMC")"
CMINUS_RT="$(cd "$(dirname "$CMINUS_RT")" && pwd)/$(basename "$CMINUS_RT")"

update=0
[[ "${1:-}" == "--update" ]] && update=1

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cd "$CASES" || exit 2

pass=0
fail=0
failed_names=()

# Build and run one case at one optimization level; echoes what it produced.
run_at() {
  local name="$1" level="$2"
  local obj="$work/$name$level.o" exe="$work/$name$level"

  if ! "$CMC" "$level" -c "$name.cm" -o "$obj" 2>&1; then
    echo "cmc failed"
    return
  fi
  if ! "$CC" "$obj" "$CMINUS_RT" -o "$exe" 2>&1; then
    echo "link failed"
    return
  fi

  local stdin_file=/dev/null
  [[ -f "$name.in" ]] && stdin_file="$name.in"

  local output status
  output="$("$exe" < "$stdin_file" 2>&1)"
  status=$?
  printf '%s\nexit status: %s\n' "$output" "$status"
}

shopt -s nullglob
for src in *.cm; do
  name="${src%.cm}"
  expected="$name.expected"

  actual="$(run_at "$name" -O0)"
  optimized="$(run_at "$name" -O2)"

  if [[ "$actual" != "$optimized" ]]; then
    echo "FAIL     exec/$name  (-O0 and -O2 disagree)"
    diff -u <(printf '%s' "$actual") <(printf '%s' "$optimized") | head -20
    failed_names+=("$name")
    (( fail++ ))
    continue
  fi

  if (( update )); then
    printf '%s' "$actual" > "$expected"
    echo "updated  exec/$name"
    continue
  fi

  if [[ ! -f "$expected" ]]; then
    echo "MISSING  exec/$name  (no $expected; run with --update)"
    failed_names+=("$name")
    (( fail++ ))
    continue
  fi

  if printf '%s' "$actual" | diff -u "$expected" - > "$work/$name.diff"; then
    echo "ok       exec/$name"
    (( pass++ ))
  else
    echo "FAIL     exec/$name"
    cat "$work/$name.diff"
    failed_names+=("$name")
    (( fail++ ))
  fi
done

(( update )) && exit 0

echo
echo "$pass passed, $fail failed"
if (( fail > 0 )); then
  printf 'failing: %s\n' "${failed_names[*]}"
  exit 1
fi
exit 0
