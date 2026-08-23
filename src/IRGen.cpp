#include "cminus/IRGen.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include "cminus/AST.h"
#include "cminus/Diagnostic.h"
#include "cminus/Symbol.h"

namespace cminus {

namespace {

/// The C- entry point is `void main(void)`, but the emitted one returns int
/// so that a C runtime can start it and report an exit status.
const char *const kMainName = "main";

/// Runtime helper called when a subscript is negative (spec 3.6).
const char *const kIndexErrorName = "__cminus_index_error";

class IRGenerator {
public:
  IRGenerator(llvm::LLVMContext &context, DiagnosticEngine &diags,
              const IRGenOptions &options)
      : ctx_(context), diags_(diags), options_(options),
        module_(std::make_unique<llvm::Module>(options.moduleName, context)),
        builder_(context), i32Ty_(llvm::Type::getInt32Ty(context)),
        voidTy_(llvm::Type::getVoidTy(context)),
        ptrTy_(llvm::PointerType::get(context, 0)) {
    // Before anything is generated: the pointer width comes from here.
    if (!options.dataLayout.empty())
      module_->setDataLayout(options.dataLayout);
    if (!options.targetTriple.empty())
      module_->setTargetTriple(llvm::Triple(options.targetTriple));
  }

  std::unique_ptr<llvm::Module> run(const Program &program);

private:
  // --- small helpers
  llvm::Constant *int32(long long value) const {
    return llvm::ConstantInt::get(i32Ty_, value, /*IsSigned=*/true);
  }
  /// Integer type a getelementptr index must have on this target: 64 bits on
  /// aarch64 or x86-64, 32 on armv7 or riscv32.
  llvm::Type *indexTy() const {
    return module_->getDataLayout().getIndexType(ptrTy_);
  }
  llvm::Constant *indexZero() const {
    return llvm::ConstantInt::get(indexTy(), 0);
  }
  /// The current block still needs a terminator.
  bool blockOpen() const {
    llvm::BasicBlock *block = builder_.GetInsertBlock();
    return block && !block->getTerminator();
  }
  /// How a symbol is stored: an int, a fixed-size array, or -- for an array
  /// parameter, which is passed by reference (spec 3.3) -- a pointer.
  llvm::Type *storageType(const Symbol &symbol) const {
    if (symbol.type != Type::IntArray)
      return i32Ty_;
    if (symbol.kind == SymbolKind::Parameter)
      return ptrTy_;
    return llvm::ArrayType::get(i32Ty_, static_cast<uint64_t>(symbol.arraySize));
  }
  llvm::Value *addressOf(const Symbol &symbol) const {
    auto found = addresses_.find(&symbol);
    return found == addresses_.end() ? nullptr : found->second;
  }

  // --- declarations
  void emitGlobal(const VarDecl &decl);
  void emitFunction(const FunDecl &decl);
  llvm::Function *functionFor(const Symbol &symbol);
  llvm::Function *indexErrorFunction();
  static void collectLocals(const Stmt &stmt,
                            std::vector<const VarDecl *> &out);

  // --- statements
  void emitStmt(const Stmt &stmt);
  void emitCompound(const CompoundStmt &block);
  void emitIf(const IfStmt &stmt);
  void emitWhile(const WhileStmt &stmt);
  void emitReturn(const ReturnStmt &stmt);

  // --- expressions
  llvm::Value *emitExpr(const Expr &expr);
  llvm::Value *emitAssign(const AssignExpr &expr);
  llvm::Value *emitBinary(const BinaryExpr &expr);
  llvm::Value *emitCall(const CallExpr &expr);
  /// Truth test: any non-zero value is true (spec 3.5).
  llvm::Value *emitCondition(const Expr &expr);
  /// Address of the int a `var` denotes.
  llvm::Value *elementAddress(const VarExpr &expr);
  /// Pointer to element zero, for passing a whole array as an argument.
  llvm::Value *arrayBase(const Symbol &symbol);
  llvm::Value *checkIndex(llvm::Value *index);

  llvm::LLVMContext &ctx_;
  DiagnosticEngine &diags_;
  IRGenOptions options_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;

  llvm::Type *i32Ty_;
  llvm::Type *voidTy_;
  llvm::PointerType *ptrTy_;

  std::unordered_map<const Symbol *, llvm::Value *> addresses_;
  std::unordered_map<const Symbol *, llvm::Function *> functions_;
  llvm::Function *currentFunction_ = nullptr;
};

// ------------------------------------------------------------------- driver

std::unique_ptr<llvm::Module> IRGenerator::run(const Program &program) {
  for (const auto &decl : program.decls) {
    if (decl->kind == NodeKind::VarDecl)
      emitGlobal(static_cast<const VarDecl &>(*decl));
    else if (decl->kind == NodeKind::FunDecl)
      emitFunction(static_cast<const FunDecl &>(*decl));
  }

  std::string report;
  llvm::raw_string_ostream stream(report);
  if (llvm::verifyModule(*module_, &stream)) {
    diags_.error(SourceLocation{},
                 "internal error: the generated module failed verification:\n" +
                     report);
    return nullptr;
  }
  return std::move(module_);
}

// ------------------------------------------------------------- declarations

void IRGenerator::emitGlobal(const VarDecl &decl) {
  const Symbol *symbol = decl.symbol;
  if (!symbol)
    return;

  llvm::Type *type = storageType(*symbol);
  auto *global = new llvm::GlobalVariable(
      *module_, type, /*isConstant=*/false, llvm::GlobalValue::ExternalLinkage,
      llvm::Constant::getNullValue(type), symbol->name);
  global->setAlignment(llvm::Align(4));
  addresses_[symbol] = global;
}

llvm::Function *IRGenerator::functionFor(const Symbol &symbol) {
  auto found = functions_.find(&symbol);
  if (found != functions_.end())
    return found->second;

  std::vector<llvm::Type *> params;
  params.reserve(symbol.paramTypes.size());
  for (Type type : symbol.paramTypes)
    params.push_back(type == Type::IntArray ? static_cast<llvm::Type *>(ptrTy_)
                                            : i32Ty_);

  llvm::Type *returnType = symbol.returnType == Type::Void ? voidTy_ : i32Ty_;
  if (symbol.name == kMainName)
    returnType = i32Ty_;

  auto *type = llvm::FunctionType::get(returnType, params, /*isVarArg=*/false);
  auto *function = llvm::Function::Create(
      type, llvm::GlobalValue::ExternalLinkage, symbol.name, module_.get());
  functions_[&symbol] = function;
  return function;
}

llvm::Function *IRGenerator::indexErrorFunction() {
  if (auto *existing = module_->getFunction(kIndexErrorName))
    return existing;

  auto *type = llvm::FunctionType::get(voidTy_, {i32Ty_}, /*isVarArg=*/false);
  auto *function = llvm::Function::Create(
      type, llvm::GlobalValue::ExternalLinkage, kIndexErrorName, module_.get());
  function->addFnAttr(llvm::Attribute::NoReturn);
  return function;
}

/// Gather every local declaration in a function body, in source order, so
/// that all the allocas can be emitted together at the top of the entry
/// block. Blocks do not reuse symbols, so a flat list is enough.
void IRGenerator::collectLocals(const Stmt &stmt,
                                std::vector<const VarDecl *> &out) {
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

void IRGenerator::emitFunction(const FunDecl &decl) {
  const Symbol *symbol = decl.symbol;
  if (!symbol || !decl.body)
    return;

  llvm::Function *function = functionFor(*symbol);
  currentFunction_ = function;

  auto *entry = llvm::BasicBlock::Create(ctx_, "entry", function);
  builder_.SetInsertPoint(entry);

  // Each parameter gets a stack slot so that assigning to it works; mem2reg
  // removes the slot again whenever optimization is on.
  unsigned index = 0;
  for (const auto &param : decl.params) {
    llvm::Argument *arg = function->getArg(index++);
    arg->setName(param->name);
    if (!param->symbol)
      continue;
    llvm::AllocaInst *slot = builder_.CreateAlloca(
        storageType(*param->symbol), nullptr, param->name + ".addr");
    slot->setAlignment(llvm::Align(param->symbol->isArray() ? 8 : 4));
    builder_.CreateStore(arg, slot);
    addresses_[param->symbol] = slot;
  }

  std::vector<const VarDecl *> locals;
  collectLocals(*decl.body, locals);
  for (const VarDecl *local : locals) {
    if (!local->symbol)
      continue;
    llvm::AllocaInst *slot =
        builder_.CreateAlloca(storageType(*local->symbol), nullptr,
                              local->name);
    slot->setAlignment(llvm::Align(4));
    addresses_[local->symbol] = slot;
  }

  emitCompound(*decl.body);

  // Spec 3.5 requires a value from a non-void function, and semantic analysis
  // has already warned if one is missing; zero keeps the IR well formed.
  if (blockOpen()) {
    if (function->getReturnType()->isVoidTy())
      builder_.CreateRetVoid();
    else
      builder_.CreateRet(int32(0));
  }

  currentFunction_ = nullptr;
}

// --------------------------------------------------------------- statements

void IRGenerator::emitCompound(const CompoundStmt &block) {
  // The allocas were emitted up front; only the statements are left.
  for (const auto &stmt : block.stmts) {
    emitStmt(*stmt);
    if (!blockOpen())
      break; // everything after a return in this block is dead
  }
}

void IRGenerator::emitStmt(const Stmt &stmt) {
  switch (stmt.kind) {
  case NodeKind::CompoundStmt:
    emitCompound(static_cast<const CompoundStmt &>(stmt));
    break;
  case NodeKind::ExprStmt: {
    const auto &node = static_cast<const ExprStmt &>(stmt);
    if (node.expr)
      emitExpr(*node.expr);
    break;
  }
  case NodeKind::IfStmt:
    emitIf(static_cast<const IfStmt &>(stmt));
    break;
  case NodeKind::WhileStmt:
    emitWhile(static_cast<const WhileStmt &>(stmt));
    break;
  case NodeKind::ReturnStmt:
    emitReturn(static_cast<const ReturnStmt &>(stmt));
    break;
  default:
    break;
  }
}

void IRGenerator::emitIf(const IfStmt &stmt) {
  llvm::Value *cond = emitCondition(*stmt.cond);

  auto *thenBlock = llvm::BasicBlock::Create(ctx_, "if.then", currentFunction_);
  auto *elseBlock = stmt.elseStmt
                        ? llvm::BasicBlock::Create(ctx_, "if.else",
                                                   currentFunction_)
                        : nullptr;
  // Created detached: if both arms return, nothing branches here and the
  // block is only appended so that later statements have somewhere to go.
  auto *endBlock = llvm::BasicBlock::Create(ctx_, "if.end");

  builder_.CreateCondBr(cond, thenBlock, elseBlock ? elseBlock : endBlock);

  builder_.SetInsertPoint(thenBlock);
  if (stmt.thenStmt)
    emitStmt(*stmt.thenStmt);
  if (blockOpen())
    builder_.CreateBr(endBlock);

  if (elseBlock) {
    builder_.SetInsertPoint(elseBlock);
    emitStmt(*stmt.elseStmt);
    if (blockOpen())
      builder_.CreateBr(endBlock);
  }

  currentFunction_->insert(currentFunction_->end(), endBlock);
  builder_.SetInsertPoint(endBlock);
}

void IRGenerator::emitWhile(const WhileStmt &stmt) {
  auto *condBlock =
      llvm::BasicBlock::Create(ctx_, "while.cond", currentFunction_);
  auto *bodyBlock =
      llvm::BasicBlock::Create(ctx_, "while.body", currentFunction_);
  auto *endBlock =
      llvm::BasicBlock::Create(ctx_, "while.end", currentFunction_);

  builder_.CreateBr(condBlock);

  builder_.SetInsertPoint(condBlock);
  llvm::Value *cond = emitCondition(*stmt.cond);
  builder_.CreateCondBr(cond, bodyBlock, endBlock);

  builder_.SetInsertPoint(bodyBlock);
  if (stmt.body)
    emitStmt(*stmt.body);
  if (blockOpen())
    builder_.CreateBr(condBlock);

  builder_.SetInsertPoint(endBlock);
}

void IRGenerator::emitReturn(const ReturnStmt &stmt) {
  // Reading the LLVM return type rather than the C- one handles main, whose
  // `return;` has to become `ret i32 0`.
  if (currentFunction_->getReturnType()->isVoidTy()) {
    if (stmt.value)
      emitExpr(*stmt.value);
    builder_.CreateRetVoid();
    return;
  }

  llvm::Value *value = stmt.value ? emitExpr(*stmt.value) : nullptr;
  builder_.CreateRet(value ? value : int32(0));
}

// -------------------------------------------------------------- expressions

llvm::Value *IRGenerator::checkIndex(llvm::Value *index) {
  if (!options_.indexChecks)
    return index;
  // A literal that is already non-negative needs no test.
  if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(index))
    if (!constant->isNegative())
      return index;

  auto *failBlock =
      llvm::BasicBlock::Create(ctx_, "index.fail", currentFunction_);
  auto *okBlock = llvm::BasicBlock::Create(ctx_, "index.ok", currentFunction_);

  llvm::Value *negative = builder_.CreateICmpSLT(index, int32(0), "index.neg");
  builder_.CreateCondBr(negative, failBlock, okBlock);

  builder_.SetInsertPoint(failBlock);
  builder_.CreateCall(indexErrorFunction(), {index});
  builder_.CreateUnreachable();

  builder_.SetInsertPoint(okBlock);
  return index;
}

llvm::Value *IRGenerator::arrayBase(const Symbol &symbol) {
  llvm::Value *address = addressOf(symbol);
  if (!address)
    return llvm::ConstantPointerNull::get(ptrTy_);

  // An array parameter already holds a pointer to somebody else's storage.
  if (symbol.kind == SymbolKind::Parameter)
    return builder_.CreateLoad(ptrTy_, address, symbol.name + ".base");

  llvm::Type *arrayType =
      llvm::ArrayType::get(i32Ty_, static_cast<uint64_t>(symbol.arraySize));
  return builder_.CreateGEP(arrayType, address, {indexZero(), indexZero()},
                            symbol.name + ".base");
}

llvm::Value *IRGenerator::elementAddress(const VarExpr &expr) {
  const Symbol *symbol = expr.symbol;
  if (!symbol)
    return nullptr;

  llvm::Value *address = addressOf(*symbol);
  if (!address)
    return nullptr;
  if (!expr.index)
    return address;

  llvm::Value *index = checkIndex(emitExpr(*expr.index));
  // Subscripts are int; widen or narrow to whatever this target indexes with.
  llvm::Value *wide = builder_.CreateSExtOrTrunc(index, indexTy(), "idx");

  // Plain GEP rather than inbounds: spec 3.6 leaves the upper bound
  // unchecked, and marking an out-of-range access inbounds would make it
  // undefined behaviour that the optimizer is free to exploit.
  if (symbol->kind == SymbolKind::Parameter) {
    llvm::Value *base =
        builder_.CreateLoad(ptrTy_, address, symbol->name + ".base");
    return builder_.CreateGEP(i32Ty_, base, wide, symbol->name + ".elem");
  }

  llvm::Type *arrayType =
      llvm::ArrayType::get(i32Ty_, static_cast<uint64_t>(symbol->arraySize));
  return builder_.CreateGEP(arrayType, address, {indexZero(), wide},
                            symbol->name + ".elem");
}

llvm::Value *IRGenerator::emitExpr(const Expr &expr) {
  switch (expr.kind) {
  case NodeKind::NumExpr:
    return int32(static_cast<const NumExpr &>(expr).value);

  case NodeKind::VarExpr: {
    const auto &node = static_cast<const VarExpr &>(expr);
    if (expr.type == Type::IntArray) {
      // Only argument passing may name a whole array, and emitCall handles
      // that before reaching here.
      diags_.error(expr.loc, "internal error: array used as a value");
      return int32(0);
    }
    llvm::Value *address = elementAddress(node);
    if (!address)
      return int32(0);
    return builder_.CreateLoad(i32Ty_, address, node.name);
  }

  case NodeKind::AssignExpr:
    return emitAssign(static_cast<const AssignExpr &>(expr));
  case NodeKind::BinaryExpr:
    return emitBinary(static_cast<const BinaryExpr &>(expr));
  case NodeKind::CallExpr:
    return emitCall(static_cast<const CallExpr &>(expr));
  default:
    return int32(0);
  }
}

/// Spec 3.6: assignment is an expression whose value is the value stored.
llvm::Value *IRGenerator::emitAssign(const AssignExpr &expr) {
  llvm::Value *address = expr.target ? elementAddress(*expr.target) : nullptr;
  llvm::Value *value = expr.value ? emitExpr(*expr.value) : int32(0);
  if (address)
    builder_.CreateStore(value, address);
  return value;
}

llvm::Value *IRGenerator::emitBinary(const BinaryExpr &expr) {
  llvm::Value *lhs = expr.lhs ? emitExpr(*expr.lhs) : int32(0);
  llvm::Value *rhs = expr.rhs ? emitExpr(*expr.rhs) : int32(0);

  switch (expr.op) {
  case BinaryOp::Add:
    return builder_.CreateAdd(lhs, rhs, "add");
  case BinaryOp::Sub:
    return builder_.CreateSub(lhs, rhs, "sub");
  case BinaryOp::Mul:
    return builder_.CreateMul(lhs, rhs, "mul");
  case BinaryOp::Div:
    // Spec 3.8: integer division, truncating toward zero.
    return builder_.CreateSDiv(lhs, rhs, "div");
  default:
    break;
  }

  llvm::Value *compared = nullptr;
  switch (expr.op) {
  case BinaryOp::LessThan:     compared = builder_.CreateICmpSLT(lhs, rhs, "cmp"); break;
  case BinaryOp::LessEqual:    compared = builder_.CreateICmpSLE(lhs, rhs, "cmp"); break;
  case BinaryOp::GreaterThan:  compared = builder_.CreateICmpSGT(lhs, rhs, "cmp"); break;
  case BinaryOp::GreaterEqual: compared = builder_.CreateICmpSGE(lhs, rhs, "cmp"); break;
  case BinaryOp::Equal:        compared = builder_.CreateICmpEQ(lhs, rhs, "cmp");  break;
  default:                     compared = builder_.CreateICmpNE(lhs, rhs, "cmp");  break;
  }
  // Spec 3.7: a comparison is worth 1 or 0, not a distinct boolean type.
  return builder_.CreateZExt(compared, i32Ty_, "cmp.int");
}

llvm::Value *IRGenerator::emitCall(const CallExpr &expr) {
  const Symbol *symbol = expr.symbol;
  if (!symbol)
    return int32(0);

  llvm::Function *callee = functionFor(*symbol);

  std::vector<llvm::Value *> args;
  args.reserve(expr.args.size());
  for (std::size_t i = 0; i < expr.args.size(); ++i) {
    const Expr &arg = *expr.args[i];
    const bool wantsArray =
        i < symbol->paramTypes.size() && symbol->paramTypes[i] == Type::IntArray;

    // Spec 3.3: an array parameter is passed by reference, so the argument is
    // the address of element zero rather than a value.
    if (wantsArray && arg.kind == NodeKind::VarExpr) {
      const auto &var = static_cast<const VarExpr &>(arg);
      if (var.symbol) {
        args.push_back(arrayBase(*var.symbol));
        continue;
      }
    }
    args.push_back(emitExpr(arg));
  }

  // A void call has no value and must stay unnamed.
  const bool hasValue = !callee->getReturnType()->isVoidTy();
  return builder_.CreateCall(callee, args, hasValue ? "call" : "");
}

llvm::Value *IRGenerator::emitCondition(const Expr &expr) {
  llvm::Value *value = emitExpr(expr);
  return builder_.CreateICmpNE(value, int32(0), "tobool");
}

} // namespace

std::unique_ptr<llvm::Module> generateIR(const Program &program,
                                         llvm::LLVMContext &context,
                                         DiagnosticEngine &diags,
                                         const IRGenOptions &options) {
  return IRGenerator(context, diags, options).run(program);
}

} // namespace cminus
