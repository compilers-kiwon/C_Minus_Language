#ifndef CMINUS_SYMBOL_H
#define CMINUS_SYMBOL_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cminus/AST.h"
#include "cminus/SourceLocation.h"

namespace cminus {

enum class SymbolKind { Variable, Parameter, Function };

const char *symbolKindName(SymbolKind kind);

struct Symbol {
  SymbolKind kind = SymbolKind::Variable;
  std::string name;
  /// For a variable or parameter, its type. For a function this mirrors
  /// returnType; use returnType when the distinction matters.
  Type type = Type::Int;
  /// Declaration site. Invalid for the two built-in functions.
  SourceLocation loc;

  /// Arrays only, and only when the size is known: an array parameter is
  /// declared `int a[]` and carries no size of its own (spec 3.3).
  long long arraySize = 0;

  // Functions only.
  Type returnType = Type::Int;
  std::vector<Type> paramTypes;
  bool isBuiltin = false;

  /// Back-link to the declaration; null for the built-ins.
  const Decl *decl = nullptr;

  bool isFunction() const { return kind == SymbolKind::Function; }
  bool isArray() const { return type == Type::IntArray; }
};

/// One closed scope, kept for --dump-symbols.
struct ScopeReport {
  int order = 0; ///< the sequence in which scopes were opened
  int depth = 0;
  std::string label;
  std::vector<const Symbol *> symbols; ///< in declaration order
};

/// A lexically scoped symbol table.
///
/// Symbols are owned by the table and outlive the scope that held them, so
/// the AST may keep pointers into it after analysis has finished.
class SymbolTable {
public:
  void pushScope(std::string label);

  /// Close the innermost scope and return what it held.
  ScopeReport popScope();

  int depth() const { return static_cast<int>(scopes_.size()) - 1; }
  bool atGlobalScope() const { return scopes_.size() == 1; }

  /// Declare `symbol` in the innermost scope. Returns null if a symbol of the
  /// same name already exists there, in which case `*existing` is set to it.
  Symbol *declare(Symbol symbol, const Symbol **existing);

  /// Innermost-first lookup across every open scope.
  const Symbol *lookup(const std::string &name) const;

private:
  struct Scope {
    int order = 0;
    std::string label;
    std::vector<Symbol *> order_of_declaration;
    std::unordered_map<std::string, Symbol *> byName;
  };

  std::vector<std::unique_ptr<Symbol>> storage_;
  std::vector<Scope> scopes_;
  int nextOrder_ = 0;
};

/// Render a symbol's type the way a declaration would read: `int`, `int[10]`,
/// `int[]`, or `int (int, int)` for a function.
std::string describeSymbolType(const Symbol &symbol);

} // namespace cminus

#endif // CMINUS_SYMBOL_H
