SUMMARY = "C-Minus runtime library"
DESCRIPTION = "Definitions of input, output and the negative-subscript \
handler, linked into every program cmc compiles. C- has no I/O statements; \
the specification instead treats these as already declared."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# Past v0.1.2: the split into CMINUS_BUILD_COMPILER and
# CMINUS_BUILD_RUNTIME, which these two recipes depend on, and the
# licence file, both landed after that tag. Behind v0.1.3, which changed
# nothing this recipe builds, so PV describes the pin and not the branch.
SRCREV = "bbca60e208fa27b3213a2e75d1b72aad5d500d5d"
PV = "0.1.2+git"

inherit cmake

# Built for the target with the target's toolchain, so LLVM is not involved
# and must not be pulled in.
EXTRA_OECMAKE = "-DCMINUS_BUILD_COMPILER=OFF"

# A static library only: the archive lands in -staticdev and the main package
# has nothing in it.
ALLOW_EMPTY:${PN} = "1"
