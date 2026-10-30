SUMMARY = "C-Minus compiler"
DESCRIPTION = "A compiler for C-Minus, the teaching language from Louden's \
Compiler Construction. Hand-written scanner and recursive-descent parser, \
LLVM back end, and cross compilation through --target."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# Pinned to the commit rather than the tag: a tag can be moved, a commit cannot.
# Past v0.1.3: LLVM 23, which openembedded-core master carries, made
# BasicBlock::getTerminator() assert on a block that has none, and cmc used
# it to test for exactly that; the fix landed after the tag. PV describes
# the pin and not the branch.
SRCREV = "b1a1f1e3c5d5ebef8b4d43a3e6ec9354d53a75e5"
PV = "0.1.3+git"

DEPENDS = "llvm-native"

inherit cmake native

# The compiler runs on the build host; the runtime belongs to the target and
# is built by cminus-runtime instead.
EXTRA_OECMAKE = "-DCMINUS_BUILD_RUNTIME=OFF"
