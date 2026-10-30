SUMMARY = "C-Minus compiler"
DESCRIPTION = "A compiler for C-Minus, the teaching language from Louden's \
Compiler Construction. Hand-written scanner and recursive-descent parser, \
LLVM back end, and cross compilation through --target."
HOMEPAGE = "https://github.com/compilers-kiwon/C_Minus_Language"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0813489dfd365ec0eee0107c4d289c63"

SRC_URI = "git://github.com/compilers-kiwon/C_Minus_Language.git;protocol=https;branch=main"
# Pinned to the commit rather than the tag: a tag can be moved, a commit cannot.
# Past v0.1.3, for two things that landed after the tag: cmc working with
# LLVM 23, which openembedded-core master carries, and the MLIR path compiling
# for other targets than the host. PV describes the pin and not the branch.
SRCREV = "94ce6f5153cea98ba3d481fbf6a3992ecb957319"
PV = "0.1.3+git"

DEPENDS = "llvm-native"

inherit cmake native

# The compiler runs on the build host; the runtime belongs to the target and
# is built by cminus-runtime instead.
EXTRA_OECMAKE = "-DCMINUS_BUILD_RUNTIME=OFF"

# --use-mlir, through mlir-native. On by default; it adds an MLIR build, so
# PACKAGECONFIG:remove:pn-cmc-native = "mlir" leaves the LLVM path alone.
PACKAGECONFIG ??= "mlir"
PACKAGECONFIG[mlir] = "-DCMINUS_ENABLE_MLIR=ON -DMLIR_DIR=${STAGING_LIBDIR_NATIVE}/cmake/mlir,-DCMINUS_ENABLE_MLIR=OFF,mlir-native"
