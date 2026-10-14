#include "ast.h"

#include <stdexcept>
#include <string_view>

namespace rx::ast {

namespace {

// Small common helpers
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error("AST: " + message);
}

std::string identifierText(rx::RxParser::IdentifierContext *ctx) {
  if (ctx == nullptr) {
    fail("null identifier context");
  }
  return ctx->getText();
}

std::string pathIdentText(rx::RxParser::PathIdentSegmentContext *ctx) {
  if (ctx == nullptr) {
    fail("null path identifier context");
  }
  if (ctx->identifier() != nullptr) {
    return identifierText(ctx->identifier());
  }
  if (ctx->SELF_VALUE() != nullptr) {
    return "self";
  }
  if (ctx->SELF_TYPE() != nullptr) {
    return "Self";
  }
  fail("invalid path identifier `" + ctx->getText() + "`");
}

bool isAssignmentRule(std::size_t rule) {
  return rule == rx::RxParser::RuleAssignmentExpression ||
         rule == rx::RxParser::RuleConditionAssignmentExpression ||
         rule == rx::RxParser::RuleConditionBreakAssignmentExpression ||
         rule == rx::RxParser::RuleStatementAssignmentExpression;
}

bool isBinaryRule(std::size_t rule) {
  return (rule >= rx::RxParser::RuleLogicalOrExpression &&
          rule <= rx::RxParser::RuleClosedMultiplicativeExpression) ||
         (rule >= rx::RxParser::RuleConditionLogicalOrExpression &&
          rule <= rx::RxParser::RuleConditionClosedMultiplicativeExpression) ||
         (rule >= rx::RxParser::RuleConditionBreakLogicalOrExpression &&
          rule <=
              rx::RxParser::RuleConditionBreakClosedMultiplicativeExpression) ||
         (rule >= rx::RxParser::RuleStatementLogicalOrExpression &&
          rule <= rx::RxParser::RuleStatementClosedMultiplicativeExpression);
}

bool isCastRule(std::size_t rule) {
  return rule == rx::RxParser::RuleCastExpression ||
         rule == rx::RxParser::RuleClosedCastExpression ||
         rule == rx::RxParser::RuleConditionCastExpression ||
         rule == rx::RxParser::RuleConditionClosedCastExpression ||
         rule == rx::RxParser::RuleConditionBreakCastExpression ||
         rule == rx::RxParser::RuleConditionBreakClosedCastExpression ||
         rule == rx::RxParser::RuleStatementCastExpression ||
         rule == rx::RxParser::RuleStatementClosedCastExpression;
}

bool isUnaryRule(std::size_t rule) {
  return rule == rx::RxParser::RuleUnaryExpression ||
         rule == rx::RxParser::RuleConditionUnaryExpression ||
         rule == rx::RxParser::RuleConditionBreakUnaryExpression ||
         rule == rx::RxParser::RuleStatementUnaryExpression;
}

bool isPostfixRule(std::size_t rule) {
  return rule == rx::RxParser::RulePostfixExpression ||
         rule == rx::RxParser::RuleConditionPostfixExpression ||
         rule == rx::RxParser::RuleConditionBreakPostfixExpression ||
         rule == rx::RxParser::RuleStatementPostfixExpression;
}

bool isExpressionRule(std::size_t rule) {
  if (rule == rx::RxParser::RuleBlockExpression ||
      rule == rx::RxParser::RuleExpressionWithBlock ||
      rule == rx::RxParser::RuleIfExpression ||
      rule == rx::RxParser::RulePathInExpression ||
      rule == rx::RxParser::RuleConstValue ||
      rule == rx::RxParser::RuleMagnitude ||
      rule == rx::RxParser::RulePrimaryExpression ||
      rule == rx::RxParser::RuleNonBlockPrimary ||
      rule == rx::RxParser::RuleConditionPrimary ||
      rule == rx::RxParser::RuleConditionPrimaryWithoutBareBlock ||
      rule == rx::RxParser::RuleLiteralExpression ||
      rule == rx::RxParser::RuleArrayExpression) {
    return true;
  }

  if (rule >= rx::RxParser::RuleExpression &&
      rule <= rx::RxParser::RuleConditionBreakPostfixExpression) {
    return true;
  }

  if (rule >= rx::RxParser::RuleStatementExpression &&
      rule <= rx::RxParser::RuleStatementPostfixExpression) {
    return true;
  }

  return false;
}

antlr4::ParserRuleContext *
firstExpressionChild(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    return nullptr;
  }

  for (auto *child : ctx->children) {
    auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child);
    if (rule != nullptr && isExpressionRule(rule->getRuleIndex())) {
      return rule;
    }
  }

  return nullptr;
}

std::vector<antlr4::ParserRuleContext *>
directExpressionChildren(antlr4::ParserRuleContext *ctx) {
  std::vector<antlr4::ParserRuleContext *> result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *child : ctx->children) {
    auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child);
    if (rule != nullptr && isExpressionRule(rule->getRuleIndex())) {
      result.push_back(rule);
    }
  }

  return result;
}

bool isBinaryOperatorText(std::string_view text) {
  return text == "+" || text == "-" || text == "*" || text == "/" ||
         text == "%" || text == "==" || text == "!=" || text == "<" ||
         text == "<=" || text == ">" || text == ">=" || text == "&&" ||
         text == "||" || text == "&" || text == "|" || text == "^" ||
         text == "<<" || text == ">>";
}

// 判断当前这一层 Context 自己有没有真正的 binary operator
bool hasDirectBinaryOperator(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    return false;
  }

  for (auto *child : ctx->children) {
    if (child != nullptr && isBinaryOperatorText(child->getText())) {
      return true;
    }
  }

  return false;
}

// 当前 Context 的直接 child 中是否存在指定文本
bool hasDirectText(antlr4::ParserRuleContext *ctx, std::string_view text) {
  if (ctx == nullptr) {
    return false;
  }

  for (auto *child : ctx->children) {
    if (child != nullptr && child->getText() == text) {
      return true;
    }
  }

  return false;
}

rx::RxParser::UnaryOperatorContext *
directUnaryOperator(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    return nullptr;
  }

  for (auto *child : ctx->children) {
    if (auto *op = dynamic_cast<rx::RxParser::UnaryOperatorContext *>(child)) {
      return op;
    }
  }

  return nullptr;
}

rx::RxParser::AssignmentOperatorContext *
directAssignmentOperator(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    return nullptr;
  }

  for (auto *child : ctx->children) {
    if (auto *op =
            dynamic_cast<rx::RxParser::AssignmentOperatorContext *>(child)) {
      return op;
    }
  }

  return nullptr;
}

NodePtr buildMagnitude(rx::RxParser::MagnitudeContext *ctx) {
  if (ctx == nullptr) {
    fail("null magnitude");
  }

  if (ctx->INTEGER_LITERAL() != nullptr) {
    return std::make_unique<IntLitExprNode>(ctx->INTEGER_LITERAL()->getText());
  }

  if (ctx->pathInExpression() != nullptr) {
    return std::make_unique<PathExprNode>(ctx->pathInExpression());
  }

  if (ctx->magnitude() != nullptr) {
    return buildMagnitude(ctx->magnitude());
  }

  fail("invalid magnitude `" + ctx->getText() + "`");
}

std::vector<NodePtr>
buildCallArguments(rx::RxParser::CallArgumentsContext *ctx) {
  std::vector<NodePtr> result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *expr : ctx->expression()) {
    result.push_back(ASTNode::BuildExpression(expr));
  }

  return result;
}

// . 后缀
NodePtr applyDotSuffix(NodePtr receiver, rx::RxParser::DotSuffixContext *ctx) {
  if (ctx == nullptr) {
    fail("null dot suffix");
  }

  if (ctx->pathExprSegment() != nullptr) {
    auto method = std::make_unique<MethodCallExprNode>();
    method->receiver = std::move(receiver);
    method->method = ASTNode::BuildExprPathSegment(ctx->pathExprSegment());
    method->args = buildCallArguments(ctx->callArguments());
    return method;
  }

  if (ctx->identifier() != nullptr) {
    auto field = std::make_unique<FieldExprNode>();
    field->receiver = std::move(receiver);
    field->name = identifierText(ctx->identifier());
    return field;
  }

  fail("invalid dot suffix `" + ctx->getText() + "`");
}

// (...) [...] .xxx
NodePtr applyPostfixSuffix(NodePtr base,
                           rx::RxParser::PostfixSuffixContext *ctx) {
  if (ctx == nullptr) {
    fail("null postfix suffix");
  }

  if (ctx->callArguments() != nullptr) {
    auto call = std::make_unique<CallExprNode>();
    call->function = std::move(base);
    call->args = buildCallArguments(ctx->callArguments());
    return call;
  }

  if (ctx->LBRACKET() != nullptr) {
    auto index = std::make_unique<IndexExprNode>();
    index->base = std::move(base);
    index->index = ASTNode::BuildExpression(ctx->expression());
    return index;
  }

  if (ctx->dotSuffix() != nullptr) {
    return applyDotSuffix(std::move(base), ctx->dotSuffix());
  }

  fail("invalid postfix suffix `" + ctx->getText() + "`");
}

NodePtr buildPostfixExpression(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    fail("null postfix expression");
  }

  NodePtr result;

  for (auto *child : ctx->children) {
    if (auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child)) {
      const auto index = rule->getRuleIndex();

      if (!result && isExpressionRule(index)) {
        result = ASTNode::BuildExpression(rule);
        continue;
      }

      if (auto *suffix =
              dynamic_cast<rx::RxParser::PostfixSuffixContext *>(rule)) {
        if (!result) {
          fail("postfix suffix appears before its base expression");
        }
        result = applyPostfixSuffix(std::move(result), suffix);
        continue;
      }

      // statementPostfixExpression has:expressionWithBlock dotSuffix
      // postfixSuffix*
      if (auto *dot = dynamic_cast<rx::RxParser::DotSuffixContext *>(rule)) {
        if (!result) {
          fail("dot suffix appears before its base expression");
        }
        result = applyDotSuffix(std::move(result), dot);
      }
    }
  }

  if (!result) {
    fail("cannot build postfix expression `" + ctx->getText() + "`");
  }

  return result;
}

// BlockExprNode IfExprNode LoopExprNode WhileExprNode
NodePtr
buildExpressionWithBlock(rx::RxParser::ExpressionWithBlockContext *ctx) {
  if (ctx == nullptr) {
    fail("null expression-with-block");
  }

  if (ctx->blockExpression() != nullptr && ctx->LOOP() == nullptr &&
      ctx->WHILE() == nullptr) {
    return std::make_unique<BlockExprNode>(ctx->blockExpression());
  }

  if (ctx->ifExpression() != nullptr) {
    return std::make_unique<IfExprNode>(ctx->ifExpression());
  }

  if (ctx->LOOP() != nullptr) {
    return std::make_unique<LoopExprNode>(ctx->blockExpression());
  }

  if (ctx->WHILE() != nullptr) {
    return std::make_unique<WhileExprNode>(ctx->conditionExpression(),
                                           ctx->blockExpression());
  }

  fail("invalid expression-with-block `" + ctx->getText() + "`");
}

NodePtr buildNonBlockPrimary(rx::RxParser::NonBlockPrimaryContext *ctx) {
  if (ctx == nullptr) {
    fail("null non-block primary");
  }

  if (ctx->literalExpression() != nullptr) {
    return ASTNode::BuildExpression(ctx->literalExpression());
  }

  if (ctx->pathInExpression() != nullptr) {
    if (ctx->LBRACE() == nullptr) {
      return std::make_unique<PathExprNode>(ctx->pathInExpression());
    }
    auto result = std::make_unique<StructExprNode>();
    result->structName =
        std::make_unique<PathExprNode>(ctx->pathInExpression());
    if (ctx->structExprFields() != nullptr) {
      for (auto *fieldCtx : ctx->structExprFields()->structExprField()) {
        StructExprField field;
        field.name = identifierText(fieldCtx->identifier());
        field.expr = ASTNode::BuildExpression(fieldCtx->expression());
        result->fields.push_back(std::move(field));
      }
    }
    return result;
  }

  if (ctx->LPAREN() != nullptr) {
    if (ctx->expression() != nullptr) {
      return ASTNode::BuildExpression(ctx->expression());
    }
    return std::make_unique<UnitLitExprNode>();
  }

  if (ctx->arrayExpression() != nullptr) {
    return ASTNode::BuildExpression(ctx->arrayExpression());
  }

  if (ctx->BREAK() != nullptr) {
    auto result = std::make_unique<BreakExprNode>();
    if (ctx->expression() != nullptr) {
      result->expr = ASTNode::BuildExpression(ctx->expression());
    }
    return result;
  }

  if (ctx->RETURN() != nullptr) {
    auto result = std::make_unique<ReturnExprNode>();
    if (ctx->expression() != nullptr) {
      result->expr = ASTNode::BuildExpression(ctx->expression());
    }
    return result;
  }

  if (ctx->CONTINUE() != nullptr) {
    return std::make_unique<ContinueExprNode>();
  }

  fail("invalid non-block primary `" + ctx->getText() + "`");
}

NodePtr buildConditionPrimaryWithoutBareBlock(
    rx::RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) {
  if (ctx == nullptr) {
    fail("null condition primary");
  }

  if (ctx->literalExpression() != nullptr) {
    return ASTNode::BuildExpression(ctx->literalExpression());
  }

  if (ctx->pathInExpression() != nullptr) {
    return std::make_unique<PathExprNode>(ctx->pathInExpression());
  }

  if (ctx->LPAREN() != nullptr) {
    if (ctx->expression() != nullptr) {
      return ASTNode::BuildExpression(ctx->expression());
    }
    return std::make_unique<UnitLitExprNode>();
  }

  if (ctx->arrayExpression() != nullptr) {
    return ASTNode::BuildExpression(ctx->arrayExpression());
  }

  if (ctx->ifExpression() != nullptr) {
    return std::make_unique<IfExprNode>(ctx->ifExpression());
  }

  if (ctx->LOOP() != nullptr) {
    return std::make_unique<LoopExprNode>(ctx->blockExpression());
  }

  if (ctx->WHILE() != nullptr) {
    return std::make_unique<WhileExprNode>(ctx->conditionExpression(),
                                           ctx->blockExpression());
  }

  if (ctx->BREAK() != nullptr) {
    auto result = std::make_unique<BreakExprNode>();
    if (ctx->conditionBreakExpression() != nullptr) {
      result->expr = ASTNode::BuildExpression(ctx->conditionBreakExpression());
    }
    return result;
  }

  if (ctx->RETURN() != nullptr) {
    auto result = std::make_unique<ReturnExprNode>();
    if (ctx->conditionExpression() != nullptr) {
      result->expr = ASTNode::BuildExpression(ctx->conditionExpression());
    }
    return result;
  }

  if (ctx->CONTINUE() != nullptr) {
    return std::make_unique<ContinueExprNode>();
  }

  fail("invalid condition primary `" + ctx->getText() + "`");
}

// dump helpers
void printIndent(std::ostream &out, std::size_t indent) {
  for (std::size_t i = 0; i < indent; ++i) {
    out << "  ";
  }
}

void dumpLifetime(std::ostream &out, const Lifetime &lifetime) {
  out << lifetime.name;
}

void dumpNode(const ASTNode *node, std::ostream &out, std::size_t indent) {
  if (node == nullptr) {
    printIndent(out, indent);
    out << "<null>\n";
    return;
  }
  node->dump(out, indent);
}

void dumpGenericParams(const GenericParams &params, std::ostream &out,
                       std::size_t indent) {
  if (params.lifetimeParams.empty()) {
    return;
  }

  printIndent(out, indent);
  out << "GenericParams\n";

  for (const auto &param : params.lifetimeParams) {
    printIndent(out, indent + 1);
    out << param.lifetime.name;
    if (!param.bounds.empty()) {
      out << " : ";
      for (std::size_t i = 0; i < param.bounds.size(); ++i) {
        if (i != 0) {
          out << " + ";
        }
        out << param.bounds[i].name;
      }
    }

    out << '\n';
  }
}

void dumpWhereClause(const WhereClause &where, std::ostream &out,
                     std::size_t indent) {
  if (where.predicates.empty()) {
    return;
  }

  printIndent(out, indent);
  out << "WhereClause\n";

  for (const auto &predicate : where.predicates) {
    printIndent(out, indent + 1);

    if (predicate.kind == WherePredicateKind::Lifetime) {
      out << "LifetimePredicate ";
      if (predicate.lifetime.has_value()) {
        out << predicate.lifetime->name;
      }
      out << '\n';
    } else {
      out << "TypePredicate\n";
      dumpNode(predicate.type.get(), out, indent + 2);
    }

    if (!predicate.bounds.empty()) {
      printIndent(out, indent + 2);
      out << "Bounds:";
      for (const auto &bound : predicate.bounds) {
        out << ' ' << bound.name;
      }
      out << '\n';
    }
  }
}

void dumpPathSegment(const PathSegment &segment, std::ostream &out) {
  out << segment.name;

  if (segment.genericArgs.empty()) {
    return;
  }

  out << '<';

  for (std::size_t i = 0; i < segment.genericArgs.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    const auto &arg = segment.genericArgs[i];
    if (arg.kind == GenericArgKind::Lifetime) {
      if (arg.lifetime.has_value()) {
        out << arg.lifetime->name;
      }
    } else {
      out << "<type>";
    }
  }

  out << '>';
}

} // namespace

// ASTNode factories
NodePtr ASTNode::BuildItem(rx::RxParser::ItemContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildItem received null");
  }

  if (ctx->useDeclaration() != nullptr) {
    return std::make_unique<UseItemNode>(ctx->useDeclaration());
  }

  if (ctx->functionDefinition() != nullptr) {
    return std::make_unique<FnItemNode>(ctx->functionDefinition());
  }

  if (ctx->structDefinition() != nullptr) {
    return std::make_unique<StructItemNode>(ctx->structDefinition());
  }

  if (ctx->constantItem() != nullptr) {
    return std::make_unique<ConstItemNode>(ctx->constantItem());
  }

  if (ctx->inherentImpl() != nullptr) {
    return std::make_unique<ImplItemNode>(ctx->inherentImpl());
  }

  fail("unknown item `" + ctx->getText() + "`");
}

NodePtr ASTNode::BuildAssociatedItem(rx::RxParser::AssociatedItemContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildAssociatedItem received null");
  }

  if (ctx->constantItem() != nullptr) {
    return std::make_unique<ConstItemNode>(ctx->constantItem());
  }

  if (ctx->functionDefinition() != nullptr) {
    return std::make_unique<FnItemNode>(ctx->functionDefinition());
  }

  fail("unknown associated item `" + ctx->getText() + "`");
}

NodePtr ASTNode::BuildStatement(rx::RxParser::StatementContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildStatement received null");
  }

  if (ctx->letStatement() != nullptr) {
    return std::make_unique<LetStmtNode>(ctx->letStatement());
  }

  if (ctx->expressionWithBlock() != nullptr ||
      ctx->statementExpression() != nullptr) {
    return std::make_unique<ExprStmtNode>(ctx);
  }

  // Empty statement `;`.
  if (ctx->SEMI() != nullptr) {
    return nullptr;
  }

  fail("unknown statement `" + ctx->getText() + "`");
}

NodePtr ASTNode::BuildExpression(antlr4::ParserRuleContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildExpression received null");
  }

  const std::size_t rule = ctx->getRuleIndex();

  // Blocks / control flow
  if (rule == rx::RxParser::RuleBlockExpression) {
    return std::make_unique<BlockExprNode>(
        static_cast<rx::RxParser::BlockExpressionContext *>(ctx));
  }

  if (rule == rx::RxParser::RuleExpressionWithBlock) {
    return buildExpressionWithBlock(
        static_cast<rx::RxParser::ExpressionWithBlockContext *>(ctx));
  }

  if (rule == rx::RxParser::RuleIfExpression) {
    return std::make_unique<IfExprNode>(
        static_cast<rx::RxParser::IfExpressionContext *>(ctx));
  }

  // Wrapper rules
  if (rule == rx::RxParser::RuleExpression ||
      rule == rx::RxParser::RuleConditionExpression ||
      rule == rx::RxParser::RuleConditionBreakExpression ||
      rule == rx::RxParser::RuleStatementExpression ||
      rule == rx::RxParser::RulePrimaryExpression) {
    auto *child = firstExpressionChild(ctx);
    if (child == nullptr) {
      fail("expression wrapper has no expression child `" + ctx->getText() +
           "`");
    }
    return BuildExpression(child);
  }

  // Assignment
  if (isAssignmentRule(rule)) {
    if (directAssignmentOperator(ctx) != nullptr) {
      return std::make_unique<AssignExprNode>(ctx);
    }

    auto *child = firstExpressionChild(ctx);
    if (child == nullptr) {
      fail("assignment wrapper has no operand `" + ctx->getText() + "`");
    }
    return BuildExpression(child);
  }

  if (isBinaryRule(rule)) {
    if (hasDirectBinaryOperator(ctx)) {
      return std::make_unique<BinaryExprNode>(ctx);
    }

    auto *child = firstExpressionChild(ctx);
    if (child == nullptr) {
      fail("binary wrapper has no operand `" + ctx->getText() + "`");
    }
    return BuildExpression(child);
  }

  // Cast
  if (isCastRule(rule)) {
    if (hasDirectText(ctx, "as")) {
      return std::make_unique<CastExprNode>(ctx);
    }

    auto *child = firstExpressionChild(ctx);
    if (child == nullptr) {
      fail("cast wrapper has no operand `" + ctx->getText() + "`");
    }
    return BuildExpression(child);
  }

  // Unary
  if (isUnaryRule(rule)) {
    if (directUnaryOperator(ctx) != nullptr) {
      return std::make_unique<UnaryExprNode>(ctx);
    }

    auto *child = firstExpressionChild(ctx);
    if (child == nullptr) {
      fail("unary wrapper has no operand `" + ctx->getText() + "`");
    }
    return BuildExpression(child);
  }

  // Postfix
  if (isPostfixRule(rule)) {
    return buildPostfixExpression(ctx);
  }

  // Primary expressions
  if (rule == rx::RxParser::RuleNonBlockPrimary) {
    return buildNonBlockPrimary(
        static_cast<rx::RxParser::NonBlockPrimaryContext *>(ctx));
  }

  if (rule == rx::RxParser::RuleConditionPrimary) {
    auto *condition = static_cast<rx::RxParser::ConditionPrimaryContext *>(ctx);

    if (condition->conditionPrimaryWithoutBareBlock() != nullptr) {
      return BuildExpression(condition->conditionPrimaryWithoutBareBlock());
    }

    if (condition->blockExpression() != nullptr) {
      return std::make_unique<BlockExprNode>(condition->blockExpression());
    }

    fail("invalid condition primary `" + ctx->getText() + "`");
  }

  if (rule == rx::RxParser::RuleConditionPrimaryWithoutBareBlock) {
    return buildConditionPrimaryWithoutBareBlock(
        static_cast<rx::RxParser::ConditionPrimaryWithoutBareBlockContext *>(
            ctx));
  }

  if (rule == rx::RxParser::RuleLiteralExpression) {
    auto *literal = static_cast<rx::RxParser::LiteralExpressionContext *>(ctx);

    if (literal->INTEGER_LITERAL() != nullptr) {
      return std::make_unique<IntLitExprNode>(
          literal->INTEGER_LITERAL()->getText());
    }

    if (literal->TRUE() != nullptr) {
      return std::make_unique<BoolLitExprNode>(true);
    }

    if (literal->FALSE() != nullptr) {
      return std::make_unique<BoolLitExprNode>(false);
    }

    fail("unknown literal `" + ctx->getText() + "`");
  }

  if (rule == rx::RxParser::RulePathInExpression) {
    return std::make_unique<PathExprNode>(
        static_cast<rx::RxParser::PathInExpressionContext *>(ctx));
  }

  if (rule == rx::RxParser::RuleArrayExpression) {
    auto *array = static_cast<rx::RxParser::ArrayExpressionContext *>(ctx);

    if (array->SEMI() != nullptr) {
      const auto expressions = array->expression();

      if (expressions.empty() || array->constValue() == nullptr) {
        fail("invalid repeat array `" + array->getText() + "`");
      }

      auto result = std::make_unique<ArrayRepeatNode>();
      result->element = BuildExpression(expressions.front());
      result->count = BuildConstValue(array->constValue());
      return result;
    }

    return std::make_unique<ArrayExprNode>(array);
  }

  if (rule == rx::RxParser::RuleConstValue) {
    return BuildConstValue(static_cast<rx::RxParser::ConstValueContext *>(ctx));
  }

  if (rule == rx::RxParser::RuleMagnitude) {
    return buildMagnitude(static_cast<rx::RxParser::MagnitudeContext *>(ctx));
  }

  fail("unsupported expression rule " + std::to_string(rule) + " (`" +
       ctx->getText() + "`)");
}

NodePtr ASTNode::BuildType(rx::RxParser::TypeRefContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildType received null");
  }

  // Parenthesized type.
  if (ctx->typeRef() != nullptr) {
    return BuildType(ctx->typeRef());
  }

  // Unit type ().
  if (ctx->LPAREN() != nullptr && ctx->typeRef() == nullptr) {
    return std::make_unique<UnitTypeNode>();
  }

  if (ctx->typePath() != nullptr) {
    return std::make_unique<PathTypeNode>(ctx->typePath());
  }

  if (ctx->referenceType() != nullptr) {
    return std::make_unique<ReferenceTypeNode>(ctx->referenceType());
  }

  if (ctx->arrayType() != nullptr) {
    return std::make_unique<ArrayTypeNode>(ctx->arrayType());
  }

  fail("unknown type `" + ctx->getText() + "`");
}

// 将 ClosedCast 四种分支分别映射回普通 AST Type
NodePtr ASTNode::BuildClosedCastType(rx::RxParser::ClosedCastTypeContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildClosedCastType received null");
  }

  // (Type) or ()
  if (ctx->LPAREN() != nullptr) {
    if (ctx->typeRef() != nullptr) {
      return BuildType(ctx->typeRef());
    }
    return std::make_unique<UnitTypeNode>();
  }

  // array
  if (ctx->arrayType() != nullptr) {
    return std::make_unique<ArrayTypeNode>(ctx->arrayType());
  }

  // &(...) / &&(...)
  if (ctx->closedCastType() != nullptr) {
    NodePtr inner = BuildClosedCastType(ctx->closedCastType());

    std::optional<Lifetime> lifetime;
    if (ctx->lifetime() != nullptr) {
      lifetime = BuildLifetime(ctx->lifetime());
    }

    const bool mutableRef = ctx->MUT() != nullptr;

    if (ctx->ANDAND() != nullptr) {
      // Grammar comment: ANDAND constructs TWO references; lifetime/MUT belong
      // to the inner.
      auto innerRef = std::make_unique<ReferenceTypeNode>();
      innerRef->type = std::move(inner);
      innerRef->lifetime = std::move(lifetime);
      innerRef->isMutable = mutableRef;

      auto outerRef = std::make_unique<ReferenceTypeNode>();
      outerRef->type = std::move(innerRef);
      outerRef->isMutable = false;
      return outerRef;
    }

    auto ref = std::make_unique<ReferenceTypeNode>();
    ref->type = std::move(inner);
    ref->lifetime = std::move(lifetime);
    ref->isMutable = mutableRef;
    return ref;
  }

  // (typePathSegment ::)* pathIdentSegment ::? genericArgs
  if (ctx->pathIdentSegment() != nullptr && ctx->genericArgs() != nullptr) {
    auto result = std::make_unique<PathTypeNode>();

    for (auto *segmentCtx : ctx->typePathSegment()) {
      result->segments.push_back(BuildTypePathSegment(segmentCtx));
    }

    PathSegment finalSegment;
    finalSegment.name = pathIdentText(ctx->pathIdentSegment());
    finalSegment.genericArgs = BuildGenericArgs(ctx->genericArgs());
    result->segments.push_back(std::move(finalSegment));

    return result;
  }

  fail("unknown closed cast type `" + ctx->getText() + "`");
}

NodePtr ASTNode::BuildConstValue(rx::RxParser::ConstValueContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildConstValue received null");
  }

  if (ctx->INTEGER_LITERAL() != nullptr) {
    return std::make_unique<IntLitExprNode>(ctx->INTEGER_LITERAL()->getText());
  }

  if (ctx->TRUE() != nullptr) {
    return std::make_unique<BoolLitExprNode>(true);
  }

  if (ctx->FALSE() != nullptr) {
    return std::make_unique<BoolLitExprNode>(false);
  }

  if (ctx->pathInExpression() != nullptr) {
    return std::make_unique<PathExprNode>(ctx->pathInExpression());
  }

  if (ctx->MINUS() != nullptr) {
    auto result = std::make_unique<UnaryExprNode>();
    result->op = "-";
    result->expr = buildMagnitude(ctx->magnitude());
    return result;
  }

  if (ctx->constValue() != nullptr) {
    return BuildConstValue(ctx->constValue());
  }

  fail("unknown const value `" + ctx->getText() + "`");
}

// Lifetime / generic / path helpers
Lifetime ASTNode::BuildLifetime(rx::RxParser::LifetimeContext *ctx) {
  if (ctx == nullptr || ctx->LIFETIME() == nullptr) {
    fail("invalid lifetime");
  }

  Lifetime result;
  result.name = ctx->LIFETIME()->getText();
  return result;
}

std::vector<Lifetime>
ASTNode::BuildLifetimeBounds(rx::RxParser::LifetimeBoundsContext *ctx) {
  std::vector<Lifetime> result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *lifetime : ctx->lifetime()) {
    result.push_back(BuildLifetime(lifetime));
  }

  return result;
}

std::vector<Lifetime>
ASTNode::BuildTypeParamBounds(rx::RxParser::TypeParamBoundsContext *ctx) {
  std::vector<Lifetime> result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *lifetime : ctx->lifetime()) {
    result.push_back(BuildLifetime(lifetime));
  }

  return result;
}

GenericParams
ASTNode::BuildGenericParams(rx::RxParser::GenericParamsContext *ctx) {
  GenericParams result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *paramCtx : ctx->lifetimeParam()) {
    LifetimeParam param;
    param.lifetime = BuildLifetime(paramCtx->lifetime());

    if (paramCtx->lifetimeBounds() != nullptr) {
      param.bounds = BuildLifetimeBounds(paramCtx->lifetimeBounds());
    }

    result.lifetimeParams.push_back(std::move(param));
  }

  return result;
}

WhereClause ASTNode::BuildWhereClause(rx::RxParser::WhereClauseContext *ctx) {
  WhereClause result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *itemCtx : ctx->whereClauseItem()) {
    WherePredicate predicate;

    if (itemCtx->lifetime() != nullptr) {
      predicate.kind = WherePredicateKind::Lifetime;
      predicate.lifetime = BuildLifetime(itemCtx->lifetime());
      predicate.bounds = BuildLifetimeBounds(itemCtx->lifetimeBounds());
    } else if (itemCtx->typeRef() != nullptr) {
      predicate.kind = WherePredicateKind::Type;
      predicate.type = BuildType(itemCtx->typeRef());
      predicate.bounds = BuildTypeParamBounds(itemCtx->typeParamBounds());
    } else {
      fail("invalid where predicate `" + itemCtx->getText() + "`");
    }

    result.predicates.push_back(std::move(predicate));
  }

  return result;
}

// lifetime | typeRef
GenericArg ASTNode::BuildGenericArg(rx::RxParser::GenericArgContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildGenericArg received null");
  }

  GenericArg result;

  if (ctx->lifetime() != nullptr) {
    result.kind = GenericArgKind::Lifetime;
    result.lifetime = BuildLifetime(ctx->lifetime());
    return result;
  }

  if (ctx->typeRef() != nullptr) {
    result.kind = GenericArgKind::Type;
    result.type = BuildType(ctx->typeRef());
    return result;
  }

  fail("invalid generic argument `" + ctx->getText() + "`");
}

std::vector<GenericArg>
ASTNode::BuildGenericArgs(rx::RxParser::GenericArgsContext *ctx) {
  std::vector<GenericArg> result;

  if (ctx == nullptr) {
    return result;
  }

  for (auto *argCtx : ctx->genericArg()) {
    result.push_back(BuildGenericArg(argCtx));
  }

  return result;
}

PathSegment
ASTNode::BuildTypePathSegment(rx::RxParser::TypePathSegmentContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildTypePathSegment received null");
  }

  PathSegment result;
  result.name = pathIdentText(ctx->pathIdentSegment());

  if (ctx->genericArgs() != nullptr) {
    result.genericArgs = BuildGenericArgs(ctx->genericArgs());
  }

  return result;
}

PathSegment
ASTNode::BuildExprPathSegment(rx::RxParser::PathExprSegmentContext *ctx) {
  if (ctx == nullptr) {
    fail("BuildExprPathSegment received null");
  }

  PathSegment result;
  result.name = pathIdentText(ctx->pathIdentSegment());

  if (ctx->genericArgs() != nullptr) {
    result.genericArgs = BuildGenericArgs(ctx->genericArgs());
  }

  return result;
}

// Root / item constructors
CrateNode::CrateNode(rx::RxParser::CrateContext *ctx)
    : ASTNode(ASTNodeType::Crate) {
  if (ctx == nullptr) {
    fail("CrateNode received null");
  }

  for (auto *itemCtx : ctx->item()) {
    items.push_back(BuildItem(itemCtx));
  }
}

UseItemNode::UseItemNode(rx::RxParser::UseDeclarationContext *ctx)
    : ASTNode(ASTNodeType::UseItem) {
  if (ctx == nullptr) {
    fail("UseItemNode received null");
  }

  text = ctx->getText();
}

FnItemNode::FnItemNode(rx::RxParser::FunctionDefinitionContext *ctx)
    : ASTNode(ASTNodeType::FnItem) {
  if (ctx == nullptr) {
    fail("FnItemNode received null");
  }

  name = identifierText(ctx->identifier());

  if (ctx->genericParams() != nullptr) {
    genericParams = BuildGenericParams(ctx->genericParams());
  }

  if (ctx->whereClause() != nullptr) {
    whereClause = BuildWhereClause(ctx->whereClause());
  }

  if (ctx->typeRef() != nullptr) {
    returnType = BuildType(ctx->typeRef());
  }

  if (ctx->functionParameters() != nullptr) {
    auto *params = ctx->functionParameters();

    // self 参数
    if (params->selfParam() != nullptr) {
      auto *selfCtx = params->selfParam();
      FunctionParam param;
      param.kind = FunctionParamKind::Self;
      param.name = "self";
      param.isMutable = selfCtx->MUT() != nullptr;
      param.isByReference = selfCtx->AMP() != nullptr;
      if (selfCtx->lifetime() != nullptr) {
        param.lifetime = BuildLifetime(selfCtx->lifetime());
      }
      // self 没有普通的 type
      param.type.reset();
      parameters.push_back(std::move(param));
    }

    // 普通参数
    for (auto *paramCtx : params->functionParam()) {
      FunctionParam param;
      param.kind = FunctionParamKind::Normal;
      auto *binding = paramCtx->identifierBinding();
      param.name = identifierText(binding->identifier());
      param.isMutable = binding->MUT() != nullptr;
      param.isByReference = false;
      param.lifetime.reset();
      param.type = BuildType(paramCtx->typeRef());
      parameters.push_back(std::move(param));
    }
  }

  body = std::make_unique<BlockExprNode>(ctx->blockExpression());
}

StructItemNode::StructItemNode(rx::RxParser::StructDefinitionContext *ctx)
    : ASTNode(ASTNodeType::StructItem) {
  if (ctx == nullptr) {
    fail("StructItemNode received null");
  }

  name = identifierText(ctx->identifier());

  if (ctx->genericParams() != nullptr) {
    genericParams = BuildGenericParams(ctx->genericParams());
  }

  if (ctx->whereClause() != nullptr) {
    whereClause = BuildWhereClause(ctx->whereClause());
  }

  for (auto *attribute : ctx->outerAttribute()) {
    for (auto *derive : attribute->deriveName()) {
      derives.push_back(derive->getText());
    }
  }

  for (auto *fieldCtx : ctx->structField()) {
    StructField field;
    field.name = fieldCtx->identifier()->getText();
    field.type = BuildType(fieldCtx->typeRef());
    fields.push_back(std::move(field));
  }
}

ConstItemNode::ConstItemNode(rx::RxParser::ConstantItemContext *ctx)
    : ASTNode(ASTNodeType::ConstItem) {
  if (ctx == nullptr) {
    fail("ConstItemNode received null");
  }

  name = identifierText(ctx->identifier());
  type = BuildType(ctx->typeRef());
  value = BuildConstValue(ctx->constValue());
}

ImplItemNode::ImplItemNode(rx::RxParser::InherentImplContext *ctx)
    : ASTNode(ASTNodeType::ImplItem) {
  if (ctx == nullptr) {
    fail("ImplItemNode received null");
  }

  if (ctx->genericParams() != nullptr) {
    genericParams = BuildGenericParams(ctx->genericParams());
  }

  if (ctx->whereClause() != nullptr) {
    whereClause = BuildWhereClause(ctx->whereClause());
  }

  type = BuildType(ctx->typeRef());

  for (auto *itemCtx : ctx->associatedItem()) {
    associatedItems.push_back(BuildAssociatedItem(itemCtx));
  }
}

// Statement constructors
LetStmtNode::LetStmtNode(rx::RxParser::LetStatementContext *ctx)
    : ASTNode(ASTNodeType::LetStmt) {
  if (ctx == nullptr) {
    fail("LetStmtNode received null");
  }

  auto *binding = ctx->identifierBinding();
  name = identifierText(binding->identifier());
  isMutable = binding->MUT() != nullptr;

  if (ctx->typeRef() != nullptr) {
    type = BuildType(ctx->typeRef());
  }

  expr = BuildExpression(ctx->expression());
}

ExprStmtNode::ExprStmtNode(antlr4::ParserRuleContext *ctx)
    : ASTNode(ASTNodeType::ExprStmt) {
  if (ctx == nullptr) {
    fail("ExprStmtNode received null");
  }

  if (auto *statement = dynamic_cast<rx::RxParser::StatementContext *>(ctx)) {
    hasSemicolon = statement->SEMI() != nullptr;

    if (statement->expressionWithBlock() != nullptr) {
      expr = BuildExpression(statement->expressionWithBlock());
      return;
    }

    if (statement->statementExpression() != nullptr) {
      expr = BuildExpression(statement->statementExpression());
      return;
    }
  }

  auto *child = firstExpressionChild(ctx);
  if (child != nullptr) {
    expr = BuildExpression(child);
  } else {
    expr = BuildExpression(ctx);
  }
}

// Basic expression constructors
UnitLitExprNode::UnitLitExprNode() : ASTNode(ASTNodeType::UnitLitExpr) {}

IntLitExprNode::IntLitExprNode(std::string rawValue_)
    : ASTNode(ASTNodeType::IntLitExpr), rawValue(std::move(rawValue_)) {}

BoolLitExprNode::BoolLitExprNode(bool value_)
    : ASTNode(ASTNodeType::BoolLitExpr), value(value_) {}

PathExprNode::PathExprNode(rx::RxParser::PathInExpressionContext *ctx)
    : ASTNode(ASTNodeType::PathExpr) {
  if (ctx == nullptr) {
    fail("PathExprNode received null");
  }

  for (auto *segmentCtx : ctx->pathExprSegment()) {
    segments.push_back(BuildExprPathSegment(segmentCtx));
  }
}

// Aggregate expressions
StructExprNode::StructExprNode() : ASTNode(ASTNodeType::StructExpr) {}

ArrayExprNode::ArrayExprNode(rx::RxParser::ArrayExpressionContext *ctx)
    : ASTNode(ASTNodeType::ArrayExpr) {
  if (ctx == nullptr) {
    fail("ArrayExprNode received null");
  }

  if (ctx->SEMI() != nullptr) {
    fail("ArrayExprNode cannot represent repeat-array syntax");
  }

  for (auto *exprCtx : ctx->expression()) {
    elements.push_back(BuildExpression(exprCtx));
  }
}

ArrayRepeatNode::ArrayRepeatNode() : ASTNode(ASTNodeType::ArrayRepeat) {}

// Unary / binary / assignment / cast
UnaryExprNode::UnaryExprNode() : ASTNode(ASTNodeType::UnaryExpr) {}

UnaryExprNode::UnaryExprNode(antlr4::ParserRuleContext *ctx)
    : ASTNode(ASTNodeType::UnaryExpr) {
  if (ctx == nullptr) {
    fail("UnaryExprNode received null");
  }

  auto *operatorCtx = directUnaryOperator(ctx);

  if (operatorCtx == nullptr) {
    fail("UnaryExprNode received a context without a unary operator");
  }

  op = operatorCtx->getText();

  // The recursive unary operand is the expression-rule child that is not
  // UnaryOperatorContext.
  for (auto *child : ctx->children) {
    auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child);
    if (rule == nullptr ||
        dynamic_cast<rx::RxParser::UnaryOperatorContext *>(rule) != nullptr) {
      continue;
    }

    if (isExpressionRule(rule->getRuleIndex())) {
      expr = BuildExpression(rule);
      break;
    }
  }

  if (!expr) {
    fail("unary expression has no operand `" + ctx->getText() + "`");
  }
}

BinaryExprNode::BinaryExprNode(antlr4::ParserRuleContext *ctx)
    : ASTNode(ASTNodeType::BinaryExpr) {
  if (ctx == nullptr) {
    fail("BinaryExprNode received null");
  }

  for (auto *child : ctx->children) {
    if (auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child)) {
      if (isExpressionRule(rule->getRuleIndex())) {
        operands.push_back(BuildExpression(rule));
        continue;
      }
    }

    if (child != nullptr && isBinaryOperatorText(child->getText())) {
      operators.push_back(child->getText());
    }
  }

  if (operands.size() < 2 || operators.size() + 1 != operands.size()) {
    fail("malformed binary expression `" + ctx->getText() + "`");
  }
}

AssignExprNode::AssignExprNode(antlr4::ParserRuleContext *ctx)
    : ASTNode(ASTNodeType::AssignExpr) {
  if (ctx == nullptr) {
    fail("AssignExprNode received null");
  }

  auto *operatorCtx = directAssignmentOperator(ctx);
  if (operatorCtx == nullptr) {
    fail("AssignExprNode received a context without assignment operator");
  }

  op = operatorCtx->getText();

  auto expressions = directExpressionChildren(ctx);

  if (expressions.size() != 2) {
    fail("assignment expression must have two direct operands `" +
         ctx->getText() + "`");
  }

  left = BuildExpression(expressions[0]);
  right = BuildExpression(expressions[1]);
}

CastExprNode::CastExprNode(antlr4::ParserRuleContext *ctx)
    : ASTNode(ASTNodeType::CastExpr) {
  if (ctx == nullptr) {
    fail("CastExprNode received null");
  }

  NodePtr base;

  for (auto *child : ctx->children) {
    auto *rule = dynamic_cast<antlr4::ParserRuleContext *>(child);
    if (rule == nullptr) {
      continue;
    }

    if (!base && isExpressionRule(rule->getRuleIndex())) {
      base = BuildExpression(rule);
      continue;
    }

    if (auto *typeRef = dynamic_cast<rx::RxParser::TypeRefContext *>(rule)) {
      types.push_back(BuildType(typeRef));
      continue;
    }

    if (auto *closedType =
            dynamic_cast<rx::RxParser::ClosedCastTypeContext *>(rule)) {
      types.push_back(BuildClosedCastType(closedType));
    }
  }

  if (!base || types.empty()) {
    fail("malformed cast expression `" + ctx->getText() + "`");
  }

  // Flatten: (x as T) as U into: expr = x types = [T, U]
  if (base->kind == ASTNodeType::CastExpr) {
    auto *previous = static_cast<CastExprNode *>(base.get());
    NodePtr originalExpr = std::move(previous->expr);
    NodeList previousTypes = std::move(previous->types);
    for (auto &type : types) {
      previousTypes.push_back(std::move(type));
    }
    expr = std::move(originalExpr);
    types = std::move(previousTypes);
  } else {
    expr = std::move(base);
  }
}

// Postfix expression constructors
CallExprNode::CallExprNode() : ASTNode(ASTNodeType::CallExpr) {}

MethodCallExprNode::MethodCallExprNode()
    : ASTNode(ASTNodeType::MethodCallExpr) {}

FieldExprNode::FieldExprNode() : ASTNode(ASTNodeType::FieldExpr) {}

IndexExprNode::IndexExprNode() : ASTNode(ASTNodeType::IndexExpr) {}

// Block / control-flow constructors
BlockExprNode::BlockExprNode(rx::RxParser::BlockExpressionContext *ctx)
    : ASTNode(ASTNodeType::BlockExpr) {
  if (ctx == nullptr) {
    fail("BlockExprNode received null");
  }

  for (auto *stmtCtx : ctx->statement()) {
    NodePtr statement = BuildStatement(stmtCtx);

    // Empty `;` is intentionally discarded.
    if (statement) {
      statements.push_back(std::move(statement));
    }
  }

  if (ctx->statementExpression() != nullptr) {
    tailExpr = BuildExpression(ctx->statementExpression());
  }
}

IfExprNode::IfExprNode(rx::RxParser::IfExpressionContext *ctx)
    : ASTNode(ASTNodeType::IfExpr) {
  if (ctx == nullptr) {
    fail("IfExprNode received null");
  }

  condition = BuildExpression(ctx->conditionExpression());

  const auto blocks = ctx->blockExpression();
  if (blocks.empty()) {
    fail("if expression has no if block");
  }

  ifBlock = std::make_unique<BlockExprNode>(blocks[0]);

  if (ctx->ifExpression() != nullptr) {
    elseBlock = std::make_unique<IfExprNode>(ctx->ifExpression());
  } else if (blocks.size() >= 2) {
    elseBlock = std::make_unique<BlockExprNode>(blocks[1]);
  }
}

LoopExprNode::LoopExprNode(rx::RxParser::BlockExpressionContext *ctx)
    : ASTNode(ASTNodeType::LoopExpr) {
  if (ctx == nullptr) {
    fail("LoopExprNode received null");
  }

  body = std::make_unique<BlockExprNode>(ctx);
}

WhileExprNode::WhileExprNode(
    rx::RxParser::ConditionExpressionContext *conditionCtx,
    rx::RxParser::BlockExpressionContext *bodyCtx)
    : ASTNode(ASTNodeType::WhileExpr) {
  if (conditionCtx == nullptr || bodyCtx == nullptr) {
    fail("WhileExprNode received null context");
  }

  condition = BuildExpression(conditionCtx);
  body = std::make_unique<BlockExprNode>(bodyCtx);
}

BreakExprNode::BreakExprNode() : ASTNode(ASTNodeType::BreakExpr) {}

ContinueExprNode::ContinueExprNode() : ASTNode(ASTNodeType::ContinueExpr) {}

ReturnExprNode::ReturnExprNode() : ASTNode(ASTNodeType::ReturnExpr) {}

// Type constructors
UnitTypeNode::UnitTypeNode() : ASTNode(ASTNodeType::UnitType) {}

PathTypeNode::PathTypeNode(rx::RxParser::TypePathContext *ctx)
    : ASTNode(ASTNodeType::PathType) {
  if (ctx == nullptr) {
    fail("PathTypeNode received null");
  }

  for (auto *segmentCtx : ctx->typePathSegment()) {
    segments.push_back(BuildTypePathSegment(segmentCtx));
  }
}

PathTypeNode::PathTypeNode() : ASTNode(ASTNodeType::PathType) {}

ReferenceTypeNode::ReferenceTypeNode(rx::RxParser::ReferenceTypeContext *ctx)
    : ASTNode(ASTNodeType::ReferenceType) {
  if (ctx == nullptr) {
    fail("RefTypeNode received null");
  }

  NodePtr referencedType = BuildType(ctx->typeRef());

  std::optional<Lifetime> parsedLifetime;
  if (ctx->lifetime() != nullptr) {
    parsedLifetime = BuildLifetime(ctx->lifetime());
  }

  const bool mutableRef = ctx->MUT() != nullptr;

  if (ctx->ANDAND() != nullptr) {
    // &&'a mut T == &(&'a mut T)
    auto inner = std::make_unique<ReferenceTypeNode>();
    inner->type = std::move(referencedType);
    inner->lifetime = std::move(parsedLifetime);
    inner->isMutable = mutableRef;

    type = std::move(inner);
    lifetime.reset();
    isMutable = false;
    return;
  }

  type = std::move(referencedType);
  lifetime = std::move(parsedLifetime);
  isMutable = mutableRef;
}

ReferenceTypeNode::ReferenceTypeNode() : ASTNode(ASTNodeType::ReferenceType) {}

ArrayTypeNode::ArrayTypeNode(rx::RxParser::ArrayTypeContext *ctx)
    : ASTNode(ASTNodeType::ArrayType) {
  if (ctx == nullptr) {
    fail("ArrayTypeNode received null");
  }

  type = BuildType(ctx->typeRef());
  length = BuildConstValue(ctx->constValue());
}

// Dump
void ASTNode::dump(std::ostream &out, std::size_t indent) const {
  printIndent(out, indent);

  switch (kind) {
  case ASTNodeType::Crate: {
    const auto &node = static_cast<const CrateNode &>(*this);
    out << "Crate\n";

    for (const auto &item : node.items) {
      dumpNode(item.get(), out, indent + 1);
    }
    return;
  }

  case ASTNodeType::UseItem: {
    const auto &node = static_cast<const UseItemNode &>(*this);
    out << "UseItem " << node.text << '\n';
    return;
  }

  case ASTNodeType::FnItem: {
    const auto &node = static_cast<const FnItemNode &>(*this);
    out << "FnItem " << node.name << '\n';

    dumpGenericParams(node.genericParams, out, indent + 1);
    dumpWhereClause(node.whereClause, out, indent + 1);

    if (node.returnType) {
      printIndent(out, indent + 1);
      out << "ReturnType\n";
      dumpNode(node.returnType.get(), out, indent + 2);
    }

    if (!node.parameters.empty()) {
      printIndent(out, indent + 1);
      out << "Parameters\n";

      for (const auto &param : node.parameters) {
        printIndent(out, indent + 2);

        if (param.kind == FunctionParamKind::Self) {
          out << "FunctionParam self";

          if (param.isByReference) {
            out << " &";
          }

          if (param.lifetime.has_value()) {
            out << param.lifetime->name;
          }

          if (param.isMutable) {
            out << " mut";
          }

          out << '\n';
        } else {
          out << "FunctionParam ";

          if (param.isMutable) {
            out << "mut ";
          }

          out << param.name << '\n';

          dumpNode(param.type.get(), out, indent + 3);
        }
      }
    }

    printIndent(out, indent + 1);
    out << "Body\n";
    dumpNode(node.body.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::StructItem: {
    const auto &node = static_cast<const StructItemNode &>(*this);
    out << "StructItem " << node.name << '\n';

    dumpGenericParams(node.genericParams, out, indent + 1);
    dumpWhereClause(node.whereClause, out, indent + 1);

    if (!node.derives.empty()) {
      printIndent(out, indent + 1);
      out << "Derives:";
      for (const auto &derive : node.derives) {
        out << ' ' << derive;
      }
      out << '\n';
    }

    for (const auto &field : node.fields) {
      printIndent(out, indent + 1);
      out << "Field " << field.name << '\n';
      dumpNode(field.type.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::ConstItem: {
    const auto &node = static_cast<const ConstItemNode &>(*this);
    out << "ConstItem " << node.name << '\n';

    printIndent(out, indent + 1);
    out << "Type\n";
    dumpNode(node.type.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Value\n";
    dumpNode(node.value.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::ImplItem: {
    const auto &node = static_cast<const ImplItemNode &>(*this);
    out << "ImplItem\n";

    dumpGenericParams(node.genericParams, out, indent + 1);
    dumpWhereClause(node.whereClause, out, indent + 1);

    printIndent(out, indent + 1);
    out << "TargetType\n";
    dumpNode(node.type.get(), out, indent + 2);

    for (const auto &item : node.associatedItems) {
      dumpNode(item.get(), out, indent + 1);
    }
    return;
  }

  case ASTNodeType::LetStmt: {
    const auto &node = static_cast<const LetStmtNode &>(*this);
    out << "LetStmt " << (node.isMutable ? "mut " : "") << node.name << '\n';

    if (node.type) {
      printIndent(out, indent + 1);
      out << "Type\n";
      dumpNode(node.type.get(), out, indent + 2);
    }

    printIndent(out, indent + 1);
    out << "Initializer\n";
    dumpNode(node.expr.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::ExprStmt: {
    const auto &node = static_cast<const ExprStmtNode &>(*this);
    out << "ExprStmt" << (node.hasSemicolon ? " ;" : "") << '\n';
    dumpNode(node.expr.get(), out, indent + 1);
    return;
  }

  case ASTNodeType::UnitLitExpr:
    out << "UnitLitExpr\n";
    return;

  case ASTNodeType::IntLitExpr: {
    const auto &node = static_cast<const IntLitExprNode &>(*this);
    out << "IntLitExpr " << node.rawValue << '\n';
    return;
  }

  case ASTNodeType::BoolLitExpr: {
    const auto &node = static_cast<const BoolLitExprNode &>(*this);
    out << "BoolLitExpr " << (node.value ? "true" : "false") << '\n';
    return;
  }

  case ASTNodeType::PathExpr: {
    const auto &node = static_cast<const PathExprNode &>(*this);
    out << "PathExpr ";

    for (std::size_t i = 0; i < node.segments.size(); ++i) {
      if (i != 0) {
        out << "::";
      }
      dumpPathSegment(node.segments[i], out);
    }

    out << '\n';
    return;
  }

  case ASTNodeType::StructExpr: {
    const auto &node = static_cast<const StructExprNode &>(*this);
    out << "StructExpr\n";

    printIndent(out, indent + 1);
    out << "StructName\n";
    dumpNode(node.structName.get(), out, indent + 2);

    for (const auto &field : node.fields) {
      printIndent(out, indent + 1);
      out << "Field " << field.name << '\n';
      dumpNode(field.expr.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::ArrayExpr: {
    const auto &node = static_cast<const ArrayExprNode &>(*this);
    out << "ArrayExpr\n";

    for (const auto &element : node.elements) {
      dumpNode(element.get(), out, indent + 1);
    }
    return;
  }

  case ASTNodeType::ArrayRepeat: {
    const auto &node = static_cast<const ArrayRepeatNode &>(*this);
    out << "ArrayRepeat\n";

    printIndent(out, indent + 1);
    out << "Element\n";
    dumpNode(node.element.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Count\n";
    dumpNode(node.count.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::UnaryExpr: {
    const auto &node = static_cast<const UnaryExprNode &>(*this);
    out << "UnaryExpr " << node.op << '\n';
    dumpNode(node.expr.get(), out, indent + 1);
    return;
  }

  case ASTNodeType::BinaryExpr: {
    const auto &node = static_cast<const BinaryExprNode &>(*this);
    out << "BinaryExpr\n";

    for (std::size_t i = 0; i < node.operands.size(); ++i) {
      dumpNode(node.operands[i].get(), out, indent + 1);

      if (i < node.operators.size()) {
        printIndent(out, indent + 1);
        out << "Operator " << node.operators[i] << '\n';
      }
    }
    return;
  }

  case ASTNodeType::AssignExpr: {
    const auto &node = static_cast<const AssignExprNode &>(*this);
    out << "AssignExpr " << node.op << '\n';

    printIndent(out, indent + 1);
    out << "Left\n";
    dumpNode(node.left.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Right\n";
    dumpNode(node.right.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::CastExpr: {
    const auto &node = static_cast<const CastExprNode &>(*this);
    out << "CastExpr\n";

    printIndent(out, indent + 1);
    out << "Expression\n";
    dumpNode(node.expr.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Types\n";
    for (const auto &type : node.types) {
      dumpNode(type.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::CallExpr: {
    const auto &node = static_cast<const CallExprNode &>(*this);
    out << "CallExpr\n";

    printIndent(out, indent + 1);
    out << "Function\n";
    dumpNode(node.function.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Arguments\n";
    for (const auto &arg : node.args) {
      dumpNode(arg.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::MethodCallExpr: {
    const auto &node = static_cast<const MethodCallExprNode &>(*this);
    out << "MethodCallExpr ";
    dumpPathSegment(node.method, out);
    out << '\n';

    printIndent(out, indent + 1);
    out << "Receiver\n";
    dumpNode(node.receiver.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Arguments\n";
    for (const auto &arg : node.args) {
      dumpNode(arg.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::FieldExpr: {
    const auto &node = static_cast<const FieldExprNode &>(*this);
    out << "FieldExpr " << node.name << '\n';
    dumpNode(node.receiver.get(), out, indent + 1);
    return;
  }

  case ASTNodeType::IndexExpr: {
    const auto &node = static_cast<const IndexExprNode &>(*this);
    out << "IndexExpr\n";

    printIndent(out, indent + 1);
    out << "Base\n";
    dumpNode(node.base.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Index\n";
    dumpNode(node.index.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::BlockExpr: {
    const auto &node = static_cast<const BlockExprNode &>(*this);
    out << "BlockExpr\n";

    for (const auto &statement : node.statements) {
      dumpNode(statement.get(), out, indent + 1);
    }

    if (node.tailExpr) {
      printIndent(out, indent + 1);
      out << "TailExpr\n";
      dumpNode(node.tailExpr.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::IfExpr: {
    const auto &node = static_cast<const IfExprNode &>(*this);
    out << "IfExpr\n";

    printIndent(out, indent + 1);
    out << "Condition\n";
    dumpNode(node.condition.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "IfBlock\n";
    dumpNode(node.ifBlock.get(), out, indent + 2);

    if (node.elseBlock) {
      printIndent(out, indent + 1);
      out << "Else\n";
      dumpNode(node.elseBlock.get(), out, indent + 2);
    }
    return;
  }

  case ASTNodeType::LoopExpr: {
    const auto &node = static_cast<const LoopExprNode &>(*this);
    out << "LoopExpr\n";
    dumpNode(node.body.get(), out, indent + 1);
    return;
  }

  case ASTNodeType::WhileExpr: {
    const auto &node = static_cast<const WhileExprNode &>(*this);
    out << "WhileExpr\n";

    printIndent(out, indent + 1);
    out << "Condition\n";
    dumpNode(node.condition.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Body\n";
    dumpNode(node.body.get(), out, indent + 2);
    return;
  }

  case ASTNodeType::BreakExpr: {
    const auto &node = static_cast<const BreakExprNode &>(*this);
    out << "BreakExpr\n";
    if (node.expr) {
      dumpNode(node.expr.get(), out, indent + 1);
    }
    return;
  }

  case ASTNodeType::ContinueExpr:
    out << "ContinueExpr\n";
    return;

  case ASTNodeType::ReturnExpr: {
    const auto &node = static_cast<const ReturnExprNode &>(*this);
    out << "ReturnExpr\n";
    if (node.expr) {
      dumpNode(node.expr.get(), out, indent + 1);
    }
    return;
  }

  case ASTNodeType::UnitType:
    out << "UnitType\n";
    return;

  case ASTNodeType::PathType: {
    const auto &node = static_cast<const PathTypeNode &>(*this);
    out << "PathType ";

    for (std::size_t i = 0; i < node.segments.size(); ++i) {
      if (i != 0) {
        out << "::";
      }
      dumpPathSegment(node.segments[i], out);
    }

    out << '\n';

    for (const auto &segment : node.segments) {
      for (const auto &arg : segment.genericArgs) {
        if (arg.kind == GenericArgKind::Type && arg.type) {
          dumpNode(arg.type.get(), out, indent + 1);
        }
      }
    }
    return;
  }

  case ASTNodeType::ReferenceType: {
    const auto &node = static_cast<const ReferenceTypeNode &>(*this);
    out << "ReferenceType";

    if (node.lifetime.has_value()) {
      out << ' ' << node.lifetime->name;
    }

    if (node.isMutable) {
      out << " mut";
    }

    out << '\n';
    dumpNode(node.type.get(), out, indent + 1);
    return;
  }

  case ASTNodeType::ArrayType: {
    const auto &node = static_cast<const ArrayTypeNode &>(*this);
    out << "ArrayType\n";

    printIndent(out, indent + 1);
    out << "ElementType\n";
    dumpNode(node.type.get(), out, indent + 2);

    printIndent(out, indent + 1);
    out << "Length\n";
    dumpNode(node.length.get(), out, indent + 2);
    return;
  }
  }
}
} // namespace rx::ast
