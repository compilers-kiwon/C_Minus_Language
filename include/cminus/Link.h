#ifndef CMINUS_LINK_H
#define CMINUS_LINK_H

#include <string>

namespace cminus {

struct LinkOptions {
  std::string objectPath;
  std::string outputPath;
  /// Archive holding input, output and the negative-subscript handler.
  std::string runtimePath;
  /// C compiler driver to invoke. May carry arguments, because a cross
  /// toolchain is unusable without its sysroot and build systems hand the
  /// driver over as a command line. Empty means: $CMINUS_CC, else cc, clang,
  /// gcc.
  std::string driver;
  /// Passed on as -fuse-ld=<name>. Empty means: use lld if it is installed.
  std::string useLinker;
  bool haveUseLinker = false; ///< the user named a linker, even an empty one
  bool verbose = false;
};

/// Find libcminus_rt.a. Looks at $CMINUS_RUNTIME first, then beside the
/// compiler (the build tree) and in ../lib (an unpacked release), so that
/// neither layout needs configuring.
///
/// `mainAddr` is the address of any symbol in the executable; it is what
/// lets LLVM resolve the real path of the running binary.
std::string findRuntime(const char *argv0, void *mainAddr);

/// Link `objectPath` and the runtime into an executable.
///
/// C- programs are linked by a C compiler driver rather than by calling a
/// linker directly. LLD would replace `ld`, not `cc`; the knowledge of where
/// crt1.o, the dynamic loader and libc live belongs to the driver and differs
/// per distribution. When lld is installed the driver is told to use it.
bool linkExecutable(const LinkOptions &options, std::string &error);

} // namespace cminus

#endif // CMINUS_LINK_H
