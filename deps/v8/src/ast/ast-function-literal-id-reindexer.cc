// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast-function-literal-id-reindexer.h"

#include "src/ast/ast.h"

namespace v8 {
namespace internal {

AstFunctionLiteralIdReindexer::AstFunctionLiteralIdReindexer(size_t stack_limit,
                                                             int delta)
    : AstTraversalVisitor(stack_limit), delta_(delta) {}

AstFunctionLiteralIdReindexer::~AstFunctionLiteralIdReindexer() = default;

void AstFunctionLiteralIdReindexer::Reindex(Expression* pattern) {
#ifdef DEBUG
  visited_.clear();
#endif
  Visit(pattern);
  CheckVisited(pattern);
}

void AstFunctionLiteralIdReindexer::VisitFunctionLiteral(FunctionLiteral* lit) {
  // Make sure we're not already in the visited set.
  DCHECK(visited_.insert(lit).second);

  AstTraversalVisitor::VisitFunctionLiteral(lit);
  lit->set_function_literal_id(lit->function_literal_id() + delta_);
}

void AstFunctionLiteralIdReindexer::VisitCall(Call* expr) {
  AstTraversalVisitor::VisitCall(expr);
  if (expr->is_possibly_eval()) {
    expr->adjust_eval_scope_info_index(delta_);
  }
}

void AstFunctionLiteralIdReindexer::VisitClassLiteral(ClassLiteral* expr) {
  // Manually visit the class literal so that we can change the property walk.
  // This should be kept in-sync with AstTraversalVisitor::VisitClassLiteral.

  if (expr->extends() != nullptr) {
    Visit(expr->extends());
  }
  Visit(expr->constructor());
  if (expr->static_initializer() != nullptr) {
    Visit(expr->static_initializer());
  }
  if (expr->instance_members_initializer_function() != nullptr) {
    Visit(expr->instance_members_initializer_function());
  }
  ZonePtrList<ClassLiteral::Property>* private_members =
      expr->private_members();
  for (int i = 0; i < private_members->length(); ++i) {
    ClassLiteralProperty* prop = private_members->at(i);

    // Private fields have their key and value present in
    // instance_members_initializer_function, so they will
    // already have been visited.
    if (prop->kind() == ClassLiteralProperty::Kind::FIELD) {
      CheckVisited(prop->value());
    } else {
      Visit(prop->value());
    }
  }
  ZonePtrList<ClassLiteral::Property>* props = expr->public_members();
  for (int i = 0; i < props->length(); ++i) {
    ClassLiteralProperty* prop = props->at(i);

    // Public fields with computed names have their key
    // and value present in instance_members_initializer_function, so they will
    // already have been visited.
    if (prop->is_computed_name() &&
        prop->kind() == ClassLiteralProperty::Kind::FIELD) {
      if (!prop->key()->IsLiteral()) {
        CheckVisited(prop->key());
      }
      CheckVisited(prop->value());
    } else {
      if (!prop->key()->IsLiteral()) {
        Visit(prop->key());
      }
      Visit(prop->value());
    }
  }
}

#ifdef DEBUG
namespace {

class AstFunctionLiteralIdReindexChecker final
    : public AstTraversalVisitor<AstFunctionLiteralIdReindexChecker> {
 public:
  AstFunctionLiteralIdReindexChecker(size_t stack_limit,
                                     const std::set<FunctionLiteral*>* visited)
      : AstTraversalVisitor(stack_limit), visited_(visited) {}

  void VisitFunctionLiteral(FunctionLiteral* lit) {
    // TODO(leszeks): It would be nice to print the unvisited function literal
    // here, but that requires more advanced DCHECK support with formatting.
    DCHECK(visited_->find(lit) != visited_->end());
  }

 private:
  const std::set<FunctionLiteral*>* visited_;
};

}  // namespace

void AstFunctionLiteralIdReindexer::CheckVisited(Expression* expr) {
  AstFunctionLiteralIdReindexChecker(stack_limit(), &visited_).Visit(expr);
}
#endif

}  // namespace internal
}  // namespace v8
