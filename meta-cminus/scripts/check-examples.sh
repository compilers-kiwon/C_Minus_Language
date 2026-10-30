#!/bin/bash
# Build tests/exec with the cmc that this layer builds, for each MACHINE, and
# run the programs under user-mode qemu against their .expected output: once
# through the LLVM path and once through each MLIR generator, at -O0 and -O2.
#
# Usage, from an initialised build directory (oe-init-build-env already
# sourced, meta-cminus added):
#   meta-cminus/scripts/check-examples.sh qemux86 qemux86-64 qemuarm64 qemuriscv64
#
# Needs qemu-user on the host. cminus-hello is built first because it pulls in
# everything else: cmc-native, the cross toolchain and the target sysroot.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
(( $# )) || { echo "usage: $0 MACHINE..." >&2; exit 2; }
command -v bitbake > /dev/null || { echo "source oe-init-build-env first" >&2; exit 2; }

wrap="$(mktemp -d)"
trap 'rm -rf "$wrap"' EXIT
failures=0

for machine in "$@"; do
  echo "== $machine"
  if ! MACHINE=$machine bitbake cminus-hello > "$wrap/bitbake.log" 2>&1; then
    grep -E '^ERROR' "$wrap/bitbake.log" | head -5
    (( failures++ ))
    continue
  fi

  env="$(MACHINE=$machine bitbake -e cminus-hello)"
  get() { sed -nE "s/^(export )?$1=\"(.*)\"$/\2/p" <<< "$env"; }
  target_sys="$(get TARGET_SYS)"
  native="$(get STAGING_DIR_NATIVE)"
  sysroot="$(get STAGING_DIR_TARGET)"
  cc="$(get CC)"
  case "$target_sys" in
    i?86-*) emulator=qemu-i386 ;;
    *)      emulator="qemu-${target_sys%%-*}" ;;
  esac

  # run_cross_tests.sh wants one executable per tool; Yocto's CC is a whole
  # command line. The target's loader reads the host's ld.so.cache, so
  # LD_LIBRARY_PATH keeps the libraries inside the sysroot (see BUILDING.md).
  dir="$wrap/$machine"
  mkdir -p "$dir"
  printf '#!/bin/sh\nPATH=%s:$PATH exec %s "$@"\n' "$native/usr/bin/$target_sys" "$cc" > "$dir/cc"
  printf '#!/bin/sh\nexec %s "$@"\n' "$native/usr/bin/$target_sys/$target_sys-ar" > "$dir/ar"
  printf '#!/bin/sh\nexec %s -E LD_LIBRARY_PATH=/usr/lib:/lib "$@"\n' "$emulator" > "$dir/emu"
  chmod +x "$dir"/*

  for flags in "" --use-mlir=std --use-mlir=cminus; do
    out="$(CMC="$native/usr/bin/cmc" CMC_FLAGS="$flags" CROSS_TARGET="$target_sys" \
           CROSS_REQUIRED=1 CROSS_CC="$dir/cc" CROSS_AR="$dir/ar" \
           CROSS_SYSROOT="$sysroot" CROSS_EMU="$dir/emu" \
           bash "$ROOT/tests/run_cross_tests.sh" 2>&1)"
    status=$?
    printf '   %-18s %s\n' "${flags:-LLVM path}" "$(tail -1 <<< "$out")"
    if (( status != 0 )); then
      grep -E '^(FAIL|cross tests:)' <<< "$out" | head -8
      (( failures++ ))
    fi
  done
done

echo "failures: $failures"
(( failures == 0 ))
