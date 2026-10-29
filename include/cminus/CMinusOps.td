// The cminus dialect: C-'s structured control flow as ops with regions.
// mlir-tblgen turns this file into C++ (CMinusDialect.*.inc, CMinusOps.*.inc
// in the build directory); CMinusDialect.h and CMinusDialect.cpp include them.

#ifndef CMINUS_OPS
#define CMINUS_OPS

include "mlir/IR/OpBase.td"

def CMinus_Dialect : Dialect {
  let name = "cminus";                    // ops print as cminus.<mnemonic>
  let cppNamespace = "::cminus::dialect"; // generated class: CMinusDialect
  let summary = "Structured control flow for C-";
  let description = [{
    if, while, return and the negative-subscript check, kept as ops with
    regions so that the source structure survives into MLIR. Arithmetic,
    memory and calls use the arith, memref and func dialects as the std
    generator does. The lower-cminus-to-cf pass flattens these ops into cf
    branches before the rest of the lowering runs.
  }];
}

// Every cminus op derives from this, so that it belongs to the dialect above.
class CMinus_Op<string mnemonic, list<Trait> traits = []>
    : Op<CMinus_Dialect, mnemonic, traits>;

// TODO(you), Task 6: define cminus.if, cminus.while, cminus.index_check,
// cminus.yield, cminus.condition and cminus.return here.

#endif // CMINUS_OPS
