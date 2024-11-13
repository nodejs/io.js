// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_AST_FUNCTION_LITERAL_ID_REINDEXER_H_
#define V8_AST_AST_FUNCTION_LITERAL_ID_REINDEXER_H_

#include "src/ast/ast-traversal-visitor.h"

#ifdef DEBUG
#include <set>
#endif

namespace v8 {
namespace internal {

// Changes the ID of all FunctionLiterals in the given Expression by adding the
// given delta.
class AstFunctionLiteralIdReindexer final
    : public AstTraversalVisitor<AstFunctionLiteralIdReindexer> {
 public:
  AstFunctionLiteralIdReindexer(size_t stack_limit, int delta);
  AstFunctionLiteralIdReindexer(const AstFunctionLiteralIdReindexer&) = delete;
  AstFunctionLiteralIdReindexer& operator=(
      const AstFunctionLiteralIdReindexer&) = delete;
  ~AstFunctionLiteralIdReindexer();

  void Reindex(Expression* pattern);

  // AstTraversalVisitor implementation.
  void VisitFunctionLiteral(FunctionLiteral* lit);
  void VisitClassLiteral(ClassLiteral* lit);
  void VisitCall(Call* lit);

 private:
  int delta_;

#ifdef DEBUG
  // Visited set, only used in DCHECKs for verification.
  std::set<FunctionLiteral*> visited_;

  // Visit all function literals, checking if they have already been visited
  // (are in the visited set).
  void CheckVisited(Expression* expr);
#else
  void CheckVisited(Expression* expr) {}
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_AST_FUNCTION_LITERAL_ID_REINDEXER_H_
