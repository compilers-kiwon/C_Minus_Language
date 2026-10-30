SUMMARY = "C-Minus runtime library"
DESCRIPTION = "Definitions of input, output and the negative-subscript \
handler, linked into every program cmc compiles. C- has no I/O statements; \
the specification instead treats these as already declared."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# The same revision as cmc-native, so that the compiler and the runtime it
# links in always come from one commit. PV describes the pin and not the
# branch.
SRCREV = "2f24b8dcd4edec8702a6dd942c996eba701695f2"
PV = "0.2.0"

inherit cmake

# Built for the target with the target's toolchain, so LLVM is not involved
# and must not be pulled in.
EXTRA_OECMAKE = "-DCMINUS_BUILD_COMPILER=OFF"

# A static library only: the archive lands in -staticdev and the main package
# has nothing in it.
ALLOW_EMPTY:${PN} = "1"
