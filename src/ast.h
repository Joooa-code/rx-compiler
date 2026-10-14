#pragma once

#include "RxParser.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace rx::ast {

// astnode kinds
enum class ASTNodeType {
  // root / items
  Crate,
  UseItem,
  FnItem,
  StructItem,
  ConstItem,
  ImplItem,
  // types
  UnitType,
  PathType,
  ReferenceType,
  ArrayType,
  // statements
  LetStmt,
  ExprStmt,
  // expressions
  UnitLitExpr,
  IntLitExpr,
  BoolLitExpr,
  PathExpr,
  StructExpr,
  ArrayExpr,
  ArrayRepeat,
  UnaryExpr,
  BinaryExpr,
  AssignExpr,
  CastExpr,
  CallExpr,
  MethodCallExpr,
  FieldExpr,
  IndexExpr,
  BlockExpr,
  IfExpr,
  LoopExpr,
  WhileExpr,
  BreakExpr,
  ContinueExpr,
  ReturnExpr,
};

class ASTNode;

using NodePtr = std::unique_ptr<ASTNode>;
using NodeList = std::vector<NodePtr>;

// Lifetime / generic / path helper structures
struct Lifetime {
  std::string name;
};

struct LifetimeParam {
  Lifetime lifetime;
  std::vector<Lifetime> bounds;
};

struct GenericParams {
  std::vector<LifetimeParam> lifetimeParams;
};

enum class GenericArgKind {
  Lifetime,
  Type,
};

struct GenericArg {
  GenericArgKind kind = GenericArgKind::Lifetime;
  std::optional<Lifetime> lifetime;
  NodePtr type;
  GenericArg() = default;
  GenericArg(GenericArg &&) noexcept = default;
  GenericArg &operator=(GenericArg &&) noexcept = default;
  GenericArg(const GenericArg &) = delete;
  GenericArg &operator=(const GenericArg &) = delete;
};

struct PathSegment {
  std::string name;
  std::vector<GenericArg> genericArgs;
  PathSegment() = default;
  PathSegment(PathSegment &&) noexcept = default;
  PathSegment &operator=(PathSegment &&) noexcept = default;
  PathSegment(const PathSegment &) = delete;
  PathSegment &operator=(const PathSegment &) = delete;
};

enum class WherePredicateKind {
  Lifetime,
  Type,
};

struct WherePredicate {  
  WherePredicateKind kind = WherePredicateKind::Lifetime;
  std::optional<Lifetime> lifetime;
  NodePtr type;
  std::vector<Lifetime> bounds;
  WherePredicate() = default;
  WherePredicate(WherePredicate &&) noexcept = default;
  WherePredicate &operator=(WherePredicate &&) noexcept = default;
  WherePredicate(const WherePredicate &) = delete;
  WherePredicate &operator=(const WherePredicate &) = delete;
};

struct WhereClause {
  std::vector<WherePredicate> predicates;
  WhereClause() = default;
  WhereClause(WhereClause &&) noexcept = default;
  WhereClause &operator=(WhereClause &&) noexcept = default;
  WhereClause(const WhereClause &) = delete;
  WhereClause &operator=(const WhereClause &) = delete;
};

struct StructField {
  std::string name;
  NodePtr type;
};

class ASTNode {
public:
  const ASTNodeType kind;
  explicit ASTNode(ASTNodeType kind_) : kind(kind_) {}
  virtual ~ASTNode() = default;
  static NodePtr BuildItem(rx::RxParser::ItemContext *ctx);
  static NodePtr BuildAssociatedItem(rx::RxParser::AssociatedItemContext *ctx);
  static NodePtr BuildStatement(rx::RxParser::StatementContext *ctx);
  // AdditiveExpression,ConditionAdditiveExpression,StatementAdditiveExpression,ConditionBreakAdditiveExpression:BinaryExpr
  static NodePtr BuildExpression(antlr4::ParserRuleContext *ctx);
  static NodePtr BuildType(rx::RxParser::TypeRefContext *ctx);
  static NodePtr BuildClosedCastType(rx::RxParser::ClosedCastTypeContext *ctx);
  static NodePtr BuildConstValue(rx::RxParser::ConstValueContext *ctx);
  // Lifetime / generic helper factories
  static Lifetime BuildLifetime(rx::RxParser::LifetimeContext *ctx);
  static std::vector<Lifetime>
  BuildLifetimeBounds(rx::RxParser::LifetimeBoundsContext *ctx);
  static std::vector<Lifetime>
  BuildTypeParamBounds(rx::RxParser::TypeParamBoundsContext *ctx);
  static GenericParams
  BuildGenericParams(rx::RxParser::GenericParamsContext *ctx);
  static WhereClause BuildWhereClause(rx::RxParser::WhereClauseContext *ctx);
  static GenericArg BuildGenericArg(rx::RxParser::GenericArgContext *ctx);
  static std::vector<GenericArg>
  BuildGenericArgs(rx::RxParser::GenericArgsContext *ctx);
  // Path helpers
  static PathSegment
  BuildTypePathSegment(rx::RxParser::TypePathSegmentContext *ctx);
  static PathSegment
  BuildExprPathSegment(rx::RxParser::PathExprSegmentContext *ctx);
  // Debug output
  void dump(std::ostream &out, std::size_t indent = 0) const;
};

// Root
class CrateNode final : public ASTNode {
public:
  NodeList items;
  explicit CrateNode(rx::RxParser::CrateContext *ctx);
};

// Items
class UseItemNode final : public ASTNode {
public:
  std::string text;
  explicit UseItemNode(rx::RxParser::UseDeclarationContext *ctx);
};

enum class FunctionParamKind {
  Normal,
  Self,
};

struct FunctionParam {
  FunctionParamKind kind;
  std::string name;
  bool isMutable;
  bool isByReference;
  std::optional<Lifetime> lifetime;
  NodePtr type;
};

class FnItemNode final : public ASTNode {
public:
  std::string name;
  GenericParams genericParams;
  WhereClause whereClause;
  NodePtr returnType;
  std::vector<FunctionParam> parameters;
  NodePtr body;
  explicit FnItemNode(rx::RxParser::FunctionDefinitionContext *ctx);
};

class StructItemNode final : public ASTNode {
public:
  std::string name;
  GenericParams genericParams;
  WhereClause whereClause;
  std::vector<StructField> fields;
  std::vector<std::string> derives;
  explicit StructItemNode(rx::RxParser::StructDefinitionContext *ctx);
};

class ConstItemNode final : public ASTNode {
public:
  std::string name;
  NodePtr type;
  NodePtr value;
  explicit ConstItemNode(rx::RxParser::ConstantItemContext *ctx);
};

class ImplItemNode final : public ASTNode {
public:
  GenericParams genericParams;
  WhereClause whereClause;
  NodePtr type;
  NodeList associatedItems;
  explicit ImplItemNode(rx::RxParser::InherentImplContext *ctx);
};

// 单独的 `;` 没有语义效果。BlockExprNode 构造时直接跳过，不单独创建 EmptyStmt。
class LetStmtNode final : public ASTNode {
public:
  std::string name;
  bool isMutable = false;
  NodePtr type;
  NodePtr expr;
  explicit LetStmtNode(rx::RxParser::LetStatementContext *ctx);
};

class ExprStmtNode final : public ASTNode {
public:
  NodePtr expr;
  bool hasSemicolon = false;
  explicit ExprStmtNode(antlr4::ParserRuleContext *ctx);
};

// Basic expressions
class UnitLitExprNode final : public ASTNode {
public:
  UnitLitExprNode();
};

class IntLitExprNode final : public ASTNode {
public:
  std::string rawValue;
  explicit IntLitExprNode(std::string rawValue_);
};

class BoolLitExprNode final : public ASTNode {
public:
  bool value = false;
  explicit BoolLitExprNode(bool value_);
};

class PathExprNode final : public ASTNode {
public:
  std::vector<PathSegment> segments;
  explicit PathExprNode(rx::RxParser::PathInExpressionContext *ctx);
};

struct StructExprField {
  std::string name;
  NodePtr expr;
  StructExprField() = default;
  StructExprField(std::string name_, NodePtr expr_)
      : name(std::move(name_)), expr(std::move(expr_)) {}
  StructExprField(StructExprField &&) noexcept = default;
  StructExprField &operator=(StructExprField &&) noexcept = default;
  StructExprField(const StructExprField &) = delete;
  StructExprField &operator=(const StructExprField &) = delete;
};

class StructExprNode final : public ASTNode {
public:
  NodePtr structName;
  std::vector<StructExprField> fields;
  StructExprNode();
};

class ArrayExprNode final : public ASTNode {
public:
  NodeList elements;
  explicit ArrayExprNode(rx::RxParser::ArrayExpressionContext *ctx);
};

class ArrayRepeatNode final : public ASTNode {
public:
  NodePtr element;
  NodePtr count;
  ArrayRepeatNode();
};

// Unary / Binary / Assignment / Cast
class UnaryExprNode final : public ASTNode {
public:
  std::string op;
  NodePtr expr;
  UnaryExprNode();
  explicit UnaryExprNode(antlr4::ParserRuleContext *ctx);
};

// 一个binary node
class BinaryExprNode final : public ASTNode {
public:
  NodeList operands;
  std::vector<std::string> operators;
  explicit BinaryExprNode(antlr4::ParserRuleContext *ctx);
};

class AssignExprNode final : public ASTNode {
public:
  NodePtr left;
  NodePtr right;
  std::string op;
  explicit AssignExprNode(antlr4::ParserRuleContext *ctx);
};

class CastExprNode final : public ASTNode {
public:
  NodePtr expr;
  NodeList types;
  explicit CastExprNode(antlr4::ParserRuleContext *ctx);
};

// Postfix expressions
class CallExprNode final : public ASTNode {
public:
  NodePtr function;
  NodeList args;
  CallExprNode();
};

class MethodCallExprNode final : public ASTNode {
public:
  NodePtr receiver;
  PathSegment method;
  NodeList args;
  MethodCallExprNode();
};

class FieldExprNode final : public ASTNode {
public:
  NodePtr receiver;
  std::string name;
  FieldExprNode();
};

class IndexExprNode final : public ASTNode {
public:
  NodePtr base;
  NodePtr index;
  IndexExprNode();
};

// Block / control-flow expressions
class BlockExprNode final : public ASTNode {
public:
  NodeList statements;
  NodePtr tailExpr;
  explicit BlockExprNode(rx::RxParser::BlockExpressionContext *ctx);
};

class IfExprNode final : public ASTNode {
public:
  NodePtr condition;
  NodePtr ifBlock;
  NodePtr elseBlock;
  explicit IfExprNode(rx::RxParser::IfExpressionContext *ctx);
};

class LoopExprNode final : public ASTNode {
public:
  NodePtr body;
  explicit LoopExprNode(rx::RxParser::BlockExpressionContext *ctx);
};

class WhileExprNode final : public ASTNode {
public:
  NodePtr condition;
  NodePtr body;
  WhileExprNode(rx::RxParser::ConditionExpressionContext *conditionCtx,
                rx::RxParser::BlockExpressionContext *bodyCtx);
};

class BreakExprNode final : public ASTNode {
public:
  NodePtr expr;
  BreakExprNode();
};

class ContinueExprNode final : public ASTNode {
public:
  ContinueExprNode();
};

class ReturnExprNode final : public ASTNode {
public:
  NodePtr expr;
  ReturnExprNode();
};

// Types
class UnitTypeNode final : public ASTNode {
public:
  UnitTypeNode();
};

class PathTypeNode final : public ASTNode {
public:
  std::vector<PathSegment> segments;
  explicit PathTypeNode(rx::RxParser::TypePathContext *ctx);
  PathTypeNode();
};

class ReferenceTypeNode final : public ASTNode {
public:
  NodePtr type;
  std::optional<Lifetime> lifetime;
  bool isMutable = false;
  explicit ReferenceTypeNode(rx::RxParser::ReferenceTypeContext *ctx);
  ReferenceTypeNode();
};

class ArrayTypeNode final : public ASTNode {
public:
  NodePtr type;
  NodePtr length;
  explicit ArrayTypeNode(rx::RxParser::ArrayTypeContext *ctx);
};
} // namespace rx::ast