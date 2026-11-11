#include "scope.h"

namespace rx::semantic {
Scope::Scope(Scope *parent) : parent_(parent) {}

Scope *Scope::parent() noexcept { return parent_; }

const Scope *Scope::parent() const noexcept { return parent_; }

// Define value symbols
bool Scope::defineValue(Symbol *symbol) {
  if (symbol == nullptr) {
    return false;
  }
  switch (symbol->kind) {
  case SymbolKind::Variable:
  case SymbolKind::Function:
  case SymbolKind::Const:
    break;
  case SymbolKind::Struct:
    return false;
  }
  return values_.emplace(symbol->name, symbol).second;
}

// Define variables
bool Scope::defineVariable(VariableSymbol *symbol, bool allowShadow) {
  if (symbol == nullptr) {
    return false;
  }
  if (!allowShadow) {
    return defineValue(symbol);
  }
  values_.insert_or_assign(symbol->name, symbol);
  return true;
}

// Define type symbols
bool Scope::defineType(StructSymbol *symbol) {
  if (symbol == nullptr) {
    return false;
  }
  return types_.emplace(symbol->name, symbol).second;
}

// Local value lookup
Symbol *Scope::lookupValueLocal(const std::string &name) {
  auto it = values_.find(name);
  if (it == values_.end()) {
    return nullptr;
  }
  return it->second;
}

const Symbol *Scope::lookupValueLocal(const std::string &name) const {
  auto it = values_.find(name);
  if (it == values_.end()) {
    return nullptr;
  }
  return it->second;
}

// Recursive value lookup
Symbol *Scope::lookupValue(const std::string &name) {
  for (Scope *scope = this; scope != nullptr; scope = scope->parent_) {
    Symbol *symbol = scope->lookupValueLocal(name);
    if (symbol != nullptr) {
      return symbol;
    }
  }
  return nullptr;
}

const Symbol *Scope::lookupValue(const std::string &name) const {
  for (const Scope *scope = this; scope != nullptr; scope = scope->parent_) {
    const Symbol *symbol = scope->lookupValueLocal(name);
    if (symbol != nullptr) {
      return symbol;
    }
  }
  return nullptr;
}

// Local type lookup
StructSymbol *Scope::lookupTypeLocal(const std::string &name) {
  auto it = types_.find(name);
  if (it == types_.end()) {
    return nullptr;
  }
  return it->second;
}

const StructSymbol *Scope::lookupTypeLocal(const std::string &name) const {
  auto it = types_.find(name);
  if (it == types_.end()) {
    return nullptr;
  }
  return it->second;
}

// Recursive type lookup
StructSymbol *Scope::lookupType(const std::string &name) {
  for (Scope *scope = this; scope != nullptr; scope = scope->parent_) {
    StructSymbol *symbol = scope->lookupTypeLocal(name);
    if (symbol != nullptr) {
      return symbol;
    }
  }
  return nullptr;
}

const StructSymbol *Scope::lookupType(const std::string &name) const {
  for (const Scope *scope = this; scope != nullptr; scope = scope->parent_) {
    const StructSymbol *symbol = scope->lookupTypeLocal(name);
    if (symbol != nullptr) {
      return symbol;
    }
  }
  return nullptr;
}
} // namespace rx::semantic
