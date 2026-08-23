#include "cminus/Parser.h"

#include <utility>

#include "cminus/Diagnostic.h"

namespace cminus {

namespace {

/// How a token reads inside "..., found X".
std::string describeToken(const Token &tok) {
  switch (tok.kind) {
  case TokenKind::EndOfFile:
    return "end of file";
  case TokenKind::Identifier:
    return "identifier '" + tok.lexeme + "'";
  case TokenKind::Number:
    return "number '" + tok.lexeme + "'";
  default:
    return "'" + tok.lexeme + "'";
  }
}

/// How a token reads inside "expected X".
std::string expectedName(TokenKind kind) {
  if (kind == TokenKind::Identifier)
    return "an identifier";
  if (kind == TokenKind::Number)
    return "a number";
  return std::string("'") + tokenKindSpelling(kind) + "'";
}

BinaryOp relopFor(TokenKind kind) {
  switch (kind) {
  case TokenKind::Less:         return BinaryOp::LessThan;
  case TokenKind::LessEqual:    return BinaryOp::LessEqual;
  case TokenKind::Greater:      return BinaryOp::GreaterThan;
  case TokenKind::GreaterEqual: return BinaryOp::GreaterEqual;
  case TokenKind::EqualEqual:   return BinaryOp::Equal;
  default:                      return BinaryOp::NotEqual;
  }
}

} // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine &diags)
    : diags_(diags) {
  // The scanner has already reported each Error token; dropping them keeps the
  // parser from emitting a second diagnostic for the same character.
  tokens_.reserve(tokens.size());
  for (Token &tok : tokens)
    if (tok.isNot(TokenKind::Error))
      tokens_.push_back(std::move(tok));

  if (tokens_.empty() || tokens_.back().isNot(TokenKind::EndOfFile)) {
    Token eof;
    eof.kind = TokenKind::EndOfFile;
    if (!tokens_.empty())
      eof.loc = tokens_.back().loc;
    tokens_.push_back(std::move(eof));
  }
}

// ------------------------------------------------------------- token access

const Token &Parser::peek(std::size_t ahead) const {
  const std::size_t index = pos_ + ahead;
  return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

const Token &Parser::previous() const {
  return pos_ > 0 ? tokens_[pos_ - 1] : tokens_.front();
}

bool Parser::atEnd() const { return peek().is(TokenKind::EndOfFile); }

bool Parser::check(TokenKind kind) const { return peek().is(kind); }

const Token &Parser::advance() {
  if (!atEnd())
    ++pos_;
  return previous();
}

bool Parser::match(TokenKind kind) {
  if (!check(kind))
    return false;
  advance();
  return true;
}

bool Parser::expect(TokenKind kind, const std::string &context) {
  if (check(kind)) {
    advance();
    return true;
  }
  errorAt(peek(), "expected " + expectedName(kind) + " " + context +
                      ", found " + describeToken(peek()));
  return false;
}

// ----------------------------------------------------------- error handling

void Parser::errorAt(const Token &tok, const std::string &message) {
  diags_.error(tok.loc, message);
}

/// Skip forward to the next top-level declaration. Every declaration starts
/// with a type specifier, but one may also appear as a local declaration
/// inside a function body, so brace depth is tracked and only a type specifier
/// at depth zero counts. Recovery starts wherever the error was, which may
/// already be inside a body; a stray `}` therefore clamps at zero rather than
/// going negative.
void Parser::synchronizeDeclaration() {
  int depth = 0;
  while (!atEnd()) {
    if (depth == 0 && peek().isTypeSpecifier())
      return;
    if (check(TokenKind::LBrace))
      ++depth;
    else if (check(TokenKind::RBrace) && depth > 0)
      --depth;
    advance();
  }
}

/// Skip forward to something that plausibly starts a new statement.
void Parser::synchronizeStatement() {
  while (!atEnd()) {
    if (previous().is(TokenKind::Semicolon))
      return;
    switch (peek().kind) {
    case TokenKind::KwIf:
    case TokenKind::KwWhile:
    case TokenKind::KwReturn:
    case TokenKind::KwInt:
    case TokenKind::KwVoid:
    case TokenKind::LBrace:
    case TokenKind::RBrace:
      return;
    default:
      advance();
    }
  }
}

// ------------------------------------------------------------- declarations

std::unique_ptr<Program> Parser::parseProgram() {
  auto program = std::make_unique<Program>();

  // Spec 3.1: a program is a non-empty list of declarations.
  if (atEnd()) {
    errorAt(peek(), "a program must contain at least one declaration");
    return program;
  }

  while (!atEnd()) {
    const std::size_t before = pos_;
    if (auto decl = parseDeclaration()) {
      program->decls.push_back(std::move(decl));
      continue;
    }
    synchronizeDeclaration();
    if (pos_ == before)
      advance(); // guarantee forward progress
  }
  return program;
}

/// rules 3, 4 and 6, left-factored on the shared `type-specifier ID` prefix.
std::unique_ptr<Decl> Parser::parseDeclaration() {
  Type type = Type::Int;
  SourceLocation typeLoc;
  if (!parseTypeSpecifier(type, typeLoc))
    return nullptr;

  if (!check(TokenKind::Identifier)) {
    errorAt(peek(), "expected an identifier after '" +
                        std::string(typeName(type)) + "', found " +
                        describeToken(peek()));
    return nullptr;
  }
  const Token name = advance();

  if (check(TokenKind::LParen))
    return finishFunDeclaration(type, typeLoc, name);
  return finishVarDeclaration(type, typeLoc, name);
}

/// rule 5: type-specifier -> int | void
bool Parser::parseTypeSpecifier(Type &type, SourceLocation &loc) {
  loc = peek().loc;
  if (match(TokenKind::KwInt)) {
    type = Type::Int;
    return true;
  }
  if (match(TokenKind::KwVoid)) {
    type = Type::Void;
    return true;
  }
  errorAt(peek(), "expected 'int' or 'void', found " + describeToken(peek()));
  return false;
}

/// rule 4, with `type-specifier ID` already consumed.
std::unique_ptr<VarDecl> Parser::finishVarDeclaration(Type type,
                                                      SourceLocation typeLoc,
                                                      const Token &name) {
  auto decl = std::make_unique<VarDecl>(name.loc, name.lexeme, typeLoc, type);

  if (match(TokenKind::LBracket)) {
    decl->isArray = true;
    if (!check(TokenKind::Number)) {
      errorAt(peek(), "expected an array size, found " + describeToken(peek()));
      return nullptr;
    }
    decl->arraySize = advance().value;
    if (!expect(TokenKind::RBracket, "after the array size"))
      return nullptr;
  }

  // Spec 3.2: one variable per declaration.
  if (check(TokenKind::Comma)) {
    errorAt(peek(), "C- declares one variable per declaration; write a "
                    "separate declaration for each name");
    return nullptr;
  }

  if (!expect(TokenKind::Semicolon,
              "after the declaration of '" + name.lexeme + "'"))
    return nullptr;
  return decl;
}

/// rule 6, with `type-specifier ID` already consumed.
std::unique_ptr<FunDecl> Parser::finishFunDeclaration(Type type,
                                                      SourceLocation typeLoc,
                                                      const Token &name) {
  auto fn = std::make_unique<FunDecl>(name.loc, name.lexeme, typeLoc, type);

  if (!expect(TokenKind::LParen, "after the function name"))
    return nullptr;
  if (!parseParams(fn->params))
    return nullptr;
  if (!expect(TokenKind::RParen, "after the parameter list"))
    return nullptr;

  if (!check(TokenKind::LBrace)) {
    // Spec 3.1: C- has no prototypes, so a declaration is always a definition.
    errorAt(peek(), "expected '{' to begin the body of '" + name.lexeme +
                        "'; C- has no function prototypes, found " +
                        describeToken(peek()));
    return nullptr;
  }

  fn->body = parseCompoundStmt();
  if (!fn->body)
    return nullptr;
  return fn;
}

/// rules 7 and 8.
bool Parser::parseParams(std::vector<std::unique_ptr<ParamDecl>> &params) {
  // `void` immediately before `)` means "no parameters" (rule 7). `void x` is
  // a void-typed parameter: it parses, and semantic analysis rejects it.
  if (check(TokenKind::KwVoid) && peek(1).is(TokenKind::RParen)) {
    advance();
    return true;
  }

  if (check(TokenKind::RParen)) {
    errorAt(peek(), "C- has no empty parameter list; write '(void)'");
    return false;
  }

  for (;;) {
    auto param = parseParam();
    if (!param)
      return false;
    params.push_back(std::move(param));
    if (!match(TokenKind::Comma))
      return true;
  }
}

/// rule 9: param -> type-specifier ID | type-specifier ID [ ]
std::unique_ptr<ParamDecl> Parser::parseParam() {
  Type type = Type::Int;
  SourceLocation typeLoc;
  if (!parseTypeSpecifier(type, typeLoc))
    return nullptr;

  if (!check(TokenKind::Identifier)) {
    errorAt(peek(),
            "expected a parameter name, found " + describeToken(peek()));
    return nullptr;
  }
  const Token name = advance();

  auto param =
      std::make_unique<ParamDecl>(name.loc, name.lexeme, typeLoc, type);
  if (match(TokenKind::LBracket)) {
    param->isArray = true;
    if (check(TokenKind::Number)) {
      errorAt(peek(),
              "an array parameter has no size; write '" + name.lexeme + "[]'");
      return nullptr;
    }
    if (!expect(TokenKind::RBracket, "after '[' in an array parameter"))
      return nullptr;
  }
  return param;
}

/// A local declaration is rule 4 only: rule 11 admits var-declaration alone.
std::unique_ptr<VarDecl> Parser::parseLocalVarDeclaration() {
  Type type = Type::Int;
  SourceLocation typeLoc;
  if (!parseTypeSpecifier(type, typeLoc))
    return nullptr;

  if (!check(TokenKind::Identifier)) {
    errorAt(peek(), "expected an identifier after '" +
                        std::string(typeName(type)) + "', found " +
                        describeToken(peek()));
    return nullptr;
  }
  const Token name = advance();

  if (check(TokenKind::LParen)) {
    errorAt(name, "functions cannot be declared inside a block in C-");
    return nullptr;
  }
  return finishVarDeclaration(type, typeLoc, name);
}

// --------------------------------------------------------------- statements

/// rules 10, 11 and 12.
std::unique_ptr<CompoundStmt> Parser::parseCompoundStmt() {
  const SourceLocation loc = peek().loc;
  if (!expect(TokenKind::LBrace, "to begin a block"))
    return nullptr;

  auto block = std::make_unique<CompoundStmt>(loc);

  // rule 11: all local declarations precede the statements.
  while (peek().isTypeSpecifier()) {
    const std::size_t before = pos_;
    if (auto decl = parseLocalVarDeclaration()) {
      block->localDecls.push_back(std::move(decl));
      continue;
    }
    synchronizeStatement();
    if (pos_ == before)
      advance();
  }

  // rule 12.
  while (!check(TokenKind::RBrace) && !atEnd()) {
    const std::size_t before = pos_;
    if (auto stmt = parseStatement()) {
      block->stmts.push_back(std::move(stmt));
      continue;
    }
    synchronizeStatement();
    if (pos_ == before)
      advance();
  }

  if (!expect(TokenKind::RBrace, "to close the block"))
    return nullptr;
  return block;
}

/// rule 13.
std::unique_ptr<Stmt> Parser::parseStatement() {
  switch (peek().kind) {
  case TokenKind::LBrace:
    return parseCompoundStmt();
  case TokenKind::KwIf:
    return parseSelectionStmt();
  case TokenKind::KwWhile:
    return parseIterationStmt();
  case TokenKind::KwReturn:
    return parseReturnStmt();
  case TokenKind::Semicolon:
  case TokenKind::Identifier:
  case TokenKind::Number:
  case TokenKind::LParen:
    return parseExpressionStmt();
  case TokenKind::KwInt:
  case TokenKind::KwVoid:
    // rule 11 puts every declaration ahead of every statement.
    errorAt(peek(), "declarations must appear before any statement in a block");
    return nullptr;
  default:
    errorAt(peek(), "expected a statement, found " + describeToken(peek()));
    return nullptr;
  }
}

/// rule 14: expression ; | ;
std::unique_ptr<Stmt> Parser::parseExpressionStmt() {
  auto stmt = std::make_unique<ExprStmt>(peek().loc);
  if (match(TokenKind::Semicolon))
    return stmt; // the empty statement

  stmt->expr = parseExpression();
  if (!stmt->expr)
    return nullptr;
  if (!expect(TokenKind::Semicolon, "after the expression"))
    return nullptr;
  return stmt;
}

/// rule 15. The grammar is ambiguous here; consuming `else` as soon as it is
/// seen attaches it to the nearest unmatched `if`, which is the standard
/// reading (spec 3.5).
std::unique_ptr<Stmt> Parser::parseSelectionStmt() {
  auto node = std::make_unique<IfStmt>(advance().loc); // 'if'

  if (!expect(TokenKind::LParen, "after 'if'"))
    return nullptr;
  node->cond = parseExpression();
  if (!node->cond)
    return nullptr;
  if (!expect(TokenKind::RParen, "after the condition of 'if'"))
    return nullptr;

  node->thenStmt = parseStatement();
  if (!node->thenStmt)
    return nullptr;

  if (match(TokenKind::KwElse)) {
    node->elseStmt = parseStatement();
    if (!node->elseStmt)
      return nullptr;
  }
  return node;
}

/// rule 16.
std::unique_ptr<Stmt> Parser::parseIterationStmt() {
  auto node = std::make_unique<WhileStmt>(advance().loc); // 'while'

  if (!expect(TokenKind::LParen, "after 'while'"))
    return nullptr;
  node->cond = parseExpression();
  if (!node->cond)
    return nullptr;
  if (!expect(TokenKind::RParen, "after the condition of 'while'"))
    return nullptr;

  node->body = parseStatement();
  if (!node->body)
    return nullptr;
  return node;
}

/// rule 17: return ; | return expression ;
std::unique_ptr<Stmt> Parser::parseReturnStmt() {
  auto node = std::make_unique<ReturnStmt>(advance().loc); // 'return'

  if (match(TokenKind::Semicolon))
    return node;

  node->value = parseExpression();
  if (!node->value)
    return nullptr;
  if (!expect(TokenKind::Semicolon, "after the return value"))
    return nullptr;
  return node;
}

// -------------------------------------------------------------- expressions

/// rule 18: expression -> var = expression | simple-expression
///
/// The two alternatives share an unbounded prefix, because `var` may be
/// `ID [ expression ]`. Rather than look ahead, parse a simple-expression and
/// then check for `=`; what was parsed is reinterpreted as the target.
std::unique_ptr<Expr> Parser::parseExpression() {
  auto lhs = parseSimpleExpression();
  if (!lhs)
    return nullptr;
  if (!check(TokenKind::Assign))
    return lhs;

  const Token eq = advance();
  auto value = parseExpression(); // right-recursive: `=` is right-associative
  if (!value)
    return nullptr;

  // Only a bare `var` is an l-value. `(x)` is a factor, not a var, so it is
  // rejected as well.
  if (lhs->kind != NodeKind::VarExpr || lhs->parenthesized) {
    errorAt(eq, "left operand of '=' must be a variable or an array element");
    return nullptr;
  }

  auto assign = std::make_unique<AssignExpr>(lhs->loc);
  assign->opLoc = eq.loc;
  assign->target.reset(static_cast<VarExpr *>(lhs.release()));
  assign->value = std::move(value);
  return assign;
}

/// rules 20 and 21.
std::unique_ptr<Expr> Parser::parseSimpleExpression() {
  auto lhs = parseAdditiveExpression();
  if (!lhs)
    return nullptr;
  if (!peek().isRelop())
    return lhs;

  const Token op = advance();
  auto rhs = parseAdditiveExpression();
  if (!rhs)
    return nullptr;

  // Rule 20 permits at most one relational operator: they do not associate.
  if (peek().isRelop()) {
    errorAt(peek(), "relational operators do not associate in C-; parenthesize "
                    "one of the comparisons");
    return nullptr;
  }

  auto node = std::make_unique<BinaryExpr>(lhs->loc, relopFor(op.kind));
  node->opLoc = op.loc;
  node->lhs = std::move(lhs);
  node->rhs = std::move(rhs);
  return node;
}

/// rules 22 and 23. The left recursion becomes a loop that folds to the left,
/// preserving left associativity.
std::unique_ptr<Expr> Parser::parseAdditiveExpression() {
  auto node = parseTerm();
  if (!node)
    return nullptr;

  while (peek().isAddop()) {
    const Token op = advance();
    auto rhs = parseTerm();
    if (!rhs)
      return nullptr;
    auto bin = std::make_unique<BinaryExpr>(
        node->loc, op.is(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub);
    bin->opLoc = op.loc;
    bin->lhs = std::move(node);
    bin->rhs = std::move(rhs);
    node = std::move(bin);
  }
  return node;
}

/// rules 24 and 25.
std::unique_ptr<Expr> Parser::parseTerm() {
  auto node = parseFactor();
  if (!node)
    return nullptr;

  while (peek().isMulop()) {
    const Token op = advance();
    auto rhs = parseFactor();
    if (!rhs)
      return nullptr;
    auto bin = std::make_unique<BinaryExpr>(
        node->loc, op.is(TokenKind::Star) ? BinaryOp::Mul : BinaryOp::Div);
    bin->opLoc = op.loc;
    bin->lhs = std::move(node);
    bin->rhs = std::move(rhs);
    node = std::move(bin);
  }
  return node;
}

/// rules 26, 19 and 27, left-factored on the shared leading ID.
std::unique_ptr<Expr> Parser::parseFactor() {
  const Token tok = peek();

  switch (tok.kind) {
  case TokenKind::LParen: {
    advance();
    auto inner = parseExpression();
    if (!inner)
      return nullptr;
    if (!expect(TokenKind::RParen, "after the parenthesized expression"))
      return nullptr;
    inner->parenthesized = true;
    return inner;
  }

  case TokenKind::Number:
    advance();
    return std::make_unique<NumExpr>(tok.loc, tok.value);

  case TokenKind::Identifier: {
    advance();
    if (match(TokenKind::LParen)) { // rule 27: call
      auto call = std::make_unique<CallExpr>(tok.loc, tok.lexeme);
      if (!parseArgs(call->args))
        return nullptr;
      if (!expect(TokenKind::RParen, "after the argument list"))
        return nullptr;
      return call;
    }
    auto var = std::make_unique<VarExpr>(tok.loc, tok.lexeme);
    if (match(TokenKind::LBracket)) { // rule 19: array element
      var->index = parseExpression();
      if (!var->index)
        return nullptr;
      if (!expect(TokenKind::RBracket, "after the array subscript"))
        return nullptr;
    }
    return var;
  }

  case TokenKind::Minus:
    // A common C habit that the grammar has no rule for.
    errorAt(tok, "C- has no unary minus; write '0 - x' instead");
    return nullptr;

  default:
    errorAt(tok, "expected an expression, found " + describeToken(tok));
    return nullptr;
  }
}

/// rules 28 and 29.
bool Parser::parseArgs(std::vector<std::unique_ptr<Expr>> &args) {
  if (check(TokenKind::RParen))
    return true; // empty argument list

  for (;;) {
    auto arg = parseExpression();
    if (!arg)
      return false;
    args.push_back(std::move(arg));
    if (!match(TokenKind::Comma))
      return true;
  }
}

} // namespace cminus
