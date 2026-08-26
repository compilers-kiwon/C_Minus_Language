SUMMARY = "C-Minus compiler"
DESCRIPTION = "A compiler for C-Minus, the teaching language from Louden's \
Compiler Construction. Hand-written scanner and recursive-descent parser, \
LLVM back end, and cross compilation through --target."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# Pinned to the commit rather than the tag: a tag can be moved, a commit cannot.
# Past v0.1.2: the split into CMINUS_BUILD_COMPILER and
# CMINUS_BUILD_RUNTIME, which these two recipes depend on, and the
# licence file, both landed after that tag. Behind v0.1.3, which changed
# nothing this recipe builds, so PV describes the pin and not the branch.
SRCREV = "bbca60e208fa27b3213a2e75d1b72aad5d500d5d"
PV = "0.1.2+git"

DEPENDS = "llvm-native"

inherit cmake native

# The compiler runs on the build host; the runtime belongs to the target and
# is built by cminus-runtime instead.
EXTRA_OECMAKE = "-DCMINUS_BUILD_RUNTIME=OFF"
