#pragma once

#include "type.h"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unordered_set>

namespace rx::ast {
class ASTNode;
}

namespace rx::semantic {
class StructSymbol;

enum class SymbolKind {
  Variable,
  Function,
  Struct,
  Const,
};

class Symbol {
public:
  const SymbolKind kind;
  std::string name;
  const ast::ASTNode *declaration = nullptr;
  Symbol(SymbolKind kind_, std::string name_,
         const ast::ASTNode *declaration_ = nullptr)
      : kind(kind_), name(std::move(name_)), declaration(declaration_) {}
  virtual ~Symbol() = default;
};

class VariableSymbol final : public Symbol {
public:
  TypePtr type;
  bool isMutable = false;
  VariableSymbol(std::string name_, TypePtr type_, bool isMutable_,
                 const ast::ASTNode *declaration_ = nullptr)
      : Symbol(SymbolKind::Variable, std::move(name_), declaration_),
        type(std::move(type_)), isMutable(isMutable_) {}
};

struct ParameterInfo {
  std::string name;
  TypePtr type;
  bool isMutable = false;
  ParameterInfo() = default;
  ParameterInfo(std::string name_, TypePtr type_, bool isMutable_ = false)
      : name(std::move(name_)), type(std::move(type_)), isMutable(isMutable_) {}
};

// 描述method里的self
enum class ReceiverKind {
  None,
  Value,
  MutableValue,
  SharedReference,
  MutableReference,
};

class FunctionSymbol final : public Symbol {
public:
  std::vector<ParameterInfo> parameters;
  TypePtr returnType;
  ReceiverKind receiverKind = ReceiverKind::None;
  StructSymbol *ownerStruct = nullptr;
  explicit FunctionSymbol(std::string name_, TypePtr returnType_,
                          const ast::ASTNode *declaration_ = nullptr)
      : Symbol(SymbolKind::Function, std::move(name_), declaration_),
        returnType(std::move(returnType_)) {}
  bool isMethod() const {
    return ownerStruct != nullptr && receiverKind != ReceiverKind::None;
  }
  bool isAssociatedFunction() const {
    return ownerStruct != nullptr && receiverKind == ReceiverKind::None;
  }
  bool isFreeFunction() const { return ownerStruct == nullptr; }
};

// const N: usize = 10 or associated const
class ConstSymbol final : public Symbol {
public:
  TypePtr type;
  StructSymbol *ownerStruct = nullptr;
  ConstSymbol(std::string name_, TypePtr type_,
              const ast::ASTNode *declaration_ = nullptr)
      : Symbol(SymbolKind::Const, std::move(name_), declaration_),
        type(std::move(type_)) {}
  bool isAssociatedConst() const { return ownerStruct != nullptr; }
};

struct StructFieldInfo {
  std::string name;
  TypePtr type;
  StructFieldInfo() = default;
  StructFieldInfo(std::string name_, TypePtr type_)
      : name(std::move(name_)), type(std::move(type_)) {}
};

class StructSymbol final : public Symbol {
public:
  std::unordered_set<std::string> derives;
  std::vector<StructFieldInfo> fields;
  std::unordered_map<std::string, std::size_t> fieldIndices;
  // method / associated function / associated const
  std::unordered_map<std::string, Symbol *> associatedValues;
  explicit StructSymbol(std::string name_,
                        const ast::ASTNode *declaration_ = nullptr)
      : Symbol(SymbolKind::Struct, std::move(name_), declaration_) {}
  bool addField(StructFieldInfo field) {
    // 同一个 struct 不能有重复字段。
    if (fieldIndices.find(field.name) != fieldIndices.end()) {
      return false;
    }
    const std::size_t index = fields.size();
    fieldIndices.emplace(field.name, index);
    fields.push_back(std::move(field));
    return true;
  }

  const StructFieldInfo *lookupField(const std::string &name_) const {
    auto it = fieldIndices.find(name_);
    if (it == fieldIndices.end()) {
      return nullptr;
    }
    return &fields[it->second];
  }

  // 取得字段在 struct 中的序号。
  bool lookupFieldIndex(const std::string &name_, std::size_t &index) const {
    auto it = fieldIndices.find(name_);
    if (it == fieldIndices.end()) {
      return false;
    }
    index = it->second;
    return true;
  }

  bool defineAssociatedValue(Symbol *symbol) {
    if (symbol == nullptr) {
      return false;
    }
    return associatedValues.emplace(symbol->name, symbol).second;
  }

  Symbol *lookupAssociatedValue(const std::string &name_) const {
    auto it = associatedValues.find(name_);
    if (it == associatedValues.end()) {
      return nullptr;
    }
    return it->second;
  }
};
} // namespace rx::semantic