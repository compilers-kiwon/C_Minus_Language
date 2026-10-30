SUMMARY = "MLIR, for the build host"
DESCRIPTION = "MLIR, the multi-level IR framework from the LLVM project, built \
against the LLVM that llvm-native provides. openembedded-core builds LLVM \
without it; cmc-native needs it for --use-mlir."
HOMEPAGE = "https://mlir.llvm.org"
SECTION = "devel"

# The same llvm-project checkout, version and settings as openembedded-core's
# llvm and lld recipes, so that MLIR matches the LLVM it links against.
require recipes-devtools/clang/common-clang.inc
require recipes-devtools/clang/common-source.inc

# Not LLVMMD5SUM: mlir's copy leaves out the legacy NCSA licence that llvm's has.
LIC_FILES_CHKSUM = "file://mlir/LICENSE.TXT;md5=7ef1911355f6e321fa0d7971689b7a67"

DEPENDS = "llvm-tblgen-native llvm-native"

inherit cmake pkgconfig native

# A standalone build of the mlir directory, as lld_git.bb builds lld: it finds
# the installed LLVM through find_package instead of building LLVM again.
# MLIR_LINK_MLIR_DYLIB would follow llvm-native's LLVM_LINK_LLVM_DYLIB=ON and
# link the tools against a libMLIR.so that a standalone build never makes.
# LLVM_BUILD_UTILS is what makes add_tablegen install and export mlir-tblgen,
# which cmc's own TableGen step runs; left unset it is built but not installed.
OECMAKE_SOURCEPATH = "${S}/mlir"

EXTRA_OECMAKE += "-DCMAKE_BUILD_TYPE=Release \
                  -DLLVM_INCLUDE_TESTS=OFF \
                  -DMLIR_INCLUDE_TESTS=OFF \
                  -DMLIR_INCLUDE_INTEGRATION_TESTS=OFF \
                  -DMLIR_ENABLE_BINDINGS_PYTHON=OFF \
                  -DMLIR_LINK_MLIR_DYLIB=OFF \
                  -DLLVM_BUILD_UTILS=ON \
                  -DLLVM_TABLEGEN_EXE=${STAGING_BINDIR_NATIVE}/llvm-tblgen"

# Generate every TableGen header before compiling anything. In a standalone
# build the mlir-irdl-to-cpp object waits only for its link libraries, so
# with enough parallelism it compiles first and fails on
# mlir/IR/BuiltinLocationAttributes.h.inc (seen with 23.1.3).
do_compile:prepend() {
    cmake_runcmake_build --target mlir-generic-headers
    cmake_runcmake_build --target mlir-headers
}
