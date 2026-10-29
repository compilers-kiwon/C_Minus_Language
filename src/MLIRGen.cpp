#include "cminus/MLIRGen.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Verifier.h"

#include "cminus/AST.h"
#include "cminus/CMinusDialect.h"
#include "cminus/Diagnostic.h"
#include "cminus/Symbol.h"

// This file is the learner's half of the MLIR path: one generator for both
// --use-mlir flavors. IRGenerator in IRGen.cpp is the reference for every rule.
//
// 두 flavor는 함수, 전역, 슬롯, 식, 호출, memref 접근이 모두 같다.
// 다른 곳은 제어 흐름과 첨자 검사 네 군데뿐이고, 각 함수 안에서 나란히 보인다.
//   Std    : if/while → 블록 + cf.br/cf.cond_br, return → func.return,
//            첨자 검사 → cmpi/cond_br/call/llvm.unreachable 마름모
//   CMinus : if/while → region을 가진 cminus.if/cminus.while,
//            return → cminus.return, 첨자 검사 → cminus.index_check 하나
// CMinus의 결과는 lower-cminus-to-cf(CMinusToCF.cpp)가 Std의 모양으로 펼친다.

namespace cminus {

namespace {

class MLIRGenerator {
public:
  MLIRGenerator(mlir::MLIRContext &context, DiagnosticEngine &diags,
                const MLIRGenOptions &options, MLIRFlavor flavor)
      : builder_(&context), diags_(diags), options_(options),
        structured_(flavor == MLIRFlavor::CMinus) {}

  mlir::OwningOpRef<mlir::ModuleOp> run(const Program &program);

private:
  /// The MLIR location for a place in the source file.
  mlir::Location loc(SourceLocation where) {
    return mlir::FileLineColLoc::get(builder_.getContext(), options_.moduleName,
                                     where.line, where.column);
  }

  // --- 선언
  void declareBuiltins();                  // input, output 외부 선언
  void emitGlobal(const VarDecl &decl);    // 전역 변수 → memref.global
  void emitFunction(const FunDecl &decl);  // 함수 하나: 시그니처 + 몸체
  void collectLocals(const Stmt &stmt, std::vector<const VarDecl *> &out);
  bool blockOpen();                        // 커서가 있는 블록에 아직 종결자가 없는가
  void startBlock(mlir::Block *block);     // 블록을 함수 끝에 붙이고 커서를 그 안으로 (1-e)

  // --- 문장 (1-c). 커서가 있는 블록 끝에 소스 순서대로 붙인다.
  void emitCompound(const CompoundStmt &block);  // { ... }
  void emitStmt(const Stmt &stmt);               // 문장 종류별로 나눠 보내기
  void emitIf(const IfStmt &stmt);               // if (c) s [else s]   (1-e)
  void emitWhile(const WhileStmt &stmt);         // while (c) s         (1-e)
  void emitReturn(const ReturnStmt &stmt);       // return; / return e;
  void emitRegion(mlir::Region &region, const Stmt *stmt);  // cminus region 하나 채우기
  void createReturn(mlir::Location where, mlir::Value value);  // func.return | cminus.return

  // --- 식 (1-c). 피연산자를 먼저 만들고 그 값으로 연산 op를 만든다 (후위 순회).
  mlir::Value emitExpr(const Expr &expr);          // 식 하나 → i32 값 하나
  mlir::Value emitAssign(const AssignExpr &expr);  // x = e  (1-d)
  mlir::Value emitBinary(const BinaryExpr &expr);  // + - * / 와 비교
  mlir::Value emitCall(const CallExpr &expr);      // f(a, b)
  mlir::Value emitCondition(const Expr &expr);     // 참/거짓 판정 → i1  (1-e)
  /// 변수의 저장소(memref)를 돌려주고, a[i]이면 첨자를 indices에 넣는다.
  mlir::Value addressOf(const VarExpr &expr,
                        llvm::SmallVectorImpl<mlir::Value> &indices);
  mlir::Value storageOf(const Symbol &symbol, SourceLocation where);  // 첨자 없는 저장소
  mlir::MemRefType storageType(const Symbol &symbol);  // memref<i32>, <Nxi32>, <?xi32>
  void checkIndex(mlir::Value index, SourceLocation where);  // 음수 첨자 검사 (Task 4)
  mlir::Value int32(SourceLocation where, long long value);  // i32 상수

  mlir::OpBuilder builder_;   // op를 만드는 "커서 달린 펜"
  DiagnosticEngine &diags_;
  MLIRGenOptions options_;
  bool structured_;           // true: --use-mlir=cminus (제어 흐름을 cminus op로)
  mlir::ModuleOp module_;    // 만들고 있는 모듈 (최상위 컨테이너)
  mlir::func::FuncOp function_;  // 지금 몸체를 채우는 함수 (emitReturn이 결과 타입을 본다)
  /// 매개변수와 지역 변수의 저장소. IRGen.cpp의 addresses_에 해당한다.
  /// 전역은 여기에 없고, 쓸 때마다 memref.get_global로 꺼낸다.
  std::unordered_map<const Symbol *, mlir::Value> slots_;
};

// ---------------------------------------------------------------- 진입점

mlir::OwningOpRef<mlir::ModuleOp> MLIRGenerator::run(const Program &program) {
  // 빈 모듈을 만들고 커서를 모듈 블록의 끝에 둔다.
  // 이후 create<>로 만드는 함수와 전역은 모두 여기, 모듈 안에 차례로 쌓인다.
  module_ = mlir::ModuleOp::create(loc(SourceLocation{}));
  builder_.setInsertionPointToEnd(module_.getBody());

  // 프로그램이 호출할 런타임 함수를 먼저 선언한다 (spec 3.10).
  declareBuiltins();

  // 최상위 선언을 소스 순서대로: 전역 변수와 함수.
  for (const auto &decl : program.decls) {
    if (decl->kind == NodeKind::VarDecl)
      emitGlobal(static_cast<const VarDecl &>(*decl));
    else if (decl->kind == NodeKind::FunDecl)
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

void MLIRGenerator::declareBuiltins() {
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

  // 음수 첨자를 만나면 부르는 런타임 함수 (spec 3.6). 돌아오지 않는다.
  // -fno-index-check이면 부를 일이 없으므로 선언도 하지 않는다.
  if (options_.indexChecks) {
    auto indexError = builder_.create<mlir::func::FuncOp>(
        where, "__cminus_index_error",
        builder_.getFunctionType(/*inputs=*/{i32}, /*results=*/{}));
    indexError.setPrivate();                             // (i32) -> ()
  }
}

void MLIRGenerator::emitGlobal(const VarDecl &decl) {
  if (!decl.symbol)
    return;

  // 스칼라 전역 = 원소가 하나인 0차원 memref, 배열 전역 = 1차원 memref.
  // 지역 변수와 같은 타입이라서 load/store 코드는 전역과 지역을 구분하지 않는다.
  //   memref.global @x : memref<i32>   = dense<0>
  //   memref.global @a : memref<5xi32> = dense<0>
  // 초기값 dense<0>은 "memref와 같은 모양의 tensor 상수, 모든 원소가 0".
  // 값을 하나만 주면 모든 원소에 퍼진다(splat). C-의 전역은 0으로 시작한다
  // (IRGen.cpp가 getNullValue로 0을 넣는 것과 같음).
  mlir::Type i32 = builder_.getI32Type();
  mlir::MemRefType type = storageType(*decl.symbol);
  auto zero = mlir::DenseElementsAttr::get(
      mlir::RankedTensorType::get(type.getShape(), i32),
      llvm::ArrayRef<mlir::Attribute>{builder_.getI32IntegerAttr(0)});
  builder_.create<mlir::memref::GlobalOp>(
      loc(decl.loc), decl.name, /*sym_visibility=*/mlir::StringAttr(), type,
      zero, /*constant=*/false, /*alignment=*/mlir::IntegerAttr());
}

void MLIRGenerator::emitFunction(const FunDecl &decl) {
  mlir::Location where = loc(decl.loc);
  mlir::Type i32 = builder_.getI32Type();

  // (1) 시그니처: IRGen.cpp의 functionFor와 같은 규칙.
  //     int 매개변수 → i32. 배열 매개변수 → memref<?xi32>: 크기를 모르는
  //     1차원 배열. 호출하는 쪽의 배열을 그대로 받는다 (참조 전달, spec 3.3).
  llvm::SmallVector<mlir::Type> inputs;
  for (const auto &param : decl.params) {
    if (param->isArray)
      inputs.push_back(mlir::MemRefType::get({mlir::ShapedType::kDynamic}, i32));
    else
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

  // (5) 저장소: 몸체의 어떤 문장보다 먼저, entry 블록 맨 위에 모아 만든다.
  //     루프 안에서 선언된 변수도 여기서 한 번만 할당되어 반복마다 새로
  //     할당되지 않는다 (IRGen.cpp와 같음).
  //  (5a) 매개변수: 블록 인자는 값일 뿐이라 대입할 수 없다. 그래서 슬롯을 만들고
  //       인자를 저장해 둔다. 이후 매개변수 읽기/쓰기는 모두 이 슬롯을 거친다.
  //       배열 매개변수는 예외: 인자 자체가 이미 memref(저장소)이고, C-에서는
  //       배열 이름에 대입할 수 없으므로 슬롯 없이 블록 인자를 그대로 쓴다.
  for (unsigned i = 0; i < decl.params.size(); ++i) {
    const ParamDecl &param = *decl.params[i];
    if (!param.symbol)
      continue;
    if (param.isArray) {
      slots_[param.symbol] = entry->getArgument(i);    // memref<?xi32>
      continue;
    }
    mlir::Value slot = builder_.create<mlir::memref::AllocaOp>(
        loc(param.loc), storageType(*param.symbol));    // memref<i32>
    builder_.create<mlir::memref::StoreOp>(loc(param.loc), entry->getArgument(i), slot);
    slots_[param.symbol] = slot;
  }
  //  (5b) 지역 변수: 중첩 블록까지 모두 모아 슬롯을 만든다. 초기값은 없다.
  //       int x → memref<i32>, int a[4] → memref<4xi32>.
  std::vector<const VarDecl *> locals;
  collectLocals(*decl.body, locals);
  for (const VarDecl *local : locals) {
    if (!local->symbol)
      continue;
    slots_[local->symbol] = builder_.create<mlir::memref::AllocaOp>(
        loc(local->loc), storageType(*local->symbol));
  }

  // (6) 몸체 문장들. 커서는 entry 블록 안, 슬롯들 뒤에 있다.
  emitCompound(*decl.body);

  // (7) 기본 return. 블록은 반드시 종결자로 끝나야 한다.
  //     몸체가 이미 return으로 끝났으면 블록이 닫혀 있으므로 건너뛴다.
  //     값이 필요한 함수가 return 없이 끝나면 0 (spec 3.5, IRGen.cpp와 같음).
  if (blockOpen())
    createReturn(where, results.empty() ? mlir::Value() : int32(decl.loc, 0));
}

void MLIRGenerator::collectLocals(const Stmt &stmt,
                                  std::vector<const VarDecl *> &out) {
  // IRGen.cpp의 collectLocals와 같다: { } 안의 선언을 if/while 속까지 찾아 모은다.
  // 이름이 같은 변수라도 선언마다 Symbol이 다르므로, 안쪽 x와 바깥쪽 x는
  // 서로 다른 슬롯을 받는다 (scope 테스트).
  switch (stmt.kind) {
  case NodeKind::CompoundStmt: {
    const auto &block = static_cast<const CompoundStmt &>(stmt);
    for (const auto &decl : block.localDecls)
      out.push_back(decl.get());
    for (const auto &inner : block.stmts)
      collectLocals(*inner, out);
    break;
  }
  case NodeKind::IfStmt: {
    const auto &node = static_cast<const IfStmt &>(stmt);
    if (node.thenStmt)
      collectLocals(*node.thenStmt, out);
    if (node.elseStmt)
      collectLocals(*node.elseStmt, out);
    break;
  }
  case NodeKind::WhileStmt: {
    const auto &node = static_cast<const WhileStmt &>(stmt);
    if (node.body)
      collectLocals(*node.body, out);
    break;
  }
  default:
    break;
  }
}

bool MLIRGenerator::blockOpen() {
  // 블록의 마지막 op가 종결자(return, cf.br, ...)가 아니면 아직 열려 있다.
  // IRGen.cpp의 blockOpen()에서 `!block->getTerminator()`에 해당한다.
  mlir::Block *block = builder_.getInsertionBlock();
  return block && !block->mightHaveTerminator();
}

void MLIRGenerator::startBlock(mlir::Block *block) {
  // if/while는 블록을 `new mlir::Block`으로 먼저 만들어 둔다. 분기(cf.br)가
  // 아직 채우지 않은 블록도 가리킬 수 있어야 하기 때문이다. 이 블록들은
  // 아직 어느 region에도 속하지 않는다. 채울 차례가 되면 함수 region의 끝에
  // 붙이고(region이 소유권을 가져간다) 커서를 그 블록 안으로 옮긴다.
  // 그래서 블록이 놓이는 순서가 소스 순서와 같아진다.
  // IRGen.cpp가 if.end를 분리된 채 만들었다가 나중에 insert하는 것과 같은 방식이다.
  function_.getBody().push_back(block);
  builder_.setInsertionPointToEnd(block);
}

// ------------------------------------------------------------------ 문장

void MLIRGenerator::emitCompound(const CompoundStmt &block) {
  // 지역 변수 슬롯은 emitFunction이 미리 만들었다. 여기서는 문장만.
  for (const auto &stmt : block.stmts) {
    emitStmt(*stmt);
    if (!blockOpen())
      break;   // return 뒤의 문장은 죽은 코드: 종결자 뒤에는 op를 붙일 수 없다
  }
}

void MLIRGenerator::emitStmt(const Stmt &stmt) {
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
  case NodeKind::IfStmt:
    emitIf(static_cast<const IfStmt &>(stmt));
    break;
  case NodeKind::WhileStmt:
    emitWhile(static_cast<const WhileStmt &>(stmt));
    break;
  default:
    break;
  }
}

void MLIRGenerator::emitIf(const IfStmt &stmt) {
  mlir::Location where = loc(stmt.loc);

  if (structured_) {
    //   조건 계산
    //   cminus.if %c {            ← op 하나가 지금 블록에 붙는다
    //     then 문장; cminus.yield
    //   } else {
    //     else 문장; cminus.yield
    //   }
    //   if 다음 문장들이 같은 블록에 이어진다
    // 새 블록을 함수에 붙이지 않는다. 몸체는 op 안의 region에 들어간다.
    // 두 갈래가 모두 return해도 지울 블록이 없다. if 다음의 죽은 문장과
    // 기본 return은 IRGen.cpp의 if.end처럼 그대로 만들어지고, 펼친 뒤
    // 아무도 오지 않는 그 블록은 lower-cminus-to-cf가 처리한다.
    mlir::Value cond = emitCondition(*stmt.cond);
    auto ifOp = builder_.create<dialect::IfOp>(where, cond);  // region 둘 다 빈 채로
    emitRegion(ifOp.getThenRegion(), stmt.thenStmt.get());
    if (stmt.elseStmt)
      emitRegion(ifOp.getElseRegion(), stmt.elseStmt.get());  // 없으면 빈 region
    return;   // emitRegion의 guard가 커서를 ifOp 바로 뒤로 돌려 놓았다
  }

  //   (지금 블록)  조건 계산; cf.cond_br %c, ^then, ^else(없으면 ^end)
  //   ^then:       then 문장; cf.br ^end
  //   ^else:       else 문장; cf.br ^end
  //   ^end:        if 다음 문장들이 여기에 이어진다
  mlir::Value cond = emitCondition(*stmt.cond);
  auto *thenBlock = new mlir::Block();
  auto *elseBlock = stmt.elseStmt ? new mlir::Block() : nullptr;
  auto *endBlock = new mlir::Block();
  builder_.create<mlir::cf::CondBranchOp>(where, cond, thenBlock,
                                          elseBlock ? elseBlock : endBlock);
  // cond_br가 지금 블록의 종결자다. 이제 이 블록은 닫혔다.

  startBlock(thenBlock);
  if (stmt.thenStmt)
    emitStmt(*stmt.thenStmt);
  // then 안에서 return했으면 블록이 이미 닫혀 있다. 열려 있을 때만 end로 간다.
  // (then 안에 if/while이 있었다면 커서는 그 안쪽의 ^end에 있고, 거기서 이어간다.)
  if (blockOpen())
    builder_.create<mlir::cf::BranchOp>(where, endBlock);

  if (elseBlock) {
    startBlock(elseBlock);
    emitStmt(*stmt.elseStmt);
    if (blockOpen())
      builder_.create<mlir::cf::BranchOp>(where, endBlock);
  }

  // 두 갈래가 모두 return하면 ^end로 오는 분기가 없다. IRGen.cpp는 그래도
  // if.end를 붙여 뒤따르는 죽은 문장을 거기에 놓지만, 여기서는 블록을 버린다.
  // 아무도 오지 않는 블록 안의 func.return은 convert-func-to-llvm이 변환하지
  // 않고 남겨 lowering이 실패하기 때문이다 (gcd.cm에서 관찰).
  // 커서는 닫힌 블록에 남으므로 blockOpen()이 false가 되고, 바깥의
  // emitCompound가 if 뒤의 죽은 문장을 건너뛴다. return 뒤와 같은 처리다.
  if (endBlock->hasNoPredecessors()) {
    delete endBlock;
    return;
  }
  startBlock(endBlock);
}

void MLIRGenerator::emitWhile(const WhileStmt &stmt) {
  mlir::Location where = loc(stmt.loc);

  if (structured_) {
    //   cminus.while {
    //     조건 계산; cminus.condition %c     ← 반복마다 다시 실행되는 region
    //   } do {
    //     몸체 문장; cminus.yield
    //   }
    auto whileOp = builder_.create<dialect::WhileOp>(where);
    {
      mlir::OpBuilder::InsertionGuard guard(builder_);
      builder_.setInsertionPointToEnd(&whileOp.getCondRegion().emplaceBlock());
      mlir::Value cond = emitCondition(*stmt.cond);
      builder_.create<dialect::ConditionOp>(where, cond);
    }
    emitRegion(whileOp.getBodyRegion(), stmt.body.get());
    return;
  }

  //   (지금 블록)  cf.br ^cond
  //   ^cond:       조건 계산; cf.cond_br %c, ^body, ^end
  //   ^body:       몸체 문장; cf.br ^cond       ← 뒤로 가는 분기 = 루프
  //   ^end:        while 다음 문장들
  // 조건은 반복마다 다시 계산해야 하므로 별도 블록 ^cond에 둔다.
  auto *condBlock = new mlir::Block();
  auto *bodyBlock = new mlir::Block();
  auto *endBlock = new mlir::Block();
  builder_.create<mlir::cf::BranchOp>(where, condBlock);

  startBlock(condBlock);
  mlir::Value cond = emitCondition(*stmt.cond);
  builder_.create<mlir::cf::CondBranchOp>(where, cond, bodyBlock, endBlock);

  startBlock(bodyBlock);
  if (stmt.body)
    emitStmt(*stmt.body);
  if (blockOpen())
    builder_.create<mlir::cf::BranchOp>(where, condBlock);

  startBlock(endBlock);
}

void MLIRGenerator::emitReturn(const ReturnStmt &stmt) {
  mlir::Location where = loc(stmt.loc);

  // C-의 반환 타입이 아니라 MLIR 함수의 결과 타입을 본다.
  // 그래야 void main의 `return;`이 `return 0`이 된다 (IRGen.cpp와 같음).
  if (function_.getFunctionType().getNumResults() == 0) {
    if (stmt.value)
      emitExpr(*stmt.value);               // void 함수의 `return f();`: f는 호출한다
    createReturn(where, mlir::Value());                              // return
    return;
  }

  mlir::Value value = stmt.value ? emitExpr(*stmt.value) : int32(stmt.loc, 0);
  createReturn(where, value);                                        // return %v
  // 이제 블록에 종결자가 생겼으므로 blockOpen()이 false가 되고,
  // emitCompound는 남은 문장을 건너뛴다.
}

void MLIRGenerator::createReturn(mlir::Location where, mlir::Value value) {
  // func.return은 func.func에 바로 속한 블록만 끝낼 수 있다(HasParent).
  // cminus region 안에서도 return할 수 있어야 하므로 cminus flavor는 어디서나
  // cminus.return을 쓴다. 값이 없으면(void 함수) 피연산자 없이 만든다.
  if (structured_)
    builder_.create<dialect::ReturnOp>(where, value);
  else if (value)
    builder_.create<mlir::func::ReturnOp>(where, mlir::ValueRange{value});
  else
    builder_.create<mlir::func::ReturnOp>(where);
}

void MLIRGenerator::emitRegion(mlir::Region &region, const Stmt *stmt) {
  // cminus.if/while의 region은 블록 하나. 블록을 만들어 커서를 넣고 문장을
  // 채운다. return으로 끝나지 않았으면 cminus.yield로 닫는다.
  // guard가 사라질 때 커서는 op 바로 뒤(바깥 블록)로 돌아간다.
  // region 안에 또 if/while이 있으면 같은 일이 한 단계 안쪽에서 반복된다.
  mlir::OpBuilder::InsertionGuard guard(builder_);
  builder_.setInsertionPointToEnd(&region.emplaceBlock());
  if (stmt)
    emitStmt(*stmt);
  if (blockOpen())
    builder_.create<dialect::YieldOp>(builder_.getUnknownLoc());
}

// -------------------------------------------------------------------- 식

mlir::Value MLIRGenerator::emitExpr(const Expr &expr) {
  switch (expr.kind) {
  case NodeKind::NumExpr:                  // 42 → arith.constant 42 : i32
    return int32(expr.loc, static_cast<const NumExpr &>(expr).value);
  case NodeKind::VarExpr: {                // x    → memref.load %slot[]   : memref<i32>
                                           // a[i] → memref.load %a[%idx]  : memref<5xi32>
    llvm::SmallVector<mlir::Value, 1> indices;
    mlir::Value address = addressOf(static_cast<const VarExpr &>(expr), indices);
    if (!address)
      return int32(expr.loc, 0);
    return builder_.create<mlir::memref::LoadOp>(loc(expr.loc), address, indices);
  }
  case NodeKind::AssignExpr:
    return emitAssign(static_cast<const AssignExpr &>(expr));
  case NodeKind::BinaryExpr:
    return emitBinary(static_cast<const BinaryExpr &>(expr));
  case NodeKind::CallExpr:
    return emitCall(static_cast<const CallExpr &>(expr));
  default:
    return int32(expr.loc, 0);
  }
}

/// spec 3.6: 대입은 식이고, 그 값은 저장한 값이다. 그래서 a = b = 3이 된다.
mlir::Value MLIRGenerator::emitAssign(const AssignExpr &expr) {
  // IRGen.cpp와 같은 순서: 주소(와 첨자 검사)를 먼저, 값을 나중에.
  // a[i] = e에서 i가 음수면 e를 계산하기 전에 멈춘다.
  llvm::SmallVector<mlir::Value, 1> indices;
  mlir::Value address =
      expr.target ? addressOf(*expr.target, indices) : mlir::Value();
  mlir::Value value = expr.value ? emitExpr(*expr.value) : int32(expr.loc, 0);
  if (address)
    builder_.create<mlir::memref::StoreOp>(loc(expr.opLoc), value, address,
                                           indices);                  // memref.store
  return value;
}

mlir::Value MLIRGenerator::addressOf(const VarExpr &expr,
                                    llvm::SmallVectorImpl<mlir::Value> &indices) {
  // IRGen.cpp의 elementAddress에 해당한다. 다만 LLVM IR은 원소 하나의 주소
  // (getelementptr)를 계산하지만, memref는 "저장소 + 첨자"를 load/store에
  // 따로 넘긴다. 주소 계산은 lowering(finalize-memref-to-llvm)이 만든다.
  const Symbol *symbol = expr.symbol;
  if (!symbol)
    return {};
  mlir::Value storage = storageOf(*symbol, expr.loc);
  if (!expr.index)
    return storage;                        // x: 첨자 없음 → load %x[]

  // a[i]: 첨자는 i32 값이다. 먼저 음수인지 검사하고(spec 3.6), memref가
  // 받는 index 타입으로 바꾼다. index_cast는 부호를 보존해 넓힌다(LLVM의 sext).
  // 위쪽 경계는 검사하지 않는다 (spec 3.6, IRGen.cpp와 같음).
  //   %i   = memref.load %islot[] : memref<i32>
  //   (검사 블록들)
  //   %idx = arith.index_cast %i : i32 to index
  mlir::Value index = emitExpr(*expr.index);
  checkIndex(index, expr.index->loc);
  indices.push_back(builder_.create<mlir::arith::IndexCastOp>(
      loc(expr.index->loc), builder_.getIndexType(), index));
  return storage;
}

mlir::Value MLIRGenerator::storageOf(const Symbol &symbol, SourceLocation where) {
  // 매개변수나 지역 변수: emitFunction이 만들어 둔 슬롯 (배열 매개변수는 블록 인자).
  auto found = slots_.find(&symbol);
  if (found != slots_.end())
    return found->second;

  // 슬롯이 없으면 전역이다. 전역은 모듈에 있고 함수는 IsolatedFromAbove라서
  // 함수 밖의 값을 직접 쓸 수 없다. 이름(@x)으로 그 주소를 꺼내 온다.
  //   %0 = memref.get_global @x : memref<i32>
  //   %1 = memref.get_global @g : memref<5xi32>
  return builder_.create<mlir::memref::GetGlobalOp>(
      loc(where), storageType(symbol), symbol.name);
}

mlir::MemRefType MLIRGenerator::storageType(const Symbol &symbol) {
  // IRGen.cpp의 storageType에 해당한다.
  //   int x           → memref<i32>    (0차원: 원소 하나, 첨자 없음)
  //   int a[5]        → memref<5xi32>  (크기를 아는 1차원)
  //   int a[] 매개변수 → memref<?xi32>  (크기를 모르는 1차원)
  mlir::Type i32 = builder_.getI32Type();
  if (!symbol.isArray())
    return mlir::MemRefType::get(/*shape=*/{}, i32);
  if (symbol.kind == SymbolKind::Parameter)
    return mlir::MemRefType::get({mlir::ShapedType::kDynamic}, i32);
  return mlir::MemRefType::get({symbol.arraySize}, i32);
}

void MLIRGenerator::checkIndex(mlir::Value index, SourceLocation where) {
  // IRGen.cpp의 checkIndex와 같은 모양:
  //   %neg = arith.cmpi slt, %i, %c0 : i32
  //   cf.cond_br %neg, ^fail, ^ok
  // ^fail:
  //   func.call @__cminus_index_error(%i) : (i32) -> ()
  //   llvm.unreachable                    ← LLVM dialect op를 그대로 섞어 쓴다
  // ^ok:
  //   (첨자를 쓰는 load/store가 여기에 이어진다)
  if (!options_.indexChecks)
    return;
  // 음수가 아닌 상수 첨자(a[2])는 검사할 필요가 없다.
  llvm::APInt constant;
  if (mlir::matchPattern(index, mlir::m_ConstantInt(&constant)) &&
      !constant.isNegative())
    return;

  mlir::Location here = loc(where);
  if (structured_) {
    // cminus.index_check %i — 위의 마름모 전체를 lower-cminus-to-cf가 만든다.
    // 블록을 나누지 않으므로 region 안에서도 커서가 그대로 이어진다.
    builder_.create<dialect::IndexCheckOp>(here, index);
    return;
  }

  mlir::Value negative = builder_.create<mlir::arith::CmpIOp>(
      here, mlir::arith::CmpIPredicate::slt, index, int32(where, 0));
  auto *failBlock = new mlir::Block();
  auto *okBlock = new mlir::Block();
  builder_.create<mlir::cf::CondBranchOp>(here, negative, failBlock, okBlock);

  // 실패 블록. 런타임 함수가 메시지를 찍고 exit(1)하므로 돌아오지 않는다.
  // std dialect에는 "여기는 도달하지 않음" 종결자가 없어서 LLVM dialect의
  // llvm.unreachable을 쓴다. 이미 LLVM dialect이므로 lowering이 건드리지 않는다.
  startBlock(failBlock);
  auto indexError =
      module_.lookupSymbol<mlir::func::FuncOp>("__cminus_index_error");
  builder_.create<mlir::func::CallOp>(here, indexError, mlir::ValueRange{index});
  builder_.create<mlir::LLVM::UnreachableOp>(here);

  // 통과 블록에서 원래 식을 이어서 만든다.
  startBlock(okBlock);
}

mlir::Value MLIRGenerator::emitBinary(const BinaryExpr &expr) {
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

mlir::Value MLIRGenerator::emitCall(const CallExpr &expr) {
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
  llvm::SmallVector<mlir::Value> args;
  for (unsigned i = 0; i < expr.args.size(); ++i) {
    const Expr &arg = *expr.args[i];
    mlir::Type paramType = callee.getArgumentTypes()[i];

    // spec 3.3: 배열 매개변수는 참조 전달이므로 값이 아니라 배열(memref) 자체를
    // 넘긴다. 의미 분석이 이 자리에 배열 이름만 오도록 보장한다.
    // 받는 쪽은 memref<?xi32>인데 넘기는 쪽은 memref<5xi32>일 수 있다.
    // 크기를 잊게 하는 변환이 memref.cast다 (LLVM IR에서는 아무 일도 하지 않는다).
    //   %g  = memref.get_global @g : memref<5xi32>
    //   %gc = memref.cast %g : memref<5xi32> to memref<?xi32>
    // 배열 매개변수를 다시 넘길 때는 이미 memref<?xi32>이므로 그대로 넘긴다.
    if (mlir::isa<mlir::MemRefType>(paramType) && arg.kind == NodeKind::VarExpr &&
        static_cast<const VarExpr &>(arg).symbol) {
      mlir::Value array =
          storageOf(*static_cast<const VarExpr &>(arg).symbol, arg.loc);
      if (array.getType() != paramType)
        array = builder_.create<mlir::memref::CastOp>(loc(arg.loc), paramType, array);
      args.push_back(array);
      continue;
    }
    args.push_back(emitExpr(arg));
  }

  auto call = builder_.create<mlir::func::CallOp>(where, callee, args);  // func.call @f(...)
  // void 함수 호출은 결과가 없다. 의미 분석이 void 값을 쓰는 식을 막으므로
  // 빈 Value는 ExprStmt나 void 함수의 return처럼 값을 버리는 곳으로만 간다.
  return call.getNumResults() ? call.getResult(0) : mlir::Value();
}

mlir::Value MLIRGenerator::emitCondition(const Expr &expr) {
  // spec 3.5: 0이 아니면 참. cf.cond_br는 i1을 받으므로 `값 != 0`으로 바꾼다.
  // 조건이 비교식이면 cmpi → extui → cmpi ne 0이 되어 장황하지만,
  // 최적화(canonicalize, LLVM instcombine)가 정리한다. IRGen.cpp도 같다.
  mlir::Value value = emitExpr(expr);
  return builder_.create<mlir::arith::CmpIOp>(
      loc(expr.loc), mlir::arith::CmpIPredicate::ne, value, int32(expr.loc, 0));
}

mlir::Value MLIRGenerator::int32(SourceLocation where, long long value) {
  // IRGen.cpp의 int32()처럼 i32로 자른다.
  return builder_.create<mlir::arith::ConstantIntOp>(
      loc(where), builder_.getI32Type(), static_cast<std::int32_t>(value));
}

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> generateStdMLIR(const Program &program,
                                                  mlir::MLIRContext &context,
                                                  DiagnosticEngine &diags,
                                                  const MLIRGenOptions &options) {
  return MLIRGenerator(context, diags, options, MLIRFlavor::Std).run(program);
}

mlir::OwningOpRef<mlir::ModuleOp>
generateCMinusMLIR(const Program &program, mlir::MLIRContext &context,
                   DiagnosticEngine &diags, const MLIRGenOptions &options) {
  return MLIRGenerator(context, diags, options, MLIRFlavor::CMinus).run(program);
}

} // namespace cminus
