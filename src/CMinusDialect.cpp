#include "cminus/CMinusDialect.h"

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
