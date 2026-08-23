#!/usr/bin/env bash
#
# Cross-compilation tests: build the end-to-end programs for another
# architecture, run them under an emulator, and require the very same output
# as the host build.
#
# The cases and expectations are the ones in tests/exec: what a C- program
# prints does not depend on the machine it runs on, so reusing them is the
# point rather than a shortcut.
#
# Needs a cross toolchain, its target libc headers, and a user-mode emulator.
# On Ubuntu:
#   sudo apt install -y gcc-aarch64-linux-gnu libc6-dev-arm64-cross qemu-user
#
# CROSS_REQUIRED=1 turns "tools missing" from a skip into a failure, which is
# what CI wants: there, a skip would look just like a pass.
#
# Everything is overridable, so another target can be tried without editing:
#   CROSS_TARGET=riscv64-linux-gnu CROSS_CC=riscv64-linux-gnu-gcc \
#   CROSS_SYSROOT=/usr/riscv64-linux-gnu CROSS_EMU=qemu-riscv64 \
#   CMC=build/cmc tests/run_cross_tests.sh
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
CASES="$HERE/exec"
CMC="${CMC:-$ROOT/build/cmc}"

CROSS_TARGET="${CROSS_TARGET:-aarch64-linux-gnu}"
CROSS_CC="${CROSS_CC:-${CROSS_TARGET}-gcc}"
CROSS_AR="${CROSS_AR:-${CROSS_TARGET}-ar}"
CROSS_SYSROOT="${CROSS_SYSROOT:-/usr/${CROSS_TARGET}}"
CROSS_EMU="${CROSS_EMU:-qemu-${CROSS_TARGET%%-*}}"

# Missing tools mean "not tested here", not "broken": a machine without a
# cross toolchain should not fail the suite. Somewhere that is supposed to
# have one, though, a skip is indistinguishable from a pass and hides exactly
# the breakage this suite exists to catch -- so CI sets CROSS_REQUIRED.
skip() {
  if [[ -n "${CROSS_REQUIRED:-}" ]]; then
    echo "cross tests were required but cannot run: $1" >&2
    exit 1
  fi
  echo "cross tests skipped: $1"
  echo "0 passed, 0 failed"
  exit 0
}

[[ -x "$CMC" ]] || skip "cannot execute '$CMC'"
[[ -d "$CASES" ]] || skip "no $CASES directory"
command -v "$CROSS_CC"  > /dev/null || skip "no cross driver '$CROSS_CC'"
command -v "$CROSS_EMU" > /dev/null || skip "no emulator '$CROSS_EMU'"

CMC="$(cd "$(dirname "$CMC")" && pwd)/$(basename "$CMC")"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "cross target: $CROSS_TARGET   driver: $CROSS_CC   emulator: $CROSS_EMU"

# The runtime has to be built for the target too; the host archive would be
# rejected by the cross linker.
if ! "$CROSS_CC" -O2 -c "$ROOT/runtime/cminus_rt.c" -o "$work/cminus_rt.o" 2>&1; then
  # A driver that exists but cannot compile is a broken install, not an
  # absent one, so this fails rather than skipping. The usual cause is that
  # the target C library headers were left out: on Debian and Ubuntu they
  # live in libc6-dev-<arch>-cross, which the compiler package only
  # recommends and so is dropped by --no-install-recommends.
  echo "cross tests failed: '$CROSS_CC' cannot compile for $CROSS_TARGET" >&2
  echo "  are the target libc headers installed? (Debian/Ubuntu:" >&2
  echo "  libc6-dev-<arch>-cross, e.g. libc6-dev-arm64-cross)" >&2
  exit 1
fi
ar_tool="$CROSS_AR"
command -v "$ar_tool" > /dev/null || ar_tool=ar
if ! "$ar_tool" rcs "$work/libcminus_rt.a" "$work/cminus_rt.o" 2>&1; then
  echo "cross tests failed: cannot archive the runtime" >&2
  exit 1
fi

emu_args=()
[[ -d "$CROSS_SYSROOT" ]] && emu_args=(-L "$CROSS_SYSROOT")

cd "$CASES" || exit 2

pass=0
fail=0
failed_names=()

build_and_run() {
  local name="$1"
  local level="$2"
  local exe="$work/$name$level"

  if ! "$CMC" --target "$CROSS_TARGET" --cc "$CROSS_CC" \
       --runtime "$work/libcminus_rt.a" "$level" \
       "$name.cm" -o "$exe" 2>&1; then
    echo "cmc failed"
    return
  fi

  local stdin_file=/dev/null
  [[ -f "$name.in" ]] && stdin_file="$name.in"

  local output
  local status
  output="$("$CROSS_EMU" "${emu_args[@]}" "$exe" < "$stdin_file" 2>&1)"
  status=$?
  printf '%s\nexit status: %s\n' "$output" "$status"
}

shopt -s nullglob
for src in *.cm; do
  name="${src%.cm}"
  expected="$name.expected"
  [[ -f "$expected" ]] || continue

  actual="$(build_and_run "$name" -O0)"
  optimized="$(build_and_run "$name" -O2)"

  if [[ "$actual" != "$optimized" ]]; then
    echo "FAIL     cross/$name  (-O0 and -O2 disagree)"
    diff -u <(printf '%s' "$actual") <(printf '%s' "$optimized") | head -20
    failed_names+=("$name")
    (( fail++ ))
    continue
  fi

  if printf '%s' "$actual" | diff -u "$expected" - > "$work/$name.diff"; then
    echo "ok       cross/$name"
    (( pass++ ))
  else
    echo "FAIL     cross/$name  (differs from the host result)"
    cat "$work/$name.diff"
    failed_names+=("$name")
    (( fail++ ))
  fi
done

echo
echo "$pass passed, $fail failed"
if (( fail > 0 )); then
  printf 'failing: %s\n' "${failed_names[*]}"
  exit 1
fi
exit 0
