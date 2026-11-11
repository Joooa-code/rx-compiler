#pragma once
#include "symbol.h"
#include <string>
#include <unordered_map>

namespace rx::semantic {
class Scope {
private:
  // 非拥有指针，父作用域必须比子作用域活得久
  Scope *parent_ = nullptr;
  std::unordered_map<std::string, Symbol *> values_;
  std::unordered_map<std::string, StructSymbol *> types_;

public:
  // parent == nullptr：根作用域
  explicit Scope(Scope *parent = nullptr);
  Scope *parent() noexcept;
  const Scope *parent() const noexcept;
  bool defineValue(Symbol *symbol);
  // 定义变量，可选择是否允许遮蔽
  bool defineVariable(VariableSymbol *symbol, bool allowShadow = false);
  bool defineType(StructSymbol *symbol);
  // 只查询当前作用域的值名称
  Symbol *lookupValueLocal(const std::string &name);
  const Symbol *lookupValueLocal(const std::string &name) const;
  // 从当前作用域向外层查找值名称
  Symbol *lookupValue(const std::string &name);
  const Symbol *lookupValue(const std::string &name) const;
  // 只查询当前作用域的类型名称
  StructSymbol *lookupTypeLocal(const std::string &name);
  const StructSymbol *lookupTypeLocal(const std::string &name) const;
  // 从当前作用域向外层查找类型名称
  StructSymbol *lookupType(const std::string &name);
  const StructSymbol *lookupType(const std::string &name) const;
};
} // namespace rx::semantic
