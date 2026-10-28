#ifndef CMINUS_MLIRGEN_H
#define CMINUS_MLIRGEN_H

#include <memory>
#include <string>

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace cminus {

struct Program;
class DiagnosticEngine;

struct MLIRGenOptions {
  std::string moduleName = "cminus";
  /// Same meaning as IRGenOptions::indexChecks.
  bool indexChecks = true;
};

/// Which generator --use-mlir picks.
enum class MLIRFlavor {
  Std,    // AST -> func, arith, cf and memref
  CMinus, // AST -> the cminus dialect, lowered to the Std dialects first
};

/// Load every dialect either generator or the lowering may create, and the
/// translations that turn the LLVM dialect into LLVM IR.
void loadMLIRDialects(mlir::MLIRContext &context);

/// Lower a program that has already passed semantic analysis to the func,
/// arith, cf and memref dialects. Returns null, after reporting it, only if
/// the result failed verification, which would be a bug in the generator.
mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options);

/// The same, through the cminus dialect.
mlir::OwningOpRef<mlir::ModuleOp>
generateCMinusMLIR(const Program &program, mlir::MLIRContext &context,
                   DiagnosticEngine &diags, const MLIRGenOptions &options);

/// Lower `module` to the LLVM dialect and translate it to LLVM IR. Returns
/// null with `error` set on failure; MLIR has already printed its own
/// diagnostics by then. printAfterAll prints the whole module to stderr after
/// every pass.
std::unique_ptr<llvm::Module> lowerMLIRToLLVM(mlir::ModuleOp module,
                                              MLIRFlavor flavor,
                                              llvm::LLVMContext &llvmContext,
                                              bool printAfterAll,
                                              std::string &error);

} // namespace cminus

#endif // CMINUS_MLIRGEN_H
