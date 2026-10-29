#include "cminus/CMinusDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/RegionUtils.h"

// lower-cminus-to-cf: region을 가진 cminus op를 부모 함수의 CFG에 펼친다.
// 결과는 std 생성기가 처음부터 만드는 것과 같은 모양(블록 + cf 분기)이다.
// MLIR의 scf → cf 변환(SCFToControlFlow.cpp의 IfLowering, WhileLowering)과
// 같은 방식이다.
//
// 패턴마다 공통으로 쓰는 두 동작:
//   splitBlock(block, op)          op부터 블록 끝까지를 새 블록(이어지는 곳)으로
//                                  떼어 낸다. 원래 블록은 종결자 없이 남는다.
//   inlineRegionBefore(region, b)  region의 블록들을 꺼내 블록 b 앞에 끼운다.
//                                  op 안에 있던 몸체가 함수의 블록 목록에 들어간다.

namespace cminus {

namespace {

using dialect::ConditionOp;
using dialect::IfOp;
using dialect::IndexCheckOp;
using dialect::ReturnOp;
using dialect::WhileOp;
using dialect::YieldOp;

/// region의 끝이 cminus.yield이면 `cf.br dest`로 바꾼다. cminus.return이면
/// 함수를 떠나는 것이므로 그대로 둔다(ReturnLowering이 func.return으로 바꾼다).
void branchInsteadOfYield(mlir::PatternRewriter &rewriter, mlir::Block &block,
                          mlir::Block *dest) {
  auto yield = llvm::dyn_cast<YieldOp>(block.getTerminator());
  if (!yield)
    return;
  // replaceOpWithNewOp은 새 op를 "바꿀 op 자리"가 아니라 rewriter의 현재
  // 커서 위치에 만든다. 커서를 먼저 yield 앞으로 옮겨야 한다.
  rewriter.setInsertionPoint(yield);
  rewriter.replaceOpWithNewOp<mlir::cf::BranchOp>(yield, dest);
}

// ---------------------------------------------------------- index_check
//
//   ^cur:  ...                          ^cur:  ...
//          cminus.index_check %i   →           %neg = arith.cmpi slt, %i, %c0
//          나머지                               cf.cond_br %neg, ^fail, ^ok
//                                       ^fail: func.call @__cminus_index_error(%i)
//                                              llvm.unreachable
//                                       ^ok:   나머지
struct IndexCheckLowering : mlir::OpRewritePattern<IndexCheckOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(IndexCheckOp op, mlir::PatternRewriter &rewriter) const override {
    auto indexError = op->getParentOfType<mlir::ModuleOp>()
                          .lookupSymbol<mlir::func::FuncOp>("__cminus_index_error");
    if (!indexError)
      return rewriter.notifyMatchFailure(op, "__cminus_index_error is not declared");

    mlir::Location loc = op.getLoc();
    mlir::Value index = op.getIndex();
    mlir::Block *current = op->getBlock();
    mlir::Block *ok = rewriter.splitBlock(current, op->getIterator());

    // createBlock(ok)는 ok 앞에 빈 블록을 만들고 커서를 그 안으로 옮긴다.
    mlir::Block *fail = rewriter.createBlock(ok);
    rewriter.create<mlir::func::CallOp>(loc, indexError, mlir::ValueRange{index});
    rewriter.create<mlir::LLVM::UnreachableOp>(loc);

    rewriter.setInsertionPointToEnd(current);
    mlir::Value zero = rewriter.create<mlir::arith::ConstantIntOp>(
        loc, rewriter.getI32Type(), 0);
    mlir::Value negative = rewriter.create<mlir::arith::CmpIOp>(
        loc, mlir::arith::CmpIPredicate::slt, index, zero);
    rewriter.create<mlir::cf::CondBranchOp>(loc, negative, fail, ok);

    rewriter.eraseOp(op);   // splitBlock으로 ok의 맨 앞에 와 있던 op
    return mlir::success();
  }
};

// ------------------------------------------------------------------- if
//
//   ^cur:  ...                          ^cur:  ...
//          cminus.if %c {                      cf.cond_br %c, ^then, ^else(없으면 ^cont)
//            then; cminus.yield    →    ^then: then; cf.br ^cont
//          } else {                     ^else: else; cf.br ^cont
//            else; cminus.yield         ^cont: 나머지
//          }
//          나머지
struct IfLowering : mlir::OpRewritePattern<IfOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(IfOp op, mlir::PatternRewriter &rewriter) const override {
    mlir::Location loc = op.getLoc();
    mlir::Block *current = op->getBlock();
    mlir::Block *cont = rewriter.splitBlock(current, op->getIterator());

    mlir::Block *thenBlock = &op.getThenRegion().front();
    branchInsteadOfYield(rewriter, *thenBlock, cont);
    rewriter.inlineRegionBefore(op.getThenRegion(), cont);

    mlir::Block *elseBlock = cont;   // else가 없으면 거짓일 때 곧장 이어지는 곳으로
    if (!op.getElseRegion().empty()) {
      elseBlock = &op.getElseRegion().front();
      branchInsteadOfYield(rewriter, *elseBlock, cont);
      rewriter.inlineRegionBefore(op.getElseRegion(), cont);
    }

    rewriter.setInsertionPointToEnd(current);
    rewriter.create<mlir::cf::CondBranchOp>(loc, op.getCond(), thenBlock, elseBlock);
    rewriter.eraseOp(op);
    // 두 갈래가 모두 return하면 ^cont로 오는 분기가 없다. 그 블록은
    // runOnOperation이 변환을 마친 뒤 한꺼번에 지운다.
    return mlir::success();
  }
};

// ---------------------------------------------------------------- while
//
//   ^cur:  ...                          ^cur:  ...; cf.br ^cond
//          cminus.while {               ^cond: 조건; cf.cond_br %c, ^body, ^cont
//            조건; cminus.condition %c  →  ^body: 몸체; cf.br ^cond
//          } do {                       ^cont: 나머지
//            몸체; cminus.yield
//          }
//          나머지
struct WhileLowering : mlir::OpRewritePattern<WhileOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(WhileOp op, mlir::PatternRewriter &rewriter) const override {
    mlir::Location loc = op.getLoc();
    mlir::Block *current = op->getBlock();
    mlir::Block *cont = rewriter.splitBlock(current, op->getIterator());

    mlir::Block *condBlock = &op.getCondRegion().front();
    mlir::Block *bodyBlock = &op.getBodyRegion().front();

    auto condition = llvm::cast<ConditionOp>(condBlock->getTerminator());
    rewriter.setInsertionPoint(condition);
    rewriter.replaceOpWithNewOp<mlir::cf::CondBranchOp>(
        condition, condition.getValue(), bodyBlock, cont);
    branchInsteadOfYield(rewriter, *bodyBlock, condBlock);   // 뒤로 가는 분기

    rewriter.inlineRegionBefore(op.getCondRegion(), cont);
    rewriter.inlineRegionBefore(op.getBodyRegion(), cont);

    rewriter.setInsertionPointToEnd(current);
    rewriter.create<mlir::cf::BranchOp>(loc, condBlock);
    rewriter.eraseOp(op);
    return mlir::success();
  }
};

// --------------------------------------------------------------- return
//
// region이 모두 펼쳐지면 cminus.return이 끝내는 블록은 func.func에 바로
// 속하므로 func.return으로 바꿀 수 있다. 펼치기 전이라도 바꾸는 순서는
// 상관없다: 변환이 끝날 때 한꺼번에 검증되기 때문이다.
struct ReturnLowering : mlir::OpRewritePattern<ReturnOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(ReturnOp op, mlir::PatternRewriter &rewriter) const override {
    if (mlir::Value value = op.getValue())
      rewriter.replaceOpWithNewOp<mlir::func::ReturnOp>(op, mlir::ValueRange{value});
    else
      rewriter.replaceOpWithNewOp<mlir::func::ReturnOp>(op);
    return mlir::success();
  }
};

// yield와 condition은 자기 패턴이 없다. 부모 if/while의 패턴이 cf 분기로 바꾼다.

struct LowerCMinusToCFPass
    : mlir::PassWrapper<LowerCMinusToCFPass,
                        mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerCMinusToCFPass)

  // The name --mlir-print-ir-after-all shows for this pass.
  llvm::StringRef getArgument() const override { return "lower-cminus-to-cf"; }
  llvm::StringRef getDescription() const override {
    return "Lower the cminus dialect to cf, func and llvm ops";
  }
  // 이 pass가 새로 만드는 op의 dialect. pass가 돌기 전에 load되어 있어야 한다.
  void getDependentDialects(mlir::DialectRegistry &registry) const override {
    registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                    mlir::func::FuncDialect, mlir::LLVM::LLVMDialect>();
  }

  void runOnOperation() override {
    // 변환 목표: cminus op는 하나도 남으면 안 되고(illegal), 나머지 dialect는
    // 남아도 된다(legal). "partial"은 목표에 없는 op(builtin.module 등)를
    // 건드리지 않고 둔다는 뜻이다.
    mlir::ConversionTarget target(getContext());
    target.addIllegalDialect<dialect::CMinusDialect>();
    target.addLegalDialect<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                           mlir::func::FuncDialect, mlir::LLVM::LLVMDialect,
                           mlir::memref::MemRefDialect>();

    mlir::RewritePatternSet patterns(&getContext());
    patterns.add<IndexCheckLowering, IfLowering, WhileLowering, ReturnLowering>(
        &getContext());

    // illegal op를 하나라도 바꾸지 못하면 실패하고, 무엇이 남았는지 출력한다.
    if (mlir::failed(mlir::applyPartialConversion(getOperation(), target,
                                                  std::move(patterns)))) {
      signalPassFailure();
      return;
    }

    // if의 두 갈래가 모두 return하면 펼친 뒤 ^cont로 오는 분기가 없다.
    // 생성기는 그 자리에 if 뒤의 죽은 문장과 기본 return을 남겨 두었다
    // (IRGen.cpp의 if.end처럼). 그대로 두면 convert-func-to-llvm이 아무도 오지
    // 않는 블록의 func.return을 변환하지 않고 남겨 lowering이 실패한다
    // (std 생성기에서 관찰한 것과 같은 문제).
    // 패턴 안에서 지우면 부족하다: 안쪽 if의 ^cont를 지우면 바깥 if의 ^cont가
    // 유일한 선행 블록을 잃어 새로 도달 불가능해진다(returns.cm의 sign).
    // 그래서 entry에서 도달할 수 있는지로 판정해 한 번에 지운다.
    mlir::IRRewriter rewriter(&getContext());
    getOperation().walk([&](mlir::func::FuncOp function) {
      (void)mlir::eraseUnreachableBlocks(rewriter, function.getBody());
    });
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerCMinusToCFPass() {
  return std::make_unique<LowerCMinusToCFPass>();
}

} // namespace cminus
