// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_AST_H_
#define V8_TORQUE_AST_H_

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/base/optional.h"
#include "src/numbers/integer-literal.h"
#include "src/torque/constants.h"
#include "src/torque/source-positions.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

#define AST_EXPRESSION_NODE_KIND_LIST(V) \
  V(CallExpression)                      \
  V(CallMethodExpression)                \
  V(IntrinsicCallExpression)             \
  V(StructExpression)                    \
  V(LogicalOrExpression)                 \
  V(LogicalAndExpression)                \
  V(SpreadExpression)                    \
  V(ConditionalExpression)               \
  V(IdentifierExpression)                \
  V(StringLiteralExpression)             \
  V(IntegerLiteralExpression)            \
  V(FloatingPointLiteralExpression)      \
  V(FieldAccessExpression)               \
  V(ElementAccessExpression)             \
  V(DereferenceExpression)               \
  V(AssignmentExpression)                \
  V(IncrementDecrementExpression)        \
  V(NewExpression)                       \
  V(AssumeTypeImpossibleExpression)      \
  V(StatementExpression)                 \
  V(TryLabelExpression)

#define AST_TYPE_EXPRESSION_NODE_KIND_LIST(V) \
  V(BasicTypeExpression)                      \
  V(FunctionTypeExpression)                   \
  V(PrecomputedTypeExpression)                \
  V(UnionTypeExpression)

#define AST_STATEMENT_NODE_KIND_LIST(V) \
  V(BlockStatement)                     \
  V(ExpressionStatement)                \
  V(IfStatement)                        \
  V(WhileStatement)                     \
  V(ForLoopStatement)                   \
  V(BreakStatement)                     \
  V(ContinueStatement)                  \
  V(ReturnStatement)                    \
  V(DebugStatement)                     \
  V(AssertStatement)                    \
  V(TailCallStatement)                  \
  V(VarDeclarationStatement)            \
  V(GotoStatement)

#define AST_TYPE_DECLARATION_NODE_KIND_LIST(V) \
  V(AbstractTypeDeclaration)                   \
  V(TypeAliasDeclaration)                      \
  V(BitFieldStructDeclaration)                 \
  V(ClassDeclaration)                          \
  V(StructDeclaration)

#define AST_DECLARATION_NODE_KIND_LIST(V) \
  AST_TYPE_DECLARATION_NODE_KIND_LIST(V)  \
  V(GenericCallableDeclaration)           \
  V(GenericTypeDeclaration)               \
  V(SpecializationDeclaration)            \
  V(ExternConstDeclaration)               \
  V(NamespaceDeclaration)                 \
  V(ConstDeclaration)                     \
  V(CppIncludeDeclaration)                \
  V(TorqueMacroDeclaration)               \
  V(TorqueBuiltinDeclaration)             \
  V(ExternalMacroDeclaration)             \
  V(ExternalBuiltinDeclaration)           \
  V(ExternalRuntimeDeclaration)           \
  V(IntrinsicDeclaration)

#define AST_NODE_KIND_LIST(V)           \
  AST_EXPRESSION_NODE_KIND_LIST(V)      \
  AST_TYPE_EXPRESSION_NODE_KIND_LIST(V) \
  AST_STATEMENT_NODE_KIND_LIST(V)       \
  AST_DECLARATION_NODE_KIND_LIST(V)     \
  V(Identifier)                         \
  V(TryHandler)                         \
  V(ClassBody)

struct AstNode {
 public:
  enum class Kind {
#define ENUM_ITEM(name) k##name,
    AST_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
  };

  AstNode(Kind kind, SourcePosition pos) : kind(kind), pos(pos) {}
  virtual ~AstNode() = default;

  const Kind kind;
  SourcePosition pos;
};

struct AstNodeClassCheck {
  template <class T>
  static bool IsInstanceOf(AstNode* node);
};

// Boilerplate for most derived classes.
#define DEFINE_AST_NODE_LEAF_BOILERPLATE(T)  \
  static const Kind kKind = Kind::k##T;      \
  static T* cast(AstNode* node) {            \
    DCHECK_EQ(node->kind, kKind);            \
    return static_cast<T*>(node);            \
  }                                          \
  static T* DynamicCast(AstNode* node) {     \
    if (!node) return nullptr;               \
    if (node->kind != kKind) return nullptr; \
    return static_cast<T*>(node);            \
  }

// Boilerplate for classes with subclasses.
#define DEFINE_AST_NODE_INNER_BOILERPLATE(T)                       \
  static T* cast(AstNode* node) {                                  \
    DCHECK(AstNodeClassCheck::IsInstanceOf<T>(node));              \
    return static_cast<T*>(node);                                  \
  }                                                                \
  static T* DynamicCast(AstNode* node) {                           \
    if (!node) return nullptr;                                     \
    if (!AstNodeClassCheck::IsInstanceOf<T>(node)) return nullptr; \
    return static_cast<T*>(node);                                  \
  }

struct Expression : AstNode {
  Expression(Kind kind, SourcePosition pos) : AstNode(kind, pos) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Expression)

  using VisitCallback = std::function<void(Expression*)>;
  virtual void VisitAllSubExpressions(VisitCallback callback) {
    // TODO(szuend): Hoist this up to AstNode and make it a
    //               general Ast visitor.
  }
};

struct LocationExpression : Expression {
  LocationExpression(Kind kind, SourcePosition pos) : Expression(kind, pos) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(LocationExpression)
};

struct TypeExpression : AstNode {
  TypeExpression(Kind kind, SourcePosition pos) : AstNode(kind, pos) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(TypeExpression)
};

struct Declaration : AstNode {
  Declaration(Kind kind, SourcePosition pos) : AstNode(kind, pos) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Declaration)
};

struct Statement : AstNode {
  Statement(Kind kind, SourcePosition pos) : AstNode(kind, pos) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Statement)
};

class Namespace;

struct NamespaceDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(NamespaceDeclaration)
  NamespaceDeclaration(SourcePosition pos, std::string name,
                       std::vector<Declaration*> declarations)
      : Declaration(kKind, pos),
        declarations(std::move(declarations)),
        name(name) {}
  std::vector<Declaration*> declarations;
  std::string name;
};

struct EnumDescription {
  struct Entry {
    std::string name;
    std::string alias_entry;
    Entry(std::string name, std::string alias_entry)
        : name(std::move(name)), alias_entry(std::move(alias_entry)) {}
  };
  SourcePosition pos;
  std::string name;
  std::string constexpr_generates;
  bool is_open;
  std::vector<Entry> entries;

  EnumDescription(SourcePosition pos, std::string name,
                  std::string constexpr_generates, bool is_open,
                  std::vector<Entry> entries = {})
      : pos(std::move(pos)),
        name(std::move(name)),
        constexpr_generates(std::move(constexpr_generates)),
        is_open(is_open),
        entries(std::move(entries)) {}
};

class Ast {
 public:
  Ast() = default;

  std::vector<Declaration*>& declarations() { return declarations_; }
  const std::vector<Declaration*>& declarations() const {
    return declarations_;
  }
  template <class T>
  T* AddNode(std::unique_ptr<T> node) {
    T* result = node.get();
    nodes_.push_back(std::move(node));
    return result;
  }

  void DeclareImportForCurrentFile(SourceId import_id) {
    declared_imports_[CurrentSourcePosition::Get().source].insert(import_id);
  }

  void AddEnumDescription(EnumDescription description) {
    std::string name = description.name;
    DCHECK(!name.empty());
    auto f = [&](const auto& d) { return d.name == name; };
    USE(f);  // Suppress unused in release.
    DCHECK_EQ(
        std::find_if(enum_descriptions_.begin(), enum_descriptions_.end(), f),
        enum_descriptions_.end());
    enum_descriptions_.push_back(std::move(description));
  }

  std::vector<EnumDescription>& EnumDescriptions() {
    return enum_descriptions_;
  }

 private:
  std::vector<Declaration*> declarations_;
  std::vector<std::unique_ptr<AstNode>> nodes_;
  std::map<SourceId, std::set<SourceId>> declared_imports_;
  std::vector<EnumDescription> enum_descriptions_;
};

static const char* const kThisParameterName = "this";

// A Identifier is a string with a SourcePosition attached.
struct Identifier : AstNode {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(Identifier)
  Identifier(SourcePosition pos, std::string identifier)
      : AstNode(kKind, pos), value(std::move(identifier)) {}
  std::string value;
};

inline std::ostream& operator<<(std::ostream& os, Identifier* id) {
  return os << id->value;
}

struct IdentifierPtrValueEq {
  bool operator()(const Identifier* a, const Identifier* b) {
    return a->value < b->value;
  }
};

struct IdentifierExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IdentifierExpression)
  IdentifierExpression(SourcePosition pos,
                       std::vector<std::string> namespace_qualification,
                       Identifier* name, std::vector<TypeExpression*> args = {})
      : LocationExpression(kKind, pos),
        namespace_qualification(std::move(namespace_qualification)),
        name(name),
        generic_arguments(std::move(args)) {}
  IdentifierExpression(SourcePosition pos, Identifier* name,
                       std::vector<TypeExpression*> args = {})
      : IdentifierExpression(pos, {}, name, std::move(args)) {}
  bool IsThis() const { return name->value == kThisParameterName; }

  void VisitAllSubExpressions(VisitCallback callback) override {
    callback(this);
  }

  std::vector<std::string> namespace_qualification;
  Identifier* name;
  std::vector<TypeExpression*> generic_arguments;
};

struct IntrinsicCallExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IntrinsicCallExpression)
  IntrinsicCallExpression(SourcePosition pos, Identifier* name,
                          std::vector<TypeExpression*> generic_arguments,
                          std::vector<Expression*> arguments)
      : Expression(kKind, pos),
        name(name),
        generic_arguments(std::move(generic_arguments)),
        arguments(std::move(arguments)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    for (auto argument : arguments) {
      argument->VisitAllSubExpressions(callback);
    }
    callback(this);
  }

  Identifier* name;
  std::vector<TypeExpression*> generic_arguments;
  std::vector<Expression*> arguments;
};

struct CallMethodExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CallMethodExpression)
  CallMethodExpression(SourcePosition pos, Expression* target,
                       IdentifierExpression* method,
                       std::vector<Expression*> arguments,
                       std::vector<Identifier*> labels)
      : Expression(kKind, pos),
        target(target),
        method(method),
        arguments(std::move(arguments)),
        labels(std::move(labels)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    target->VisitAllSubExpressions(callback);
    method->VisitAllSubExpressions(callback);
    for (auto argument : arguments) {
      argument->VisitAllSubExpressions(callback);
    }
    callback(this);
  }

  Expression* target;
  IdentifierExpression* method;
  std::vector<Expression*> arguments;
  std::vector<Identifier*> labels;
};

struct CallExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CallExpression)
  CallExpression(SourcePosition pos, IdentifierExpression* callee,
                 std::vector<Expression*> arguments,
                 std::vector<Identifier*> labels)
      : Expression(kKind, pos),
        callee(callee),
        arguments(std::move(arguments)),
        labels(std::move(labels)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    callee->VisitAllSubExpressions(callback);
    for (auto argument : arguments) {
      argument->VisitAllSubExpressions(callback);
    }
    callback(this);
  }

  IdentifierExpression* callee;
  std::vector<Expression*> arguments;
  std::vector<Identifier*> labels;
};

struct NameAndExpression {
  Identifier* name;
  Expression* expression;
};

struct StructExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(StructExpression)
  StructExpression(SourcePosition pos, TypeExpression* type,
                   std::vector<NameAndExpression> initializers)
      : Expression(kKind, pos),
        type(type),
        initializers(std::move(initializers)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    for (auto& initializer : initializers) {
      initializer.expression->VisitAllSubExpressions(callback);
    }
    callback(this);
  }

  TypeExpression* type;
  std::vector<NameAndExpression> initializers;
};

struct LogicalOrExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(LogicalOrExpression)
  LogicalOrExpression(SourcePosition pos, Expression* left, Expression* right)
      : Expression(kKind, pos), left(left), right(right) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    left->VisitAllSubExpressions(callback);
    right->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* left;
  Expression* right;
};

struct LogicalAndExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(LogicalAndExpression)
  LogicalAndExpression(SourcePosition pos, Expression* left, Expression* right)
      : Expression(kKind, pos), left(left), right(right) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    left->VisitAllSubExpressions(callback);
    right->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* left;
  Expression* right;
};

struct SpreadExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(SpreadExpression)
  SpreadExpression(SourcePosition pos, Expression* spreadee)
      : Expression(kKind, pos), spreadee(spreadee) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    spreadee->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* spreadee;
};

struct ConditionalExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ConditionalExpression)
  ConditionalExpression(SourcePosition pos, Expression* condition,
                        Expression* if_true, Expression* if_false)
      : Expression(kKind, pos),
        condition(condition),
        if_true(if_true),
        if_false(if_false) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    condition->VisitAllSubExpressions(callback);
    if_true->VisitAllSubExpressions(callback);
    if_false->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* condition;
  Expression* if_true;
  Expression* if_false;
};

struct StringLiteralExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(StringLiteralExpression)
  StringLiteralExpression(SourcePosition pos, std::string literal)
      : Expression(kKind, pos), literal(std::move(literal)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    callback(this);
  }

  std::string literal;
};

struct IntegerLiteralExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IntegerLiteralExpression)
  IntegerLiteralExpression(SourcePosition pos, IntegerLiteral value)
      : Expression(kKind, pos), value(std::move(value)) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    callback(this);
  }

  IntegerLiteral value;
};

struct FloatingPointLiteralExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(FloatingPointLiteralExpression)
  FloatingPointLiteralExpression(SourcePosition pos, double value)
      : Expression(kKind, pos), value(value) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    callback(this);
  }

  double value;
};

struct ElementAccessExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ElementAccessExpression)
  ElementAccessExpression(SourcePosition pos, Expression* array,
                          Expression* index)
      : LocationExpression(kKind, pos), array(array), index(index) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    array->VisitAllSubExpressions(callback);
    index->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* array;
  Expression* index;
};

struct FieldAccessExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(FieldAccessExpression)
  FieldAccessExpression(SourcePosition pos, Expression* object,
                        Identifier* field)
      : LocationExpression(kKind, pos), object(object), field(field) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    object->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* object;
  Identifier* field;
};

struct DereferenceExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(DereferenceExpression)
  DereferenceExpression(SourcePosition pos, Expression* reference)
      : LocationExpression(kKind, pos), reference(reference) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    reference->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* reference;
};

struct AssignmentExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AssignmentExpression)
  AssignmentExpression(SourcePosition pos, Expression* location,
                       Expression* value)
      : AssignmentExpression(pos, location, base::nullopt, value) {}
  AssignmentExpression(SourcePosition pos, Expression* location,
                       base::Optional<std::string> op, Expression* value)
      : Expression(kKind, pos),
        location(location),
        op(std::move(op)),
        value(value) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    location->VisitAllSubExpressions(callback);
    value->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* location;
  base::Optional<std::string> op;
  Expression* value;
};

enum class IncrementDecrementOperator { kIncrement, kDecrement };

struct IncrementDecrementExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IncrementDecrementExpression)
  IncrementDecrementExpression(SourcePosition pos, Expression* location,
                               IncrementDecrementOperator op, bool postfix)
      : Expression(kKind, pos), location(location), op(op), postfix(postfix) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    location->VisitAllSubExpressions(callback);
    callback(this);
  }

  Expression* location;
  IncrementDecrementOperator op;
  bool postfix;
};

// This expression is only used in the desugaring of typeswitch, and it allows
// to bake in the static information that certain types are impossible at a
// certain position in the control flow.
// The result type is the type of {expression} minus the provided type.
struct AssumeTypeImpossibleExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AssumeTypeImpossibleExpression)
  AssumeTypeImpossibleExpression(SourcePosition pos,
                                 TypeExpression* excluded_type,
                                 Expression* expression)
      : Expression(kKind, pos),
        excluded_type(excluded_type),
        expression(expression) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    expression->VisitAllSubExpressions(callback);
    callback(this);
  }

  TypeExpression* excluded_type;
  Expression* expression;
};

struct NewExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(NewExpression)
  NewExpression(SourcePosition pos, TypeExpression* type,
                std::vector<NameAndExpression> initializers, bool pretenured,
                bool clear_padding)
      : Expression(kKind, pos),
        type(type),
        initializers(std::move(initializers)),
        pretenured(pretenured),
        clear_padding(clear_padding) {}

  void VisitAllSubExpressions(VisitCallback callback) override {
    for (auto& initializer : initializers) {
      initializer.expression->VisitAllSubExpressions(callback);
    }
    callback(this);
  }

  TypeExpression* type;
  std::vector<NameAndExpression> initializers;
  bool pretenured;
  bool clear_padding;
};

enum class ImplicitKind { kNoImplicit, kJSImplicit, kImplicit };

struct ParameterList {
  std::vector<Identifier*> names;
  std::vector<TypeExpression*> types;
  ImplicitKind implicit_kind = ImplicitKind::kNoImplicit;
  SourcePosition implicit_kind_pos = SourcePosition::Invalid();
  size_t implicit_count = 0;
  bool has_varargs = false;
  std::string arguments_variable = "";

  static ParameterList Empty() { return {}; }
  std::vector<TypeExpression*> GetImplicitTypes() {
    return std::vector<TypeExpression*>(types.begin(),
                                        types.begin() + implicit_count);
  }
  std::vector<TypeExpression*> GetExplicitTypes() {
    return std::vector<TypeExpression*>(types.begin() + implicit_count,
                                        types.end());
  }
};

struct BasicTypeExpression : TypeExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BasicTypeExpression)
  BasicTypeExpression(SourcePosition pos,
                      std::vector<std::string> namespace_qualification,
                      Identifier* name,
                      std::vector<TypeExpression*> generic_arguments)
      : TypeExpression(kKind, pos),
        namespace_qualification(std::move(namespace_qualification)),
        is_constexpr(IsConstexprName(name->value)),
        name(name),
        generic_arguments(std::move(generic_arguments)) {}
  BasicTypeExpression(SourcePosition pos, Identifier* name)
      : BasicTypeExpression(pos, {}, name, {}) {}
  std::vector<std::string> namespace_qualification;
  bool is_constexpr;
  Identifier* name;
  std::vector<TypeExpression*> generic_arguments;
};

struct FunctionTypeExpression : TypeExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(FunctionTypeExpression)
  FunctionTypeExpression(SourcePosition pos,
                         std::vector<TypeExpression*> parameters,
                         TypeExpression* return_type)
      : TypeExpression(kKind, pos),
        parameters(std::move(parameters)),
        return_type(return_type) {}
  std::vector<TypeExpression*> parameters;
  TypeExpression* return_type;
};

// A PrecomputedTypeExpression is never created directly by the parser. Later
// stages can use this to insert AST snippets where the type has already been
// resolved.
class Type;
struct PrecomputedTypeExpression : TypeExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(PrecomputedTypeExpression)
  PrecomputedTypeExpression(SourcePosition pos, const Type* type)
      : TypeExpression(kKind, pos), type(type) {}
  const Type* type;
};

struct UnionTypeExpression : TypeExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(UnionTypeExpression)
  UnionTypeExpression(SourcePosition pos, TypeExpression* a, TypeExpression* b)
      : TypeExpression(kKind, pos), a(a), b(b) {}
  TypeExpression* a;
  TypeExpression* b;
};

struct ExpressionStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExpressionStatement)
  ExpressionStatement(SourcePosition pos, Expression* expression)
      : Statement(kKind, pos), expression(expression) {}
  Expression* expression;
};

struct IfStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IfStatement)
  IfStatement(SourcePosition pos, bool is_constexpr, Expression* condition,
              Statement* if_true, base::Optional<Statement*> if_false)
      : Statement(kKind, pos),
        condition(condition),
        is_constexpr(is_constexpr),
        if_true(if_true),
        if_false(if_false) {}
  Expression* condition;
  bool is_constexpr;
  Statement* if_true;
  base::Optional<Statement*> if_false;
};

struct WhileStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(WhileStatement)
  WhileStatement(SourcePosition pos, Expression* condition, Statement* body)
      : Statement(kKind, pos), condition(condition), body(body) {}
  Expression* condition;
  Statement* body;
};

struct ReturnStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ReturnStatement)
  ReturnStatement(SourcePosition pos, base::Optional<Expression*> value)
      : Statement(kKind, pos), value(value) {}
  base::Optional<Expression*> value;
};

struct DebugStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(DebugStatement)
  enum class Kind { kUnreachable, kDebug };
  DebugStatement(SourcePosition pos, Kind kind)
      : Statement(kKind, pos), kind(kind) {}
  Kind kind;
};

struct AssertStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AssertStatement)
  enum class AssertKind { kDcheck, kCheck, kStaticAssert };
  AssertStatement(SourcePosition pos, AssertKind kind, Expression* expression,
                  std::string source)
      : Statement(kKind, pos),
        kind(kind),
        expression(expression),
        source(std::move(source)) {}
  AssertKind kind;
  Expression* expression;
  std::string source;
};

struct TailCallStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TailCallStatement)
  TailCallStatement(SourcePosition pos, CallExpression* call)
      : Statement(kKind, pos), call(call) {}
  CallExpression* call;
};

struct VarDeclarationStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(VarDeclarationStatement)
  VarDeclarationStatement(
      SourcePosition pos, bool const_qualified, Identifier* name,
      base::Optional<TypeExpression*> type,
      base::Optional<Expression*> initializer = base::nullopt)
      : Statement(kKind, pos),
        const_qualified(const_qualified),
        name(name),
        type(type),
        initializer(initializer) {}
  bool const_qualified;
  Identifier* name;
  base::Optional<TypeExpression*> type;
  base::Optional<Expression*> initializer;
};

struct BreakStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BreakStatement)
  explicit BreakStatement(SourcePosition pos) : Statement(kKind, pos) {}
};

struct ContinueStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ContinueStatement)
  explicit ContinueStatement(SourcePosition pos) : Statement(kKind, pos) {}
};

struct GotoStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(GotoStatement)
  GotoStatement(SourcePosition pos, Identifier* label,
                const std::vector<Expression*>& arguments)
      : Statement(kKind, pos), label(label), arguments(std::move(arguments)) {}
  Identifier* label;
  std::vector<Expression*> arguments;
};

struct ForLoopStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ForLoopStatement)
  ForLoopStatement(SourcePosition pos, base::Optional<Statement*> declaration,
                   base::Optional<Expression*> test,
                   base::Optional<Statement*> action, Statement* body)
      : Statement(kKind, pos),
        var_declaration(),
        test(std::move(test)),
        action(std::move(action)),
        body(std::move(body)) {
    if (declaration)
      var_declaration = VarDeclarationStatement::cast(*declaration);
  }
  base::Optional<VarDeclarationStatement*> var_declaration;
  base::Optional<Expression*> test;
  base::Optional<Statement*> action;
  Statement* body;
};

struct TryHandler : AstNode {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TryHandler)
  enum class HandlerKind { kCatch, kLabel };
  TryHandler(SourcePosition pos, HandlerKind handler_kind, Identifier* label,
             const ParameterList& parameters, Statement* body)
      : AstNode(kKind, pos),
        handler_kind(handler_kind),
        label(label),
        parameters(parameters),
        body(std::move(body)) {}
  HandlerKind handler_kind;
  Identifier* label;
  ParameterList parameters;
  Statement* body;
};

struct StatementExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(StatementExpression)
  StatementExpression(SourcePosition pos, Statement* statement)
      : Expression(kKind, pos), statement(statement) {}
  Statement* statement;
};

struct TryLabelExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TryLabelExpression)
  TryLabelExpression(SourcePosition pos, Expression* try_expression,
                     TryHandler* label_block)
      : Expression(kKind, pos),
        try_expression(try_expression),
        label_block(label_block) {}
  Expression* try_expression;
  TryHandler* label_block;
};

struct BlockStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BlockStatement)
  explicit BlockStatement(SourcePosition pos, bool deferred = false,
                          std::vector<Statement*> statements = {})
      : Statement(kKind, pos),
        deferred(deferred),
        statements(std::move(statements)) {}
  bool deferred;
  std::vector<Statement*> statements;
};

struct TypeDeclaration : Declaration {
  DEFINE_AST_NODE_INNER_BOILERPLATE(TypeDeclaration)
  TypeDeclaration(Kind kKind, SourcePosition pos, Identifier* name)
      : Declaration(kKind, pos), name(name) {}
  Identifier* name;
};

struct InstanceTypeConstraints {
  InstanceTypeConstraints() : value(-1), num_flags_bits(-1) {}
  int value;
  int num_flags_bits;
};

struct AbstractTypeDeclaration : TypeDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AbstractTypeDeclaration)
  AbstractTypeDeclaration(SourcePosition pos, Identifier* name,
                          AbstractTypeFlags flags,
                          base::Optional<TypeExpression*> extends,
                          base::Optional<std::string> generates)
      : TypeDeclaration(kKind, pos, name),
        flags(flags),
        extends(extends),
        generates(std::move(generates)) {
    CHECK_EQ(IsConstexprName(name->value),
             !!(flags & AbstractTypeFlag::kConstexpr));
  }

  bool IsConstexpr() const { return flags & AbstractTypeFlag::kConstexpr; }
  bool IsTransient() const { return flags & AbstractTypeFlag::kTransient; }

  AbstractTypeFlags flags;
  base::Optional<TypeExpression*> extends;
  base::Optional<std::string> generates;
};

struct TypeAliasDeclaration : TypeDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TypeAliasDeclaration)
  TypeAliasDeclaration(SourcePosition pos, Identifier* name,
                       TypeExpression* type)
      : TypeDeclaration(kKind, pos, name), type(type) {}
  TypeExpression* type;
};

struct NameAndTypeExpression {
  Identifier* name;
  TypeExpression* type;
};

struct ImplicitParameters {
  Identifier* kind;
  std::vector<NameAndTypeExpression> parameters;
};

struct StructFieldExpression {
  NameAndTypeExpression name_and_type;
  bool const_qualified;
};

struct BitFieldDeclaration {
  NameAndTypeExpression name_and_type;
  int num_bits;
};

enum class ConditionalAnnotationType {
  kPositive,
  kNegative,
};

struct ConditionalAnnotation {
  std::string condition;
  ConditionalAnnotationType type;
};

struct AnnotationParameter {
  std::string string_value;
  int int_value;
  bool is_int;
};

struct Annotation {
  Identifier* name;
  base::Optional<AnnotationParameter> param;
};

struct ClassFieldIndexInfo {
  // The expression that can compute how many items are in the indexed field.
  Expression* expr;

  // Whether the field was declared as optional, meaning it can only hold zero
  // or one values, and thus should not require an index expression to access.
  bool optional;
};

struct ClassFieldExpression {
  NameAndTypeExpression name_and_type;
  base::Optional<ClassFieldIndexInfo> index;
  std::vector<ConditionalAnnotation> conditions;
  bool custom_weak_marking;
  bool const_qualified;
  FieldSynchronization read_synchronization;
  FieldSynchronization write_synchronization;
};

struct LabelAndTypes {
  Identifier* name;
  std::vector<TypeExpression*> types;
};

using LabelAndTypesVector = std::vector<LabelAndTypes>;

struct CallableDeclaration : Declaration {
  CallableDeclaration(AstNode::Kind kind, SourcePosition pos,
                      bool transitioning, Identifier* name,
                      ParameterList parameters, TypeExpression* return_type,
                      LabelAndTypesVector labels)
      : Declaration(kind, pos),
        transitioning(transitioning),
        name(name),
        parameters(std::move(parameters)),
        return_type(return_type),
        labels(std::move(labels)) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(CallableDeclaration)
  bool transitioning;
  Identifier* name;
  ParameterList parameters;
  TypeExpression* return_type;
  LabelAndTypesVector labels;
};

struct MacroDeclaration : CallableDeclaration {
  DEFINE_AST_NODE_INNER_BOILERPLATE(MacroDeclaration)
  MacroDeclaration(AstNode::Kind kind, SourcePosition pos, bool transitioning,
                   Identifier* name, base::Optional<std::string> op,
                   ParameterList parameters, TypeExpression* return_type,
                   const LabelAndTypesVector& labels)
      : CallableDeclaration(kind, pos, transitioning, name,
                            std::move(parameters), return_type, labels),
        op(std::move(op)) {
    if (parameters.implicit_kind == ImplicitKind::kJSImplicit) {
      Error("Cannot use \"js-implicit\" with macros, use \"implicit\" instead.")
          .Position(parameters.implicit_kind_pos);
    }
  }
  base::Optional<std::string> op;
};

struct ExternalMacroDeclaration : MacroDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalMacroDeclaration)
  ExternalMacroDeclaration(SourcePosition pos, bool transitioning,
                           std::string external_assembler_name,
                           Identifier* name, base::Optional<std::string> op,
                           ParameterList parameters,
                           TypeExpression* return_type,
                           const LabelAndTypesVector& labels)
      : MacroDeclaration(kKind, pos, transitioning, name, std::move(op),
                         std::move(parameters), return_type, labels),
        external_assembler_name(std::move(external_assembler_name)) {}
  std::string external_assembler_name;
};

struct IntrinsicDeclaration : CallableDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IntrinsicDeclaration)
  IntrinsicDeclaration(SourcePosition pos, Identifier* name,
                       ParameterList parameters, TypeExpression* return_type)
      : CallableDeclaration(kKind, pos, false, name, std::move(parameters),
                            return_type, {}) {
    if (parameters.implicit_kind != ImplicitKind::kNoImplicit) {
      Error("Intinsics cannot have implicit parameters.");
    }
  }
};

struct TorqueMacroDeclaration : MacroDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TorqueMacroDeclaration)
  TorqueMacroDeclaration(SourcePosition pos, bool transitioning,
                         Identifier* name, base::Optional<std::string> op,
                         ParameterList parameters, TypeExpression* return_type,
                         const LabelAndTypesVector& labels, bool export_to_csa,
                         base::Optional<Statement*> body)
      : MacroDeclaration(kKind, pos, transitioning, name, std::move(op),
                         std::move(parameters), return_type, labels),
        export_to_csa(export_to_csa),
        body(body) {}
  bool export_to_csa;
  base::Optional<Statement*> body;
};

struct BuiltinDeclaration : CallableDeclaration {
  DEFINE_AST_NODE_INNER_BOILERPLATE(BuiltinDeclaration)
  BuiltinDeclaration(AstNode::Kind kind, SourcePosition pos,
                     bool javascript_linkage, bool transitioning,
                     Identifier* name, ParameterList parameters,
                     TypeExpression* return_type)
      : CallableDeclaration(kind, pos, transitioning, name,
                            std::move(parameters), return_type, {}),
        javascript_linkage(javascript_linkage) {
    if (parameters.implicit_kind == ImplicitKind::kJSImplicit &&
        !javascript_linkage) {
      Error(
          "\"js-implicit\" is for implicit parameters passed according to the "
          "JavaScript calling convention. Use \"implicit\" instead.");
    }
    if (parameters.implicit_kind == ImplicitKind::kImplicit &&
        javascript_linkage) {
      Error(
          "The JavaScript calling convention implicitly passes a fixed set of "
          "values. Use \"js-implicit\" to refer to those.")
          .Position(parameters.implicit_kind_pos);
    }
  }
  bool javascript_linkage;
};

struct ExternalBuiltinDeclaration : BuiltinDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalBuiltinDeclaration)
  ExternalBuiltinDeclaration(SourcePosition pos, bool transitioning,
                             bool javascript_linkage, Identifier* name,
                             ParameterList parameters,
                             TypeExpression* return_type)
      : BuiltinDeclaration(kKind, pos, javascript_linkage, transitioning, name,
                           std::move(parameters), return_type) {}
};

struct TorqueBuiltinDeclaration : BuiltinDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TorqueBuiltinDeclaration)
  TorqueBuiltinDeclaration(SourcePosition pos, bool transitioning,
                           bool javascript_linkage, Identifier* name,
                           ParameterList parameters,
                           TypeExpression* return_type,
                           bool has_custom_interface_descriptor,
                           base::Optional<Statement*> body)
      : BuiltinDeclaration(kKind, pos, javascript_linkage, transitioning, name,
                           std::move(parameters), return_type),
        has_custom_interface_descriptor(has_custom_interface_descriptor),
        body(body) {}
  bool has_custom_interface_descriptor;
  base::Optional<Statement*> body;
};

struct ExternalRuntimeDeclaration : CallableDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalRuntimeDeclaration)
  ExternalRuntimeDeclaration(SourcePosition pos, bool transitioning,
                             Identifier* name, ParameterList parameters,
                             TypeExpression* return_type)
      : CallableDeclaration(kKind, pos, transitioning, name, parameters,
                            return_type, {}) {}
};

struct ConstDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ConstDeclaration)
  ConstDeclaration(SourcePosition pos, Identifier* name, TypeExpression* type,
                   Expression* expression)
      : Declaration(kKind, pos),
        name(name),
        type(type),
        expression(expression) {}
  Identifier* name;
  TypeExpression* type;
  Expression* expression;
};

struct GenericParameter {
  Identifier* name;
  base::Optional<TypeExpression*> constraint;
};

using GenericParameters = std::vector<GenericParameter>;

// The AST re-shuffles generics from the concrete syntax:
// Instead of the generic parameters being part of a normal declaration,
// a declaration with generic parameters gets wrapped in a generic declaration,
// which holds the generic parameters. This corresponds to how you write
// templates in C++, with the template parameters coming before the declaration.

struct GenericCallableDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(GenericCallableDeclaration)
  GenericCallableDeclaration(SourcePosition pos,
                             GenericParameters generic_parameters,
                             CallableDeclaration* declaration)
      : Declaration(kKind, pos),
        generic_parameters(std::move(generic_parameters)),
        declaration(declaration) {}

  GenericParameters generic_parameters;
  CallableDeclaration* declaration;
};

struct GenericTypeDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(GenericTypeDeclaration)
  GenericTypeDeclaration(SourcePosition pos,
                         GenericParameters generic_parameters,
                         TypeDeclaration* declaration)
      : Declaration(kKind, pos),
        generic_parameters(std::move(generic_parameters)),
        declaration(declaration) {}

  GenericParameters generic_parameters;
  TypeDeclaration* declaration;
};

struct SpecializationDeclaration : CallableDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(SpecializationDeclaration)
  SpecializationDeclaration(SourcePosition pos, bool transitioning,
                            Identifier* name,
                            std::vector<TypeExpression*> generic_parameters,
                            ParameterList parameters,
                            TypeExpression* return_type,
                            LabelAndTypesVector labels, Statement* body)
      : CallableDeclaration(kKind, pos, transitioning, name,
                            std::move(parameters), return_type,
                            std::move(labels)),
        generic_parameters(std::move(generic_parameters)),
        body(body) {}
  std::vector<TypeExpression*> generic_parameters;
  Statement* body;
};

struct ExternConstDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternConstDeclaration)
  ExternConstDeclaration(SourcePosition pos, Identifier* name,
                         TypeExpression* type, std::string literal)
      : Declaration(kKind, pos),
        name(name),
        type(type),
        literal(std::move(literal)) {}
  Identifier* name;
  TypeExpression* type;
  std::string literal;
};

struct StructDeclaration : TypeDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(StructDeclaration)
  StructDeclaration(SourcePosition pos, StructFlags flags, Identifier* name,
                    std::vector<Declaration*> methods,
                    std::vector<StructFieldExpression> fields)
      : TypeDeclaration(kKind, pos, name),
        flags(flags),
        methods(std::move(methods)),
        fields(std::move(fields)) {}
  StructFlags flags;
  std::vector<Declaration*> methods;
  std::vector<StructFieldExpression> fields;
};

struct BitFieldStructDeclaration : TypeDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BitFieldStructDeclaration)
  BitFieldStructDeclaration(SourcePosition pos, Identifier* name,
                            TypeExpression* parent,
                            std::vector<BitFieldDeclaration> fields)
      : TypeDeclaration(kKind, pos, name),
        parent(parent),
        fields(std::move(fields)) {}
  TypeExpression* parent;
  std::vector<BitFieldDeclaration> fields;
};

struct ClassBody : AstNode {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ClassBody)
  ClassBody(SourcePosition pos, std::vector<Declaration*> methods,
            std::vector<ClassFieldExpression> fields)
      : AstNode(kKind, pos),
        methods(std::move(methods)),
        fields(std::move(fields)) {}
  std::vector<Declaration*> methods;
  std::vector<ClassFieldExpression> fields;
};

struct ClassDeclaration : TypeDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ClassDeclaration)
  ClassDeclaration(SourcePosition pos, Identifier* name, ClassFlags flags,
                   TypeExpression* super, base::Optional<std::string> generates,
                   std::vector<Declaration*> methods,
                   std::vector<ClassFieldExpression> fields,
                   InstanceTypeConstraints instance_type_constraints)
      : TypeDeclaration(kKind, pos, name),
        flags(flags),
        super(super),
        generates(std::move(generates)),
        methods(std::move(methods)),
        fields(std::move(fields)),
        instance_type_constraints(std::move(instance_type_constraints)) {}
  ClassFlags flags;
  TypeExpression* super;
  base::Optional<std::string> generates;
  std::vector<Declaration*> methods;
  std::vector<ClassFieldExpression> fields;
  InstanceTypeConstraints instance_type_constraints;
};

struct CppIncludeDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CppIncludeDeclaration)
  CppIncludeDeclaration(SourcePosition pos, std::string include_path)
      : Declaration(kKind, pos), include_path(std::move(include_path)) {}
  std::string include_path;
};

#define ENUM_ITEM(name)                     \
  case AstNode::Kind::k##name:              \
    return std::is_base_of<T, name>::value; \
    break;

template <class T>
bool AstNodeClassCheck::IsInstanceOf(AstNode* node) {
  switch (node->kind) {
    AST_NODE_KIND_LIST(ENUM_ITEM)
    default:
      UNIMPLEMENTED();
  }
  return true;
}

#undef ENUM_ITEM

inline bool IsDeferred(Statement* stmt) {
  if (auto* block = BlockStatement::DynamicCast(stmt)) {
    return block->deferred;
  }
  return false;
}

DECLARE_CONTEXTUAL_VARIABLE(CurrentAst, Ast);

template <class T, class... Args>
T* MakeNode(Args... args) {
  return CurrentAst::Get().AddNode(
      std::make_unique<T>(CurrentSourcePosition::Get(), std::move(args)...));
}

inline FieldAccessExpression* MakeFieldAccessExpression(Expression* object,
                                                        std::string field) {
  return MakeNode<FieldAccessExpression>(
      object, MakeNode<Identifier>(std::move(field)));
}

inline IdentifierExpression* MakeIdentifierExpression(
    std::vector<std::string> namespace_qualification, std::string name,
    std::vector<TypeExpression*> args = {}) {
  return MakeNode<IdentifierExpression>(std::move(namespace_qualification),
                                        MakeNode<Identifier>(std::move(name)),
                                        std::move(args));
}

inline IdentifierExpression* MakeIdentifierExpression(std::string name) {
  return MakeIdentifierExpression({}, std::move(name));
}

inline CallExpression* MakeCallExpression(
    IdentifierExpression* callee, std::vector<Expression*> arguments,
    std::vector<Identifier*> labels = {}) {
  return MakeNode<CallExpression>(callee, std::move(arguments),
                                  std::move(labels));
}

inline CallExpression* MakeCallExpression(
    std::string callee, std::vector<Expression*> arguments,
    std::vector<Identifier*> labels = {}) {
  return MakeCallExpression(MakeIdentifierExpression(std::move(callee)),
                            std::move(arguments), std::move(labels));
}

inline VarDeclarationStatement* MakeConstDeclarationStatement(
    std::string name, Expression* initializer) {
  return MakeNode<VarDeclarationStatement>(
      /*const_qualified=*/true, MakeNode<Identifier>(std::move(name)),
      base::Optional<TypeExpression*>{}, initializer);
}

inline BasicTypeExpression* MakeBasicTypeExpression(
    std::vector<std::string> namespace_qualification, Identifier* name,
    std::vector<TypeExpression*> generic_arguments = {}) {
  return MakeNode<BasicTypeExpression>(std::move(namespace_qualification), name,
                                       std::move(generic_arguments));
}

inline StructExpression* MakeStructExpression(
    TypeExpression* type, std::vector<NameAndExpression> initializers) {
  return MakeNode<StructExpression>(type, std::move(initializers));
}

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_AST_H_
