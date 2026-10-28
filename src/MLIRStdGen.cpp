#include "cminus/MLIRGen.h"

#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
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

  // --- 선언
  void declareBuiltins();                  // input, output 외부 선언
  void emitFunction(const FunDecl &decl);  // 함수 하나: 시그니처 + 몸체
  bool blockOpen();                        // 커서가 있는 블록에 아직 종결자가 없는가

  // TODO(you): declarations -- IRGen.cpp's emitGlobal, indexErrorFunction
  // and collectLocals: memref.alloca slots at the top of the entry block.

  // TODO(you): statements -- emitCompound, emitStmt, emitIf, emitWhile and
  // emitReturn, with cf.br / cf.cond_br between blocks. Stop emitting a block
  // once it has a terminator (blockOpen).

  // TODO(you): expressions -- emitExpr, emitAssign, emitBinary, emitCall and
  // emitCondition, with arith ops. arith.cmpi gives i1; widen it with
  // arith.extui to get C-'s 0 or 1.

  // TODO(you), Task 4: arrays -- emitGlobal for memref.global,
  // elementAddress / arrayBase with memref.load, memref.store and
  // memref.cast, and checkIndex calling __cminus_index_error then
  // llvm.unreachable.

  mlir::OpBuilder builder_;   // op를 만드는 "커서 달린 펜"
  DiagnosticEngine &diags_;
  MLIRGenOptions options_;
  mlir::ModuleOp module_;     // 만들고 있는 모듈 (최상위 컨테이너)
};

// ---------------------------------------------------------------- 진입점

mlir::OwningOpRef<mlir::ModuleOp> StdGenerator::run(const Program &program) {
  // 빈 모듈을 만들고 커서를 모듈 블록의 끝에 둔다.
  // 이후 create<>로 만드는 함수들은 모두 여기, 모듈 안에 차례로 쌓인다.
  module_ = mlir::ModuleOp::create(loc(SourceLocation{}));
  builder_.setInsertionPointToEnd(module_.getBody());

  // 프로그램이 호출할 런타임 함수를 먼저 선언한다 (spec 3.10).
  declareBuiltins();

  // 최상위 선언을 소스 순서대로. 전역 변수는 Task 4에서.
  for (const auto &decl : program.decls) {
    if (decl->kind == NodeKind::FunDecl)
      emitFunction(static_cast<const FunDecl &>(*decl));
  }

  // 다 만든 모듈을 검사한다. 실패하면 생성기 버그이고,
  // MLIR이 무엇이 틀렸는지 stderr에 먼저 출력한다.
  mlir::OwningOpRef<mlir::ModuleOp> module(module_);
  if (mlir::failed(mlir::verify(*module))) {
    diags_.error(SourceLocation{},
                 "internal error: the generated MLIR module failed "
                 "verification");
    return nullptr;
  }
  return module;
}

// ------------------------------------------------------------------ 선언

void StdGenerator::declareBuiltins() {
  mlir::Location where = loc(SourceLocation{});
  mlir::Type i32 = builder_.getI32Type();

  // 블록(몸체)을 만들지 않은 func.func = 외부 함수 선언.
  // 정의는 런타임(cminus_rt.c)에 있고, LLVM IR에서는 `declare`가 된다.
  // 몸체 없는 함수는 private이어야 verifier가 받아 준다.
  auto input = builder_.create<mlir::func::FuncOp>(
      where, "input", builder_.getFunctionType(/*inputs=*/{}, /*results=*/{i32}));
  input.setPrivate();                                    // () -> i32

  auto output = builder_.create<mlir::func::FuncOp>(
      where, "output", builder_.getFunctionType(/*inputs=*/{i32}, /*results=*/{}));
  output.setPrivate();                                   // (i32) -> ()
}

void StdGenerator::emitFunction(const FunDecl &decl) {
  mlir::Location where = loc(decl.loc);
  mlir::Type i32 = builder_.getI32Type();

  // (1) 시그니처: IRGen.cpp의 functionFor와 같은 규칙.
  //     매개변수는 모두 int → i32. (배열 매개변수는 Task 4에서 memref<?xi32>)
  llvm::SmallVector<mlir::Type> inputs;
  for (const auto &param : decl.params) {
    (void)param; // TODO(you), Task 4: param->isArray면 memref<?xi32>
    inputs.push_back(i32);
  }
  //     결과: void 함수는 결과 없음. 단 main은 C 런타임에 종료 코드를
  //     돌려줘야 하므로 C-에서 void여도 i32.
  llvm::SmallVector<mlir::Type> results;
  if (decl.returnType != Type::Void || decl.name == "main")
    results.push_back(i32);

  // (2) func.func 생성. 커서가 모듈 끝에 있으므로 모듈 안에 생긴다.
  auto fn = builder_.create<mlir::func::FuncOp>(
      where, decl.name, builder_.getFunctionType(inputs, results));

  // (3) 몸체의 첫 블록. 매개변수 수만큼 블록 인자(%arg0, %arg1, ...)가 생기고,
  //     함수 안에서 매개변수 값은 곧 이 블록 인자다.
  mlir::Block *entry = fn.addEntryBlock();

  // (4) 커서를 entry 블록으로 옮긴다. guard가 함수 끝에서 사라질 때
  //     커서를 모듈 끝으로 되돌려 놓으므로, 다음 함수는 다시 모듈 안에 생긴다.
  mlir::OpBuilder::InsertionGuard guard(builder_);
  builder_.setInsertionPointToStart(entry);

  // (5) 몸체 문장들.
  // TODO(you), 1-c: 매개변수 슬롯과 지역 변수, 그리고 emitCompound(*decl.body).

  // (6) 기본 return. 블록은 반드시 종결자로 끝나야 한다.
  //     몸체가 이미 return으로 끝났으면 블록이 닫혀 있으므로 건너뛴다.
  //     값이 필요한 함수가 return 없이 끝나면 0 (spec 3.5, IRGen.cpp와 같음).
  if (blockOpen()) {
    if (results.empty()) {
      builder_.create<mlir::func::ReturnOp>(where);                 // return
    } else {
      mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(where, i32, 0);
      builder_.create<mlir::func::ReturnOp>(where, mlir::ValueRange{zero});  // return 0
    }
  }
}

bool StdGenerator::blockOpen() {
  // 블록의 마지막 op가 종결자(return, cf.br, ...)가 아니면 아직 열려 있다.
  // IRGen.cpp의 blockOpen()에서 `!block->getTerminator()`에 해당한다.
  mlir::Block *block = builder_.getInsertionBlock();
  return block && !block->mightHaveTerminator();
}

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options) {
  return StdGenerator(context, diags, options).run(program);
}

} // namespace cminus
