// The cminus dialect: C-'s structured control flow as ops with regions.
// mlir-tblgen turns this file into C++ (CMinusDialect.*.inc, CMinusOps.*.inc
// in the build directory); CMinusDialect.h and CMinusDialect.cpp include them.

#ifndef CMINUS_OPS
#define CMINUS_OPS

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

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

// ------------------------------------------------------- structured ops
// Every region holds exactly one block. A block ends in cminus.yield (or
// cminus.condition) to fall through, or in cminus.return to leave the
// function; nested control flow is a nested cminus.if or cminus.while.

def CMinus_IfOp : CMinus_Op<"if", [RecursiveMemoryEffects, NoRegionArguments]> {
  let summary = "if (cond) then-region [else else-region]";
  let description = [{
    Runs the then region when `cond` is true, otherwise the else region, which
    may be absent. Unlike scf.if, either region may end in cminus.return.

    ```mlir
    cminus.if %c {
      ...
      cminus.yield
    } else {
      cminus.return %v : i32
    }
    ```
  }];

  let arguments = (ins I1:$cond);
  let regions = (region SizedRegion<1>:$thenRegion,
                        MaxSizedRegion<1>:$elseRegion);   // empty: no else
  // An optional group anchored on a region prints only when it is not empty.
  let assemblyFormat = "$cond $thenRegion (`else` $elseRegion^)? attr-dict";
}

def CMinus_WhileOp : CMinus_Op<"while", [RecursiveMemoryEffects, NoRegionArguments]> {
  let summary = "while (cond) body";
  let description = [{
    Runs the cond region, which ends in cminus.condition; while that value is
    true, runs the body region and starts over.

    ```mlir
    cminus.while {
      ...
      cminus.condition %c
    } do {
      ...
      cminus.yield
    }
    ```
  }];

  let regions = (region SizedRegion<1>:$condRegion, SizedRegion<1>:$bodyRegion);
  let assemblyFormat = "$condRegion `do` $bodyRegion attr-dict";
}

// ---------------------------------------------------------- terminators

def CMinus_YieldOp : CMinus_Op<"yield",
    [Pure, Terminator, ParentOneOf<["IfOp", "WhileOp"]>]> {
  let summary = "Falls out of a then, else or body region";
  let assemblyFormat = "attr-dict";
}

def CMinus_ConditionOp : CMinus_Op<"condition",
    [Pure, Terminator, HasParent<"WhileOp">]> {
  let summary = "Ends a while's cond region with the loop condition";
  let arguments = (ins I1:$value);
  let assemblyFormat = "$value attr-dict";
}

def CMinus_ReturnOp : CMinus_Op<"return", [Terminator]> {
  let summary = "Returns from the enclosing function, from any depth";
  let description = [{
    func.return may only end a block that belongs to the func.func itself, so
    a return inside a cminus region needs an op of its own. The generator uses
    it at the top level too; lower-cminus-to-cf turns every one into
    func.return once the regions are flattened.
  }];
  let arguments = (ins Optional<I32>:$value);   // absent in a void function
  let assemblyFormat = "($value^ `:` type($value))? attr-dict";
  let hasVerifier = 1;   // ReturnOp::verify() in CMinusDialect.cpp
}

// ------------------------------------------------------------- the rest

def CMinus_IndexCheckOp : CMinus_Op<"index_check"> {
  let summary = "Stops the program if a subscript is negative (spec 3.6)";
  let description = [{
    Calls __cminus_index_error, which does not return, when `index` is less
    than zero. Having no memory-effect trait, it counts as having unknown side
    effects, so nothing deletes it although it has no result.
  }];
  let arguments = (ins I32:$index);
  let assemblyFormat = "$index attr-dict";
}

#endif // CMINUS_OPS
