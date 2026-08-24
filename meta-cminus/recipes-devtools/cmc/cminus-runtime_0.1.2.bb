SUMMARY = "C-Minus runtime library"
DESCRIPTION = "Definitions of input, output and the negative-subscript \
handler, linked into every program cmc compiles. C- has no I/O statements; \
the specification instead treats these as already declared."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
SRCREV = "4ab9b7ff03008d01b9e034ddb0bbb15e0531a580"

S = "${WORKDIR}/git"

inherit cmake

# Built for the target with the target's toolchain, so LLVM is not involved
# and must not be pulled in.
EXTRA_OECMAKE = "-DCMINUS_BUILD_COMPILER=OFF"

# A static library only: the archive lands in -staticdev and the main package
# has nothing in it.
ALLOW_EMPTY:${PN} = "1"
