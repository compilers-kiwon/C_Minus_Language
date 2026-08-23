#include "cminus/Symbol.h"

#include <cstddef>
#include <utility>

namespace cminus {

const char *symbolKindName(SymbolKind kind) {
  switch (kind) {
  case SymbolKind::Variable:  return "variable";
  case SymbolKind::Parameter: return "parameter";
  case SymbolKind::Function:  return "function";
  }
  return "<invalid>";
}

void SymbolTable::pushScope(std::string label) {
  Scope scope;
  scope.order = nextOrder_++;
  scope.label = std::move(label);
  scopes_.push_back(std::move(scope));
}

ScopeReport SymbolTable::popScope() {
  ScopeReport report;
  if (scopes_.empty())
    return report;

  Scope &scope = scopes_.back();
  report.order = scope.order;
  report.depth = depth();
  report.label = std::move(scope.label);
  report.symbols.reserve(scope.order_of_declaration.size());
  for (Symbol *symbol : scope.order_of_declaration)
    report.symbols.push_back(symbol);

  scopes_.pop_back();
  return report;
}

Symbol *SymbolTable::declare(Symbol symbol, const Symbol **existing) {
  if (scopes_.empty())
    return nullptr;

  Scope &scope = scopes_.back();
  auto found = scope.byName.find(symbol.name);
  if (found != scope.byName.end()) {
    if (existing)
      *existing = found->second;
    return nullptr;
  }

  storage_.push_back(std::make_unique<Symbol>(std::move(symbol)));
  Symbol *stored = storage_.back().get();
  scope.order_of_declaration.push_back(stored);
  scope.byName.emplace(stored->name, stored);
  return stored;
}

const Symbol *SymbolTable::lookup(const std::string &name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    auto found = scope->byName.find(name);
    if (found != scope->byName.end())
      return found->second;
  }
  return nullptr;
}

std::string describeSymbolType(const Symbol &symbol) {
  if (!symbol.isFunction()) {
    if (symbol.type != Type::IntArray)
      return typeName(symbol.type);
    if (symbol.kind == SymbolKind::Parameter)
      return "int[]";
    return "int[" + std::to_string(symbol.arraySize) + "]";
  }

  std::string text = typeName(symbol.returnType);
  text += " (";
  if (symbol.paramTypes.empty()) {
    text += "void";
  } else {
    for (std::size_t i = 0; i < symbol.paramTypes.size(); ++i) {
      if (i > 0)
        text += ", ";
      text += symbol.paramTypes[i] == Type::IntArray ? "int[]" : "int";
    }
  }
  text += ')';
  return text;
}

} // namespace cminus
