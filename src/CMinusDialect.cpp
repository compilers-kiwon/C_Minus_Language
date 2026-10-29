#include "cminus/CMinusDialect.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/OpImplementation.h"

// The dialect's constructor and TypeID.
#include "CMinusDialect.cpp.inc"
// The op classes' method bodies (builders, verifiers, parsers, printers).
#define GET_OP_CLASSES
#include "CMinusOps.cpp.inc"

void cminus::dialect::CMinusDialect::initialize() {
  // Register every op the .td file defines; the list comes from mlir-tblgen,
  // so a new op needs no change here.
  addOperations<
#define GET_OP_LIST
#include "CMinusOps.cpp.inc"
      >();
}

// hasVerifier = 1 declares this; the type constraint (Optional<I32>) is
// already checked by generated code before it runs.
mlir::LogicalResult cminus::dialect::ReturnOp::verify() {
  auto function = (*this)->getParentOfType<mlir::func::FuncOp>();
  if (!function)
    return emitOpError("must be inside a func.func");
  const unsigned expected = function.getFunctionType().getNumResults();
  const unsigned given = getValue() ? 1 : 0;
  if (given != expected)
    return emitOpError("returns ")
           << given << " value(s) but '" << function.getName() << "' has "
           << expected << " result(s)";
  return mlir::success();
}
