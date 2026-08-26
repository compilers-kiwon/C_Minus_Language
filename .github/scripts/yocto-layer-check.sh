#!/bin/bash
# Parse meta-cminus against openembedded-core and check what its recipes
# resolve to.
#
# A real build compiles LLVM from source and takes hours. Parsing is seconds
# and still catches the layer going stale: a renamed CMake option, a recipe
# that stops resolving, a LAYERSERIES_COMPAT that no longer exists.
#
# CI runs this script, so a failure there reproduces here:
#
#     .github/scripts/yocto-layer-check.sh "$PWD/meta-cminus" /tmp/oe
#
set -eo pipefail

LAYER=${1:?usage: yocto-layer-check.sh <path to meta-cminus> [work dir]}
WORK=${2:-$PWD/oe}

banner() { echo; echo "--- $* ---"; }

banner "host"
uname -srm
python3 -V
echo "LANG=${LANG-unset}"
# bitbake stops at the first missing HOSTTOOLS entry, a second in, before it
# reads a line of this layer. rpcgen is the one no base system carries.
for t in rpcgen pzstd zstd chrpath diffstat cpio gawk file wget xz; do
    command -v "$t" > /dev/null || echo "ERROR: $t is not in PATH (bitbake requires it)"
done

banner "bitbake and openembedded-core"
mkdir -p "$WORK"
cd "$WORK"
[ -d bitbake ]           || git clone -q --depth 1 https://git.openembedded.org/bitbake
[ -d openembedded-core ] || git clone -q --depth 1 https://git.openembedded.org/openembedded-core
echo "bitbake           $(git -C bitbake rev-parse --short HEAD)"
echo "openembedded-core $(git -C openembedded-core rev-parse --short HEAD)"

banner "build directory"
export BITBAKEDIR="$WORK/bitbake"
# shellcheck disable=SC1091
source openembedded-core/oe-init-build-env build > /dev/null
grep -q SANITY_TESTED_DISTROS conf/local.conf ||
    echo 'SANITY_TESTED_DISTROS = ""' >> conf/local.conf

banner "add the layer"
bitbake-layers show-layers | grep -q "^cminus " ||
    bitbake-layers add-layer "$LAYER"

banner "parse every recipe"
bitbake -p

banner "what the recipes resolve to"
fails=0
has() {
    if grep -q -- "$1" "$2"; then
        echo "  ok   $3"
    else
        echo "ERROR: $3"
        fails=$((fails + 1))
    fi
}

for r in cmc-native cminus-runtime cminus-hello; do
    bitbake -e "$r" > "$WORK/$r.env"
    has "^PN=\"$r\"" "$WORK/$r.env" "PN is $r"
done

# The compiler is native and needs LLVM; the runtime is neither.
has 'DEPENDS=.*llvm-native'        "$WORK/cmc-native.env"     "cmc-native depends on llvm-native"
has 'CLASSOVERRIDE="class-native"' "$WORK/cmc-native.env"     "cmc-native is native"
has 'CMINUS_BUILD_COMPILER=OFF'    "$WORK/cminus-runtime.env" "the runtime skips the compiler"

# The example must hand the driver a whole command and the distro's link
# flags; without the latter package QA rejects the binary.
sed -n '/^do_compile()/,/^}/p' "$WORK/cminus-hello.env" > "$WORK/hello.sh"
has '--cc "'         "$WORK/hello.sh" "cminus-hello hands cmc a driver command"
has 'hash-style=gnu' "$WORK/hello.sh" "cminus-hello passes the distro link flags"

echo
if [ "$fails" -ne 0 ]; then
    echo "ERROR: $fails check(s) failed"
    exit 1
fi
echo "all checks passed"
