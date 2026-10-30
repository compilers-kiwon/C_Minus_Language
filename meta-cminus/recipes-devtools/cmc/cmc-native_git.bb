SUMMARY = "C-Minus compiler"
DESCRIPTION = "A compiler for C-Minus, the teaching language from Louden's \
Compiler Construction. Hand-written scanner and recursive-descent parser, \
LLVM back end, and cross compilation through --target."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# Pinned to v0.2.0's commit rather than the tag: a tag can be moved, a commit
# cannot. PV describes the pin and not the branch.
SRCREV = "2f24b8dcd4edec8702a6dd942c996eba701695f2"
PV = "0.2.0"

DEPENDS = "llvm-native"

inherit cmake native

# The compiler runs on the build host; the runtime belongs to the target and
# is built by cminus-runtime instead.
EXTRA_OECMAKE = "-DCMINUS_BUILD_RUNTIME=OFF"

# --use-mlir, through mlir-native. On by default; it adds an MLIR build, so
# PACKAGECONFIG:remove:pn-cmc-native = "mlir" leaves the LLVM path alone.
PACKAGECONFIG ??= "mlir"
PACKAGECONFIG[mlir] = "-DCMINUS_ENABLE_MLIR=ON -DMLIR_DIR=${STAGING_LIBDIR_NATIVE}/cmake/mlir,-DCMINUS_ENABLE_MLIR=OFF,mlir-native"
