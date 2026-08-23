#ifndef CMINUS_SEMANTICANALYZER_H
#define CMINUS_SEMANTICANALYZER_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "cminus/AST.h"
#include "cminus/Symbol.h"

namespace cminus {

class DiagnosticEngine;

/// Checks the rules of spec section 3 that the grammar cannot express, and
/// resolves every name to a symbol.
///
/// Analysis walks the declarations in source order, which is exactly what
/// "declared before use" means in C-: there are no prototypes and no forward
/// references, so a name is visible only if its declaration has already been
/// walked. A function is entered into the table before its own body is
/// analyzed, which is what makes recursion legal.
///
/// Every error keeps going with a plausible type (int) so that one bad
/// subexpression does not produce a cascade of follow-on complaints.
class SemanticAnalyzer {
public:
  explicit SemanticAnalyzer(DiagnosticEngine &diags);

  void analyze(Program &program);

  /// Scopes in the order they were opened, for --dump-symbols. Valid only
  /// after analyze() has run.
  const std::vector<ScopeReport> &scopes() const { return scopes_; }
  void printSymbols(std::ostream &os) const;

private:
  // --- declarations
  void declareBuiltins();
  void analyzeDecl(Decl &decl);
  void analyzeVarDecl(VarDecl &decl);
  void analyzeParam(ParamDecl &param);
  void analyzeFunDecl(FunDecl &decl);
  void checkMain(const Program &program);

  // --- statements
  void analyzeCompound(CompoundStmt &block, bool ownScope);
  void analyzeStmt(Stmt &stmt);
  void analyzeReturn(ReturnStmt &stmt);
  void analyzeCondition(Expr &cond);

  // --- expressions; each sets and returns Expr::type
  Type analyzeExpr(Expr &expr);
  Type analyzeVarExpr(VarExpr &expr);
  Type analyzeCallExpr(CallExpr &expr);
  Type analyzeAssign(AssignExpr &expr);
  Type analyzeBinary(BinaryExpr &expr);

  // --- helpers
  const Symbol *declareOrDiagnose(Symbol symbol, SourceLocation loc);
  /// Report unless `expr` has type int. `context` completes the sentence
  /// "... cannot be used <context>".
  bool requireInt(const Expr &expr, const std::string &context);
  void closeScope();

  DiagnosticEngine &diags_;
  SymbolTable table_;
  std::vector<ScopeReport> scopes_;
  const FunDecl *currentFunction_ = nullptr;
};

} // namespace cminus

#endif // CMINUS_SEMANTICANALYZER_H
