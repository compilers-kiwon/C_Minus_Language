#include "cminus/MLIRGen.h"

#include "cminus/CMinusDialect.h"
#include "llvm/IR/Module.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

namespace cminus {

void loadMLIRDialects(mlir::MLIRContext &context) {
  context.loadDialect<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                      mlir::func::FuncDialect, mlir::LLVM::LLVMDialect,
                      mlir::memref::MemRefDialect, dialect::CMinusDialect>();
  mlir::registerBuiltinDialectTranslation(context);
  mlir::registerLLVMDialectTranslation(context);
}

std::unique_ptr<llvm::Module> lowerMLIRToLLVM(mlir::ModuleOp module,
                                              MLIRFlavor flavor,
                                              llvm::LLVMContext &llvmContext,
                                              bool printAfterAll,
                                              std::string &error) {
  mlir::MLIRContext *context = module.getContext();
  mlir::PassManager pm(context);
  if (printAfterAll) {
    // Printing the whole module needs the passes to run one at a time.
    context->disableMultithreading();
    pm.enableIRPrinting(
        /*shouldPrintBeforePass=*/[](mlir::Pass *, mlir::Operation *) {
          return false;
        },
        /*shouldPrintAfterPass=*/[](mlir::Pass *, mlir::Operation *) {
          return true;
        },
        /*printModuleScope=*/true, /*printAfterOnlyOnChange=*/false);
  }

  // The cminus generator's output first becomes what the std generator emits.
  if (flavor == MLIRFlavor::CMinus)
    pm.addPass(createLowerCMinusToCFPass());

  // One pass per dialect rather than convert-to-llvm, so that
  // --mlir-print-ir-after-all shows each dialect going away in turn.
  pm.addPass(mlir::createFinalizeMemRefToLLVMConversionPass());
  pm.addPass(mlir::createArithToLLVMConversionPass());
  pm.addPass(mlir::createConvertControlFlowToLLVMPass());
  pm.addPass(mlir::createConvertFuncToLLVMPass());
  pm.addPass(mlir::createReconcileUnrealizedCastsPass());

  if (mlir::failed(pm.run(module))) {
    error = "lowering the MLIR module to the LLVM dialect failed";
    return nullptr;
  }

  std::unique_ptr<llvm::Module> result =
      mlir::translateModuleToLLVMIR(module, llvmContext);
  if (!result)
    error = "translating the LLVM dialect to LLVM IR failed";
  return result;
}

} // namespace cminus
