#ifndef CMINUS_CMINUSDIALECT_H
#define CMINUS_CMINUSDIALECT_H

#include <memory>

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"

// Generated from CMinusOps.td: the CMinusDialect class, then the op classes.
#include "CMinusDialect.h.inc"
#define GET_OP_CLASSES
#include "CMinusOps.h.inc"

namespace cminus {

/// Rewrite every cminus op into cf, func and llvm ops (lower-cminus-to-cf).
std::unique_ptr<mlir::Pass> createLowerCMinusToCFPass();

} // namespace cminus

#endif // CMINUS_CMINUSDIALECT_H
