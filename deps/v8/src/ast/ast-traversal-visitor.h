// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_AST_TRAVERSAL_VISITOR_H_
#define V8_AST_AST_TRAVERSAL_VISITOR_H_

#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Traversal visitor
// - fully traverses the entire AST.
//
// Sub-class should parametrize AstTraversalVisitor with itself, e.g.:
//   class SpecificVisitor : public AstTraversalVisitor<SpecificVisitor> { ... }
//
// It invokes VisitNode on each AST node, before proceeding with its subtrees.
// It invokes VisitExpression (after VisitNode) on each AST node that is an
// expression, before proceeding with its subtrees.
// It proceeds with the subtrees only if these two methods return true.
// Sub-classes may override VisitNode and VisitExpressions, whose implementation
// is dummy here.  Or they may override the specific Visit* methods.

template <class Subclass>
class AstTraversalVisitor : public AstVisitor<Subclass> {
 public:
  explicit AstTraversalVisitor(Isolate* isolate, AstNode* root = nullptr);
  explicit AstTraversalVisitor(uintptr_t stack_limit, AstNode* root = nullptr);
  AstTraversalVisitor(const AstTraversalVisitor&) = delete;
  AstTraversalVisitor& operator=(const AstTraversalVisitor&) = delete;

  void Run() {
    DCHECK_NOT_NULL(root_);
    Visit(root_);
  }

  bool VisitNode(AstNode* node) { return true; }
  bool VisitExpression(Expression* node) { return true; }

  // Iteration left-to-right.
  void VisitDeclarations(Declaration::List* declarations);
  void VisitStatements(const ZonePtrList<Statement>* statements);

// Individual nodes
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 protected:
  int depth() const { return depth_; }

 private:
  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

  AstNode* root_;
  int depth_;
};

// ----------------------------------------------------------------------------
// Implementation of AstTraversalVisitor

#define PROCESS_NODE(node) do {                         \
    if (!(this->impl()->VisitNode(node))) return;       \
  } while (false)

#define PROCESS_EXPRESSION(node) do {                           \
    PROCESS_NODE(node);                                         \
    if (!(this->impl()->VisitExpression(node))) return;         \
  } while (false)

#define RECURSE(call)               \
  do {                              \
    DCHECK(!HasStackOverflow());    \
    this->impl()->call;             \
    if (HasStackOverflow()) return; \
  } while (false)

#define RECURSE_EXPRESSION(call)    \
  do {                              \
    DCHECK(!HasStackOverflow());    \
    ++depth_;                       \
    this->impl()->call;             \
    --depth_;                       \
    if (HasStackOverflow()) return; \
  } while (false)

template <class Subclass>
AstTraversalVisitor<Subclass>::AstTraversalVisitor(Isolate* isolate,
                                                   AstNode* root)
    : root_(root), depth_(0) {
  InitializeAstVisitor(isolate);
}

template <class Subclass>
AstTraversalVisitor<Subclass>::AstTraversalVisitor(uintptr_t stack_limit,
                                                   AstNode* root)
    : root_(root), depth_(0) {
  InitializeAstVisitor(stack_limit);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitDeclarations(
    Declaration::List* decls) {
  for (Declaration* decl : *decls) {
    RECURSE(Visit(decl));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitStatements(
    const ZonePtrList<Statement>* stmts) {
  for (int i = 0; i < stmts->length(); ++i) {
    Statement* stmt = stmts->at(i);
    RECURSE(Visit(stmt));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitVariableDeclaration(
    VariableDeclaration* decl) {
  PROCESS_NODE(decl);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitFunctionDeclaration(
    FunctionDeclaration* decl) {
  PROCESS_NODE(decl);
  RECURSE(Visit(decl->fun()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitBlock(Block* stmt) {
  PROCESS_NODE(stmt);
  if (stmt->scope() != nullptr) {
    RECURSE_EXPRESSION(VisitDeclarations(stmt->scope()->declarations()));
  }
  RECURSE(VisitStatements(stmt->statements()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitExpressionStatement(
    ExpressionStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitEmptyStatement(EmptyStatement* stmt) {}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->statement()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitIfStatement(IfStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->condition()));
  RECURSE(Visit(stmt->then_statement()));
  RECURSE(Visit(stmt->else_statement()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitContinueStatement(
    ContinueStatement* stmt) {
  PROCESS_NODE(stmt);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitBreakStatement(BreakStatement* stmt) {
  PROCESS_NODE(stmt);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitReturnStatement(
    ReturnStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitWithStatement(WithStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->expression()));
  RECURSE(Visit(stmt->statement()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSwitchStatement(
    SwitchStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->tag()));

  ZonePtrList<CaseClause>* clauses = stmt->cases();
  for (int i = 0; i < clauses->length(); ++i) {
    CaseClause* clause = clauses->at(i);
    if (!clause->is_default()) {
      Expression* label = clause->label();
      RECURSE(Visit(label));
    }
    const ZonePtrList<Statement>* stmts = clause->statements();
    RECURSE(VisitStatements(stmts));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitDoWhileStatement(
    DoWhileStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->body()));
  RECURSE(Visit(stmt->cond()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitWhileStatement(WhileStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->cond()));
  RECURSE(Visit(stmt->body()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitForStatement(ForStatement* stmt) {
  PROCESS_NODE(stmt);
  if (stmt->init() != nullptr) {
    RECURSE(Visit(stmt->init()));
  }
  if (stmt->cond() != nullptr) {
    RECURSE(Visit(stmt->cond()));
  }
  if (stmt->next() != nullptr) {
    RECURSE(Visit(stmt->next()));
  }
  RECURSE(Visit(stmt->body()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitForInStatement(ForInStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->each()));
  RECURSE(Visit(stmt->subject()));
  RECURSE(Visit(stmt->body()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitForOfStatement(ForOfStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->each()));
  RECURSE(Visit(stmt->subject()));
  RECURSE(Visit(stmt->body()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitTryCatchStatement(
    TryCatchStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->try_block()));
  RECURSE(Visit(stmt->catch_block()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitTryFinallyStatement(
    TryFinallyStatement* stmt) {
  PROCESS_NODE(stmt);
  RECURSE(Visit(stmt->try_block()));
  RECURSE(Visit(stmt->finally_block()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitDebuggerStatement(
    DebuggerStatement* stmt) {
  PROCESS_NODE(stmt);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitFunctionLiteral(
    FunctionLiteral* expr) {
  PROCESS_EXPRESSION(expr);
  DeclarationScope* scope = expr->scope();
  RECURSE_EXPRESSION(VisitDeclarations(scope->declarations()));
  // A lazily parsed function literal won't have a body.
  if (expr->scope()->was_lazily_parsed()) return;
  RECURSE_EXPRESSION(VisitStatements(expr->body()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitConditionalChain(
    ConditionalChain* expr) {
  PROCESS_EXPRESSION(expr);
  for (size_t i = 0; i < expr->conditional_chain_length(); ++i) {
    RECURSE_EXPRESSION(Visit(expr->condition_at(i)));
    RECURSE_EXPRESSION(Visit(expr->then_expression_at(i)));
  }
  RECURSE(Visit(expr->else_expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitConditional(Conditional* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->condition()));
  RECURSE_EXPRESSION(Visit(expr->then_expression()));
  RECURSE_EXPRESSION(Visit(expr->else_expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitVariableProxy(VariableProxy* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitLiteral(Literal* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitRegExpLiteral(RegExpLiteral* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitObjectLiteral(ObjectLiteral* expr) {
  PROCESS_EXPRESSION(expr);
  const ZonePtrList<ObjectLiteralProperty>* props = expr->properties();
  for (int i = 0; i < props->length(); ++i) {
    ObjectLiteralProperty* prop = props->at(i);
    RECURSE_EXPRESSION(Visit(prop->key()));
    RECURSE_EXPRESSION(Visit(prop->value()));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitArrayLiteral(ArrayLiteral* expr) {
  PROCESS_EXPRESSION(expr);
  const ZonePtrList<Expression>* values = expr->values();
  for (int i = 0; i < values->length(); ++i) {
    Expression* value = values->at(i);
    RECURSE_EXPRESSION(Visit(value));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitAssignment(Assignment* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->target()));
  RECURSE_EXPRESSION(Visit(expr->value()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCompoundAssignment(
    CompoundAssignment* expr) {
  VisitAssignment(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitYield(Yield* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitYieldStar(YieldStar* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitAwait(Await* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitThrow(Throw* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->exception()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitOptionalChain(OptionalChain* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitProperty(Property* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->obj()));
  RECURSE_EXPRESSION(Visit(expr->key()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCall(Call* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
  const ZonePtrList<Expression>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCallNew(CallNew* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
  const ZonePtrList<Expression>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCallRuntime(CallRuntime* expr) {
  PROCESS_EXPRESSION(expr);
  const ZonePtrList<Expression>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitUnaryOperation(UnaryOperation* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCountOperation(CountOperation* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitBinaryOperation(
    BinaryOperation* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->left()));
  RECURSE_EXPRESSION(Visit(expr->right()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitNaryOperation(NaryOperation* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->first()));
  for (size_t i = 0; i < expr->subsequent_length(); ++i) {
    RECURSE_EXPRESSION(Visit(expr->subsequent(i)));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitCompareOperation(
    CompareOperation* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->left()));
  RECURSE_EXPRESSION(Visit(expr->right()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitThisExpression(ThisExpression* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitClassLiteral(ClassLiteral* expr) {
  PROCESS_EXPRESSION(expr);
  if (expr->extends() != nullptr) {
    RECURSE_EXPRESSION(Visit(expr->extends()));
  }
  RECURSE_EXPRESSION(Visit(expr->constructor()));
  if (expr->static_initializer() != nullptr) {
    RECURSE_EXPRESSION(Visit(expr->static_initializer()));
  }
  if (expr->instance_members_initializer_function() != nullptr) {
    RECURSE_EXPRESSION(Visit(expr->instance_members_initializer_function()));
  }
  ZonePtrList<ClassLiteral::Property>* private_members =
      expr->private_members();
  for (int i = 0; i < private_members->length(); ++i) {
    ClassLiteralProperty* prop = private_members->at(i);
    RECURSE_EXPRESSION(Visit(prop->value()));
  }
  ZonePtrList<ClassLiteral::Property>* props = expr->public_members();
  for (int i = 0; i < props->length(); ++i) {
    ClassLiteralProperty* prop = props->at(i);
    if (!prop->key()->IsLiteral()) {
      RECURSE_EXPRESSION(Visit(prop->key()));
    }
    RECURSE_EXPRESSION(Visit(prop->value()));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitInitializeClassMembersStatement(
    InitializeClassMembersStatement* stmt) {
  PROCESS_NODE(stmt);
  ZonePtrList<ClassLiteral::Property>* props = stmt->fields();
  for (int i = 0; i < props->length(); ++i) {
    ClassLiteralProperty* prop = props->at(i);
    if (!prop->key()->IsLiteral()) {
      RECURSE(Visit(prop->key()));
    }
    RECURSE(Visit(prop->value()));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitInitializeClassStaticElementsStatement(
    InitializeClassStaticElementsStatement* stmt) {
  PROCESS_NODE(stmt);
  ZonePtrList<ClassLiteral::StaticElement>* elements = stmt->elements();
  for (int i = 0; i < elements->length(); ++i) {
    ClassLiteral::StaticElement* element = elements->at(i);
    switch (element->kind()) {
      case ClassLiteral::StaticElement::PROPERTY: {
        ClassLiteral::Property* prop = element->property();
        if (!prop->key()->IsLiteral()) {
          RECURSE(Visit(prop->key()));
        }
        RECURSE(Visit(prop->value()));
        break;
      }
      case ClassLiteral::StaticElement::STATIC_BLOCK:
        RECURSE(Visit(element->static_block()));
        break;
    }
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSpread(Spread* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitEmptyParentheses(
    EmptyParentheses* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitGetTemplateObject(
    GetTemplateObject* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitTemplateLiteral(
    TemplateLiteral* expr) {
  PROCESS_EXPRESSION(expr);
  for (Expression* sub : *expr->substitutions()) {
    RECURSE_EXPRESSION(Visit(sub));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitImportCallExpression(
    ImportCallExpression* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->specifier()));
  if (expr->import_options()) {
    RECURSE_EXPRESSION(Visit(expr->import_options()));
  }
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSuperPropertyReference(
    SuperPropertyReference* expr) {
  PROCESS_EXPRESSION(expr);
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSuperCallReference(
    SuperCallReference* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(VisitVariableProxy(expr->new_target_var()));
  RECURSE_EXPRESSION(VisitVariableProxy(expr->this_function_var()));
}

template <class Subclass>
void AstTraversalVisitor<Subclass>::VisitSuperCallForwardArgs(
    SuperCallForwardArgs* expr) {
  PROCESS_EXPRESSION(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}

#undef PROCESS_NODE
#undef PROCESS_EXPRESSION
#undef RECURSE_EXPRESSION
#undef RECURSE

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_AST_TRAVERSAL_VISITOR_H_
