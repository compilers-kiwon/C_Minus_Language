#include "cminus/MLIRGen.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"

#include "cminus/AST.h"
#include "cminus/CMinusDialect.h"
#include "cminus/Diagnostic.h"

// The cminus generator. It starts as a skeleton that produces an empty
// module.
//
// TODO(you), Task 6: fill it in. Most of it is MLIRStdGen.cpp unchanged:
// functions, globals, slots, expressions, calls and memref accesses stay the
// same ops. What changes is control flow and the subscript check:
//   - if and while become cminus.if / cminus.while, whose bodies are regions
//     nested inside the op, instead of new blocks joined by cf.br and
//     cf.cond_br. The builder's cursor goes into a region's block and comes
//     back out, so no startBlock and no hasNoPredecessors cleanup.
//   - return becomes cminus.return, because func.return may only end a block
//     that belongs directly to the func.func, not one inside a cminus region.
//   - checkIndex becomes one cminus.index_check op instead of a
//     cmpi / cond_br / call / llvm.unreachable diamond.
// lower-cminus-to-cf (CMinusToCF.cpp) later turns these back into what the
// std generator emits directly.

namespace cminus {

mlir::OwningOpRef<mlir::ModuleOp>
generateCMinusMLIR(const Program &program, mlir::MLIRContext &context,
                   DiagnosticEngine &diags, const MLIRGenOptions &options) {
  (void)program;
  mlir::OwningOpRef<mlir::ModuleOp> module = mlir::ModuleOp::create(
      mlir::FileLineColLoc::get(&context, options.moduleName, 0, 0));

  if (mlir::failed(mlir::verify(*module))) {
    diags.error(SourceLocation{}, "internal error: the generated MLIR module "
                                  "failed verification");
    return nullptr;
  }
  return module;
}

} // namespace cminus
