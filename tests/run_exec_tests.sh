#!/usr/bin/env bash
#
# End-to-end tests: build each program, run it, and compare what it printed.
#
#   tests/exec/<name>.cm        the program
#   tests/exec/<name>.in        standard input (optional)
#   tests/exec/<name>.expected  expected stdout+stderr, then "exit status: N"
#
# Every case is built three ways and all three must agree:
#
#   -O0 linked by cmc      the default path
#   -O2 linked by cmc      catches code generation that only works unoptimized
#   -O0 via -c, linked by  keeps the separate compile-then-link path honest
#   the C driver
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

if [[ ! -x "$CMC" ]]; then
  echo "run_exec_tests.sh: cannot execute '$CMC' (set CMC=<path to cmc>)" >&2
  exit 2
fi
if [[ ! -f "$CMINUS_RT" ]]; then
  echo "run_exec_tests.sh: no runtime at '$CMINUS_RT' (set CMINUS_RT=<path>)" >&2
  exit 2
fi
if [[ ! -d "$CASES" ]]; then
  echo "0 passed, 0 failed"
  exit 0
fi

CMC="$(cd "$(dirname "$CMC")" && pwd)/$(basename "$CMC")"
CMINUS_RT="$(cd "$(dirname "$CMINUS_RT")" && pwd)/$(basename "$CMINUS_RT")"

# cmc finds its own C driver; CC, when set, overrides it and is also what the
# separate compile-then-link variant uses.
CMC_CC_ARGS=()
[[ -n "${CC:-}" ]] && CMC_CC_ARGS=(--cc "$CC")
LINK_CC="${CC:-cc}"

update=0
[[ "${1:-}" == "--update" ]] && update=1

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cd "$CASES" || exit 2

pass=0
fail=0
failed_names=()

# Run a built program on its input; echoes what it produced.
capture() {
  local name="$1" exe="$2"
  local stdin_file=/dev/null
  [[ -f "$name.in" ]] && stdin_file="$name.in"

  local output status
  output="$("$exe" < "$stdin_file" 2>&1)"
  status=$?
  printf '%s\nexit status: %s\n' "$output" "$status"
}

# Compile and link in one step, the way cmc is normally used.
build_linked() {
  # Separate statements on purpose: bash 5.3 expands every right-hand side of
  # a `local` before assigning any of them, so referring to an earlier name in
  # the same statement trips `set -u`.
  local name="$1"
  local level="$2"
  local exe="$work/$name$level"
  if ! "$CMC" "$level" "${CMC_CC_ARGS[@]}" --runtime "$CMINUS_RT" \
       "$name.cm" -o "$exe" 2>&1; then
    echo "cmc failed"
    return
  fi
  capture "$name" "$exe"
}

# Compile to an object and link it separately.
build_via_object() {
  local name="$1"
  local obj="$work/$name.sep.o"
  local exe="$work/$name.sep"
  if ! "$CMC" -c "$name.cm" -o "$obj" 2>&1; then
    echo "cmc -c failed"
    return
  fi
  if ! "$LINK_CC" "$obj" "$CMINUS_RT" -o "$exe" 2>&1; then
    echo "link failed"
    return
  fi
  capture "$name" "$exe"
}

shopt -s nullglob
for src in *.cm; do
  name="${src%.cm}"
  expected="$name.expected"

  actual="$(build_linked "$name" -O0)"
  optimized="$(build_linked "$name" -O2)"
  separate="$(build_via_object "$name")"

  disagreement=""
  [[ "$actual" != "$optimized" ]] && disagreement="-O0 and -O2 disagree"
  [[ -z "$disagreement" && "$actual" != "$separate" ]] &&
    disagreement="linking by cmc and linking separately disagree"

  if [[ -n "$disagreement" ]]; then
    echo "FAIL     exec/$name  ($disagreement)"
    if [[ "$actual" != "$optimized" ]]; then
      diff -u <(printf '%s' "$actual") <(printf '%s' "$optimized") | head -20
    else
      diff -u <(printf '%s' "$actual") <(printf '%s' "$separate") | head -20
    fi
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
