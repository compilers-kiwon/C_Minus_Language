#ifndef CMINUS_PARSER_H
#define CMINUS_PARSER_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "cminus/AST.h"
#include "cminus/Token.h"

namespace cminus {

class DiagnosticEngine;

/// Hand-written recursive-descent parser for the grammar in
/// docs/spec/c-minus.md. The left recursion of rules 2, 8, 11, 12, 22, 24 and
/// 29 becomes iteration; rules 3/4/6 and 19/26/27 are left-factored on their
/// shared prefixes. See docs/spec/analysis.md section 4.
///
/// Errors are recovered from in panic mode so that one run reports several
/// problems instead of stopping at the first.
class Parser {
public:
  Parser(std::vector<Token> tokens, DiagnosticEngine &diags);

  /// rule 1: program -> declaration-list
  std::unique_ptr<Program> parseProgram();

private:
  // --- token access
  const Token &peek(std::size_t ahead = 0) const;
  const Token &previous() const;
  bool atEnd() const;
  bool check(TokenKind kind) const;
  const Token &advance();
  bool match(TokenKind kind);
  bool expect(TokenKind kind, const std::string &context);

  // --- error handling
  void errorAt(const Token &tok, const std::string &message);
  void synchronizeDeclaration();
  void synchronizeStatement();

  // --- declarations (rules 3-9)
  std::unique_ptr<Decl> parseDeclaration();
  bool parseTypeSpecifier(Type &type, SourceLocation &loc);
  std::unique_ptr<VarDecl> finishVarDeclaration(Type type,
                                                SourceLocation typeLoc,
                                                const Token &name);
  std::unique_ptr<FunDecl> finishFunDeclaration(Type type,
                                                SourceLocation typeLoc,
                                                const Token &name);
  bool parseParams(std::vector<std::unique_ptr<ParamDecl>> &params);
  std::unique_ptr<ParamDecl> parseParam();
  std::unique_ptr<VarDecl> parseLocalVarDeclaration();

  // --- statements (rules 10-17)
  std::unique_ptr<CompoundStmt> parseCompoundStmt();
  std::unique_ptr<Stmt> parseStatement();
  std::unique_ptr<Stmt> parseExpressionStmt();
  std::unique_ptr<Stmt> parseSelectionStmt();
  std::unique_ptr<Stmt> parseIterationStmt();
  std::unique_ptr<Stmt> parseReturnStmt();

  // --- expressions (rules 18-29)
  std::unique_ptr<Expr> parseExpression();
  std::unique_ptr<Expr> parseSimpleExpression();
  std::unique_ptr<Expr> parseAdditiveExpression();
  std::unique_ptr<Expr> parseTerm();
  std::unique_ptr<Expr> parseFactor();
  bool parseArgs(std::vector<std::unique_ptr<Expr>> &args);

  std::vector<Token> tokens_;
  DiagnosticEngine &diags_;
  std::size_t pos_ = 0;
};

} // namespace cminus

#endif // CMINUS_PARSER_H
