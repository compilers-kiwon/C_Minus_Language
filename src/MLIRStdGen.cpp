#include "cminus/MLIRGen.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

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
  void emitFunction(const FunDecl &decl);

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
    if (decl->kind == NodeKind::FunDecl)
      emitFunction(static_cast<const FunDecl &>(*decl));
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

void StdGenerator::emitFunction(const FunDecl &decl) {
  if (decl.name != "main")
    return;

  mlir::Location where = loc(decl.loc);

  // (a) 함수 타입 만들기: 입력 없음, 결과 i32 
  mlir::FunctionType type = builder_.getFunctionType({}, {builder_.getI32Type()});

  // (b) func.func 만들기. 위치는 loc(decl.loc), 이름은 decl.name
  auto fn = builder_.create<mlir::func::FuncOp>(where, decl.name, type);

  // (c) entry 블록 만들기
  mlir::Block *entry = fn.addEntryBlock();

  // (d) 커서를 기억해 두고 entry 블록으로 옮기기
  mlir::OpBuilder::InsertionGuard guard(builder_);
  builder_.setInsertionPointToStart(entry);

  // (e) 0 상수 만들고 그 값으로 return
  mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(where, builder_.getI32Type(), 0);
  builder_.create<mlir::func::ReturnOp>(where, mlir::ValueRange{zero});
}

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options) {
  return StdGenerator(context, diags, options).run(program);
}

} // namespace cminus
