// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/rewriter.h"

#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/zone/zone-list-inl.h"

// Use this macro when `replacement_` or other data produced by Visit() is used
// in a non-trivial way (needs to be valid) after calling Visit().
#define VISIT_AND_RETURN_IF_STACK_OVERFLOW(param) \
  Visit(param);                                   \
  if (CheckStackOverflow()) return;

namespace v8 {
namespace internal {

class Processor final : public AstVisitor<Processor> {
 public:
  Processor(uintptr_t stack_limit, DeclarationScope* closure_scope,
            Variable* result, AstValueFactory* ast_value_factory, Zone* zone)
      : result_(result),
        replacement_(nullptr),
        zone_(zone),
        closure_scope_(closure_scope),
        factory_(ast_value_factory, zone),
        result_assigned_(false),
        is_set_(false),
        breakable_(false) {
    DCHECK_EQ(closure_scope, closure_scope->GetClosureScope());
    InitializeAstVisitor(stack_limit);
  }

  Processor(Parser* parser, DeclarationScope* closure_scope, Variable* result,
            AstValueFactory* ast_value_factory, Zone* zone)
      : result_(result),
        replacement_(nullptr),
        zone_(zone),
        closure_scope_(closure_scope),
        factory_(ast_value_factory, zone_),
        result_assigned_(false),
        is_set_(false),
        breakable_(false) {
    DCHECK_EQ(closure_scope, closure_scope->GetClosureScope());
    InitializeAstVisitor(parser->stack_limit());
  }

  void Process(ZonePtrList<Statement>* statements);
  bool result_assigned() const { return result_assigned_; }

  Zone* zone() { return zone_; }
  DeclarationScope* closure_scope() { return closure_scope_; }
  AstNodeFactory* factory() { return &factory_; }

  // Returns ".result = value"
  Expression* SetResult(Expression* value) {
    result_assigned_ = true;
    VariableProxy* result_proxy = factory()->NewVariableProxy(result_);
    return factory()->NewAssignment(Token::kAssign, result_proxy, value,
                                    kNoSourcePosition);
  }

  // Inserts '.result = undefined' in front of the given statement.
  Statement* AssignUndefinedBefore(Statement* s);

 private:
  Variable* result_;

  // When visiting a node, we "return" a replacement for that node in
  // [replacement_].  In many cases this will just be the original node.
  Statement* replacement_;

  class V8_NODISCARD BreakableScope final {
   public:
    explicit BreakableScope(Processor* processor, bool breakable = true)
        : processor_(processor), previous_(processor->breakable_) {
      processor->breakable_ = processor->breakable_ || breakable;
    }

    ~BreakableScope() { processor_->breakable_ = previous_; }

   private:
    Processor* processor_;
    bool previous_;
  };

  Zone* zone_;
  DeclarationScope* closure_scope_;
  AstNodeFactory factory_;

  // Node visitors.
#define DEF_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  void VisitIterationStatement(IterationStatement* stmt);

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

  // We are not tracking result usage via the result_'s use
  // counts (we leave the accurate computation to the
  // usage analyzer). Instead we simple remember if
  // there was ever an assignment to result_.
  bool result_assigned_;

  // To avoid storing to .result all the time, we eliminate some of
  // the stores by keeping track of whether or not we're sure .result
  // will be overwritten anyway. This is a bit more tricky than what I
  // was hoping for.
  bool is_set_;

  bool breakable_;
};


Statement* Processor::AssignUndefinedBefore(Statement* s) {
  Expression* undef = factory()->NewUndefinedLiteral(kNoSourcePosition);
  Expression* assignment = SetResult(undef);
  Block* b = factory()->NewBlock(2, false);
  b->statements()->Add(
      factory()->NewExpressionStatement(assignment, kNoSourcePosition), zone());
  b->statements()->Add(s, zone());
  return b;
}

void Processor::Process(ZonePtrList<Statement>* statements) {
  // If we're in a breakable scope (named block, iteration, or switch), we walk
  // all statements. The last value producing statement before the break needs
  // to assign to .result. If we're not in a breakable scope, only the last
  // value producing statement in the block assigns to .result, so we can stop
  // early.
  for (int i = statements->length() - 1; i >= 0 && (breakable_ || !is_set_);
       --i) {
    Visit(statements->at(i));
    statements->Set(i, replacement_);
  }
}


void Processor::VisitBlock(Block* node) {
  // An initializer block is the rewritten form of a variable declaration
  // with initialization expressions. The initializer block contains the
  // list of assignments corresponding to the initialization expressions.
  // While unclear from the spec (ECMA-262, 3rd., 12.2), the value of
  // a variable declaration with initialization expression is 'undefined'
  // with some JS VMs: For instance, using smjs, print(eval('var x = 7'))
  // returns 'undefined'. To obtain the same behavior with v8, we need
  // to prevent rewriting in that case.
  if (!node->ignore_completion_value()) {
    BreakableScope scope(this, node->is_breakable());
    Process(node->statements());
  }
  replacement_ = node;
}


void Processor::VisitExpressionStatement(ExpressionStatement* node) {
  // Rewrite : <x>; -> .result = <x>;
  if (!is_set_) {
    node->set_expression(SetResult(node->expression()));
    is_set_ = true;
  }
  replacement_ = node;
}


void Processor::VisitIfStatement(IfStatement* node) {
  // Rewrite both branches.
  bool set_after = is_set_;

  Visit(node->then_statement());
  node->set_then_statement(replacement_);
  bool set_in_then = is_set_;

  is_set_ = set_after;
  Visit(node->else_statement());
  node->set_else_statement(replacement_);

  replacement_ = set_in_then && is_set_ ? node : AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitIterationStatement(IterationStatement* node) {
  // The statement may have to produce a value, so always assign undefined
  // before.
  // TODO(verwaest): Omit it if we know that there's no break/continue leaving
  // it early.
  DCHECK(breakable_ || !is_set_);
  BreakableScope scope(this);

  Visit(node->body());
  node->set_body(replacement_);

  replacement_ = AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitDoWhileStatement(DoWhileStatement* node) {
  VisitIterationStatement(node);
}


void Processor::VisitWhileStatement(WhileStatement* node) {
  VisitIterationStatement(node);
}


void Processor::VisitForStatement(ForStatement* node) {
  VisitIterationStatement(node);
}


void Processor::VisitForInStatement(ForInStatement* node) {
  VisitIterationStatement(node);
}


void Processor::VisitForOfStatement(ForOfStatement* node) {
  VisitIterationStatement(node);
}


void Processor::VisitTryCatchStatement(TryCatchStatement* node) {
  // Rewrite both try and catch block.
  bool set_after = is_set_;

  VISIT_AND_RETURN_IF_STACK_OVERFLOW(node->try_block());
  node->set_try_block(static_cast<Block*>(replacement_));
  bool set_in_try = is_set_;

  is_set_ = set_after;
  VISIT_AND_RETURN_IF_STACK_OVERFLOW(node->catch_block());
  node->set_catch_block(static_cast<Block*>(replacement_));

  replacement_ = is_set_ && set_in_try ? node : AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitTryFinallyStatement(TryFinallyStatement* node) {
  // Only rewrite finally if it could contain 'break' or 'continue'. Always
  // rewrite try.
  if (breakable_) {
    // Only set result before a 'break' or 'continue'.
    is_set_ = true;
    VISIT_AND_RETURN_IF_STACK_OVERFLOW(node->finally_block());
    node->set_finally_block(replacement_->AsBlock());
    CHECK_NOT_NULL(closure_scope());
    if (is_set_) {
      // Save .result value at the beginning of the finally block and restore it
      // at the end again: ".backup = .result; ...; .result = .backup" This is
      // necessary because the finally block does not normally contribute to the
      // completion value.
      Variable* backup = closure_scope()->NewTemporary(
          factory()->ast_value_factory()->dot_result_string());
      Expression* backup_proxy = factory()->NewVariableProxy(backup);
      Expression* result_proxy = factory()->NewVariableProxy(result_);
      Expression* save = factory()->NewAssignment(
          Token::kAssign, backup_proxy, result_proxy, kNoSourcePosition);
      Expression* restore = factory()->NewAssignment(
          Token::kAssign, result_proxy, backup_proxy, kNoSourcePosition);
      node->finally_block()->statements()->InsertAt(
          0, factory()->NewExpressionStatement(save, kNoSourcePosition),
          zone());
      node->finally_block()->statements()->Add(
          factory()->NewExpressionStatement(restore, kNoSourcePosition),
          zone());
    } else {
      // If is_set_ is false, it means the finally block has a 'break' or a
      // 'continue' and was not preceded by a statement that assigned to
      // .result. Try-finally statements return the abrupt completions from the
      // finally block, meaning this case should get an undefined.
      //
      // Since the finally block will definitely result in an abrupt completion,
      // there's no need to save and restore the .result.
      Expression* undef = factory()->NewUndefinedLiteral(kNoSourcePosition);
      Expression* assignment = SetResult(undef);
      node->finally_block()->statements()->InsertAt(
          0, factory()->NewExpressionStatement(assignment, kNoSourcePosition),
          zone());
    }
    // We can't tell whether the finally-block is guaranteed to set .result, so
    // reset is_set_ before visiting the try-block.
    is_set_ = false;
  }
  VISIT_AND_RETURN_IF_STACK_OVERFLOW(node->try_block());
  node->set_try_block(replacement_->AsBlock());

  replacement_ = is_set_ ? node : AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitSwitchStatement(SwitchStatement* node) {
  // The statement may have to produce a value, so always assign undefined
  // before.
  // TODO(verwaest): Omit it if we know that there's no break/continue leaving
  // it early.
  DCHECK(breakable_ || !is_set_);
  BreakableScope scope(this);
  // Rewrite statements in all case clauses.
  ZonePtrList<CaseClause>* clauses = node->cases();
  for (int i = clauses->length() - 1; i >= 0; --i) {
    CaseClause* clause = clauses->at(i);
    Process(clause->statements());
  }

  replacement_ = AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitContinueStatement(ContinueStatement* node) {
  is_set_ = false;
  replacement_ = node;
}


void Processor::VisitBreakStatement(BreakStatement* node) {
  is_set_ = false;
  replacement_ = node;
}


void Processor::VisitWithStatement(WithStatement* node) {
  Visit(node->statement());
  node->set_statement(replacement_);

  replacement_ = is_set_ ? node : AssignUndefinedBefore(node);
  is_set_ = true;
}


void Processor::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* node) {
  Visit(node->statement());
  node->set_statement(replacement_);
  replacement_ = node;
}


void Processor::VisitEmptyStatement(EmptyStatement* node) {
  replacement_ = node;
}


void Processor::VisitReturnStatement(ReturnStatement* node) {
  is_set_ = true;
  replacement_ = node;
}


void Processor::VisitDebuggerStatement(DebuggerStatement* node) {
  replacement_ = node;
}

void Processor::VisitInitializeClassMembersStatement(
    InitializeClassMembersStatement* node) {
  replacement_ = node;
}

void Processor::VisitInitializeClassStaticElementsStatement(
    InitializeClassStaticElementsStatement* node) {
  replacement_ = node;
}

// Expressions are never visited.
#define DEF_VISIT(type)                                         \
  void Processor::Visit##type(type* expr) { UNREACHABLE(); }
EXPRESSION_NODE_LIST(DEF_VISIT)
#undef DEF_VISIT


// Declarations are never visited.
#define DEF_VISIT(type) \
  void Processor::Visit##type(type* expr) { UNREACHABLE(); }
DECLARATION_NODE_LIST(DEF_VISIT)
#undef DEF_VISIT


// Assumes code has been parsed.  Mutates the AST, so the AST should not
// continue to be used in the case of failure.
bool Rewriter::Rewrite(ParseInfo* info) {
  RCS_SCOPE(info->runtime_call_stats(),
            RuntimeCallCounterId::kCompileRewriteReturnResult,
            RuntimeCallStats::kThreadSpecific);

  FunctionLiteral* function = info->literal();
  DCHECK_NOT_NULL(function);
  Scope* scope = function->scope();
  DCHECK_NOT_NULL(scope);
  DCHECK_EQ(scope, scope->GetClosureScope());

  if (scope->is_repl_mode_scope()) return true;
  if (!(scope->is_script_scope() || scope->is_eval_scope() ||
        scope->is_module_scope())) {
    return true;
  }

  ZonePtrList<Statement>* body = function->body();
  return RewriteBody(info, scope, body).has_value();
}

base::Optional<VariableProxy*> Rewriter::RewriteBody(
    ParseInfo* info, Scope* scope, ZonePtrList<Statement>* body) {
  DisallowGarbageCollection no_gc;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  DCHECK_IMPLIES(scope->is_module_scope(), !body->is_empty());
  if (!body->is_empty()) {
    Variable* result = scope->AsDeclarationScope()->NewTemporary(
        info->ast_value_factory()->dot_result_string());
    Processor processor(info->stack_limit(), scope->AsDeclarationScope(),
                        result, info->ast_value_factory(), info->zone());
    processor.Process(body);

    DCHECK_IMPLIES(scope->is_module_scope(), processor.result_assigned());
    if (processor.result_assigned()) {
      int pos = kNoSourcePosition;
      VariableProxy* result_value =
          processor.factory()->NewVariableProxy(result, pos);
      if (!info->flags().is_repl_mode()) {
        Statement* result_statement =
            processor.factory()->NewReturnStatement(result_value, pos);
        body->Add(result_statement, info->zone());
      }
      return result_value;
    }

    if (processor.HasStackOverflow()) {
      info->pending_error_handler()->set_stack_overflow();
      return base::nullopt;
    }
  }
  return nullptr;
}

#undef VISIT_AND_RETURN_IF_STACK_OVERFLOW

}  // namespace internal
}  // namespace v8
