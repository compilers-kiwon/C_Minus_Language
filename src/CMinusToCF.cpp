#include "cminus/CMinusDialect.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

namespace cminus {

namespace {

// TODO(you), Task 7: the rewrite patterns go here, one per cminus op, e.g.
//   struct IfLowering : mlir::OpRewritePattern<dialect::IfOp> { ... };
// Each one splits the block at the op, inlines the op's regions into the
// function's CFG (inlineRegionBefore) and replaces the region terminators
// (cminus.yield, cminus.condition, cminus.return) with cf.br, cf.cond_br and
// func.return.

struct LowerCMinusToCFPass
    : mlir::PassWrapper<LowerCMinusToCFPass,
                        mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerCMinusToCFPass)

  // The name --mlir-print-ir-after-all shows for this pass.
  llvm::StringRef getArgument() const override { return "lower-cminus-to-cf"; }
  llvm::StringRef getDescription() const override {
    return "Lower the cminus dialect to cf, func and llvm ops";
  }

  void runOnOperation() override {
    // TODO(you), Task 7: until then the pass changes nothing.
    //   mlir::ConversionTarget target(getContext());
    //   target.addIllegalDialect<dialect::CMinusDialect>();   // must all go
    //   target.addLegalDialect<arith, cf, func, memref, LLVM>(); // may stay
    //   mlir::RewritePatternSet patterns(&getContext());
    //   patterns.add<IfLowering, WhileLowering, ...>(&getContext());
    //   if (mlir::failed(mlir::applyPartialConversion(
    //           getOperation(), target, std::move(patterns))))
    //     signalPassFailure();
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerCMinusToCFPass() {
  return std::make_unique<LowerCMinusToCFPass>();
}

} // namespace cminus
