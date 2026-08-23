#ifndef CMINUS_IRGEN_H
#define CMINUS_IRGEN_H

#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace cminus {

struct Program;
class DiagnosticEngine;

struct IRGenOptions {
  std::string moduleName = "cminus";
  /// Triple and textual data layout of the target, from Target. Both are
  /// stamped on the module before anything is generated: the data layout is
  /// what says how wide a pointer is, and a getelementptr index has to match.
  std::string targetTriple;
  std::string dataLayout;
  /// Spec 3.6 makes a negative subscript a run-time error that stops the
  /// program. Turning this off leaves those checks out of the emitted IR,
  /// which makes it much easier to read.
  bool indexChecks = true;
};

/// Lower a program that has already passed semantic analysis to an LLVM
/// module. Returns null only if the result failed verification, which would
/// be a bug in the generator rather than in the input.
std::unique_ptr<llvm::Module> generateIR(const Program &program,
                                         llvm::LLVMContext &context,
                                         DiagnosticEngine &diags,
                                         const IRGenOptions &options);

} // namespace cminus

#endif // CMINUS_IRGEN_H
