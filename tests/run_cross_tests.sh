#!/usr/bin/env bash
#
# Cross-compilation tests: build the end-to-end programs for other
# architectures, run them under an emulator, and require the very same output
# as the host build.
#
# The cases and expectations are the ones in tests/exec: what a C- program
# prints does not depend on the machine it runs on, so reusing them is the
# point rather than a shortcut.
#
# The default target list is chosen for what each one exercises:
#
#   aarch64-linux-gnu      64-bit, the common cross target
#   arm-linux-gnueabihf    32-bit -- the only one that covers the i32
#                          getelementptr index path
#   riscv64-linux-gnu      a different instruction set family
#
# Needs a cross toolchain per target, its libc headers, and a user-mode
# emulator. On Ubuntu:
#
#   sudo apt install -y qemu-user \
#     gcc-aarch64-linux-gnu   libc6-dev-arm64-cross \
#     gcc-arm-linux-gnueabihf libc6-dev-armhf-cross \
#     gcc-riscv64-linux-gnu   libc6-dev-riscv64-cross
#
# A target whose tools are missing is skipped, so a machine with only some of
# them still runs what it can. CROSS_REQUIRED=1 turns every skip into a
# failure, which is what CI wants: there, a skip looks just like a pass.
#
# Usage:
#   CMC=build/cmc tests/run_cross_tests.sh
#   CROSS_TARGETS="riscv64-linux-gnu" CMC=build/cmc tests/run_cross_tests.sh
#
# With a single target, CROSS_CC, CROSS_AR, CROSS_SYSROOT and CROSS_EMU
# override what is derived from the triple.
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
CASES="$HERE/exec"
CMC="${CMC:-$ROOT/build/cmc}"

# CROSS_TARGET (singular) is accepted as a one-element list.
CROSS_TARGETS="${CROSS_TARGETS:-${CROSS_TARGET:-aarch64-linux-gnu arm-linux-gnueabihf riscv64-linux-gnu}}"
read -r -a targets <<< "$CROSS_TARGETS"

required="${CROSS_REQUIRED:-}"

fatal() {
  echo "cross tests: $1" >&2
  exit 1
}

if [[ ! -x "$CMC" ]]; then
  [[ -n "$required" ]] && fatal "cannot execute '$CMC'"
  echo "cross tests skipped: cannot execute '$CMC'"
  echo "0 targets, 0 passed, 0 failed"
  exit 0
fi
if [[ ! -d "$CASES" ]]; then
  echo "0 targets, 0 passed, 0 failed"
  exit 0
fi
CMC="$(cd "$(dirname "$CMC")" && pwd)/$(basename "$CMC")"

# qemu's name for a triple. The leading component matches often enough to be
# the fallback, but not always -- powerpc64le is qemu-ppc64le.
emulator_for() {
  case "$1" in
  aarch64-*)     echo qemu-aarch64 ;;
  arm-*)         echo qemu-arm ;;
  riscv64-*)     echo qemu-riscv64 ;;
  riscv32-*)     echo qemu-riscv32 ;;
  powerpc64le-*) echo qemu-ppc64le ;;
  powerpc64-*)   echo qemu-ppc64 ;;
  powerpc-*)     echo qemu-ppc ;;
  s390x-*)       echo qemu-s390x ;;
  *)             echo "qemu-${1%%-*}" ;;
  esac
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
ran=0
failed_names=()
skipped=()

# Run a built program on its input; echoes what it produced.
capture() {
  local name="$1"
  local exe="$2"
  local emu="$3"
  local stdin_file=/dev/null
  [[ -f "$name.in" ]] && stdin_file="$name.in"

  local output
  local status
  # shellcheck disable=SC2086
  output="$($emu "$exe" < "$stdin_file" 2>&1)"
  status=$?
  printf '%s\nexit status: %s\n' "$output" "$status"
}

build_and_run() {
  local triple="$1"
  local cc="$2"
  local runtime="$3"
  local name="$4"
  local level="$5"
  local emu="$6"
  local exe="$work/${triple%%-*}-$name$level"

  if ! "$CMC" --target "$triple" --cc "$cc" --runtime "$runtime" "$level" \
       "$name.cm" -o "$exe" 2>&1; then
    echo "cmc failed"
    return
  fi
  capture "$name" "$exe" "$emu"
}

run_target() {
  local triple="$1"
  local label="${triple%%-*}"

  local cc="${triple}-gcc"
  local ar="${triple}-ar"
  local sysroot="/usr/${triple}"
  local emu
  emu="$(emulator_for "$triple")"

  if (( ${#targets[@]} == 1 )); then
    cc="${CROSS_CC:-$cc}"
    ar="${CROSS_AR:-$ar}"
    sysroot="${CROSS_SYSROOT:-$sysroot}"
    emu="${CROSS_EMU:-$emu}"
  fi

  if ! command -v "$cc" > /dev/null; then
    [[ -n "$required" ]] && fatal "required target $triple has no driver '$cc'"
    skipped+=("$triple (no $cc)")
    return
  fi
  if ! command -v "$emu" > /dev/null; then
    [[ -n "$required" ]] && fatal "required target $triple has no emulator '$emu'"
    skipped+=("$triple (no $emu)")
    return
  fi

  echo "cross target: $triple   driver: $cc   emulator: $emu"

  # The runtime has to be built for the target too; the host archive would be
  # rejected by the cross linker.
  local rt="$work/$label"
  mkdir -p "$rt"
  if ! "$cc" -O2 -c "$ROOT/runtime/cminus_rt.c" -o "$rt/cminus_rt.o" 2>&1; then
    # A driver that exists but cannot compile is a broken install, not an
    # absent one. The usual cause is missing target libc headers: on Debian
    # and Ubuntu those live in libc6-dev-<arch>-cross, which the compiler
    # package only recommends and so --no-install-recommends drops.
    fatal "'$cc' cannot compile for $triple; are the target libc headers installed? (libc6-dev-<arch>-cross)"
  fi
  command -v "$ar" > /dev/null || ar=ar
  "$ar" rcs "$rt/libcminus_rt.a" "$rt/cminus_rt.o" 2>&1 ||
    fatal "cannot archive the runtime for $triple"

  local emu_cmd="$emu"
  [[ -d "$sysroot" ]] && emu_cmd="$emu -L $sysroot"

  local src
  local name
  local expected
  local actual
  local optimized
  for src in *.cm; do
    name="${src%.cm}"
    expected="$name.expected"
    [[ -f "$expected" ]] || continue

    actual="$(build_and_run "$triple" "$cc" "$rt/libcminus_rt.a" "$name" -O0 "$emu_cmd")"
    optimized="$(build_and_run "$triple" "$cc" "$rt/libcminus_rt.a" "$name" -O2 "$emu_cmd")"

    if [[ "$actual" != "$optimized" ]]; then
      echo "FAIL     $label/$name  (-O0 and -O2 disagree)"
      diff -u <(printf '%s' "$actual") <(printf '%s' "$optimized") | head -20
      failed_names+=("$label/$name")
      (( fail++ ))
      continue
    fi

    if printf '%s' "$actual" | diff -u "$expected" - > "$work/$label-$name.diff"; then
      echo "ok       $label/$name"
      (( pass++ ))
    else
      echo "FAIL     $label/$name  (differs from the host result)"
      cat "$work/$label-$name.diff"
      failed_names+=("$label/$name")
      (( fail++ ))
    fi
  done
  (( ran++ ))
  echo
}

cd "$CASES" || exit 2

shopt -s nullglob
for triple in "${targets[@]}"; do
  run_target "$triple"
done

for note in "${skipped[@]}"; do
  echo "skipped  $note"
done
(( ${#skipped[@]} > 0 )) && echo

echo "$ran targets, $pass passed, $fail failed"
if (( fail > 0 )); then
  printf 'failing: %s\n' "${failed_names[*]}"
  exit 1
fi
exit 0
