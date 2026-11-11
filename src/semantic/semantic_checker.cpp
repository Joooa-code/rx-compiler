#include "semantic_checker.h"
#include "../ast.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace rx::semantic {
namespace {

using ast::ASTNode;
using ast::ASTNodeType;
using ast::ExprNode;
using ast::TypeNode;

TypePtr recoveryType() {
  return Type::makeNever();
}

const ExprNode *asExpr(const ASTNode *node) {
  return dynamic_cast<const ExprNode *>(node);
}

const TypeNode *asType(const ASTNode *node) {
  return dynamic_cast<const TypeNode *>(node);
}

bool endsWith(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string integerSuffix(const std::string &raw) {
  static const char *suffixes[] = {"isize", "usize", "i32", "u32"};
  for (const char *suffix : suffixes) {
    if (endsWith(raw, suffix)) {
      return suffix;
    }
  }
  return {};
}

TypePtr integerTypeFromSuffix(const std::string &suffix) {
  if (suffix == "i32") {
    return Type::makeI32();
  }
  if (suffix == "u32") {
    return Type::makeU32();
  }
  if (suffix == "isize") {
    return Type::makeISize();
  }
  if (suffix == "usize") {
    return Type::makeUSize();
  }
  return nullptr;
}

std::optional<std::uint64_t>
parseUnsignedIntegerLiteral(const std::string &raw) {
  std::string text;
  text.reserve(raw.size());

  for (char c : raw) {
    if (c != '_') {
      text.push_back(c);
    }
  }

  const std::string suffix = integerSuffix(text);
  if (!suffix.empty()) {
    text.resize(text.size() - suffix.size());
  }

  int base = 10;
  std::size_t start = 0;

  if (text.size() >= 2 && text[0] == '0') {
    switch (text[1]) {
    case 'x':
    case 'X':
      base = 16;
      start = 2;
      break;
    case 'o':
    case 'O':
      base = 8;
      start = 2;
      break;
    case 'b':
    case 'B':
      base = 2;
      start = 2;
      break;
    default:
      break;
    }
  }

  if (start >= text.size()) {
    return std::nullopt;
  }

  std::uint64_t value = 0;
  for (std::size_t i = start; i < text.size(); ++i) {
    const char c = text[i];

    unsigned digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A' + 10);
    } else {
      return std::nullopt;
    }

    if (digit >= static_cast<unsigned>(base)) {
      return std::nullopt;
    }

    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) /
                    static_cast<unsigned>(base)) {
      return std::nullopt;
    }

    value = value * static_cast<unsigned>(base) + digit;
  }

  return value;
}

std::string pathText(const ast::PathExprNode &path) {
  std::ostringstream out;

  for (std::size_t i = 0; i < path.segments.size(); ++i) {
    if (i != 0) {
      out << "::";
    }
    out << path.segments[i].name;
  }

  return out.str();
}

std::string typePathText(const ast::PathTypeNode &path) {
  std::ostringstream out;

  for (std::size_t i = 0; i < path.segments.size(); ++i) {
    if (i != 0) {
      out << "::";
    }
    out << path.segments[i].name;
  }

  return out.str();
}

const TypeNode *genericTypeArgument(const ast::PathSegment &segment,
                                    std::size_t index) {
  std::size_t current = 0;

  for (const auto &arg : segment.genericArgs) {
    if (arg.kind != ast::GenericArgKind::Type) {
      continue;
    }

    if (current == index) {
      return asType(arg.type.get());
    }

    ++current;
  }

  return nullptr;
}

std::size_t genericTypeArgumentCount(const ast::PathSegment &segment) {
  std::size_t result = 0;
  for (const auto &arg : segment.genericArgs) {
    if (arg.kind == ast::GenericArgKind::Type) {
      ++result;
    }
  }
  return result;
}

Symbol *lookupPathSymbol(const ast::PathExprNode &path, Scope *scope,
                         StructSymbol *currentStruct) {
  if (scope == nullptr || path.segments.empty()) {
    return nullptr;
  }

  if (path.segments.size() == 1) {
    return scope->lookupValue(path.segments.front().name);
  }

  if (path.segments.size() == 2) {
    const std::string &ownerName = path.segments[0].name;
    const std::string &memberName = path.segments[1].name;

    StructSymbol *owner = nullptr;

    if (ownerName == "Self") {
      owner = currentStruct;
    } else {
      owner = scope->lookupType(ownerName);
    }

    if (owner == nullptr) {
      return nullptr;
    }

    return owner->lookupAssociatedValue(memberName);
  }

  return nullptr;
}

StructSymbol *lookupStructPath(const ast::PathExprNode &path, Scope *scope,
                               StructSymbol *currentStruct) {
  if (scope == nullptr || path.segments.empty()) {
    return nullptr;
  }

  if (path.segments.size() != 1) {
    return nullptr;
  }

  const std::string &name = path.segments.front().name;

  if (name == "Self") {
    return currentStruct;
  }

  return scope->lookupType(name);
}

TypePtr peelOneDeref(TypePtr type) {
  if (!type) {
    return nullptr;
  }

  if (type->isReference() || type->isBox()) {
    return type->elementType();
  }

  return nullptr;
}

TypePtr peelAllDeref(TypePtr type) {
  while (type && (type->isReference() || type->isBox())) {
    type = type->elementType();
  }
  return type;
}

bool isComparisonOperator(const std::string &op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
         op == ">=";
}

bool isArithmeticOperator(const std::string &op) {
  return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

bool isBitwiseOperator(const std::string &op) {
  return op == "&" || op == "|" || op == "^";
}

bool isShiftOperator(const std::string &op) { return op == "<<" || op == ">>"; }

bool isLogicalOperator(const std::string &op) {
  return op == "&&" || op == "||";
}

std::string compoundBaseOperator(const std::string &op) {
  if (op == "+=")
    return "+";
  if (op == "-=")
    return "-";
  if (op == "*=")
    return "*";
  if (op == "/=")
    return "/";
  if (op == "%=")
    return "%";
  if (op == "&=")
    return "&";
  if (op == "|=")
    return "|";
  if (op == "^=")
    return "^";
  if (op == "<<=")
    return "<<";
  if (op == ">>=")
    return ">>";
  return {};
}

// Only the one shared-reference layer listed in the operator table is accepted
TypePtr scalarOperand(TypePtr type) {
  if (type && type->isReference() && !type->isMutableReference())
    return type->elementType();
  return type;
}

bool orderedPair(TypePtr lhs, TypePtr rhs) {
  if (!lhs || !rhs)
    return false;
  if (!sameType(lhs, rhs)) {
    if (!(lhs->isReference() && rhs->isReference() &&
          !lhs->isMutableReference() && rhs->isMutableReference() &&
          sameType(lhs->elementType(), rhs->elementType())))
      return false;
  }
  while (lhs->isReference())
    lhs = lhs->elementType();
  return lhs->isInteger() || lhs->isBool();
}

bool canExplicitlyCast(const TypePtr &source, const TypePtr &target) {
  if (!source || !target) {
    return false;
  }

  if (source->isInteger() && target->isInteger()) {
    return true;
  }

  // The spec permits bool -> integer, but never integer -> bool.
  if (source->isBool() && target->isInteger()) {
    return true;
  }

  return false;
}

} // namespace

bool SemanticChecker::check(const ast::CrateNode &crate) {
  errors_.clear();
  context_.reset();
  expressionTypes_.clear();
  expressionDiverges_.clear();
  constantStates_.clear();
  currentScope_ = &context_.globalScope();
  currentFunction_ = nullptr;
  currentStruct_ = nullptr;
  loopStack_.clear();
  loopTargetBarriers_.clear();

  for (const char *name : {"get_i32", "print_i32", "println_i32"}) {
    auto *builtin = context_.makeSymbol<FunctionSymbol>(
        name,
        std::string(name) == "get_i32" ? Type::makeI32() : Type::makeUnit());
    if (std::string(name) != "get_i32")
      builtin->parameters.emplace_back("value", Type::makeI32());
    currentScope_->defineValue(builtin);
  }
  collectGlobalDeclarations(crate);
  collectImplDeclarations(crate);
  checkItems(crate);
  Symbol *entry = context_.globalScope().lookupValueLocal("main");
  if (!entry || entry->kind != SymbolKind::Function) {
    error("program requires a top-level main function");
  } else {
    auto *main = static_cast<FunctionSymbol *>(entry);
    const auto *decl = dynamic_cast<const ast::FnItemNode *>(main->declaration);
    if (!main->parameters.empty() || !main->returnType->isUnit() ||
        (decl && !decl->genericParams.lifetimeParams.empty()))
      error("main must have no parameters or generics and return ()");
  }

  currentScope_ = nullptr;
  currentFunction_ = nullptr;
  currentStruct_ = nullptr;

  return !hasError();
}

void SemanticChecker::collectGlobalDeclarations(const ast::CrateNode &crate) {
  currentScope_ = &context_.globalScope();
  // collect every struct name first
  for (const auto &item : crate.items) {
    if (item == nullptr || item->kind != ASTNodeType::StructItem) {
      continue;
    }
    declareStruct(static_cast<const ast::StructItemNode &>(*item));
  }

  for (const auto &item : crate.items) {
    if (!item) {
      continue;
    }
    switch (item->kind) {
    case ASTNodeType::ConstItem: {
      const auto &c = static_cast<const ast::ConstItemNode &>(*item);
      auto *symbol = context_.makeSymbol<ConstSymbol>(c.name, nullptr, &c);
      if (!currentScope_->defineValue(symbol))
        error("duplicate value `" + c.name + "`");
      break;
    }
    // register associated constants
    case ASTNodeType::ImplItem: {
      const auto &impl = static_cast<const ast::ImplItemNode &>(*item);
      auto *target = asType(impl.type.get());
      TypePtr type = target ? resolveType(*target) : nullptr;
      if (!type || !type->isStruct())
        continue;
      auto *owner = const_cast<StructSymbol *>(type->structSymbol());
      for (const auto &a : impl.associatedItems) {
        if (!a || a->kind != ASTNodeType::ConstItem)
          continue;
        const auto &c = static_cast<const ast::ConstItemNode &>(*a);
        auto *v = context_.makeSymbol<ConstSymbol>(c.name, nullptr, &c);
        v->ownerStruct = owner;
        if (!owner->defineAssociatedValue(v))
          error("duplicate associated value `" + c.name + "`");
      }
      break;
    }
    default:
      break;
    }
  }

  for (const auto &item : crate.items) {
    if (item && item->kind == ASTNodeType::ConstItem) {
      const auto &c = static_cast<const ast::ConstItemNode &>(*item);
      auto *v = currentScope_->lookupValueLocal(c.name);
      if (v && v->kind == SymbolKind::Const && v->declaration == &c) {
        const auto *type = asType(c.type.get());
        if (!type)
          error("constant has no valid declared type");
        static_cast<ConstSymbol *>(v)->type =
            type ? resolveType(*type) : recoveryType();
      }
    }
  }

  for (const auto &item : crate.items) {
    if (item == nullptr) {
      continue;
    }
    switch (item->kind) {
    case ASTNodeType::FnItem:
      declareFunction(static_cast<const ast::FnItemNode &>(*item));
      break;

    case ASTNodeType::ConstItem:
      // Const names and types were collected above.
      break;

    default:
      break;
    }
  }
}

// collect const and function in impl
void SemanticChecker::collectImplDeclarations(const ast::CrateNode &crate) {
  currentScope_ = &context_.globalScope();
  for (const auto &item : crate.items) {
    if (item == nullptr || item->kind != ASTNodeType::ImplItem) {
      continue;
    }
    const auto &implementation = static_cast<const ast::ImplItemNode &>(*item);

    const TypeNode *targetNode = asType(implementation.type.get());
    if (targetNode == nullptr) {
      error("impl target is not a type");
      continue;
    }
    TypePtr targetType = resolveType(*targetNode);
    if (!targetType || !targetType->isStruct() ||
        targetType->structSymbol() == nullptr) {
      error("impl target must be a struct type");
      continue;
    }
    auto *owner = const_cast<StructSymbol *>(targetType->structSymbol());
    StructSymbol *oldStruct = currentStruct_;
    currentStruct_ = owner;

    for (const auto &associated : implementation.associatedItems) {
      if (associated == nullptr) {
        continue;
      }
      switch (associated->kind) {
      case ASTNodeType::FnItem:
        declareFunction(static_cast<const ast::FnItemNode &>(*associated),
                        owner);
        break;
      case ASTNodeType::ConstItem: {
        const auto &c = static_cast<const ast::ConstItemNode &>(*associated);
        auto *v = owner->lookupAssociatedValue(c.name);
        if (v && v->kind == SymbolKind::Const && v->declaration == &c) {
          const auto *type = asType(c.type.get());
          if (!type)
            error("associated constant has no valid declared type");
          static_cast<ConstSymbol *>(v)->type =
              type ? resolveType(*type) : recoveryType();
        }
      } break;

      default:
        error("unsupported associated item");
        break;
      }
    }

    currentStruct_ = oldStruct;
  }
}

// Item checking
void SemanticChecker::checkItems(const ast::CrateNode &crate) {
  // check structs first,because they could be used in other items
  for (const auto &item : crate.items) {
    if (item != nullptr && item->kind == ASTNodeType::StructItem) {
      checkStruct(static_cast<const ast::StructItemNode &>(*item));
    }
  }
  validateStructs(crate);

  for (const auto &item : crate.items) {
    if (item == nullptr || item->kind == ASTNodeType::StructItem) {
      continue;
    }
    const auto *typedItem = dynamic_cast<const ast::ItemNode *>(item.get());
    if (typedItem != nullptr) {
      checkItem(*typedItem);
    }
  }
}

void SemanticChecker::checkItem(const ast::ItemNode &item) {
  switch (item.kind) {
  case ASTNodeType::UseItem:
    // Rx explicitly discards use declarations after parsing.
    return;
  case ASTNodeType::FnItem:
    checkFunction(static_cast<const ast::FnItemNode &>(item));
    return;
  case ASTNodeType::StructItem:
    checkStruct(static_cast<const ast::StructItemNode &>(item));
    return;
  case ASTNodeType::ConstItem:
    checkConst(static_cast<const ast::ConstItemNode &>(item));
    return;
  case ASTNodeType::ImplItem:
    checkImpl(static_cast<const ast::ImplItemNode &>(item));
    return;
  default:
    error("invalid AST node in item position");
    return;
  }
}

// Processing fields information of a struct
void SemanticChecker::checkStruct(const ast::StructItemNode &structure) {
  StructSymbol *symbol = context_.globalScope().lookupTypeLocal(structure.name);

  if (symbol == nullptr) {
    symbol = declareStruct(structure);
  }
  if (symbol == nullptr || symbol->declaration != &structure) {
    return;
  }

  StructSymbol *oldStruct = currentStruct_;
  currentStruct_ = symbol;
  for (const auto &field : structure.fields) {
    const TypeNode *typeNode = asType(field.type.get());
    if (typeNode == nullptr) {
      error("field `" + field.name + "` of struct `" + structure.name +
            "` has no valid type");
      continue;
    }
    TypePtr fieldType = resolveType(*typeNode);
    if (!symbol->addField(StructFieldInfo(field.name, std::move(fieldType)))) {
      error("duplicate field `" + field.name + "` in struct `" +
            structure.name + "`");
    }
  }

  currentStruct_ = oldStruct;
}

void SemanticChecker::checkFunction(const ast::FnItemNode &function) {
  FunctionSymbol *symbol = nullptr;
  if (currentStruct_ != nullptr) {
    Symbol *candidate = currentStruct_->lookupAssociatedValue(function.name);
    if (candidate != nullptr && candidate->kind == SymbolKind::Function) {
      symbol = static_cast<FunctionSymbol *>(candidate);
    }
  } else {
    Symbol *candidate = context_.globalScope().lookupValueLocal(function.name);
    if (candidate != nullptr && candidate->kind == SymbolKind::Function) {
      symbol = static_cast<FunctionSymbol *>(candidate);
    }
  }
  if (symbol == nullptr) {
    error("internal semantic error: function `" + function.name +
          "` has no FunctionSymbol");
    return;
  }
  if (symbol->declaration != &function) {
    // Duplicate function already diagnosed
    return;
  }

  FunctionSymbol *oldFunction = currentFunction_;
  Scope *oldScope = currentScope_;

  currentFunction_ = symbol;
  currentScope_ = &context_.globalScope();

  enterScope();

  // Install `self` if this is a method
  if (symbol->ownerStruct != nullptr &&
      symbol->receiverKind != ReceiverKind::None) {
    TypePtr selfType = Type::makeStruct(symbol->ownerStruct);
    bool bindingMutable = false;

    switch (symbol->receiverKind) {
    case ReceiverKind::Value:
      break;
    case ReceiverKind::MutableValue:
      bindingMutable = true;
      break;
    case ReceiverKind::SharedReference:
      selfType = Type::makeReference(std::move(selfType), false);
      break;
    case ReceiverKind::MutableReference:
      selfType = Type::makeReference(std::move(selfType), true);
      break;
    case ReceiverKind::None:
      break;
    }

    auto *selfSymbol = context_.makeSymbol<VariableSymbol>(
        "self", std::move(selfType), bindingMutable, &function);
    if (!currentScope_->defineVariable(selfSymbol)) {
      error("duplicate `self` binding in method `" + function.name + "`");
    }
  }

  std::size_t normalParameterIndex = 0;
  for (const auto &parameter : function.parameters) {
    if (parameter.kind == ast::FunctionParamKind::Self) {
      continue;
    }
    if (normalParameterIndex >= symbol->parameters.size()) {
      error("internal semantic error: parameter metadata mismatch in `" +
            function.name + "`");
      break;
    }
    const ParameterInfo &info = symbol->parameters[normalParameterIndex++];
    auto *parameterSymbol = context_.makeSymbol<VariableSymbol>(
        info.name, info.type, info.isMutable, &function);
    if (!currentScope_->defineVariable(parameterSymbol)) {
      error("duplicate parameter `" + info.name + "` in function `" +
            function.name + "`");
    }
  }

  const ExprNode *body = asExpr(function.body.get());

  if (body == nullptr) {
    error("function `" + function.name + "` has no valid body");
  } else {
    TypePtr bodyType = checkExpr(*body, symbol->returnType);

    if (bodyType && symbol->returnType &&
        !canCoerce(bodyType, symbol->returnType)) {
      error("function `" + function.name + "` returns `" +
            typeToString(bodyType) + "`, expected `" +
            typeToString(symbol->returnType) + "`");
    }
  }

  leaveScope();

  currentScope_ = oldScope;
  currentFunction_ = oldFunction;
}

void SemanticChecker::checkConst(const ast::ConstItemNode &constant) {
  ConstSymbol *symbol = nullptr;

  if (currentStruct_ != nullptr) {
    Symbol *candidate = currentStruct_->lookupAssociatedValue(constant.name);
    if (candidate != nullptr && candidate->kind == SymbolKind::Const) {
      symbol = static_cast<ConstSymbol *>(candidate);
    }
  } else {
    Symbol *candidate = context_.globalScope().lookupValueLocal(constant.name);
    if (candidate != nullptr && candidate->kind == SymbolKind::Const) {
      symbol = static_cast<ConstSymbol *>(candidate);
    }
  }
  if (symbol == nullptr) {
    error("internal semantic error: const `" + constant.name +
          "` has no ConstSymbol");
    return;
  }

  if (symbol->declaration != &constant) {
    return;
  }

  const ExprNode *value = asExpr(constant.value.get());
  if (value == nullptr) {
    error("const `" + constant.name + "` has no valid initializer");
    return;
  }
  validateConstant(*symbol);
  TypePtr actual = checkExpr(*value, symbol->type);
  if (actual && symbol->type && !canCoerce(actual, symbol->type)) {
    error("const `" + constant.name + "` has type `" + typeToString(actual) +
          "`, expected `" + typeToString(symbol->type) + "`");
  }
}

void SemanticChecker::checkImpl(const ast::ImplItemNode &implementation) {
  const TypeNode *targetNode = asType(implementation.type.get());

  if (targetNode == nullptr) {
    error("impl target is not a valid type");
    return;
  }

  TypePtr targetType = resolveType(*targetNode);

  if (!targetType || !targetType->isStruct() ||
      targetType->structSymbol() == nullptr) {
    error("impl target must be a struct type");
    return;
  }

  StructSymbol *oldStruct = currentStruct_;
  currentStruct_ = const_cast<StructSymbol *>(targetType->structSymbol());

  for (const auto &associated : implementation.associatedItems) {
    if (associated == nullptr) {
      continue;
    }
    const auto *item = dynamic_cast<const ast::ItemNode *>(associated.get());
    if (item == nullptr) {
      error("invalid associated item");
      continue;
    }
    checkItem(*item);
  }

  currentStruct_ = oldStruct;
}

// Statement checking
void SemanticChecker::checkStmt(const ast::StmtNode &stmt) {
  switch (stmt.kind) {
  case ASTNodeType::LetStmt:
    checkLetStmt(static_cast<const ast::LetStmtNode &>(stmt));
    return;

  case ASTNodeType::ExprStmt:
    checkExprStmt(static_cast<const ast::ExprStmtNode &>(stmt));
    return;

  default:
    error("invalid AST node in statement position");
    return;
  }
}

void SemanticChecker::checkLetStmt(const ast::LetStmtNode &stmt) {
  if (currentScope_ == nullptr) {
    error("internal semantic error: let statement outside a scope");
    return;
  }
  // 解析显式声明类型
  TypePtr declaredType;
  if (stmt.type != nullptr) {
    const TypeNode *typeNode = asType(stmt.type.get());
    if (typeNode == nullptr) {
      error("invalid declared type of variable `" + stmt.name + "`");
      declaredType = recoveryType();
    } else {
      declaredType = resolveType(*typeNode);
    }
  }

  const ExprNode *initializer = asExpr(stmt.expr.get());
  if (initializer == nullptr) {
    error("variable `" + stmt.name + "` has no valid initializer");
    return;
  }

  TypePtr actualType = checkExpr(*initializer, declaredType);
  TypePtr variableType = declaredType;

  if (!variableType) {
    variableType = actualType;
  } else if (actualType && !canCoerce(actualType, variableType)) {
    error("initializer of `" + stmt.name + "` has type `" +
          typeToString(actualType) + "`, expected `" +
          typeToString(variableType) + "`");
  }

  if (!variableType) {
    variableType = recoveryType();
  }
  // establish variable symbol
  auto *symbol = context_.makeSymbol<VariableSymbol>(
      stmt.name, std::move(variableType), stmt.isMutable, &stmt);

  // 加入当前作用域
  currentScope_->defineVariable(symbol, true);
}

void SemanticChecker::checkExprStmt(const ast::ExprStmtNode &stmt) {
  const ExprNode *expr = asExpr(stmt.expr.get());
  if (expr == nullptr) {
    error("invalid expression statement");
    return;
  }

  TypePtr type = checkExpr(*expr);
  if (!stmt.hasSemicolon && !canCoerce(type, Type::makeUnit()))
    error("expression statement without semicolon must have type ()");
}

// 对一个表达式进行语义分析，并返回它的类型
TypePtr SemanticChecker::checkExpr(const ast::ExprNode &expr,
                                   TypePtr expected) {
  TypePtr result = checkExprImpl(expr, expected);
  expressionTypes_[&expr] = result;
  if (expected && expected->isReference() && expected->isMutableReference() &&
      result && result->isReference() && result->isMutableReference() &&
      placeInfo(expr).shared)
    error("cannot reborrow a mutable reference through shared access");
  expressionDiverges_[&expr] = computeDivergence(expr, result);
  return result;
}

// from ast to type
TypePtr SemanticChecker::checkExprImpl(const ast::ExprNode &expr,
                                       TypePtr expected) {
  switch (expr.kind) {
  case ASTNodeType::UnitLitExpr:
    return Type::makeUnit();

  case ASTNodeType::IntLitExpr: {
    const auto &literal = static_cast<const ast::IntLitExprNode &>(expr);
    // 提取后缀
    const std::string suffix = integerSuffix(literal.rawValue);

    if (!suffix.empty()) {
      TypePtr type = integerTypeFromSuffix(suffix);
      if (!type) {
        error("unknown integer suffix in `" + literal.rawValue + "`");
        return recoveryType();
      }
      return type;
    }

    if (expected && expected->isInteger()) {
      return expected;
    }

    return Type::makeI32();
  }

  case ASTNodeType::BoolLitExpr:
    return Type::makeBool();

  case ASTNodeType::PathExpr: {
    const auto &path = static_cast<const ast::PathExprNode &>(expr);
    // 检查路径是否带有不允许的泛型参数
    for (const auto &seg : path.segments)
      if (genericTypeArgumentCount(seg))
        error("value path does not accept type arguments");
    Symbol *symbol = lookupPathSymbol(path, currentScope_, currentStruct_);

    if (symbol == nullptr) {
      error("undefined value `" + pathText(path) + "`");
      return recoveryType();
    }
    if (symbol->kind == SymbolKind::Variable) {
      return static_cast<VariableSymbol *>(symbol)->type;
    }
    if (symbol->kind == SymbolKind::Const) {
      auto *c = static_cast<ConstSymbol *>(symbol);
      if (!c->type)
        validateConstant(*c);
      return c->type ? c->type : recoveryType();
    }
    if (symbol->kind == SymbolKind::Function) {
      error("function `" + pathText(path) + "` cannot be used as a value");
      return recoveryType();
    }
    error("`" + pathText(path) + "` is not a value");
    return recoveryType();
  }

  case ASTNodeType::StructExpr: {
    const auto &structure = static_cast<const ast::StructExprNode &>(expr);
    const auto *name =
        dynamic_cast<const ast::PathExprNode *>(structure.structName.get());

    if (name == nullptr) {
      error("struct expression has an invalid struct path");
      return recoveryType();
    }

    for (const auto &seg : name->segments)
      if (genericTypeArgumentCount(seg))
        error("struct path does not accept type arguments");
    StructSymbol *structSymbol =
        lookupStructPath(*name, currentScope_, currentStruct_);
    if (structSymbol == nullptr) {
      error("unknown struct `" + pathText(*name) + "`");
      return recoveryType();
    }

    std::unordered_set<std::string> seenFields;
    for (const auto &field : structure.fields) {
      // 字段是否重复
      if (!seenFields.insert(field.name).second) {
        error("field `" + field.name +
              "` specified more than once in struct `" + structSymbol->name +
              "`");
        continue;
      }
      // 是否存在
      const StructFieldInfo *fieldInfo = structSymbol->lookupField(field.name);
      if (fieldInfo == nullptr) {
        error("struct `" + structSymbol->name + "` has no field `" +
              field.name + "`");
        continue;
      }

      const ExprNode *value = asExpr(field.expr.get());
      if (value == nullptr) {
        error("field `" + field.name + "` has an invalid initializer");
        continue;
      }
      // 字段初始化表达式类型是否匹配
      TypePtr actual = checkExpr(*value, fieldInfo->type);
      if (actual && !canCoerce(actual, fieldInfo->type)) {
        error("field `" + field.name + "` of struct `" + structSymbol->name +
              "` has type `" + typeToString(actual) + "`, expected `" +
              typeToString(fieldInfo->type) + "`");
      }
    }

    for (const auto &fieldInfo : structSymbol->fields) {
      if (seenFields.find(fieldInfo.name) == seenFields.end()) {
        error("missing field `" + fieldInfo.name + "` in struct `" +
              structSymbol->name + "`");
      }
    }

    return Type::makeStruct(structSymbol);
  }

  case ASTNodeType::ArrayExpr: {
    const auto &array = static_cast<const ast::ArrayExprNode &>(expr);

    TypePtr expectedElement;

    if (expected && expected->isArray()) {
      expectedElement = expected->elementType();

      if (expected->arrayLength() != array.elements.size()) {
        error("array has length " + std::to_string(array.elements.size()) +
              ", expected " + std::to_string(expected->arrayLength()));
      }
    }

    if (array.elements.empty()) {
      if (expected && expected->isArray()) {
        return expected;
      }

      error("cannot infer element type of an empty array");
      return Type::makeArray(recoveryType(), 0);
    }

    TypePtr elementType = expectedElement;

    for (std::size_t i = 0; i < array.elements.size(); ++i) {
      const ExprNode *element = asExpr(array.elements[i].get());

      if (element == nullptr) {
        error("invalid array element");
        continue;
      }

      TypePtr actual = checkExpr(*element, expectedElement);

      if (!elementType) {
        elementType = actual;
        continue;
      }

      if (!expectedElement) {
        TypePtr merged = mergeLoopBreakTypes(elementType, actual);
        if (merged) {
          elementType = merged;
          continue;
        }
      }
      if (actual && !canCoerce(actual, elementType)) {
        error("array element " + std::to_string(i) + " has type `" +
              typeToString(actual) + "`, expected `" +
              typeToString(elementType) + "`");
      }
    }

    if (!elementType) {
      elementType = recoveryType();
    }

    return Type::makeArray(std::move(elementType),
                           static_cast<std::uint32_t>(array.elements.size()));
  }

  case ASTNodeType::ArrayRepeat: {
    const auto &array = static_cast<const ast::ArrayRepeatNode &>(expr);

    if (array.element == nullptr || array.count == nullptr) {
      error("invalid repeat-array expression");
      return recoveryType();
    }

    auto length = constantLength(*array.count);

    if (!length) {
      error("array repeat count is not a usize constant");
      return recoveryType();
    }

    TypePtr expectedElement;

    if (expected && expected->isArray()) {
      expectedElement = expected->elementType();

      if (expected->arrayLength() != *length) {
        error("array has length " + std::to_string(*length) + ", expected " +
              std::to_string(expected->arrayLength()));
      }
    }

    const ExprNode *element = asExpr(array.element.get());

    if (element == nullptr) {
      error("invalid repeat-array element");
      return recoveryType();
    }

    TypePtr elementType = checkExpr(*element, expectedElement);

    if (!elementType) {
      elementType = recoveryType();
    }

    if (expectedElement && !canCoerce(elementType, expectedElement))
      error("array repeat element does not match expected type");
    if (expectedElement)
      elementType = expectedElement;
    if (*length > 1 && !hasTrait(elementType, "Copy"))
      error("array repetition with length > 1 requires Copy");
    return Type::makeArray(std::move(elementType), *length);
  }

  case ASTNodeType::UnaryExpr: {
    const auto &unary = static_cast<const ast::UnaryExprNode &>(expr);

    const ExprNode *operand = asExpr(unary.expr.get());

    if (operand == nullptr) {
      error("unary operator `" + unary.op + "` has no operand");
      return recoveryType();
    }

    if (unary.op == "&" || unary.op == "&mut" || unary.op == "&&" ||
        unary.op == "&&mut") {
      const bool mutableInner = unary.op == "&mut" || unary.op == "&&mut";

      TypePtr operandType = checkExpr(*operand);
      if (mutableInner && !mutablePlace(*operand, true)) {
        error("cannot take mutable reference to an immutable value");
      }

      if (unary.op == "&&" || unary.op == "&&mut") {
        TypePtr inner =
            Type::makeReference(std::move(operandType), mutableInner);

        return Type::makeReference(std::move(inner), false);
      }

      return Type::makeReference(std::move(operandType), mutableInner);
    }

    TypePtr operandType = checkExpr(*operand);
    if (unary.op == "-" || unary.op == "!")
      operandType = scalarOperand(operandType);

    if (!operandType) {
      return recoveryType();
    }

    if (unary.op == "-") {
      if (!operandType->isSignedInteger()) {
        error("unary `-` requires a signed integer, found `" +
              typeToString(operandType) + "`");
        return recoveryType();
      }

      return operandType;
    }

    if (unary.op == "!") {
      if (operandType->isBool() || operandType->isInteger()) {
        return operandType;
      }

      error("unary `!` requires bool or integer, found `" +
            typeToString(operandType) + "`");
      return recoveryType();
    }

    if (unary.op == "*") {
      TypePtr result = peelOneDeref(operandType);

      if (!result) {
        error("cannot dereference value of type `" + typeToString(operandType) +
              "`");
        return recoveryType();
      }

      return result;
    }

    error("unsupported unary operator `" + unary.op + "`");
    return recoveryType();
  }

  case ASTNodeType::BinaryExpr: {
    const auto &binary = static_cast<const ast::BinaryExprNode &>(expr);

    if (binary.operands.empty() ||
        binary.operators.size() + 1 != binary.operands.size()) {
      error("malformed binary expression");
      return recoveryType();
    }

    const ExprNode *first = asExpr(binary.operands.front().get());

    if (first == nullptr) {
      error("invalid binary operand");
      return recoveryType();
    }

    TypePtr result = checkExpr(*first);

    for (std::size_t i = 0; i < binary.operators.size(); ++i) {
      const std::string &op = binary.operators[i];

      const ExprNode *rhsExpr = asExpr(binary.operands[i + 1].get());

      if (rhsExpr == nullptr) {
        error("invalid right operand of `" + op + "`");
        result = recoveryType();
        continue;
      }

      TypePtr rhs;
      if (isArithmeticOperator(op) || isBitwiseOperator(op) ||
          isShiftOperator(op))
        result = scalarOperand(result);

      if (isLogicalOperator(op)) {
        rhs = checkExpr(*rhsExpr);

        if (!result->isBool() || !rhs->isBool()) {
          error("operator `" + op + "` requires bool operands");
          result = recoveryType();
        } else {
          result = Type::makeBool();
        }

        continue;
      }

      if (isArithmeticOperator(op)) {
        rhs = scalarOperand(checkExpr(*rhsExpr));

        if (!result->isInteger() || !rhs->isInteger() ||
            !sameType(result, rhs)) {
          error("operator `" + op +
                "` requires two operands of the same integer type");
          result = recoveryType();
        }

        continue;
      }

      if (isBitwiseOperator(op)) {
        rhs = scalarOperand(checkExpr(*rhsExpr));

        const bool bothInteger =
            result->isInteger() && rhs->isInteger() && sameType(result, rhs);

        const bool bothBool = result->isBool() && rhs->isBool();

        if (!bothInteger && !bothBool) {
          error("operator `" + op +
                "` requires matching integer operands or two bool operands");
          result = recoveryType();
        }

        continue;
      }

      if (isShiftOperator(op)) {
        rhs = scalarOperand(checkExpr(*rhsExpr));

        if (!result->isInteger() || !rhs->isInteger()) {
          error("operator `" + op + "` requires integer operands");
          result = recoveryType();
        }

        continue;
      }

      if (isComparisonOperator(op)) {
        rhs = checkExpr(*rhsExpr);

        if (op == "==" || op == "!=") {
          if (!hasTrait(result, "PartialEq"))
            error("equality requires PartialEq");
          if (!sameType(result, rhs)) {
            error("operator `" + op + "` requires operands of the same type");
          }
        } else {
          if (!orderedPair(result, rhs)) {
            error("operator `" + op +
                  "` requires matching ordered scalar/reference operands");
          }
        }

        result = Type::makeBool();
        continue;
      }

      error("unsupported binary operator `" + op + "`");
      result = recoveryType();
    }

    return result;
  }

  case ASTNodeType::AssignExpr: {
    const auto &assignment = static_cast<const ast::AssignExprNode &>(expr);

    const ExprNode *left = asExpr(assignment.left.get());
    const ExprNode *right = asExpr(assignment.right.get());

    if (left == nullptr || right == nullptr) {
      error("invalid assignment expression");
      return recoveryType();
    }

    TypePtr leftType = checkExpr(*left);

    if (!mutablePlace(*left)) {
      error("left-hand side of assignment is not mutable");
    }

    TypePtr rightType =
        checkExpr(*right, assignment.op == "=" ? leftType : nullptr);
    if (assignment.op != "=")
      rightType = scalarOperand(rightType);

    if (assignment.op == "=") {
      if (leftType && rightType && !canCoerce(rightType, leftType)) {
        error("cannot assign `" + typeToString(rightType) + "` to `" +
              typeToString(leftType) + "`");
      }

      return Type::makeUnit();
    }

    const std::string baseOp = compoundBaseOperator(assignment.op);

    if (baseOp.empty()) {
      error("unsupported assignment operator `" + assignment.op + "`");
      return recoveryType();
    }

    bool valid = false;

    if (isArithmeticOperator(baseOp)) {
      valid = leftType && rightType && leftType->isInteger() &&
              rightType->isInteger() && sameType(leftType, rightType);
    } else if (isBitwiseOperator(baseOp)) {
      valid = leftType && rightType &&
              ((leftType->isInteger() && rightType->isInteger() &&
                sameType(leftType, rightType)) ||
               (leftType->isBool() && rightType->isBool()));
    } else if (isShiftOperator(baseOp)) {
      valid = leftType && rightType && leftType->isInteger() &&
              rightType->isInteger();
    }

    if (!valid) {
      error("invalid operands for `" + assignment.op + "`");
    }

    return Type::makeUnit();
  }

  case ASTNodeType::CastExpr: {
    const auto &cast = static_cast<const ast::CastExprNode &>(expr);
    const ExprNode *base = asExpr(cast.expr.get());
    if (base == nullptr) {
      error("cast has no valid source expression");
      return recoveryType();
    }

    TypePtr current = checkExpr(*base);

    for (const auto &targetNode : cast.types) {
      const TypeNode *targetTypeNode = asType(targetNode.get());
      if (targetTypeNode == nullptr) {
        error("invalid cast target type");
        current = recoveryType();
        continue;
      }
      TypePtr target = resolveType(*targetTypeNode);
      if (!canExplicitlyCast(current, target)) {
        error("cannot cast `" + typeToString(current) + "` to `" +
              typeToString(target) + "`");
      }
      current = target;
    }

    return current;
  }

  case ASTNodeType::CallExpr: {
    const auto &call = static_cast<const ast::CallExprNode &>(expr);

    const auto *path =
        dynamic_cast<const ast::PathExprNode *>(call.function.get());

    if (path == nullptr || path->segments.empty()) {
      error("only named functions can be called");
      return recoveryType();
    }

    std::vector<TypePtr> parameters;
    TypePtr result;
    FunctionSymbol *fn = nullptr;
    for (const auto &segment : path->segments) {
      if (genericTypeArgumentCount(segment) &&
          !(&segment == &path->segments.front() && path->segments.size() == 2 &&
            (segment.name == "Box" || segment.name == "Vec")))
        error("type arguments not permitted on this function path");
    }
    if (path->segments.size() == 2) {
      const auto &head = path->segments[0];
      const auto &name = path->segments[1].name;
      TypePtr owner = resolvePathHead(head);
      if ((owner->isBox() || owner->isVec()) && name == "new") {
        result = owner;
        if (owner->isBox())
          parameters.push_back(owner->elementType());
      } else {
        Symbol *member =
            owner->isStruct()
                ? owner->structSymbol()->lookupAssociatedValue(name)
                : nullptr;
        if (member && member->kind == SymbolKind::Function) {
          fn = static_cast<FunctionSymbol *>(member);
        } else {
          Operation op;
          for (TypePtr candidate : {owner, Type::makeReference(owner, false),
                                    Type::makeReference(owner, true)}) {
            op = operationFor(candidate, name);
            if (op.result)
              break;
          }
          if (op.result) {
            result = op.result;
            parameters.push_back(op.receiver);
            parameters.insert(parameters.end(), op.parameters.begin(),
                              op.parameters.end());
          }
        }
      }
    } else {
      Symbol *symbol = lookupPathSymbol(*path, currentScope_, currentStruct_);
      if (symbol && symbol->kind == SymbolKind::Function)
        fn = static_cast<FunctionSymbol *>(symbol);
    }
    if (fn) {
      result = fn->returnType;
      if (fn->isMethod()) {
        TypePtr receiver = Type::makeStruct(fn->ownerStruct);
        if (fn->receiverKind == ReceiverKind::SharedReference ||
            fn->receiverKind == ReceiverKind::MutableReference)
          receiver = Type::makeReference(
              receiver, fn->receiverKind == ReceiverKind::MutableReference);
        parameters.push_back(receiver);
      }
      for (const auto &p : fn->parameters)
        parameters.push_back(p.type);
    }
    if (!result)
      error("`" + pathText(*path) + "` is not a supported function");
    if (result && call.args.size() != parameters.size())
      error("incorrect function argument count");
    for (std::size_t i = 0; i < call.args.size(); ++i) {
      auto *argument = asExpr(call.args[i].get());
      if (!argument) {
        error("invalid call argument");
        continue;
      }
      TypePtr target = i < parameters.size() ? parameters[i] : nullptr;
      TypePtr actual = checkExpr(*argument, target);
      if (target && !canCoerce(actual, target))
        error("function argument type mismatch");
    }
    return result ? result : recoveryType();
  }

  case ASTNodeType::MethodCallExpr: {
    const auto &call = static_cast<const ast::MethodCallExprNode &>(expr);
    const ExprNode *receiver = asExpr(call.receiver.get());
    if (!receiver) {
      error("missing method receiver");
      return recoveryType();
    }
    if (genericTypeArgumentCount(call.method))
      error("methods do not take type arguments");

    TypePtr type = checkExpr(*receiver);
    PlaceInfo access = placeInfo(*receiver);
    Operation selected;
    while (type) {
      for (TypePtr candidate : {type, Type::makeReference(type, false),
                                Type::makeReference(type, true)}) {
        selected = operationFor(candidate, call.method.name);
        if (!selected.result)
          continue;
        if (candidate->isReference() && candidate->isMutableReference()) {
          // An existing &mut may reborrow; an inserted autoref needs a mutable
          // place.
          PlaceInfo reached =
              sameType(candidate, type) ? dereferencePlace(access) : access;
          if (!reached.mutableAccess || reached.shared)
            error("method `" + call.method.name + "` requires mutable access");
        }
        break;
      }
      if (selected.result)
        break;
      if (!type->isReference() && !type->isBox())
        break;
      access = dereferencePlace(access);
      type = type->elementType();
    }

    if (!selected.result) {
      error("no method `" + call.method.name + "` for receiver");
      for (const auto &a : call.args)
        if (asExpr(a.get()))
          checkExpr(*asExpr(a.get()));
      return recoveryType();
    }
    if (call.args.size() != selected.parameters.size())
      error("incorrect method argument count");

    for (std::size_t i = 0; i < call.args.size(); ++i) {
      if (!asExpr(call.args[i].get())) {
        error("invalid method argument");
        continue;
      }
      TypePtr target =
          i < selected.parameters.size() ? selected.parameters[i] : nullptr;
      TypePtr actual = checkExpr(*asExpr(call.args[i].get()), target);
      if (target && !canCoerce(actual, target))
        error("method argument type mismatch");
    }
    return selected.result;
  }

  case ASTNodeType::FieldExpr: {
    const auto &field = static_cast<const ast::FieldExprNode &>(expr);

    const ExprNode *receiver = asExpr(field.receiver.get());

    if (receiver == nullptr) {
      error("field expression has no valid receiver");
      return recoveryType();
    }

    TypePtr receiverType = peelAllDeref(checkExpr(*receiver));

    if (!receiverType || !receiverType->isStruct() ||
        receiverType->structSymbol() == nullptr) {
      error("cannot access field `" + field.name + "` on non-struct value");
      return recoveryType();
    }

    const StructFieldInfo *fieldInfo =
        receiverType->structSymbol()->lookupField(field.name);

    if (fieldInfo == nullptr) {
      error("struct `" + receiverType->structSymbol()->name +
            "` has no field `" + field.name + "`");
      return recoveryType();
    }

    return fieldInfo->type;
  }

  case ASTNodeType::IndexExpr: {
    const auto &index = static_cast<const ast::IndexExprNode &>(expr);

    const ExprNode *base = asExpr(index.base.get());
    const ExprNode *subscript = asExpr(index.index.get());

    if (base == nullptr || subscript == nullptr) {
      error("invalid index expression");
      return recoveryType();
    }

    TypePtr baseType = peelAllDeref(checkExpr(*base));

    TypePtr indexType = checkExpr(*subscript, Type::makeUSize());

    if (!sameType(indexType, Type::makeUSize())) {
      error("array/vector index must have type usize");
    }

    if (baseType && (baseType->isArray() || baseType->isVec())) {
      return baseType->elementType();
    }

    error("type `" + typeToString(baseType) + "` cannot be indexed");
    return recoveryType();
  }

  case ASTNodeType::BlockExpr: {
    const auto &block = static_cast<const ast::BlockExprNode &>(expr);

    enterScope();

    const ast::ExprStmtNode *trailingStmt = nullptr;
    if (block.tailExpr == nullptr && !block.statements.empty()) {
      const auto *last =
          dynamic_cast<const ast::StmtNode *>(block.statements.back().get());
      if (last != nullptr && last->kind == ASTNodeType::ExprStmt) {
        const auto &exprStmt = static_cast<const ast::ExprStmtNode &>(*last);
        if (!exprStmt.hasSemicolon)
          trailingStmt = &exprStmt;
      }
    }

    for (std::size_t index = 0; index < block.statements.size(); ++index) {
      const auto &statement = block.statements[index];
      const auto *stmt = dynamic_cast<const ast::StmtNode *>(statement.get());

      if (stmt == nullptr) {
        error("invalid statement in block");
        continue;
      }

      // The final semicolon-free statement is checked as the block's tail
      if (trailingStmt != nullptr && index + 1 == block.statements.size())
        continue;

      checkStmt(*stmt);
    }

    TypePtr result;

    if (trailingStmt != nullptr) {
      if (const ExprNode *trailingExpr = asExpr(trailingStmt->expr.get()))
        result = checkExpr(*trailingExpr, expected);
      else {
        error("invalid expression statement");
        result = recoveryType();
      }
    } else if (const ExprNode *tail = asExpr(block.tailExpr.get())) {
      result = checkExpr(*tail, expected);
    } else if (!block.statements.empty()) {
      bool diverges = false;
      for (const auto &entry : block.statements) {
        if (entry->kind == ASTNodeType::LetStmt)
          diverges |= divergesNode(
              static_cast<const ast::LetStmtNode &>(*entry).expr.get());
        else if (entry->kind == ASTNodeType::ExprStmt)
          diverges |= divergesNode(
              static_cast<const ast::ExprStmtNode &>(*entry).expr.get());
      }
      if (diverges) {
        result = Type::makeNever();
      } else {
        result = Type::makeUnit();
      }
    } else {
      result = Type::makeUnit();
    }

    leaveScope();
    return result;
  }

  case ASTNodeType::IfExpr: {
    const auto &ifExpr = static_cast<const ast::IfExprNode &>(expr);
    const ExprNode *condition = asExpr(ifExpr.condition.get());
    const ExprNode *thenExpr = asExpr(ifExpr.ifBlock.get());
    const ExprNode *elseExpr = asExpr(ifExpr.elseBlock.get());
    if (condition == nullptr || thenExpr == nullptr) {
      error("malformed if expression");
      return recoveryType();
    }

    TypePtr conditionType = checkExpr(*condition, Type::makeBool());
    if (!conditionType->isBool() && !conditionType->isNever()) {
      error("if condition must have type bool, found `" +
            typeToString(conditionType) + "`");
    }
    if (elseExpr == nullptr) {
      TypePtr thenType = checkExpr(*thenExpr, Type::makeUnit());
      if (!canCoerce(thenType, Type::makeUnit())) {
        error("if expression without else must produce `()`");
      }
      return Type::makeUnit();
    }

    TypePtr thenType = checkExpr(*thenExpr, expected);
    TypePtr elseType = checkExpr(*elseExpr, expected);

    if (expected) {
      if (!canCoerce(thenType, expected) || !canCoerce(elseType, expected))
        error("if branch does not match expected type `" +
              typeToString(expected) + "`");
      if (thenType->isNever() && elseType->isNever())
        return Type::makeNever();
      return expected;
    }
    if (sameType(thenType, elseType)) {
      return thenType;
    }
    if (thenType->isNever()) {
      return elseType;
    }
    if (elseType->isNever()) {
      return thenType;
    }
    if (canCoerce(thenType, elseType)) {
      return elseType;
    }
    if (canCoerce(elseType, thenType)) {
      return thenType;
    }
    error("if branches have incompatible types `" + typeToString(thenType) +
          "` and `" + typeToString(elseType) + "`");
    return recoveryType();
  }

  case ASTNodeType::LoopExpr: {
    const auto &loop = static_cast<const ast::LoopExprNode &>(expr);
    const ExprNode *body = asExpr(loop.body.get());
    if (body == nullptr) {
      error("loop has no valid body");
      return recoveryType();
    }

    LoopContext context;
    context.kind = LoopKind::Loop;
    context.expectedType = expected;
    loopStack_.push_back(std::move(context));

    // Rx spec: the body block of loop has expected type (), independently of the expected type of the loop expression itself.
    TypePtr bodyType = checkExpr(*body, Type::makeUnit());
    if (bodyType && !canCoerce(bodyType, Type::makeUnit())) {
      error("loop body must have type `()`, found `" + typeToString(bodyType) +
            "`");
    }
    LoopContext finished = std::move(loopStack_.back());
    loopStack_.pop_back();

    if (!finished.sawBreak || !finished.hasNonDivergingBreakOperand) {
      return Type::makeNever();
    }
    if (finished.expectedType) {
      return finished.expectedType;
    }
    return finished.breakType ? finished.breakType : Type::makeUnit();
  }

  case ASTNodeType::WhileExpr: {
    const auto &whileExpr = static_cast<const ast::WhileExprNode &>(expr);
    const ExprNode *condition = asExpr(whileExpr.condition.get());
    const ExprNode *body = asExpr(whileExpr.body.get());

    if (condition == nullptr || body == nullptr) {
      error("malformed while expression");
      return recoveryType();
    }

    loopTargetBarriers_.push_back(loopStack_.size());
    TypePtr conditionType = checkExpr(*condition, Type::makeBool());
    loopTargetBarriers_.pop_back();
    if (!conditionType->isBool() && !conditionType->isNever()) {
      error("while condition must have type bool, found `" +
            typeToString(conditionType) + "`");
    }
    LoopContext context;
    context.kind = LoopKind::While;
    loopStack_.push_back(std::move(context));
    TypePtr bodyType = checkExpr(*body, Type::makeUnit());
    if (bodyType && !canCoerce(bodyType, Type::makeUnit())) {
      error("while body must have type `()`, found `" + typeToString(bodyType) +
            "`");
    }

    loopStack_.pop_back();
    // while always has type ()
    return Type::makeUnit();
  }

  case ASTNodeType::BreakExpr: {
    const auto &breakExpr = static_cast<const ast::BreakExprNode &>(expr);
    const ExprNode *value = asExpr(breakExpr.expr.get());

    LoopContext *target = currentLoopTarget();
    if (target == nullptr) {
      error("`break` used outside of a valid loop target");
      if (value != nullptr) {
        (void)checkExpr(*value);
      }
      return Type::makeNever();
    }

    if (value != nullptr && target->kind == LoopKind::While) {
      (void)checkExpr(*value);
      error("`break` with a value requires a `loop` target");
      return Type::makeNever();
    }

    if (target->kind == LoopKind::While) {
      target->sawBreak = true;
      target->hasNonDivergingBreakOperand = true;
      return Type::makeNever();
    }

    const std::size_t targetIndex = loopStack_.size() - 1;
    TypePtr operandType;

    if (value != nullptr) {
      TypePtr breakExpected = loopStack_[targetIndex].expectedType;
      operandType = checkExpr(*value, breakExpected);
    } else {
      operandType = Type::makeUnit();
    }

    LoopContext &context = loopStack_[targetIndex];
    context.sawBreak = true;
    if (operandType && !operandType->isNever() &&
        (!value || !divergesNode(value))) {
      context.hasNonDivergingBreakOperand = true;
    }
    if (context.expectedType) {
      if (operandType && !canCoerce(operandType, context.expectedType)) {
        error("`break` value has type `" + typeToString(operandType) +
              "`, expected `" + typeToString(context.expectedType) + "`");
      }
      context.breakType = context.expectedType;
    } else {
      TypePtr merged = mergeLoopBreakTypes(context.breakType, operandType);
      if (!merged) {
        error("incompatible `break` value types `" +
              typeToString(context.breakType) + "` and `" +
              typeToString(operandType) + "`");
      } else {
        context.breakType = std::move(merged);
      }
    }

    // break itself always diverges and has type !
    return Type::makeNever();
  }

  case ASTNodeType::ContinueExpr:
    if (currentLoopTarget() == nullptr) {
      error("`continue` used outside of a valid loop target");
    }
    return Type::makeNever();

  case ASTNodeType::ReturnExpr: {
    const auto &returnExpr = static_cast<const ast::ReturnExprNode &>(expr);
    if (currentFunction_ == nullptr) {
      error("`return` used outside of a function");
      return Type::makeNever();
    }

    TypePtr actual;
    if (const ExprNode *value = asExpr(returnExpr.expr.get())) {
      actual = checkExpr(*value, currentFunction_->returnType);
    } else {
      actual = Type::makeUnit();
    }
    if (actual && !canCoerce(actual, currentFunction_->returnType)) {
      error("return expression has type `" + typeToString(actual) +
            "`, expected `" + typeToString(currentFunction_->returnType) + "`");
    }

    return Type::makeNever();
  }

  default:
    error("unsupported expression AST node");
    return recoveryType();
  }
}

// Type resolution
TypePtr SemanticChecker::resolveType(const ast::TypeNode &type) {
  switch (type.kind) {
  //  根据 TypeNode 的种类进行分发
  case ASTNodeType::UnitType:
    return Type::makeUnit();

  case ASTNodeType::ReferenceType: {
    const auto &reference = static_cast<const ast::ReferenceTypeNode &>(type);
    const TypeNode *referent = asType(reference.type.get());
    if (referent == nullptr) {
      error("reference has no valid referent type");
      return recoveryType();
    }

    return Type::makeReference(resolveType(*referent), reference.isMutable);
  }

  case ASTNodeType::ArrayType: {
    const auto &array = static_cast<const ast::ArrayTypeNode &>(type);
    const TypeNode *element = asType(array.type.get());
    if (element == nullptr || array.length == nullptr) {
      error("invalid array type");
      return recoveryType();
    }
    auto length = constantLength(*array.length);
    if (!length) {
      error("array length is not a usize constant");
      return recoveryType();
    }
    return Type::makeArray(resolveType(*element), *length);
  }

  case ASTNodeType::PathType: {
    const auto &path = static_cast<const ast::PathTypeNode &>(type);
    if (path.segments.empty()) {
      error("empty type path");
      return recoveryType();
    }
    if (path.segments.size() != 1) {
      error("qualified type path `" + typePathText(path) +
            "` is not a supported Rx type path");
      return recoveryType();
    }
    return resolvePathHead(path.segments.front());
  }

  default:
    error("invalid AST node in type position");
    return recoveryType();
  }
}

// Name resolution helpers
Symbol *SemanticChecker::resolveValue(const std::string &name) {
  if (currentScope_ == nullptr) {
    error("internal semantic error: no current scope while resolving `" + name +
          "`");
    return nullptr;
  }
  Symbol *symbol = currentScope_->lookupValue(name);
  if (symbol == nullptr) {
    error("undefined value `" + name + "`");
  }
  return symbol;
}

// 在类型命名空间中查找 struct
StructSymbol *SemanticChecker::resolveStruct(const std::string &name) {
  if (currentScope_ == nullptr) {
    error("internal semantic error: no current scope while resolving type `" +
          name + "`");
    return nullptr;
  }
  StructSymbol *symbol = currentScope_->lookupType(name);
  if (symbol == nullptr) {
    error("unknown type `" + name + "`");
  }
  return symbol;
}

// Symbol declaration helpers
// structnode to structsymbol
StructSymbol *
SemanticChecker::declareStruct(const ast::StructItemNode &structure) {
  Scope &global = context_.globalScope();

  if (StructSymbol *existing = global.lookupTypeLocal(structure.name)) {
    error("duplicate struct definition `" + structure.name + "`");
    return existing;
  }

  if (structure.name == "i32")
    error("cannot redefine protected type i32");
  auto *symbol = context_.makeSymbol<StructSymbol>(structure.name, &structure);
  symbol->derives.insert(structure.derives.begin(), structure.derives.end());

  if (!global.defineType(symbol)) {
    error("duplicate type definition `" + structure.name + "`");
  }

  return symbol;
}

FunctionSymbol *
SemanticChecker::declareFunction(const ast::FnItemNode &function,
                                 StructSymbol *owner) {
  StructSymbol *oldStruct = currentStruct_;
  if (owner != nullptr) {
    currentStruct_ = owner;
  }
  // 确定返回类型
  TypePtr returnType = Type::makeUnit();
  if (function.returnType != nullptr) {
    const TypeNode *returnNode = asType(function.returnType.get());
    if (returnNode == nullptr) {
      error("function `" + function.name + "` has an invalid return type");
      returnType = recoveryType();
    } else {
      returnType = resolveType(*returnNode);
    }
  }
  // 创建 FunctionSymbol
  auto *symbol = context_.makeSymbol<FunctionSymbol>(
      function.name, std::move(returnType), &function);
  symbol->ownerStruct = owner;
  // 处理 self
  bool seenReceiver = false;
  for (const auto &parameter : function.parameters) {
    if (parameter.kind == ast::FunctionParamKind::Self) {
      if (seenReceiver) {
        error("function `" + function.name +
              "` has more than one `self` parameter");
        continue;
      }
      seenReceiver = true;
      if (owner == nullptr) {
        error("free function `" + function.name +
              "` cannot have a `self` parameter");
        continue;
      }
      if (parameter.isByReference) {
        symbol->receiverKind = parameter.isMutable
                                   ? ReceiverKind::MutableReference
                                   : ReceiverKind::SharedReference;
      } else {
        symbol->receiverKind = parameter.isMutable ? ReceiverKind::MutableValue
                                                   : ReceiverKind::Value;
      }
      continue;
    }

    const TypeNode *parameterType = asType(parameter.type.get());
    TypePtr resolvedType;
    if (parameterType == nullptr) {
      error("parameter `" + parameter.name + "` of function `" + function.name +
            "` has no valid type");
      resolvedType = recoveryType();
    } else {
      resolvedType = resolveType(*parameterType);
    }
    symbol->parameters.emplace_back(parameter.name, std::move(resolvedType),
                                    parameter.isMutable);
  }

  if (owner != nullptr) {
    if (!owner->defineAssociatedValue(symbol)) {
      error("duplicate associated value `" + function.name + "` in struct `" +
            owner->name + "`");
    }
  } else {
    Scope &global = context_.globalScope();

    if (!global.defineValue(symbol)) {
      error("duplicate value definition `" + function.name + "`");
    }
  }

  currentStruct_ = oldStruct;
  return symbol;
}


// Loop checking
// 当前 break/continue 应该作用于哪个循环
SemanticChecker::LoopContext *SemanticChecker::currentLoopTarget() {
  std::size_t barrier = 0;
  if (!loopTargetBarriers_.empty()) {
    barrier = loopTargetBarriers_.back();
  }

  if (loopStack_.size() <= barrier) {
    return nullptr;
  }

  return &loopStack_.back();
}

TypePtr SemanticChecker::mergeLoopBreakTypes(const TypePtr &lhs,
                                             const TypePtr &rhs) {
  if (!lhs) {
    return rhs;
  }
  if (!rhs) {
    return lhs;
  }
  if (lhs->isNever()) {
    return rhs;
  }
  if (rhs->isNever()) {
    return lhs;
  }
  if (sameType(lhs, rhs)) {
    return lhs;
  }

  // LUB for the coercions currently supported by Type::canCoerce().
  if (canCoerce(lhs, rhs)) {
    return rhs;
  }
  if (canCoerce(rhs, lhs)) {
    return lhs;
  }

  return nullptr;
}

void SemanticChecker::enterScope() {
  if (currentScope_ == nullptr) {
    error("internal semantic error: cannot enter child of null scope");
    return;
  }

  Scope &child = context_.createChildScope(*currentScope_);

  currentScope_ = &child;
}

void SemanticChecker::leaveScope() {
  if (currentScope_ == nullptr) {
    error("internal semantic error: cannot leave null scope");
    return;
  }

  Scope *parent = currentScope_->parent();

  if (parent == nullptr) {
    error("internal semantic error: attempted to leave global scope");
    return;
  }

  currentScope_ = parent;
}

// Diagnostics
void SemanticChecker::error(const std::string &message) {
  errors_.push_back(message);
}

SemanticChecker::PlaceInfo SemanticChecker::dereferencePlace(PlaceInfo p) {
  if (!p.type)
    return {};
  if (p.type->isReference()) {
    p.shared |= !p.type->isMutableReference();
    p.mutableAccess = !p.shared && p.type->isMutableReference();
  } else if (!p.type->isBox()) {
    return {};
  }
  p.type = p.type->elementType();
  p.place = true;
  return p;
}

SemanticChecker::PlaceInfo SemanticChecker::placeInfo(const ExprNode &expr) {
  auto found = expressionTypes_.find(&expr);
  TypePtr type = found == expressionTypes_.end() ? nullptr : found->second;
  switch (expr.kind) {
  case ASTNodeType::PathExpr: {
    const auto &path = static_cast<const ast::PathExprNode &>(expr);
    Symbol *s = lookupPathSymbol(path, currentScope_, currentStruct_);
    if (s && s->kind == SymbolKind::Variable)
      return {type, true, static_cast<VariableSymbol *>(s)->isMutable, false};
    return {type, false, s && s->kind == SymbolKind::Const, false};
  }
  case ASTNodeType::UnaryExpr: {
    const auto &u = static_cast<const ast::UnaryExprNode &>(expr);
    if (u.op == "*" && asExpr(u.expr.get()))
      return dereferencePlace(placeInfo(*asExpr(u.expr.get())));
    break;
  }
  case ASTNodeType::FieldExpr:
  case ASTNodeType::IndexExpr: {
    const ExprNode *base =
        expr.kind == ASTNodeType::FieldExpr
            ? asExpr(
                  static_cast<const ast::FieldExprNode &>(expr).receiver.get())
            : asExpr(static_cast<const ast::IndexExprNode &>(expr).base.get());
    if (!base)
      return {};
    PlaceInfo p = placeInfo(*base);
    while (p.type && (p.type->isReference() || p.type->isBox()))
      p = dereferencePlace(p);
   
    if (expr.kind == ASTNodeType::IndexExpr && p.type && p.type->isVec())
      p.shared |= !p.mutableAccess;
    p.type = type;
    p.place = true;
    return p;
  }
  default:
    break;
  }
  return {type, false, true, false};
}

// 判断一个表达式是否可以被当作可修改的存储位置
bool SemanticChecker::mutablePlace(const ExprNode &expr, bool allowTemporary) {
  PlaceInfo p = placeInfo(expr);
  return (p.place || allowTemporary) && p.mutableAccess && !p.shared;
}

// 判断一个类型是否具有指定 Trait
bool SemanticChecker::hasTrait(const TypePtr &type,
                               const std::string &trait) const {
  std::unordered_set<const StructSymbol *> active;
  std::function<bool(TypePtr)> visit = [&](TypePtr t) -> bool {
    if (!t)
      return false;
    if (t->isNever() || t->isUnit() || t->isInteger() || t->isBool())
      return true;
    if (t->isReference()) {
      if (trait == "Copy" || trait == "Clone")
        return !t->isMutableReference();
      return visit(t->elementType());
    }
    if (t->isArray())
      return visit(t->elementType());
    if (t->isBox() || t->isVec())
      return trait != "Copy" && visit(t->elementType());
    const StructSymbol *s = t->structSymbol();
    if (!s || !s->derives.count(trait))
      return false;
    if (trait == "Copy" && !s->derives.count("Clone"))
      return false;
    if (trait == "Eq" && !s->derives.count("PartialEq"))
      return false;

    if (!active.insert(s).second)
      return true;
    for (const auto &field : s->fields) {
      if (!visit(field.type)) {
        active.erase(s);
        return false;
      }
    }
    active.erase(s);
    return true;
  };
  return visit(type);
}

// 对所有 struct 进行整体验证
void SemanticChecker::validateStructs(const ast::CrateNode &crate) {
  std::unordered_map<const StructSymbol *, int> state;
  std::function<void(TypePtr)> layout = [&](TypePtr t) {
    if (!t)
      return;
    if (t->isArray()) {
      layout(t->elementType());
      return;
    }
    if (!t->isStruct() || !t->structSymbol())
      return;
    const auto *s = t->structSymbol();
    if (state[s] == 1) {
      error("recursive inline layout in struct `" + s->name + "`");
      return;
    }
    if (state[s] == 2)
      return;
    state[s] = 1;
    for (const auto &field : s->fields)
      layout(field.type);
    state[s] = 2;
  };
  for (const auto &item : crate.items) {
    if (!item || item->kind != ASTNodeType::StructItem)
      continue;
    const auto &node = static_cast<const ast::StructItemNode &>(*item);
    auto *s = context_.globalScope().lookupTypeLocal(node.name);
    if (!s || s->declaration != &node)
      continue;
    layout(Type::makeStruct(s));
    std::unordered_set<std::string> seenDerives;
    for (const auto &trait : node.derives) {
      if (!seenDerives.insert(trait).second)
        error("duplicate derive `" + trait + "`");
      if (trait != "Copy" && trait != "Clone" && trait != "PartialEq" &&
          trait != "Eq")
        error("unsupported derive `" + trait + "`");
      else if (!hasTrait(Type::makeStruct(s), trait))
        error("invalid derive `" + trait + "` for struct `" + node.name + "`");
    }
  }
}

// 检查一个 AST 节点是否属于允许的常量表达式形式
bool SemanticChecker::constantForm(const ASTNode &node, bool magnitude) {
  switch (node.kind) {
  case ASTNodeType::IntLitExpr:
    return true;
  case ASTNodeType::BoolLitExpr:
    return !magnitude;
  case ASTNodeType::PathExpr: {
    auto *s = lookupPathSymbol(static_cast<const ast::PathExprNode &>(node),
                               &context_.globalScope(), currentStruct_);
    if (!s || s->kind != SymbolKind::Const)
      return false;
    return validateConstant(*static_cast<ConstSymbol *>(s));
  }
  case ASTNodeType::UnaryExpr: {
    const auto &u = static_cast<const ast::UnaryExprNode &>(node);
    return !magnitude && u.op == "-" && u.expr && constantForm(*u.expr, true);
  }
  default:
    return false;
  }
}

// 完整验证某个常量声明是否合法
bool SemanticChecker::validateConstant(ConstSymbol &symbol) {
  int state = constantStates_[&symbol];
  if (state == 1) {
    error("cyclic constant dependency at `" + symbol.name + "`");
    return false;
  }
  if (state >= 2)
    return state == 2;
  constantStates_[&symbol] = 1;
  auto *decl = dynamic_cast<const ast::ConstItemNode *>(symbol.declaration);
  auto *oldStruct = currentStruct_;
  auto *oldScope = currentScope_;
  currentStruct_ = symbol.ownerStruct;
  currentScope_ = &context_.globalScope();
  if (!symbol.type && decl && asType(decl->type.get()))
    symbol.type = resolveType(*asType(decl->type.get()));
  bool ok = decl && decl->value && constantForm(*decl->value);
  if (!ok)
    error("invalid constant initializer for `" + symbol.name + "`");
  if (decl && asExpr(decl->value.get())) {
    TypePtr actual = checkExpr(*asExpr(decl->value.get()), symbol.type);
    if (!canCoerce(actual, symbol.type)) {
      error("constant initializer type mismatch for `" + symbol.name + "`");
      ok = false;
    }
  }
  currentStruct_ = oldStruct;
  currentScope_ = oldScope;
  constantStates_[&symbol] = ok ? 2 : 3;
  return ok;
}

std::optional<std::int64_t>
SemanticChecker::constantValue(const ASTNode &node, StructSymbol *owner) {
  if (node.kind == ASTNodeType::IntLitExpr) {
    auto n = parseUnsignedIntegerLiteral(
        static_cast<const ast::IntLitExprNode &>(node).rawValue);
    if (n && *n <= std::uint64_t(INT64_MAX))
      return static_cast<std::int64_t>(*n);
  } else if (node.kind == ASTNodeType::PathExpr) {
    auto *s = lookupPathSymbol(static_cast<const ast::PathExprNode &>(node),
                               &context_.globalScope(), owner);
    if (s && s->kind == SymbolKind::Const) {
      auto *c = static_cast<ConstSymbol *>(s);
      if (!validateConstant(*c))
        return std::nullopt;
      auto *decl = static_cast<const ast::ConstItemNode *>(c->declaration);
      return constantValue(*decl->value, c->ownerStruct);
    }
  } else if (node.kind == ASTNodeType::UnaryExpr) {
    const auto &u = static_cast<const ast::UnaryExprNode &>(node);
    if (u.op == "-" && u.expr) {
      auto n = constantValue(*u.expr, owner);
      if (n)
        return -*n;
    }
  }
  return std::nullopt;
}

std::optional<std::uint32_t>
SemanticChecker::constantLength(const ASTNode &node) {
  if (!constantForm(node) || !asExpr(&node))
    return std::nullopt;
  // Resolve lexical names here (locals cannot masquerade as const items).
  if (node.kind == ASTNodeType::PathExpr) {
    auto *s = lookupPathSymbol(static_cast<const ast::PathExprNode &>(node),
                               currentScope_, currentStruct_);
    if (!s || s->kind != SymbolKind::Const)
      return std::nullopt;
  }
  TypePtr type = checkExpr(*asExpr(&node), Type::makeUSize());
  if (!sameType(type, Type::makeUSize()))
    return std::nullopt;
  auto n = constantValue(node, currentStruct_);
  if (!n || *n < 0 || std::uint64_t(*n) > UINT32_MAX)
    return std::nullopt;
  return static_cast<std::uint32_t>(*n);
}

// 查询某个 AST 节点是否已经被记录为发散
bool SemanticChecker::divergesNode(const ASTNode *node) const {
  const auto *e = asExpr(node);
  auto it = expressionDiverges_.find(e);
  return it != expressionDiverges_.end() && it->second;
}

bool SemanticChecker::computeDivergence(const ExprNode &expr,
                                        const TypePtr &type) const {
  if (type && type->isNever())
    return true;
  auto d = [&](const auto &p) { return divergesNode(p.get()); };
  auto any = [&](const auto &list) {
    return std::any_of(list.begin(), list.end(),
                       [&](const auto &p) { return d(p); });
  };
  switch (expr.kind) {
  case ASTNodeType::BlockExpr: {
    const auto &b = static_cast<const ast::BlockExprNode &>(expr);
    if (d(b.tailExpr))
      return true;
    for (const auto &s : b.statements) {
      if (s->kind == ASTNodeType::LetStmt &&
          d(static_cast<const ast::LetStmtNode &>(*s).expr))
        return true;
      if (s->kind == ASTNodeType::ExprStmt &&
          d(static_cast<const ast::ExprStmtNode &>(*s).expr))
        return true;
    }
    return false;
  }
  case ASTNodeType::IfExpr: {
    const auto &e = static_cast<const ast::IfExprNode &>(expr);
    return d(e.condition) || (e.elseBlock && d(e.ifBlock) && d(e.elseBlock));
  }
  case ASTNodeType::WhileExpr:
    return d(static_cast<const ast::WhileExprNode &>(expr).condition);
  case ASTNodeType::UnaryExpr:
    return d(static_cast<const ast::UnaryExprNode &>(expr).expr);
  case ASTNodeType::BinaryExpr: {
    const auto &b = static_cast<const ast::BinaryExprNode &>(expr);
    if (b.operands.empty())
      return false;
    if (d(b.operands[0]))
      return true;
    for (std::size_t i = 0; i < b.operators.size(); ++i) {
      if (isLogicalOperator(b.operators[i]))
        return false;
      if (d(b.operands[i + 1]))
        return true;
    }
    return false;
  }
  case ASTNodeType::AssignExpr: {
    const auto &e = static_cast<const ast::AssignExprNode &>(expr);
    return d(e.left) || d(e.right);
  }
  case ASTNodeType::CastExpr:
    return d(static_cast<const ast::CastExprNode &>(expr).expr);
  case ASTNodeType::CallExpr:
    return any(static_cast<const ast::CallExprNode &>(expr).args);
  case ASTNodeType::MethodCallExpr: {
    const auto &e = static_cast<const ast::MethodCallExprNode &>(expr);
    return d(e.receiver) || any(e.args);
  }
  case ASTNodeType::FieldExpr:
    return d(static_cast<const ast::FieldExprNode &>(expr).receiver);
  case ASTNodeType::IndexExpr: {
    const auto &e = static_cast<const ast::IndexExprNode &>(expr);
    return d(e.base) || d(e.index);
  }
  case ASTNodeType::ArrayExpr:
    return any(static_cast<const ast::ArrayExprNode &>(expr).elements);
  case ASTNodeType::ArrayRepeat:
    return d(static_cast<const ast::ArrayRepeatNode &>(expr).element);
  default:
    return false;
  }
}

// 查询该receiver是否支持对应操作，并返回操作签名
SemanticChecker::Operation
SemanticChecker::operationFor(TypePtr candidate, const std::string &name) {
  Operation op;
  if (!candidate)
    return op;
  TypePtr owner =
      candidate->isReference() ? candidate->elementType() : candidate;
  ReceiverKind kind = !candidate->isReference() ? ReceiverKind::Value
                      : candidate->isMutableReference()
                          ? ReceiverKind::MutableReference
                          : ReceiverKind::SharedReference;
  if (owner->isStruct() && owner->structSymbol()) {
    Symbol *s = owner->structSymbol()->lookupAssociatedValue(name);
    if (s && s->kind == SymbolKind::Function) {
      auto *fn = static_cast<FunctionSymbol *>(s);
      ReceiverKind fk = fn->receiverKind == ReceiverKind::MutableValue
                            ? ReceiverKind::Value
                            : fn->receiverKind;
      if (fk == kind) {
        op.receiver = candidate;
        op.result = fn->returnType;
        op.function = fn;
        for (const auto &p : fn->parameters)
          op.parameters.push_back(p.type);
        return op;
      }
    }
  }
  if (kind == ReceiverKind::SharedReference) {
    if (name == "clone" && hasTrait(owner, "Clone")) {
      op.receiver = candidate;
      op.result = owner;
    } else if (name == "len" && (owner->isArray() || owner->isVec())) {
      op.receiver = candidate;
      op.result = Type::makeUSize();
    } else if (name == "is_empty" && owner->isVec()) {
      op.receiver = candidate;
      op.result = Type::makeBool();
    }
  } else if (kind == ReceiverKind::MutableReference && owner->isVec()) {
    if (name == "push") {
      op.receiver = candidate;
      op.result = Type::makeUnit();
      op.parameters.push_back(owner->elementType());
    } else if (name == "remove") {
      op.receiver = candidate;
      op.result = owner->elementType();
      op.parameters.push_back(Type::makeUSize());
    }
  }
  return op;
}

TypePtr SemanticChecker::resolvePathHead(const ast::PathSegment &segment) {
  const auto &name = segment.name;
  auto count = genericTypeArgumentCount(segment);
  if (name == "Box" || name == "Vec") {
    if (count != 1 || segment.genericArgs.size() != 1 ||
        !genericTypeArgument(segment, 0)) {
      error("`" + name + "` requires one concrete type argument");
      return recoveryType();
    }
    TypePtr t = resolveType(*genericTypeArgument(segment, 0));
    return name == "Box" ? Type::makeBox(t) : Type::makeVec(t);
  }
  if (count) {
    error("type arguments not allowed on `" + name + "`");
    return recoveryType();
  }
  if (name == "i32" || name == "u32" || name == "isize" || name == "usize" ||
      name == "bool") {
    if (!segment.genericArgs.empty())
      error("primitive types do not take generic arguments");
    return name == "bool" ? Type::makeBool() : integerTypeFromSuffix(name);
  }
  if (name == "Self") {
    if (currentStruct_)
      return Type::makeStruct(currentStruct_);
    error("Self outside impl");
    return recoveryType();
  }
  auto *s = resolveStruct(name);
  return s ? Type::makeStruct(s) : recoveryType();
}

} // namespace rx::semantic
