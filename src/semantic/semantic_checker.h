#pragma once
#include "context.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rx::ast {
class ASTNode;
class CrateNode;
class ItemNode;
class FnItemNode;
class StructItemNode;
class ConstItemNode;
class ImplItemNode;
class StmtNode;
class LetStmtNode;
class ExprStmtNode;
class ExprNode;
class TypeNode;
struct PathSegment;
} // namespace rx::ast

namespace rx::semantic {
class SemanticChecker {
private:
  SemanticContext context_;
  // AST expression -> Semantic type
  std::unordered_map<const ast::ExprNode *, TypePtr> expressionTypes_;
  std::unordered_map<const ast::ExprNode *, bool> expressionDiverges_;
  std::unordered_map<const ConstSymbol *, int> constantStates_;
  struct Operation {
    TypePtr receiver;
    std::vector<TypePtr> parameters;
    TypePtr result;
    FunctionSymbol *function = nullptr;
  };
  Operation operationFor(TypePtr, const std::string &);
  TypePtr resolvePathHead(const ast::PathSegment &);
  TypePtr checkExprImpl(const ast::ExprNode &, TypePtr);
  // shared records an access barrier, later &mut dereferences cannot erase it.
  struct PlaceInfo {
    TypePtr type;
    bool place = false;
    bool mutableAccess = false;
    bool shared = false;
  };
  PlaceInfo placeInfo(const ast::ExprNode &);
  PlaceInfo dereferencePlace(PlaceInfo);
  bool mutablePlace(const ast::ExprNode &, bool allowTemporary = false);
  bool divergesNode(const ast::ASTNode *) const;
  bool computeDivergence(const ast::ExprNode &, const TypePtr &) const;
  bool validateConstant(ConstSymbol &);
  bool constantForm(const ast::ASTNode &, bool magnitude = false);
  std::optional<std::int64_t> constantValue(const ast::ASTNode &,
                                            StructSymbol *);
  std::optional<std::uint32_t> constantLength(const ast::ASTNode &);
  bool hasTrait(const TypePtr &, const std::string &) const;
  void validateStructs(const ast::CrateNode &);

  Scope *currentScope_ = nullptr;
  FunctionSymbol *currentFunction_ = nullptr;
  StructSymbol *currentStruct_ = nullptr;
  enum class LoopKind {
    Loop,
    While,
  };

  struct LoopContext {
    LoopKind kind = LoopKind::Loop;
    // Expected type of the loop expression
    TypePtr expectedType;
    // Common type when no expected type exists.
    TypePtr breakType;
    bool sawBreak = false;
    bool hasNonDivergingBreakOperand = false;
  };

  std::vector<LoopContext> loopStack_;
  // While-condition barrier
  std::vector<std::size_t> loopTargetBarriers_;
  std::vector<std::string> errors_;

public:
  SemanticChecker() = default;
  bool check(const ast::CrateNode &crate);
  bool hasError() const noexcept { return !errors_.empty(); }
  const std::vector<std::string> &errors() const noexcept { return errors_; }
  SemanticContext &context() noexcept { return context_; }
  const SemanticContext &context() const noexcept { return context_; }

private:
  // establish declarations
  void collectGlobalDeclarations(const ast::CrateNode &crate);
  // get associated items in impl
  void collectImplDeclarations(const ast::CrateNode &crate);
  void checkItems(const ast::CrateNode &crate);
  // Item checking
  void checkItem(const ast::ItemNode &item);
  void checkFunction(const ast::FnItemNode &function);
  void checkStruct(const ast::StructItemNode &structure);
  void checkConst(const ast::ConstItemNode &constant);
  void checkImpl(const ast::ImplItemNode &implementation);
  // Statement checking
  void checkStmt(const ast::StmtNode &stmt);
  void checkLetStmt(const ast::LetStmtNode &stmt);
  void checkExprStmt(const ast::ExprStmtNode &stmt);
  // Expression checking
  TypePtr checkExpr(const ast::ExprNode &expr, TypePtr expected = nullptr);
  // Type resolution
  TypePtr resolveType(const ast::TypeNode &type);
  // Name resolution helpers
  Symbol *resolveValue(const std::string &name);
  StructSymbol *resolveStruct(const std::string &name);
  // Symbol creation helpers
  StructSymbol *declareStruct(const ast::StructItemNode &structure);
  FunctionSymbol *declareFunction(const ast::FnItemNode &function,
                                  StructSymbol *owner = nullptr);
  // Loop checking
  LoopContext *currentLoopTarget();
  TypePtr mergeLoopBreakTypes(const TypePtr &lhs, const TypePtr &rhs);
  // Scope management
  void enterScope();
  void leaveScope();
  // Error reporting
  void error(const std::string &message);
};

} // namespace rx::semantic