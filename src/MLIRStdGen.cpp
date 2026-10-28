#include "cminus/MLIRGen.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"

#include "cminus/AST.h"
#include "cminus/Diagnostic.h"
#include "cminus/Symbol.h"

// This file is the learner's half of the MLIR path. It starts as a skeleton
// that produces an empty module; the TODO(you) notes say what goes where.
// IRGenerator in IRGen.cpp is the reference for every rule.

namespace cminus {

namespace {

class StdGenerator {
public:
  StdGenerator(mlir::MLIRContext &context, DiagnosticEngine &diags,
               const MLIRGenOptions &options)
      : builder_(&context), diags_(diags), options_(options) {}

  mlir::OwningOpRef<mlir::ModuleOp> run(const Program &program);

private:
  /// The MLIR location for a place in the source file.
  mlir::Location loc(SourceLocation where) {
    return mlir::FileLineColLoc::get(builder_.getContext(), options_.moduleName,
                                     where.line, where.column);
  }

  // TODO(you): declarations -- IRGen.cpp's emitGlobal, functionFor,
  // indexErrorFunction, collectLocals and emitFunction.
  //   func.func for each function (main returns i32), func.func private for
  //   input and output, memref.alloca slots at the top of the entry block.

  // TODO(you): statements -- emitCompound, emitStmt, emitIf, emitWhile and
  // emitReturn, with cf.br / cf.cond_br between blocks. Stop emitting a block
  // once it has a terminator (blockOpen in IRGen.cpp).

  // TODO(you): expressions -- emitExpr, emitAssign, emitBinary, emitCall and
  // emitCondition, with arith ops. arith.cmpi gives i1; widen it with
  // arith.extui to get C-'s 0 or 1.

  // TODO(you), Task 4: arrays -- emitGlobal for memref.global,
  // elementAddress / arrayBase with memref.load, memref.store and
  // memref.cast, and checkIndex calling __cminus_index_error then
  // llvm.unreachable.

  mlir::OpBuilder builder_;
  DiagnosticEngine &diags_;
  MLIRGenOptions options_;
  mlir::ModuleOp module_;
};

mlir::OwningOpRef<mlir::ModuleOp> StdGenerator::run(const Program &program) {
  module_ = mlir::ModuleOp::create(loc(SourceLocation{}));
  builder_.setInsertionPointToEnd(module_.getBody());

  for (const auto &decl : program.decls) {
    (void)decl; // TODO(you): emit each global variable and function.
  }

  mlir::OwningOpRef<mlir::ModuleOp> module(module_);
  if (mlir::failed(mlir::verify(*module))) {
    diags_.error(SourceLocation{},
                 "internal error: the generated MLIR module failed "
                 "verification");
    return nullptr;
  }
  return module;
}

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options) {
  return StdGenerator(context, diags, options).run(program);
}

} // namespace cminus
