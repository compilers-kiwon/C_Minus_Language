#include "cminus/MLIRGen.h"

#include <cstdint>

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

  // --- 문장 (1-c). 커서가 있는 블록 끝에 소스 순서대로 붙인다.
  void emitCompound(const CompoundStmt &block);  // { ... }
  void emitStmt(const Stmt &stmt);               // 문장 종류별로 나눠 보내기
  void emitReturn(const ReturnStmt &stmt);       // return; / return e;

  // --- 식 (1-c). 피연산자를 먼저 만들고 그 값으로 연산 op를 만든다 (후위 순회).
  mlir::Value emitExpr(const Expr &expr);          // 식 하나 → i32 값 하나
  mlir::Value emitBinary(const BinaryExpr &expr);  // + - * / 와 비교
  mlir::Value emitCall(const CallExpr &expr);      // f(a, b)
  mlir::Value int32(SourceLocation where, long long value);  // i32 상수
  void notYet(SourceLocation where, const char *what);       // 아직 못 만드는 노드

  // TODO(you), 1-d: collectLocals -- memref.alloca slots at the top of the
  // entry block; VarExpr (memref.load) and emitAssign (memref.store).

  // TODO(you), 1-e: emitIf, emitWhile and emitCondition, with cf.br /
  // cf.cond_br between blocks.

  // TODO(you), Task 4: arrays -- emitGlobal for memref.global,
  // elementAddress / arrayBase with memref.load, memref.store and
  // memref.cast, and checkIndex calling __cminus_index_error then
  // llvm.unreachable.

  mlir::OpBuilder builder_;   // op를 만드는 "커서 달린 펜"
  DiagnosticEngine &diags_;
  MLIRGenOptions options_;
  mlir::ModuleOp module_;     // 만들고 있는 모듈 (최상위 컨테이너)
  mlir::func::FuncOp function_;  // 지금 몸체를 채우는 함수 (emitReturn이 결과 타입을 본다)
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
  //     몸체보다 먼저 만들어 두므로, 몸체 안의 재귀 호출도 이 함수를 찾는다.
  auto fn = builder_.create<mlir::func::FuncOp>(
      where, decl.name, builder_.getFunctionType(inputs, results));
  function_ = fn;

  // (3) 몸체의 첫 블록. 매개변수 수만큼 블록 인자(%arg0, %arg1, ...)가 생기고,
  //     함수 안에서 매개변수 값은 곧 이 블록 인자다.
  mlir::Block *entry = fn.addEntryBlock();

  // (4) 커서를 entry 블록으로 옮긴다. guard가 함수 끝에서 사라질 때
  //     커서를 모듈 끝으로 되돌려 놓으므로, 다음 함수는 다시 모듈 안에 생긴다.
  mlir::OpBuilder::InsertionGuard guard(builder_);
  builder_.setInsertionPointToStart(entry);

  // (5) 몸체 문장들. 커서는 entry 블록 안에 있고, 문장마다 그 끝에 op가 붙는다.
  // TODO(you), 1-d: 그 전에 매개변수 슬롯과 지역 변수(collectLocals).
  emitCompound(*decl.body);

  // (6) 기본 return. 블록은 반드시 종결자로 끝나야 한다.
  //     몸체가 이미 return으로 끝났으면 블록이 닫혀 있으므로 건너뛴다.
  //     값이 필요한 함수가 return 없이 끝나면 0 (spec 3.5, IRGen.cpp와 같음).
  if (blockOpen()) {
    if (results.empty()) {
      builder_.create<mlir::func::ReturnOp>(where);                 // return
    } else {
      mlir::Value zero = int32(decl.loc, 0);
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

// ------------------------------------------------------------------ 문장

void StdGenerator::emitCompound(const CompoundStmt &block) {
  // 지역 변수 슬롯은 1-d에서 entry 블록 맨 앞에 미리 만든다. 여기서는 문장만.
  for (const auto &stmt : block.stmts) {
    emitStmt(*stmt);
    if (!blockOpen())
      break;   // return 뒤의 문장은 죽은 코드: 종결자 뒤에는 op를 붙일 수 없다
  }
}

void StdGenerator::emitStmt(const Stmt &stmt) {
  switch (stmt.kind) {
  case NodeKind::CompoundStmt:             // { ... } 안의 { ... }
    emitCompound(static_cast<const CompoundStmt &>(stmt));
    break;
  case NodeKind::ExprStmt: {               // f(x);  x = 1;  ;
    const auto &node = static_cast<const ExprStmt &>(stmt);
    if (node.expr)
      emitExpr(*node.expr);                // 값은 만들고 버린다
    break;
  }
  case NodeKind::ReturnStmt:
    emitReturn(static_cast<const ReturnStmt &>(stmt));
    break;
  case NodeKind::IfStmt:                   // 1-e
    notYet(stmt.loc, "if statements");
    break;
  case NodeKind::WhileStmt:                // 1-e
    notYet(stmt.loc, "while statements");
    break;
  default:
    break;
  }
}

void StdGenerator::emitReturn(const ReturnStmt &stmt) {
  mlir::Location where = loc(stmt.loc);

  // C-의 반환 타입이 아니라 MLIR 함수의 결과 타입을 본다.
  // 그래야 void main의 `return;`이 `return 0`이 된다 (IRGen.cpp와 같음).
  if (function_.getFunctionType().getNumResults() == 0) {
    if (stmt.value)
      emitExpr(*stmt.value);               // void 함수의 `return f();`: f는 호출한다
    builder_.create<mlir::func::ReturnOp>(where);                    // return
    return;
  }

  mlir::Value value = stmt.value ? emitExpr(*stmt.value) : int32(stmt.loc, 0);
  builder_.create<mlir::func::ReturnOp>(where, mlir::ValueRange{value});  // return %v
  // 이제 블록에 종결자가 생겼으므로 blockOpen()이 false가 되고,
  // emitCompound는 남은 문장을 건너뛴다.
}

// -------------------------------------------------------------------- 식

mlir::Value StdGenerator::emitExpr(const Expr &expr) {
  switch (expr.kind) {
  case NodeKind::NumExpr:                  // 42 → arith.constant 42 : i32
    return int32(expr.loc, static_cast<const NumExpr &>(expr).value);
  case NodeKind::BinaryExpr:
    return emitBinary(static_cast<const BinaryExpr &>(expr));
  case NodeKind::CallExpr:
    return emitCall(static_cast<const CallExpr &>(expr));
  case NodeKind::VarExpr:                  // 1-d
    notYet(expr.loc, "variables");
    return int32(expr.loc, 0);
  case NodeKind::AssignExpr:               // 1-d
    notYet(expr.loc, "assignments");
    return int32(expr.loc, 0);
  default:
    return int32(expr.loc, 0);
  }
}

mlir::Value StdGenerator::emitBinary(const BinaryExpr &expr) {
  mlir::Location where = loc(expr.opLoc);

  // 피연산자 먼저: 왼쪽, 오른쪽 순서로 op가 블록에 붙는다.
  mlir::Value lhs = expr.lhs ? emitExpr(*expr.lhs) : int32(expr.loc, 0);
  mlir::Value rhs = expr.rhs ? emitExpr(*expr.rhs) : int32(expr.loc, 0);

  // 산술: 결과가 바로 i32.
  switch (expr.op) {
  case BinaryOp::Add:
    return builder_.create<mlir::arith::AddIOp>(where, lhs, rhs);   // arith.addi
  case BinaryOp::Sub:
    return builder_.create<mlir::arith::SubIOp>(where, lhs, rhs);   // arith.subi
  case BinaryOp::Mul:
    return builder_.create<mlir::arith::MulIOp>(where, lhs, rhs);   // arith.muli
  case BinaryOp::Div:
    // spec 3.8: 0 쪽으로 자르는 부호 있는 나눗셈 = divsi (LLVM의 sdiv).
    return builder_.create<mlir::arith::DivSIOp>(where, lhs, rhs);  // arith.divsi
  default:
    break;
  }

  // 비교: arith.cmpi는 i1을 낸다. 술어(predicate)가 비교 종류를 정한다.
  // s = signed. IRGen.cpp의 CreateICmpSLT 등과 하나씩 대응한다.
  mlir::arith::CmpIPredicate predicate;
  switch (expr.op) {
  case BinaryOp::LessThan:     predicate = mlir::arith::CmpIPredicate::slt; break;
  case BinaryOp::LessEqual:    predicate = mlir::arith::CmpIPredicate::sle; break;
  case BinaryOp::GreaterThan:  predicate = mlir::arith::CmpIPredicate::sgt; break;
  case BinaryOp::GreaterEqual: predicate = mlir::arith::CmpIPredicate::sge; break;
  case BinaryOp::Equal:        predicate = mlir::arith::CmpIPredicate::eq;  break;
  default:                     predicate = mlir::arith::CmpIPredicate::ne;  break;
  }
  mlir::Value compared =
      builder_.create<mlir::arith::CmpIOp>(where, predicate, lhs, rhs);  // i1
  // spec 3.7: C-에는 bool이 없고 비교의 값은 int 1 또는 0.
  // i1을 0으로 채워 넓힌다 (extui = zero-extend, LLVM의 zext).
  return builder_.create<mlir::arith::ExtUIOp>(where, builder_.getI32Type(),
                                               compared);                 // i32
}

mlir::Value StdGenerator::emitCall(const CallExpr &expr) {
  mlir::Location where = loc(expr.loc);

  // 호출 대상은 모듈의 심볼 테이블에서 이름(@callee)으로 찾는다.
  // 의미 분석이 끝났으므로 모든 이름은 이미 선언되어 있다
  // (input/output은 declareBuiltins, 나머지는 소스 순서대로 emitFunction).
  auto callee = module_.lookupSymbol<mlir::func::FuncOp>(expr.callee);
  if (!callee) {
    diags_.error(expr.loc, "internal error: no MLIR function for '" +
                               expr.callee + "'");
    return int32(expr.loc, 0);
  }

  // 인자도 피연산자: 왼쪽부터 먼저 만든다.
  // TODO(you), Task 4: 배열 매개변수 자리에는 배열 자체(memref)를 넘긴다.
  llvm::SmallVector<mlir::Value> args;
  for (const auto &arg : expr.args)
    args.push_back(emitExpr(*arg));

  auto call = builder_.create<mlir::func::CallOp>(where, callee, args);  // func.call @f(...)
  // void 함수 호출은 결과가 없다. 의미 분석이 void 값을 쓰는 식을 막으므로
  // 빈 Value는 ExprStmt나 void 함수의 return처럼 값을 버리는 곳으로만 간다.
  return call.getNumResults() ? call.getResult(0) : mlir::Value();
}

mlir::Value StdGenerator::int32(SourceLocation where, long long value) {
  // IRGen.cpp의 int32()처럼 i32로 자른다.
  return builder_.create<mlir::arith::ConstantIntOp>(
      loc(where), builder_.getI32Type(), static_cast<std::int32_t>(value));
}

void StdGenerator::notYet(SourceLocation where, const char *what) {
  // 아직 만들지 않은 노드를 조용히 0으로 바꾸면 틀린 프로그램이 돌아간다.
  // 오류로 멈추게 해서, 단계마다 무엇이 남았는지 드러나게 한다.
  diags_.error(where, std::string("the MLIR generator does not handle ") +
                          what + " yet");
}

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options) {
  return StdGenerator(context, diags, options).run(program);
}

} // namespace cminus
