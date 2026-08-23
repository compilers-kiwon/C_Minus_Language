#!/usr/bin/env bash
#
# Golden-file test runner.
#
# Each suite is a directory of inputs plus the compiler flags it runs under:
#
#   tests/lex/<name>.cm    ->  cmc --dump-tokens
#   tests/parse/<name>.cm  ->  cmc --dump-ast
#
# The combined stdout+stderr and the exit status are compared against
# <name>.expected.
#
# Usage:
#   CMC=build/cmc tests/run_tests.sh            # check
#   CMC=build/cmc tests/run_tests.sh --update   # regenerate the .expected files
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
CMC="${CMC:-$ROOT/build/cmc}"

if [[ ! -x "$CMC" ]]; then
  echo "run_tests.sh: cannot execute '$CMC' (set CMC=<path to cmc>)" >&2
  exit 2
fi
CMC="$(cd "$(dirname "$CMC")" && pwd)/$(basename "$CMC")"

update=0
[[ "${1:-}" == "--update" ]] && update=1

pass=0
fail=0
failed_names=()

run_suite() {
  local suite="$1"
  shift
  local flags=("$@")

  [[ -d "$HERE/$suite" ]] || return 0

  # Run from the suite directory so diagnostics carry a short, stable file name.
  pushd "$HERE/$suite" > /dev/null || return

  shopt -s nullglob
  local src name expected actual status
  for src in *.cm; do
    name="${src%.cm}"
    expected="$name.expected"

    actual="$("$CMC" "${flags[@]}" --no-color "$src" 2>&1)"
    status=$?
    actual="$actual"$'\n'"exit status: $status"

    if (( update )); then
      printf '%s\n' "$actual" > "$expected"
      echo "updated  $suite/$name"
      continue
    fi

    if [[ ! -f "$expected" ]]; then
      echo "MISSING  $suite/$name  (no $expected; run with --update)"
      failed_names+=("$suite/$name")
      (( fail++ ))
      continue
    fi

    if printf '%s\n' "$actual" | diff -u "$expected" - > "$name.actual.diff"; then
      rm -f "$name.actual.diff"
      echo "ok       $suite/$name"
      (( pass++ ))
    else
      echo "FAIL     $suite/$name"
      cat "$name.actual.diff"
      failed_names+=("$suite/$name")
      (( fail++ ))
    fi
  done

  popd > /dev/null || return
}

run_suite lex   --dump-tokens
run_suite parse --dump-ast

(( update )) && exit 0

echo
echo "$pass passed, $fail failed"
if (( fail > 0 )); then
  printf 'failing: %s\n' "${failed_names[*]}"
  exit 1
fi
exit 0
