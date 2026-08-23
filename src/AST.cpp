#include "cminus/AST.h"

#include <ostream>

namespace cminus {

// Out-of-line destructors: these nodes hold unique_ptr members whose pointee
// types are still incomplete at the point of declaration in AST.h.
FunDecl::~FunDecl() = default;
ExprStmt::~ExprStmt() = default;
IfStmt::~IfStmt() = default;
WhileStmt::~WhileStmt() = default;
ReturnStmt::~ReturnStmt() = default;
VarExpr::~VarExpr() = default;
AssignExpr::~AssignExpr() = default;
BinaryExpr::~BinaryExpr() = default;
CallExpr::~CallExpr() = default;

const char *typeName(Type type) {
  switch (type) {
  case Type::Int:      return "int";
  case Type::Void:     return "void";
  case Type::IntArray: return "int[]";
  }
  return "<invalid>";
}

const char *binaryOpSpelling(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:          return "+";
  case BinaryOp::Sub:          return "-";
  case BinaryOp::Mul:          return "*";
  case BinaryOp::Div:          return "/";
  case BinaryOp::LessThan:     return "<";
  case BinaryOp::LessEqual:    return "<=";
  case BinaryOp::GreaterThan:  return ">";
  case BinaryOp::GreaterEqual: return ">=";
  case BinaryOp::Equal:        return "==";
  case BinaryOp::NotEqual:     return "!=";
  }
  return "<invalid>";
}

bool isComparison(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
    return false;
  default:
    return true;
  }
}

namespace {

class Printer {
public:
  explicit Printer(std::ostream &os) : os_(os) {}

  void program(const Program &prog) {
    os_ << "Program\n";
    ++depth_;
    for (const auto &d : prog.decls)
      decl(*d);
    --depth_;
  }

private:
  void pad() const {
    for (int i = 0; i < depth_; ++i)
      os_ << "  ";
  }

  void at(SourceLocation loc) const {
    os_ << "  <" << loc.line << ':' << loc.column << ">\n";
  }

  void label(const char *text) {
    pad();
    os_ << text << ":\n";
  }

  void decl(const Decl &d) {
    switch (d.kind) {
    case NodeKind::VarDecl: {
      const auto &v = static_cast<const VarDecl &>(d);
      pad();
      os_ << "VarDecl '" << v.name << "' : " << typeName(v.declaredType);
      if (v.isArray)
        os_ << '[' << v.arraySize << ']';
      at(v.loc);
      break;
    }
    case NodeKind::ParamDecl: {
      const auto &p = static_cast<const ParamDecl &>(d);
      pad();
      os_ << "ParamDecl '" << p.name << "' : " << typeName(p.declaredType);
      if (p.isArray)
        os_ << "[]";
      at(p.loc);
      break;
    }
    case NodeKind::FunDecl: {
      const auto &f = static_cast<const FunDecl &>(d);
      pad();
      os_ << "FunDecl '" << f.name << "' -> " << typeName(f.returnType);
      at(f.loc);
      ++depth_;
      if (f.params.empty()) {
        pad();
        os_ << "(no parameters)\n";
      } else {
        for (const auto &p : f.params)
          decl(*p);
      }
      if (f.body) {
        label("body");
        ++depth_;
        stmt(*f.body);
        --depth_;
      }
      --depth_;
      break;
    }
    default:
      pad();
      os_ << "<unexpected declaration>\n";
      break;
    }
  }

  void child(const Stmt *s) {
    ++depth_;
    if (s)
      stmt(*s);
    --depth_;
  }

  void child(const Expr *e) {
    ++depth_;
    if (e)
      expr(*e);
    --depth_;
  }

  void stmt(const Stmt &s) {
    switch (s.kind) {
    case NodeKind::CompoundStmt: {
      const auto &c = static_cast<const CompoundStmt &>(s);
      pad();
      os_ << "CompoundStmt";
      at(c.loc);
      ++depth_;
      for (const auto &d : c.localDecls)
        decl(*d);
      for (const auto &inner : c.stmts)
        stmt(*inner);
      if (c.localDecls.empty() && c.stmts.empty()) {
        pad();
        os_ << "(empty)\n";
      }
      --depth_;
      break;
    }
    case NodeKind::ExprStmt: {
      const auto &e = static_cast<const ExprStmt &>(s);
      pad();
      os_ << "ExprStmt";
      if (!e.expr)
        os_ << " (empty)";
      at(e.loc);
      child(e.expr.get());
      break;
    }
    case NodeKind::IfStmt: {
      const auto &i = static_cast<const IfStmt &>(s);
      pad();
      os_ << "IfStmt";
      at(i.loc);
      ++depth_;
      label("cond");
      child(i.cond.get());
      label("then");
      child(i.thenStmt.get());
      if (i.elseStmt) {
        label("else");
        child(i.elseStmt.get());
      }
      --depth_;
      break;
    }
    case NodeKind::WhileStmt: {
      const auto &w = static_cast<const WhileStmt &>(s);
      pad();
      os_ << "WhileStmt";
      at(w.loc);
      ++depth_;
      label("cond");
      child(w.cond.get());
      label("body");
      child(w.body.get());
      --depth_;
      break;
    }
    case NodeKind::ReturnStmt: {
      const auto &r = static_cast<const ReturnStmt &>(s);
      pad();
      os_ << "ReturnStmt";
      if (!r.value)
        os_ << " (no value)";
      at(r.loc);
      child(r.value.get());
      break;
    }
    default:
      pad();
      os_ << "<unexpected statement>\n";
      break;
    }
  }

  void expr(const Expr &e) {
    switch (e.kind) {
    case NodeKind::AssignExpr: {
      const auto &a = static_cast<const AssignExpr &>(e);
      pad();
      os_ << "AssignExpr";
      at(a.opLoc);
      child(a.target.get());
      child(a.value.get());
      break;
    }
    case NodeKind::BinaryExpr: {
      const auto &b = static_cast<const BinaryExpr &>(e);
      pad();
      os_ << "BinaryExpr '" << binaryOpSpelling(b.op) << "'";
      at(b.opLoc);
      child(b.lhs.get());
      child(b.rhs.get());
      break;
    }
    case NodeKind::VarExpr: {
      const auto &v = static_cast<const VarExpr &>(e);
      pad();
      os_ << "VarExpr '" << v.name << "'";
      at(v.loc);
      if (v.index) {
        ++depth_;
        label("index");
        child(v.index.get());
        --depth_;
      }
      break;
    }
    case NodeKind::CallExpr: {
      const auto &c = static_cast<const CallExpr &>(e);
      pad();
      os_ << "CallExpr '" << c.callee << "'";
      at(c.loc);
      ++depth_;
      if (c.args.empty()) {
        pad();
        os_ << "(no arguments)\n";
      }
      for (const auto &a : c.args)
        expr(*a);
      --depth_;
      break;
    }
    case NodeKind::NumExpr: {
      const auto &n = static_cast<const NumExpr &>(e);
      pad();
      os_ << "NumExpr " << n.value;
      at(n.loc);
      break;
    }
    default:
      pad();
      os_ << "<unexpected expression>\n";
      break;
    }
  }

  std::ostream &os_;
  int depth_ = 0;
};

} // namespace

void printAST(const Program &program, std::ostream &os) {
  Printer(os).program(program);
}

} // namespace cminus
