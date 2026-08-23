#ifndef CMINUS_AST_H
#define CMINUS_AST_H

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "cminus/SourceLocation.h"

namespace cminus {

/// Semantic types. C- has `int` and `void` (rule 5); arrays are a separate
/// semantic type because they may only appear as a whole when passed to an
/// array parameter (spec 3.9).
enum class Type { Int, Void, IntArray };

const char *typeName(Type type);

enum class NodeKind {
  // Declarations.
  VarDecl,
  ParamDecl,
  FunDecl,
  // Statements.
  CompoundStmt,
  ExprStmt,
  IfStmt,
  WhileStmt,
  ReturnStmt,
  // Expressions.
  AssignExpr,
  BinaryExpr,
  VarExpr,
  CallExpr,
  NumExpr,
};

struct Node {
  NodeKind kind;
  SourceLocation loc;

  Node(NodeKind k, SourceLocation l) : kind(k), loc(l) {}
  virtual ~Node() = default;

  Node(const Node &) = delete;
  Node &operator=(const Node &) = delete;
};

// ---------------------------------------------------------------- declarations

struct Decl : Node {
  std::string name;
  /// Location of the type-specifier, so "variable declared void" can point at
  /// the word `void` rather than at the name.
  SourceLocation typeLoc;

  Decl(NodeKind k, SourceLocation l, std::string n, SourceLocation tl)
      : Node(k, l), name(std::move(n)), typeLoc(tl) {}
};

/// rule 4: type-specifier ID ; | type-specifier ID [ NUM ] ;
struct VarDecl : Decl {
  Type declaredType = Type::Int;
  bool isArray = false;
  long long arraySize = 0; // meaningful only when isArray

  VarDecl(SourceLocation l, std::string n, SourceLocation tl, Type t)
      : Decl(NodeKind::VarDecl, l, std::move(n), tl), declaredType(t) {}
};

/// rule 9: type-specifier ID | type-specifier ID [ ]
struct ParamDecl : Decl {
  Type declaredType = Type::Int;
  bool isArray = false; // written as `int a[]`

  ParamDecl(SourceLocation l, std::string n, SourceLocation tl, Type t)
      : Decl(NodeKind::ParamDecl, l, std::move(n), tl), declaredType(t) {}
};

struct CompoundStmt;

/// rule 6: type-specifier ID ( params ) compound-stmt
struct FunDecl : Decl {
  Type returnType = Type::Int;
  std::vector<std::unique_ptr<ParamDecl>> params;
  std::unique_ptr<CompoundStmt> body;

  FunDecl(SourceLocation l, std::string n, SourceLocation tl, Type ret)
      : Decl(NodeKind::FunDecl, l, std::move(n), tl), returnType(ret) {}
  ~FunDecl() override;
};

// ------------------------------------------------------------------ statements

struct Stmt : Node {
  Stmt(NodeKind k, SourceLocation l) : Node(k, l) {}
};

struct Expr;

/// rule 10: { local-declarations statement-list }
struct CompoundStmt : Stmt {
  std::vector<std::unique_ptr<VarDecl>> localDecls;
  std::vector<std::unique_ptr<Stmt>> stmts;

  explicit CompoundStmt(SourceLocation l)
      : Stmt(NodeKind::CompoundStmt, l) {}
};

/// rule 14: expression ; | ;   (expr is null for the empty statement)
struct ExprStmt : Stmt {
  std::unique_ptr<Expr> expr;

  explicit ExprStmt(SourceLocation l) : Stmt(NodeKind::ExprStmt, l) {}
  ~ExprStmt() override;
};

/// rule 15. elseStmt is null when there is no else branch.
struct IfStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> thenStmt;
  std::unique_ptr<Stmt> elseStmt;

  explicit IfStmt(SourceLocation l) : Stmt(NodeKind::IfStmt, l) {}
  ~IfStmt() override;
};

/// rule 16.
struct WhileStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> body;

  explicit WhileStmt(SourceLocation l) : Stmt(NodeKind::WhileStmt, l) {}
  ~WhileStmt() override;
};

/// rule 17: return ; | return expression ;   (value is null for `return;`)
struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value;

  explicit ReturnStmt(SourceLocation l) : Stmt(NodeKind::ReturnStmt, l) {}
  ~ReturnStmt() override;
};

// ----------------------------------------------------------------- expressions

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  LessThan,
  LessEqual,
  GreaterThan,
  GreaterEqual,
  Equal,
  NotEqual,
};

const char *binaryOpSpelling(BinaryOp op);
bool isComparison(BinaryOp op);

struct Expr : Node {
  /// Filled in by semantic analysis; Int until then.
  Type type = Type::Int;
  /// True when the expression was written inside parentheses. Rule 18 allows
  /// only a bare `var` on the left of `=`, so `(x) = 1` must be rejected.
  bool parenthesized = false;

  Expr(NodeKind k, SourceLocation l) : Node(k, l) {}
};

/// rule 19: ID | ID [ expression ]   (index is null for a scalar reference)
struct VarExpr : Expr {
  std::string name;
  std::unique_ptr<Expr> index;

  VarExpr(SourceLocation l, std::string n)
      : Expr(NodeKind::VarExpr, l), name(std::move(n)) {}
  ~VarExpr() override;
};

/// rule 18: var = expression
struct AssignExpr : Expr {
  std::unique_ptr<VarExpr> target;
  std::unique_ptr<Expr> value;
  SourceLocation opLoc;

  explicit AssignExpr(SourceLocation l) : Expr(NodeKind::AssignExpr, l) {}
  ~AssignExpr() override;
};

/// rules 20, 22, 24 collapsed into one node.
struct BinaryExpr : Expr {
  BinaryOp op = BinaryOp::Add;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  SourceLocation opLoc;

  BinaryExpr(SourceLocation l, BinaryOp o) : Expr(NodeKind::BinaryExpr, l), op(o) {}
  ~BinaryExpr() override;
};

/// rule 27: ID ( args )
struct CallExpr : Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> args;

  CallExpr(SourceLocation l, std::string c)
      : Expr(NodeKind::CallExpr, l), callee(std::move(c)) {}
  ~CallExpr() override;
};

struct NumExpr : Expr {
  long long value = 0;

  NumExpr(SourceLocation l, long long v)
      : Expr(NodeKind::NumExpr, l), value(v) {}
};

// --------------------------------------------------------------------- program

/// rule 1: program -> declaration-list
struct Program {
  std::vector<std::unique_ptr<Decl>> decls;
};

/// Print an indented textual form of the tree, for --dump-ast.
void printAST(const Program &program, std::ostream &os);

} // namespace cminus

#endif // CMINUS_AST_H
