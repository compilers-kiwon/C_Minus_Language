#include "cminus/SemanticAnalyzer.h"

#include <algorithm>
#include <ostream>
#include <utility>

#include "cminus/Diagnostic.h"

namespace cminus {

namespace {

/// How an expression is referred to inside a diagnostic.
std::string exprLabel(const Expr &expr) {
  if (expr.kind == NodeKind::VarExpr)
    return "'" + static_cast<const VarExpr &>(expr).name + "'";
  if (expr.kind == NodeKind::CallExpr)
    return "the result of '" + static_cast<const CallExpr &>(expr).callee + "'";
  return "this expression";
}

std::string plural(std::size_t n, const char *singular, const char *many) {
  return std::to_string(n) + ' ' + (n == 1 ? singular : many);
}

/// Conservative "every path through this statement returns". A `while` is
/// assumed not to run, and an `if` without an `else` may fall through.
bool alwaysReturns(const Stmt &stmt) {
  switch (stmt.kind) {
  case NodeKind::ReturnStmt:
    return true;
  case NodeKind::CompoundStmt: {
    const auto &block = static_cast<const CompoundStmt &>(stmt);
    for (const auto &inner : block.stmts)
      if (alwaysReturns(*inner))
        return true;
    return false;
  }
  case NodeKind::IfStmt: {
    const auto &node = static_cast<const IfStmt &>(stmt);
    return node.elseStmt && node.thenStmt && alwaysReturns(*node.thenStmt) &&
           alwaysReturns(*node.elseStmt);
  }
  default:
    return false;
  }
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine &diags) : diags_(diags) {}

// ------------------------------------------------------------------- driver

void SemanticAnalyzer::analyze(Program &program) {
  table_.pushScope("global");
  declareBuiltins();

  for (const auto &decl : program.decls)
    analyzeDecl(*decl);

  checkMain(program);
  closeScope();

  // Report scopes in the order they were opened, which reads top-down.
  std::sort(scopes_.begin(), scopes_.end(),
            [](const ScopeReport &a, const ScopeReport &b) {
              return a.order < b.order;
            });
}

void SemanticAnalyzer::closeScope() { scopes_.push_back(table_.popScope()); }

/// Spec 3.10: input and output behave as if declared in the global scope.
void SemanticAnalyzer::declareBuiltins() {
  Symbol input;
  input.kind = SymbolKind::Function;
  input.name = "input";
  input.type = Type::Int;
  input.returnType = Type::Int;
  input.isBuiltin = true;
  table_.declare(std::move(input), nullptr);

  Symbol output;
  output.kind = SymbolKind::Function;
  output.name = "output";
  output.type = Type::Void;
  output.returnType = Type::Void;
  output.paramTypes.push_back(Type::Int);
  output.isBuiltin = true;
  table_.declare(std::move(output), nullptr);
}

const Symbol *SemanticAnalyzer::declareOrDiagnose(Symbol symbol,
                                                  SourceLocation loc) {
  const std::string name = symbol.name;
  const Symbol *existing = nullptr;
  if (const Symbol *inserted = table_.declare(std::move(symbol), &existing))
    return inserted;

  if (existing && existing->isBuiltin)
    diags_.error(loc, "'" + name +
                          "' redeclares a built-in function of the same name");
  else {
    diags_.error(loc, "redeclaration of '" + name + "'");
    if (existing)
      diags_.note(existing->loc, "previous declaration of '" + name + "' is here");
  }
  return nullptr;
}

// ------------------------------------------------------------- declarations

void SemanticAnalyzer::analyzeDecl(Decl &decl) {
  switch (decl.kind) {
  case NodeKind::VarDecl:
    analyzeVarDecl(static_cast<VarDecl &>(decl));
    break;
  case NodeKind::FunDecl:
    analyzeFunDecl(static_cast<FunDecl &>(decl));
    break;
  default:
    break;
  }
}

void SemanticAnalyzer::analyzeVarDecl(VarDecl &decl) {
  // Spec 3.2: `void` passes the grammar but is not a legal variable type.
  if (decl.declaredType == Type::Void)
    diags_.error(decl.typeLoc, "variable '" + decl.name + "' declared void");

  // The spec does not forbid a zero-length array, but no subscript into one
  // can ever be in range, so it is worth pointing out.
  if (decl.isArray && decl.arraySize == 0)
    diags_.warning(decl.loc,
                   "array '" + decl.name + "' has size 0 and cannot be indexed");

  Symbol symbol;
  symbol.kind = SymbolKind::Variable;
  symbol.name = decl.name;
  // Recorded as int even when declared void, so later uses do not cascade.
  symbol.type = decl.isArray ? Type::IntArray : Type::Int;
  symbol.arraySize = decl.isArray ? decl.arraySize : 0;
  symbol.loc = decl.loc;
  symbol.decl = &decl;
  decl.symbol = declareOrDiagnose(std::move(symbol), decl.loc);
}

void SemanticAnalyzer::analyzeParam(ParamDecl &param) {
  if (param.declaredType == Type::Void)
    diags_.error(param.typeLoc, "parameter '" + param.name + "' declared void");

  Symbol symbol;
  symbol.kind = SymbolKind::Parameter;
  symbol.name = param.name;
  symbol.type = param.isArray ? Type::IntArray : Type::Int;
  symbol.loc = param.loc;
  symbol.decl = &param;
  param.symbol = declareOrDiagnose(std::move(symbol), param.loc);
}

void SemanticAnalyzer::analyzeFunDecl(FunDecl &decl) {
  Symbol symbol;
  symbol.kind = SymbolKind::Function;
  symbol.name = decl.name;
  symbol.type = decl.returnType;
  symbol.returnType = decl.returnType;
  symbol.loc = decl.loc;
  symbol.decl = &decl;
  for (const auto &param : decl.params)
    symbol.paramTypes.push_back(param->isArray ? Type::IntArray : Type::Int);

  // Declared before the body is walked, so the function can call itself.
  decl.symbol = declareOrDiagnose(std::move(symbol), decl.loc);

  const FunDecl *enclosing = currentFunction_;
  currentFunction_ = &decl;

  // Spec 3.3: a parameter's scope is the function's compound statement, so
  // the body does not open a scope of its own. A local that repeats a
  // parameter name is therefore a redeclaration, as it is in C.
  table_.pushScope("function '" + decl.name + "'");
  for (const auto &param : decl.params)
    analyzeParam(*param);
  if (decl.body)
    analyzeCompound(*decl.body, /*ownScope=*/false);
  closeScope();

  currentFunction_ = enclosing;

  if (decl.returnType != Type::Void && decl.body && !alwaysReturns(*decl.body))
    diags_.warning(decl.loc, "control may reach the end of non-void function '" +
                                 decl.name + "' without returning a value");
}

/// Spec 3.1: the last declaration must be `void main(void)`.
void SemanticAnalyzer::checkMain(const Program &program) {
  if (program.decls.empty())
    return; // the parser has already complained

  const FunDecl *main = nullptr;
  std::size_t mainIndex = 0;
  for (std::size_t i = 0; i < program.decls.size(); ++i) {
    const Decl &decl = *program.decls[i];
    if (decl.name == "main" && decl.kind == NodeKind::FunDecl) {
      main = static_cast<const FunDecl *>(&decl);
      mainIndex = i;
    }
  }

  if (!main) {
    diags_.error(program.decls.back()->loc,
                 "the last declaration of a C- program must be "
                 "'void main(void)'");
    return;
  }

  if (mainIndex + 1 != program.decls.size())
    diags_.error(main->loc, "'main' must be the last declaration in the program");

  if (main->returnType != Type::Void || !main->params.empty())
    diags_.error(main->loc, "'main' must be declared as 'void main(void)'");
}

// --------------------------------------------------------------- statements

void SemanticAnalyzer::analyzeCompound(CompoundStmt &block, bool ownScope) {
  if (ownScope)
    table_.pushScope("block");

  for (const auto &decl : block.localDecls)
    analyzeVarDecl(*decl);
  for (const auto &stmt : block.stmts)
    analyzeStmt(*stmt);

  if (ownScope)
    closeScope();
}

void SemanticAnalyzer::analyzeStmt(Stmt &stmt) {
  switch (stmt.kind) {
  case NodeKind::CompoundStmt:
    analyzeCompound(static_cast<CompoundStmt &>(stmt), /*ownScope=*/true);
    break;

  case NodeKind::ExprStmt: {
    // An expression statement exists for its side effects, so a void result
    // is fine here and only here.
    auto &node = static_cast<ExprStmt &>(stmt);
    if (node.expr)
      analyzeExpr(*node.expr);
    break;
  }

  case NodeKind::IfStmt: {
    auto &node = static_cast<IfStmt &>(stmt);
    if (node.cond)
      analyzeCondition(*node.cond);
    if (node.thenStmt)
      analyzeStmt(*node.thenStmt);
    if (node.elseStmt)
      analyzeStmt(*node.elseStmt);
    break;
  }

  case NodeKind::WhileStmt: {
    auto &node = static_cast<WhileStmt &>(stmt);
    if (node.cond)
      analyzeCondition(*node.cond);
    if (node.body)
      analyzeStmt(*node.body);
    break;
  }

  case NodeKind::ReturnStmt:
    analyzeReturn(static_cast<ReturnStmt &>(stmt));
    break;

  default:
    break;
  }
}

void SemanticAnalyzer::analyzeCondition(Expr &cond) {
  analyzeExpr(cond);
  requireInt(cond, "as a condition");
}

/// Spec 3.5: a non-void function must return a value and a void function must
/// not.
void SemanticAnalyzer::analyzeReturn(ReturnStmt &stmt) {
  const Type expected =
      currentFunction_ ? currentFunction_->returnType : Type::Void;
  const std::string where =
      currentFunction_ ? " in function '" + currentFunction_->name + "'" : "";

  if (!stmt.value) {
    if (expected != Type::Void)
      diags_.error(stmt.loc, "'return' with no value" + where +
                                 ", which returns " + typeName(expected));
    return;
  }

  analyzeExpr(*stmt.value);
  if (expected == Type::Void) {
    diags_.error(stmt.loc, "'return' with a value" + where +
                               ", which returns void");
    return;
  }
  requireInt(*stmt.value, "as a return value");
}

// -------------------------------------------------------------- expressions

bool SemanticAnalyzer::requireInt(const Expr &expr, const std::string &context) {
  if (expr.type == Type::Int)
    return true;

  if (expr.type == Type::IntArray)
    diags_.error(expr.loc, exprLabel(expr) + " is an array and cannot be used " +
                               context + "; index it to get an element");
  else
    diags_.error(expr.loc, exprLabel(expr) + " has type void and cannot be used " +
                               context);
  return false;
}

Type SemanticAnalyzer::analyzeExpr(Expr &expr) {
  switch (expr.kind) {
  case NodeKind::NumExpr:
    expr.type = Type::Int;
    return expr.type;
  case NodeKind::VarExpr:
    return analyzeVarExpr(static_cast<VarExpr &>(expr));
  case NodeKind::CallExpr:
    return analyzeCallExpr(static_cast<CallExpr &>(expr));
  case NodeKind::AssignExpr:
    return analyzeAssign(static_cast<AssignExpr &>(expr));
  case NodeKind::BinaryExpr:
    return analyzeBinary(static_cast<BinaryExpr &>(expr));
  default:
    expr.type = Type::Int;
    return expr.type;
  }
}

/// rule 19. A bare array name keeps type IntArray; only the argument-passing
/// path in analyzeCallExpr accepts that, which is exactly spec 3.9.
Type SemanticAnalyzer::analyzeVarExpr(VarExpr &expr) {
  expr.type = Type::Int;

  const Symbol *symbol = table_.lookup(expr.name);
  if (!symbol) {
    diags_.error(expr.loc, "use of undeclared identifier '" + expr.name + "'");
    if (expr.index)
      analyzeExpr(*expr.index);
    return expr.type;
  }

  if (symbol->isFunction()) {
    diags_.error(expr.loc, "'" + expr.name +
                               "' is a function; call it as '" + expr.name +
                               "(...)'");
    if (expr.index)
      analyzeExpr(*expr.index);
    return expr.type;
  }

  expr.symbol = symbol;

  if (!expr.index) {
    expr.type = symbol->type;
    return expr.type;
  }

  analyzeExpr(*expr.index);
  requireInt(*expr.index, "as an array subscript");

  if (symbol->type != Type::IntArray) {
    diags_.error(expr.loc,
                 "'" + expr.name + "' is not an array and cannot be subscripted");
    return expr.type;
  }

  // Spec 3.6 leaves the upper bound unchecked at run time, but a constant
  // index that is already out of range is worth reporting now. An array
  // parameter has no size of its own, hence the arraySize > 0 guard.
  if (symbol->arraySize > 0 && expr.index->kind == NodeKind::NumExpr) {
    const long long index = static_cast<NumExpr &>(*expr.index).value;
    if (index >= symbol->arraySize)
      diags_.warning(expr.index->loc,
                     "index " + std::to_string(index) + " is past the end of '" +
                         expr.name + "', which has " +
                         std::to_string(symbol->arraySize) + " elements");
  }
  return expr.type;
}

/// rules 27-29, plus spec 3.3 and 3.9 on argument passing.
Type SemanticAnalyzer::analyzeCallExpr(CallExpr &expr) {
  expr.type = Type::Int;

  const Symbol *symbol = table_.lookup(expr.callee);
  for (const auto &arg : expr.args)
    analyzeExpr(*arg);

  if (!symbol) {
    diags_.error(expr.loc, "call to undeclared function '" + expr.callee + "'");
    return expr.type;
  }
  if (!symbol->isFunction()) {
    diags_.error(expr.loc, "'" + expr.callee + "' is a " +
                               symbolKindName(symbol->kind) +
                               ", not a function");
    diags_.note(symbol->loc, "'" + expr.callee + "' declared here");
    return expr.type;
  }

  expr.symbol = symbol;
  expr.type = symbol->returnType;

  if (expr.args.size() != symbol->paramTypes.size()) {
    diags_.error(expr.loc, "'" + expr.callee + "' takes " +
                               plural(symbol->paramTypes.size(), "argument",
                                      "arguments") +
                               " but " +
                               plural(expr.args.size(), "was", "were") +
                               " given");
    if (!symbol->isBuiltin)
      diags_.note(symbol->loc, "'" + expr.callee + "' declared here");
    return expr.type;
  }

  for (std::size_t i = 0; i < expr.args.size(); ++i) {
    const Type wanted = symbol->paramTypes[i];
    const Expr &arg = *expr.args[i];
    if (arg.type == wanted)
      continue;

    const std::string position =
        "as argument " + std::to_string(i + 1) + " of '" + expr.callee + "'";
    if (wanted == Type::IntArray)
      diags_.error(arg.loc, "argument " + std::to_string(i + 1) + " of '" +
                                expr.callee +
                                "' must be an array variable, but " +
                                exprLabel(arg) + " is not an array");
    else
      requireInt(arg, position);
  }
  return expr.type;
}

/// rule 18. The parser has already checked that the target is a `var`; what
/// is left is that it names a single int, not a whole array.
Type SemanticAnalyzer::analyzeAssign(AssignExpr &expr) {
  expr.type = Type::Int;

  if (expr.target) {
    analyzeVarExpr(*expr.target);
    if (expr.target->type == Type::IntArray)
      diags_.error(expr.target->loc, "cannot assign to array '" +
                                         expr.target->name +
                                         "' as a whole; assign to an element");
  }
  if (expr.value) {
    analyzeExpr(*expr.value);
    requireInt(*expr.value, "as the value of an assignment");
  }
  return expr.type;
}

/// rules 20, 22 and 24. Both operands must be int; the result always is,
/// with a comparison yielding 1 or 0 (spec 3.7).
Type SemanticAnalyzer::analyzeBinary(BinaryExpr &expr) {
  expr.type = Type::Int;

  const std::string context =
      isComparison(expr.op) ? "in a comparison" : "in arithmetic";

  if (expr.lhs) {
    analyzeExpr(*expr.lhs);
    requireInt(*expr.lhs, context);
  }
  if (expr.rhs) {
    analyzeExpr(*expr.rhs);
    requireInt(*expr.rhs, context);

    if (expr.op == BinaryOp::Div && expr.rhs->kind == NodeKind::NumExpr &&
        static_cast<NumExpr &>(*expr.rhs).value == 0)
      diags_.warning(expr.opLoc, "division by zero");
  }
  return expr.type;
}

// ------------------------------------------------------------------ dumping

void SemanticAnalyzer::printSymbols(std::ostream &os) const {
  bool first = true;
  for (const ScopeReport &scope : scopes_) {
    if (!first)
      os << '\n';
    first = false;

    os << "scope " << scope.order << "  depth " << scope.depth << "  "
       << scope.label << '\n';
    if (scope.symbols.empty()) {
      os << "  (empty)\n";
      continue;
    }
    for (const Symbol *symbol : scope.symbols) {
      std::string name = symbol->name;
      std::string kind = symbolKindName(symbol->kind);
      std::string type = describeSymbolType(*symbol);
      name.resize(std::max<std::size_t>(name.size(), 10), ' ');
      kind.resize(std::max<std::size_t>(kind.size(), 10), ' ');
      type.resize(std::max<std::size_t>(type.size(), 16), ' ');

      os << "  " << name << ' ' << kind << ' ' << type << ' ';
      if (symbol->isBuiltin)
        os << "<built-in>";
      else
        os << '<' << symbol->loc.line << ':' << symbol->loc.column << '>';
      os << '\n';
    }
  }
}

} // namespace cminus
