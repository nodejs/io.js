// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSER_BASE_H_
#define V8_PARSING_PARSER_BASE_H_

#include <stdint.h>

#include <optional>
#include <utility>
#include <vector>

#include "src/ast/ast-source-ranges.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/base/flags.h"
#include "src/base/hashmap.h"
#include "src/base/pointer-with-payload.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/function-kind.h"
#include "src/parsing/expression-scope.h"
#include "src/parsing/func-name-inferrer.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/scanner.h"
#include "src/parsing/token.h"
#include "src/regexp/regexp.h"
#include "src/zone/zone-chunk-list.h"

namespace v8::internal {

class PreParserIdentifier;

enum FunctionNameValidity {
  kFunctionNameIsStrictReserved,
  kSkipFunctionNameCheck,
  kFunctionNameValidityUnknown
};

enum AllowLabelledFunctionStatement {
  kAllowLabelledFunctionStatement,
  kDisallowLabelledFunctionStatement,
};

enum ParsingArrowHeadFlag { kCertainlyNotArrowHead, kMaybeArrowHead };

enum class ParseFunctionFlag : uint8_t {
  kIsNormal = 0,
  kIsGenerator = 1 << 0,
  kIsAsync = 1 << 1
};

using ParseFunctionFlags = base::Flags<ParseFunctionFlag>;

struct FormalParametersBase {
  explicit FormalParametersBase(DeclarationScope* scope) : scope(scope) {}

  int num_parameters() const {
    // Don't include the rest parameter into the function's formal parameter
    // count (esp. the SharedFunctionInfo::internal_formal_parameter_count,
    // which says whether we need to create an inlined arguments frame).
    return arity - has_rest;
  }

  void UpdateArityAndFunctionLength(bool is_optional, bool is_rest) {
    if (!is_optional && !is_rest && function_length == arity) {
      ++function_length;
    }
    ++arity;
  }

  DeclarationScope* scope;
  bool has_rest = false;
  bool is_simple = true;
  int function_length = 0;
  int arity = 0;
};

// Stack-allocated scope to collect source ranges from the parser.
class V8_NODISCARD SourceRangeScope final {
 public:
  SourceRangeScope(const Scanner* scanner, SourceRange* range)
      : scanner_(scanner), range_(range) {
    range_->start = scanner->peek_location().beg_pos;
    DCHECK_NE(range_->start, kNoSourcePosition);
    DCHECK_EQ(range_->end, kNoSourcePosition);
  }

  ~SourceRangeScope() {
    DCHECK_EQ(kNoSourcePosition, range_->end);
    range_->end = scanner_->location().end_pos;
    DCHECK_NE(range_->end, kNoSourcePosition);
  }

 private:
  const Scanner* scanner_;
  SourceRange* range_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(SourceRangeScope);
};

// ----------------------------------------------------------------------------
// The RETURN_IF_PARSE_ERROR macro is a convenient macro to enforce error
// handling for functions that may fail (by returning if there was an parser
// error).
//
// Usage:
//     foo = ParseFoo(); // may fail
//     RETURN_IF_PARSE_ERROR
//
//     SAFE_USE(foo);

#define RETURN_IF_PARSE_ERROR \
  if (has_error()) return impl()->NullStatement();

// Common base class template shared between parser and pre-parser.
// The Impl parameter is the actual class of the parser/pre-parser,
// following the Curiously Recurring Template Pattern (CRTP).
// The structure of the parser objects is roughly the following:
//
//   // A structure template containing type definitions, needed to
//   // avoid a cyclic dependency.
//   template <typename Impl>
//   struct ParserTypes;
//
//   // The parser base object, which should just implement pure
//   // parser behavior.  The Impl parameter is the actual derived
//   // class (according to CRTP), which implements impure parser
//   // behavior.
//   template <typename Impl>
//   class ParserBase { ... };
//
//   // And then, for each parser variant (e.g., parser, preparser, etc):
//   class Parser;
//
//   template <>
//   class ParserTypes<Parser> { ... };
//
//   class Parser : public ParserBase<Parser> { ... };
//
// The parser base object implements pure parsing, according to the
// language grammar.  Different parser implementations may exhibit
// different parser-driven behavior that is not considered as pure
// parsing, e.g., early error detection and reporting, AST generation, etc.

// The ParserTypes structure encapsulates the differences in the
// types used in parsing methods.  E.g., Parser methods use Expression*
// and PreParser methods use PreParserExpression.  For any given parser
// implementation class Impl, it is expected to contain the following typedefs:
//
// template <>
// struct ParserTypes<Impl> {
//   // Synonyms for ParserBase<Impl> and Impl, respectively.
//   typedef Base;
//   typedef Impl;
//   // Return types for traversing functions.
//   typedef Identifier;
//   typedef Expression;
//   typedef FunctionLiteral;
//   typedef ObjectLiteralProperty;
//   typedef ClassLiteralProperty;
//   typedef ExpressionList;
//   typedef ObjectPropertyList;
//   typedef ClassPropertyList;
//   typedef FormalParameters;
//   typedef Statement;
//   typedef StatementList;
//   typedef Block;
//   typedef BreakableStatement;
//   typedef ForStatement;
//   typedef IterationStatement;
//   // For constructing objects returned by the traversing functions.
//   typedef Factory;
//   // For other implementation-specific tasks.
//   typedef Target;
//   typedef TargetScope;
// };

template <typename Impl>
struct ParserTypes;

enum class ParsePropertyKind : uint8_t {
  kAutoAccessorClassField,
  kAccessorGetter,
  kAccessorSetter,
  kValue,
  kShorthand,
  kAssign,
  kMethod,
  kClassField,
  kShorthandOrClassField,
  kSpread,
  kNotSet
};

template <typename Impl>
class ParserBase {
 public:
  // Shorten type names defined by ParserTypes<Impl>.
  using Types = ParserTypes<Impl>;
  using ExpressionScope = typename v8::internal::ExpressionScope<Types>;
  using ExpressionParsingScope =
      typename v8::internal::ExpressionParsingScope<Types>;
  using AccumulationScope = typename v8::internal::AccumulationScope<Types>;
  using ArrowHeadParsingScope =
      typename v8::internal::ArrowHeadParsingScope<Types>;
  using VariableDeclarationParsingScope =
      typename v8::internal::VariableDeclarationParsingScope<Types>;
  using ParameterDeclarationParsingScope =
      typename v8::internal::ParameterDeclarationParsingScope<Types>;

  // Return types for traversing functions.
  using BlockT = typename Types::Block;
  using BreakableStatementT = typename Types::BreakableStatement;
  using ClassLiteralPropertyT = typename Types::ClassLiteralProperty;
  using ClassPropertyListT = typename Types::ClassPropertyList;
  using ClassStaticElementListT = typename Types::ClassStaticElementList;
  using ExpressionT = typename Types::Expression;
  using ExpressionListT = typename Types::ExpressionList;
  using FormalParametersT = typename Types::FormalParameters;
  using ForStatementT = typename Types::ForStatement;
  using FunctionLiteralT = typename Types::FunctionLiteral;
  using IdentifierT = typename Types::Identifier;
  using IterationStatementT = typename Types::IterationStatement;
  using ObjectLiteralPropertyT = typename Types::ObjectLiteralProperty;
  using ObjectPropertyListT = typename Types::ObjectPropertyList;
  using StatementT = typename Types::Statement;
  using StatementListT = typename Types::StatementList;
  using SuspendExpressionT = typename Types::Suspend;
  // For constructing objects returned by the traversing functions.
  using FactoryT = typename Types::Factory;
  // Other implementation-specific tasks.
  using FuncNameInferrer = typename Types::FuncNameInferrer;
  using FuncNameInferrerState = typename Types::FuncNameInferrer::State;
  using SourceRange = typename Types::SourceRange;
  using SourceRangeScope = typename Types::SourceRangeScope;

  // All implementation-specific methods must be called through this.
  Impl* impl() { return static_cast<Impl*>(this); }
  const Impl* impl() const { return static_cast<const Impl*>(this); }

  ParserBase(Zone* zone, Scanner* scanner, uintptr_t stack_limit,
             AstValueFactory* ast_value_factory,
             PendingCompilationErrorHandler* pending_error_handler,
             RuntimeCallStats* runtime_call_stats, V8FileLogger* v8_file_logger,
             UnoptimizedCompileFlags flags, bool parsing_on_main_thread)
      : scope_(nullptr),
        original_scope_(nullptr),
        function_state_(nullptr),
        fni_(ast_value_factory),
        ast_value_factory_(ast_value_factory),
        ast_node_factory_(ast_value_factory, zone),
        runtime_call_stats_(runtime_call_stats),
        v8_file_logger_(v8_file_logger),
        parsing_on_main_thread_(parsing_on_main_thread),
        stack_limit_(stack_limit),
        pending_error_handler_(pending_error_handler),
        zone_(zone),
        expression_scope_(nullptr),
        scanner_(scanner),
        flags_(flags),
        info_id_(0),
        has_module_in_scope_chain_(flags_.is_module()),
        default_eager_compile_hint_(FunctionLiteral::kShouldLazyCompile) {
    pointer_buffer_.reserve(32);
    variable_buffer_.reserve(32);
  }

  const UnoptimizedCompileFlags& flags() const { return flags_; }
  bool has_module_in_scope_chain() const { return has_module_in_scope_chain_; }

  // DebugEvaluate code
  bool IsParsingWhileDebugging() const {
    return flags().parsing_while_debugging() == ParsingWhileDebugging::kYes;
  }

  bool allow_eval_cache() const { return allow_eval_cache_; }
  void set_allow_eval_cache(bool allow) { allow_eval_cache_ = allow; }

  V8_INLINE bool has_error() const { return scanner()->has_parser_error(); }

  uintptr_t stack_limit() const { return stack_limit_; }

  void set_stack_limit(uintptr_t stack_limit) { stack_limit_ = stack_limit; }

  void set_default_eager_compile_hint(
      FunctionLiteral::EagerCompileHint eager_compile_hint) {
    default_eager_compile_hint_ = eager_compile_hint;
  }

  FunctionLiteral::EagerCompileHint default_eager_compile_hint() const {
    return default_eager_compile_hint_;
  }

  int loop_nesting_depth() const {
    return function_state_->loop_nesting_depth();
  }
  int PeekNextInfoId() { return info_id_ + 1; }
  int GetNextInfoId() { return ++info_id_; }
  int GetLastInfoId() const { return info_id_; }

  void SkipInfos(int delta) { info_id_ += delta; }

  void ResetInfoId() { info_id_ = 0; }

  // The Zone where the parsing outputs are stored.
  Zone* main_zone() const { return ast_value_factory()->single_parse_zone(); }

  // The current Zone, which might be the main zone or a temporary Zone.
  Zone* zone() const { return zone_; }

  V8_INLINE bool IsExtraordinaryPrivateNameAccessAllowed() const;

 protected:
  friend class v8::internal::ExpressionScope<ParserTypes<Impl>>;
  friend class v8::internal::ExpressionParsingScope<ParserTypes<Impl>>;
  friend class v8::internal::ArrowHeadParsingScope<ParserTypes<Impl>>;

  enum VariableDeclarationContext {
    kStatementListItem,
    kStatement,
    kForStatement
  };

  class ClassLiteralChecker;

  // ---------------------------------------------------------------------------
  // BlockState and FunctionState implement the parser's scope stack.
  // The parser's current scope is in scope_. BlockState and FunctionState
  // constructors push on the scope stack and the destructors pop. They are also
  // used to hold the parser's per-funcion state.
  class BlockState {
   public:
    BlockState(Scope** scope_stack, Scope* scope)
        : scope_stack_(scope_stack), outer_scope_(*scope_stack) {
      *scope_stack_ = scope;
    }

    BlockState(Zone* zone, Scope** scope_stack)
        : BlockState(scope_stack,
                     zone->New<Scope>(zone, *scope_stack, BLOCK_SCOPE)) {}

    ~BlockState() { *scope_stack_ = outer_scope_; }

   private:
    Scope** const scope_stack_;
    Scope* const outer_scope_;
  };

  // ---------------------------------------------------------------------------
  // Target is a support class to facilitate manipulation of the
  // Parser's target_stack_ (the stack of potential 'break' and
  // 'continue' statement targets). Upon construction, a new target is
  // added; it is removed upon destruction.

  // |labels| is a list of all labels that can be used as a target for break.
  // |own_labels| is a list of all labels that an iteration statement is
  // directly prefixed with, i.e. all the labels that a continue statement in
  // the body can use to continue this iteration statement. This is always a
  // subset of |labels|.
  //
  // Example: "l1: { l2: if (b) l3: l4: for (;;) s }"
  // labels() of the Block will be l1.
  // labels() of the ForStatement will be l2, l3, l4.
  // own_labels() of the ForStatement will be l3, l4.
  class Target {
   public:
    enum TargetType { TARGET_FOR_ANONYMOUS, TARGET_FOR_NAMED_ONLY };

    Target(ParserBase* parser, BreakableStatementT statement,
           ZonePtrList<const AstRawString>* labels,
           ZonePtrList<const AstRawString>* own_labels, TargetType target_type)
        : stack_(parser->function_state_->target_stack_address()),
          statement_(statement),
          labels_(labels),
          own_labels_(own_labels),
          target_type_(target_type),
          previous_(*stack_) {
      DCHECK_IMPLIES(Impl::IsIterationStatement(statement_),
                     target_type == Target::TARGET_FOR_ANONYMOUS);
      DCHECK_IMPLIES(!Impl::IsIterationStatement(statement_),
                     own_labels == nullptr);
      *stack_ = this;
    }

    ~Target() { *stack_ = previous_; }

    const Target* previous() const { return previous_; }
    const BreakableStatementT statement() const { return statement_; }
    const ZonePtrList<const AstRawString>* labels() const { return labels_; }
    const ZonePtrList<const AstRawString>* own_labels() const {
      return own_labels_;
    }
    bool is_iteration() const { return Impl::IsIterationStatement(statement_); }
    bool is_target_for_anonymous() const {
      return target_type_ == TARGET_FOR_ANONYMOUS;
    }

   private:
    Target** const stack_;
    const BreakableStatementT statement_;
    const ZonePtrList<const AstRawString>* const labels_;
    const ZonePtrList<const AstRawString>* const own_labels_;
    const TargetType target_type_;
    Target* const previous_;
  };

  Target* target_stack() { return *function_state_->target_stack_address(); }

  BreakableStatementT LookupBreakTarget(IdentifierT label) {
    bool anonymous = impl()->IsNull(label);
    for (const Target* t = target_stack(); t != nullptr; t = t->previous()) {
      if ((anonymous && t->is_target_for_anonymous()) ||
          (!anonymous &&
           ContainsLabel(t->labels(),
                         impl()->GetRawNameFromIdentifier(label)))) {
        return t->statement();
      }
    }
    return impl()->NullStatement();
  }

  IterationStatementT LookupContinueTarget(IdentifierT label) {
    bool anonymous = impl()->IsNull(label);
    for (const Target* t = target_stack(); t != nullptr; t = t->previous()) {
      if (!t->is_iteration()) continue;

      DCHECK(t->is_target_for_anonymous());
      if (anonymous || ContainsLabel(t->own_labels(),
                                     impl()->GetRawNameFromIdentifier(label))) {
        return impl()->AsIterationStatement(t->statement());
      }
    }
    return impl()->NullStatement();
  }

  class FunctionState final : public BlockState {
   public:
    FunctionState(FunctionState** function_state_stack, Scope** scope_stack,
                  DeclarationScope* scope);
    ~FunctionState();

    DeclarationScope* scope() const { return scope_->AsDeclarationScope(); }

    void AddProperty() { expected_property_count_++; }
    int expected_property_count() { return expected_property_count_; }

    void DisableOptimization(BailoutReason reason) {
      dont_optimize_reason_ = reason;
    }
    BailoutReason dont_optimize_reason() { return dont_optimize_reason_; }

    void AddSuspend() { suspend_count_++; }
    int suspend_count() const { return suspend_count_; }
    bool CanSuspend() const { return suspend_count_ > 0; }

    FunctionKind kind() const { return scope()->function_kind(); }

    bool next_function_is_likely_called() const {
      return next_function_is_likely_called_;
    }

    bool previous_function_was_likely_called() const {
      return previous_function_was_likely_called_;
    }

    void set_next_function_is_likely_called() {
      next_function_is_likely_called_ = !v8_flags.max_lazy;
    }

    void RecordFunctionOrEvalCall() { contains_function_or_eval_ = true; }
    bool contains_function_or_eval() const {
      return contains_function_or_eval_;
    }

    class V8_NODISCARD FunctionOrEvalRecordingScope {
     public:
      explicit FunctionOrEvalRecordingScope(FunctionState* state)
          : state_and_prev_value_(state, state->contains_function_or_eval_) {
        state->contains_function_or_eval_ = false;
      }
      ~FunctionOrEvalRecordingScope() {
        bool found = state_and_prev_value_->contains_function_or_eval_;
        if (!found) {
          state_and_prev_value_->contains_function_or_eval_ =
              state_and_prev_value_.GetPayload();
        }
      }

     private:
      base::PointerWithPayload<FunctionState, bool, 1> state_and_prev_value_;
    };

    class V8_NODISCARD LoopScope final {
     public:
      explicit LoopScope(FunctionState* function_state)
          : function_state_(function_state) {
        function_state_->loop_nesting_depth_++;
      }

      ~LoopScope() { function_state_->loop_nesting_depth_--; }

     private:
      FunctionState* function_state_;
    };

    int loop_nesting_depth() const { return loop_nesting_depth_; }

    Target** target_stack_address() { return &target_stack_; }

   private:
    // Properties count estimation.
    int expected_property_count_;

    // How many suspends are needed for this function.
    int suspend_count_;

    // How deeply nested we currently are in this function.
    int loop_nesting_depth_ = 0;

    FunctionState** function_state_stack_;
    FunctionState* outer_function_state_;
    DeclarationScope* scope_;
    Target* target_stack_ = nullptr;  // for break, continue statements

    // A reason, if any, why this function should not be optimized.
    BailoutReason dont_optimize_reason_;

    // Record whether the next (=== immediately following) function literal is
    // preceded by a parenthesis / exclamation mark. Also record the previous
    // state.
    // These are managed by the FunctionState constructor; the caller may only
    // call set_next_function_is_likely_called.
    bool next_function_is_likely_called_;
    bool previous_function_was_likely_called_;

    // Track if a function or eval occurs within this FunctionState
    bool contains_function_or_eval_;

    friend Impl;
  };

  struct DeclarationDescriptor {
    VariableMode mode;
    VariableKind kind;
    int declaration_pos;
    int initialization_pos;
  };

  struct DeclarationParsingResult {
    struct Declaration {
      Declaration(ExpressionT pattern, ExpressionT initializer)
          : pattern(pattern), initializer(initializer) {
        DCHECK_IMPLIES(Impl::IsNull(pattern), Impl::IsNull(initializer));
      }

      ExpressionT pattern;
      ExpressionT initializer;
      int value_beg_pos = kNoSourcePosition;
    };

    DeclarationParsingResult()
        : first_initializer_loc(Scanner::Location::invalid()),
          bindings_loc(Scanner::Location::invalid()) {}

    DeclarationDescriptor descriptor;
    std::vector<Declaration> declarations;
    Scanner::Location first_initializer_loc;
    Scanner::Location bindings_loc;
  };

  struct CatchInfo {
   public:
    explicit CatchInfo(ParserBase* parser)
        : pattern(parser->impl()->NullExpression()),
          variable(nullptr),
          scope(nullptr) {}
    ExpressionT pattern;
    Variable* variable;
    Scope* scope;
  };

  struct ForInfo {
   public:
    explicit ForInfo(ParserBase* parser)
        : bound_names(1, parser->zone()),
          mode(ForEachStatement::ENUMERATE),
          position(kNoSourcePosition),
          parsing_result() {}
    ZonePtrList<const AstRawString> bound_names;
    ForEachStatement::VisitMode mode;
    int position;
    DeclarationParsingResult parsing_result;
  };

  struct ClassInfo {
   public:
    explicit ClassInfo(ParserBase* parser)
        : extends(parser->impl()->NullExpression()),
          public_members(parser->impl()->NewClassPropertyList(4)),
          private_members(parser->impl()->NewClassPropertyList(4)),
          static_elements(parser->impl()->NewClassStaticElementList(4)),
          instance_fields(parser->impl()->NewClassPropertyList(4)),
          constructor(parser->impl()->NullExpression()) {}
    ExpressionT extends;
    ClassPropertyListT public_members;
    ClassPropertyListT private_members;
    ClassStaticElementListT static_elements;
    ClassPropertyListT instance_fields;
    FunctionLiteralT constructor;

    bool has_static_elements() const {
      return static_elements_scope != nullptr;
    }
    bool has_instance_members() const {
      return instance_members_scope != nullptr;
    }

    DeclarationScope* EnsureStaticElementsScope(ParserBase* parser, int beg_pos,
                                                int info_id) {
      if (!has_static_elements()) {
        static_elements_scope = parser->NewFunctionScope(
            FunctionKind::kClassStaticInitializerFunction);
        static_elements_scope->SetLanguageMode(LanguageMode::kStrict);
        static_elements_scope->set_start_position(beg_pos);
        static_elements_function_id = info_id;
        // Actually consume the id. The id that was passed in might be an
        // earlier id in case of computed property names.
        parser->GetNextInfoId();
      }
      return static_elements_scope;
    }

    DeclarationScope* EnsureInstanceMembersScope(ParserBase* parser,
                                                 int beg_pos, int info_id) {
      if (!has_instance_members()) {
        instance_members_scope = parser->NewFunctionScope(
            FunctionKind::kClassMembersInitializerFunction);
        instance_members_scope->SetLanguageMode(LanguageMode::kStrict);
        instance_members_scope->set_start_position(beg_pos);
        instance_members_function_id = info_id;
        // Actually consume the id. The id that was passed in might be an
        // earlier id in case of computed property names.
        parser->GetNextInfoId();
      }
      return instance_members_scope;
    }

    DeclarationScope* static_elements_scope = nullptr;
    DeclarationScope* instance_members_scope = nullptr;
    Variable* home_object_variable = nullptr;
    Variable* static_home_object_variable = nullptr;
    int autoaccessor_count = 0;
    int static_elements_function_id = -1;
    int instance_members_function_id = -1;
    int computed_field_count = 0;
    bool has_seen_constructor = false;
    bool has_static_computed_names : 1 = false;
    bool has_static_private_methods_or_accessors : 1 = false;
    bool has_static_blocks : 1 = false;
    bool requires_brand : 1 = false;
    bool is_anonymous : 1 = false;
  };

  enum class PropertyPosition { kObjectLiteral, kClassLiteral };
  struct ParsePropertyInfo {
   public:
    explicit ParsePropertyInfo(ParserBase* parser,
                               AccumulationScope* accumulation_scope = nullptr)
        : accumulation_scope(accumulation_scope),
          name(parser->impl()->NullIdentifier()),
          position(PropertyPosition::kClassLiteral),
          function_flags(ParseFunctionFlag::kIsNormal),
          kind(ParsePropertyKind::kNotSet),
          is_computed_name(false),
          is_private(false),
          is_static(false),
          is_rest(false) {}

    bool ParsePropertyKindFromToken(Token::Value token) {
      // This returns true, setting the property kind, iff the given token is
      // one which must occur after a property name, indicating that the
      // previous token was in fact a name and not a modifier (like the "get" in
      // "get x").
      switch (token) {
        case Token::kColon:
          kind = ParsePropertyKind::kValue;
          return true;
        case Token::kComma:
          kind = ParsePropertyKind::kShorthand;
          return true;
        case Token::kRightBrace:
          kind = ParsePropertyKind::kShorthandOrClassField;
          return true;
        case Token::kAssign:
          kind = ParsePropertyKind::kAssign;
          return true;
        case Token::kLeftParen:
          kind = ParsePropertyKind::kMethod;
          return true;
        case Token::kMul:
        case Token::kSemicolon:
          kind = ParsePropertyKind::kClassField;
          return true;
        default:
          break;
      }
      return false;
    }

    AccumulationScope* accumulation_scope;
    IdentifierT name;
    PropertyPosition position;
    ParseFunctionFlags function_flags;
    ParsePropertyKind kind;
    bool is_computed_name;
    bool is_private;
    bool is_static;
    bool is_rest;
  };

  void DeclareLabel(ZonePtrList<const AstRawString>** labels,
                    ZonePtrList<const AstRawString>** own_labels,
                    const AstRawString* label) {
    if (ContainsLabel(*labels, label) || TargetStackContainsLabel(label)) {
      ReportMessage(MessageTemplate::kLabelRedeclaration, label);
      return;
    }

    // Add {label} to both {labels} and {own_labels}.
    if (*labels == nullptr) {
      DCHECK_NULL(*own_labels);
      *labels =
          zone()->template New<ZonePtrList<const AstRawString>>(1, zone());
      *own_labels =
          zone()->template New<ZonePtrList<const AstRawString>>(1, zone());
    } else {
      if (*own_labels == nullptr) {
        *own_labels =
            zone()->template New<ZonePtrList<const AstRawString>>(1, zone());
      }
    }
    (*labels)->Add(label, zone());
    (*own_labels)->Add(label, zone());
  }

  bool ContainsLabel(const ZonePtrList<const AstRawString>* labels,
                     const AstRawString* label) {
    DCHECK_NOT_NULL(label);
    if (labels != nullptr) {
      for (int i = labels->length(); i-- > 0;) {
        if (labels->at(i) == label) return true;
      }
    }
    return false;
  }

  bool TargetStackContainsLabel(const AstRawString* label) {
    for (const Target* t = target_stack(); t != nullptr; t = t->previous()) {
      if (ContainsLabel(t->labels(), label)) return true;
    }
    return false;
  }

  ClassLiteralProperty::Kind ClassPropertyKindFor(ParsePropertyKind kind) {
    switch (kind) {
      case ParsePropertyKind::kAutoAccessorClassField:
        return ClassLiteralProperty::AUTO_ACCESSOR;
      case ParsePropertyKind::kAccessorGetter:
        return ClassLiteralProperty::GETTER;
      case ParsePropertyKind::kAccessorSetter:
        return ClassLiteralProperty::SETTER;
      case ParsePropertyKind::kMethod:
        return ClassLiteralProperty::METHOD;
      case ParsePropertyKind::kClassField:
        return ClassLiteralProperty::FIELD;
      default:
        // Only returns for deterministic kinds
        UNREACHABLE();
    }
  }

  VariableMode GetVariableMode(ClassLiteralProperty::Kind kind) {
    switch (kind) {
      case ClassLiteralProperty::Kind::FIELD:
        return VariableMode::kConst;
      case ClassLiteralProperty::Kind::METHOD:
        return VariableMode::kPrivateMethod;
      case ClassLiteralProperty::Kind::GETTER:
        return VariableMode::kPrivateGetterOnly;
      case ClassLiteralProperty::Kind::SETTER:
        return VariableMode::kPrivateSetterOnly;
      case ClassLiteralProperty::Kind::AUTO_ACCESSOR:
        return VariableMode::kPrivateGetterAndSetter;
    }
  }

  const AstRawString* ClassFieldVariableName(AstValueFactory* ast_value_factory,
                                             int index) {
    std::string name = ".class-field-" + std::to_string(index);
    return ast_value_factory->GetOneByteString(name.c_str());
  }

  const AstRawString* AutoAccessorVariableName(
      AstValueFactory* ast_value_factory, int index) {
    std::string name = ".accessor-storage-" + std::to_string(index);
    return ast_value_factory->GetOneByteString(name.c_str());
  }

  DeclarationScope* NewScriptScope(REPLMode repl_mode) const {
    return zone()->template New<DeclarationScope>(zone(), ast_value_factory(),
                                                  repl_mode);
  }

  DeclarationScope* NewVarblockScope() const {
    return zone()->template New<DeclarationScope>(zone(), scope(), BLOCK_SCOPE);
  }

  ModuleScope* NewModuleScope(DeclarationScope* parent) const {
    return zone()->template New<ModuleScope>(parent, ast_value_factory());
  }

  DeclarationScope* NewEvalScope(Scope* parent) const {
    return zone()->template New<DeclarationScope>(zone(), parent, EVAL_SCOPE);
  }

  ClassScope* NewClassScope(Scope* parent, bool is_anonymous) const {
    return zone()->template New<ClassScope>(zone(), parent, is_anonymous);
  }

  Scope* NewBlockScopeForObjectLiteral() {
    Scope* scope = NewScope(BLOCK_SCOPE);
    scope->set_is_block_scope_for_object_literal();
    return scope;
  }

  Scope* NewScope(ScopeType scope_type) const {
    return NewScopeWithParent(scope(), scope_type);
  }

  // This constructor should only be used when absolutely necessary. Most scopes
  // should automatically use scope() as parent, and be fine with
  // NewScope(ScopeType) above.
  Scope* NewScopeWithParent(Scope* parent, ScopeType scope_type) const {
    // Must always use the specific constructors for the blocklisted scope
    // types.
    DCHECK_NE(FUNCTION_SCOPE, scope_type);
    DCHECK_NE(SCRIPT_SCOPE, scope_type);
    DCHECK_NE(REPL_MODE_SCOPE, scope_type);
    DCHECK_NE(MODULE_SCOPE, scope_type);
    DCHECK_NOT_NULL(parent);
    return zone()->template New<Scope>(zone(), parent, scope_type);
  }

  // Creates a function scope that always allocates in zone(). The function
  // scope itself is either allocated in zone() or in target_zone if one is
  // passed in.
  DeclarationScope* NewFunctionScope(FunctionKind kind,
                                     Zone* parse_zone = nullptr) const {
    DCHECK(ast_value_factory());
    if (parse_zone == nullptr) parse_zone = zone();
    DeclarationScope* result = zone()->template New<DeclarationScope>(
        parse_zone, scope(), FUNCTION_SCOPE, kind);

    // Record presence of an inner function scope
    function_state_->RecordFunctionOrEvalCall();

    // TODO(verwaest): Move into the DeclarationScope constructor.
    if (!IsArrowFunction(kind)) {
      result->DeclareDefaultFunctionVariables(ast_value_factory());
    }
    return result;
  }

  V8_INLINE DeclarationScope* GetDeclarationScope() const {
    return scope()->GetDeclarationScope();
  }
  V8_INLINE DeclarationScope* GetClosureScope() const {
    return scope()->GetClosureScope();
  }

  VariableProxy* NewRawVariable(const AstRawString* name, int pos) {
    return factory()->ast_node_factory()->NewVariableProxy(
        name, NORMAL_VARIABLE, pos);
  }

  VariableProxy* NewUnresolved(const AstRawString* name) {
    return scope()->NewUnresolved(factory()->ast_node_factory(), name,
                                  scanner()->location().beg_pos);
  }

  VariableProxy* NewUnresolved(const AstRawString* name, int begin_pos,
                               VariableKind kind = NORMAL_VARIABLE) {
    return scope()->NewUnresolved(factory()->ast_node_factory(), name,
                                  begin_pos, kind);
  }

  Scanner* scanner() const { return scanner_; }
  AstValueFactory* ast_value_factory() const { return ast_value_factory_; }
  int position() const { return scanner_->location().beg_pos; }
  int peek_position() const { return scanner_->peek_location().beg_pos; }
  int end_position() const { return scanner_->location().end_pos; }
  int peek_end_position() const { return scanner_->peek_location().end_pos; }
  bool stack_overflow() const {
    return pending_error_handler()->stack_overflow();
  }
  void set_stack_overflow() {
    scanner_->set_parser_error();
    pending_error_handler()->set_stack_overflow();
  }
  void CheckStackOverflow() {
    // Any further calls to Next or peek will return the illegal token.
    if (GetCurrentStackPosition() < stack_limit_) set_stack_overflow();
  }

  V8_INLINE Token::Value peek() { return scanner()->peek(); }

  // Returns the position past the following semicolon (if it exists), and the
  // position past the end of the current token otherwise.
  int PositionAfterSemicolon() {
    return (peek() == Token::kSemicolon) ? peek_end_position() : end_position();
  }

  V8_INLINE Token::Value PeekAheadAhead() {
    return scanner()->PeekAheadAhead();
  }

  V8_INLINE Token::Value PeekAhead() { return scanner()->PeekAhead(); }

  V8_INLINE Token::Value Next() { return scanner()->Next(); }

  V8_INLINE void Consume(Token::Value token) {
    Token::Value next = scanner()->Next();
    USE(next);
    USE(token);
    DCHECK_IMPLIES(!has_error(), next == token);
  }

  V8_INLINE bool Check(Token::Value token) {
    Token::Value next = scanner()->peek();
    if (next == token) {
      Consume(next);
      return true;
    }
    return false;
  }

  void Expect(Token::Value token) {
    Token::Value next = Next();
    if (V8_UNLIKELY(next != token)) {
      ReportUnexpectedToken(next);
    }
  }

  void ExpectSemicolon() {
    // Check for automatic semicolon insertion according to
    // the rules given in ECMA-262, section 7.9, page 21.
    Token::Value tok = peek();
    if (V8_LIKELY(tok == Token::kSemicolon)) {
      Next();
      return;
    }
    if (V8_LIKELY(scanner()->HasLineTerminatorBeforeNext() ||
                  Token::IsAutoSemicolon(tok))) {
      return;
    }

    if (scanner()->current_token() == Token::kAwait && !is_async_function()) {
      if (flags().parsing_while_debugging() == ParsingWhileDebugging::kYes) {
        ReportMessageAt(scanner()->location(),
                        MessageTemplate::kAwaitNotInDebugEvaluate);
      } else {
        ReportMessageAt(scanner()->location(),
                        MessageTemplate::kAwaitNotInAsyncContext);
      }
      return;
    }

    ReportUnexpectedToken(Next());
  }

  bool peek_any_identifier() { return Token::IsAnyIdentifier(peek()); }

  bool PeekContextualKeyword(const AstRawString* name) {
    return peek() == Token::kIdentifier &&
           !scanner()->next_literal_contains_escapes() &&
           scanner()->NextSymbol(ast_value_factory()) == name;
  }

  bool PeekContextualKeyword(Token::Value token) {
    return peek() == token && !scanner()->next_literal_contains_escapes();
  }

  bool CheckContextualKeyword(const AstRawString* name) {
    if (PeekContextualKeyword(name)) {
      Consume(Token::kIdentifier);
      return true;
    }
    return false;
  }

  bool CheckContextualKeyword(Token::Value token) {
    if (PeekContextualKeyword(token)) {
      Consume(token);
      return true;
    }
    return false;
  }

  void ExpectContextualKeyword(const AstRawString* name,
                               const char* fullname = nullptr, int pos = -1) {
    Expect(Token::kIdentifier);
    if (V8_UNLIKELY(scanner()->CurrentSymbol(ast_value_factory()) != name)) {
      ReportUnexpectedToken(scanner()->current_token());
    }
    if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
      const char* full = fullname == nullptr
                             ? reinterpret_cast<const char*>(name->raw_data())
                             : fullname;
      int start = pos == -1 ? position() : pos;
      impl()->ReportMessageAt(Scanner::Location(start, end_position()),
                              MessageTemplate::kInvalidEscapedMetaProperty,
                              full);
    }
  }

  void ExpectContextualKeyword(Token::Value token) {
    // Token Should be in range of Token::kIdentifier + 1 to Token::kAsync
    DCHECK(base::IsInRange(token, Token::kGet, Token::kAsync));
    Token::Value next = Next();
    if (V8_UNLIKELY(next != token)) {
      ReportUnexpectedToken(next);
    }
    if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
      impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
    }
  }

  bool CheckInOrOf(ForEachStatement::VisitMode* visit_mode) {
    if (Check(Token::kIn)) {
      *visit_mode = ForEachStatement::ENUMERATE;
      return true;
    } else if (CheckContextualKeyword(Token::kOf)) {
      *visit_mode = ForEachStatement::ITERATE;
      return true;
    }
    return false;
  }

  bool PeekInOrOf() {
    return peek() == Token::kIn || PeekContextualKeyword(Token::kOf);
  }

  // Checks whether an octal literal was last seen between beg_pos and end_pos.
  // Only called for strict mode strings.
  void CheckStrictOctalLiteral(int beg_pos, int end_pos) {
    Scanner::Location octal = scanner()->octal_position();
    if (octal.IsValid() && beg_pos <= octal.beg_pos &&
        octal.end_pos <= end_pos) {
      MessageTemplate message = scanner()->octal_message();
      DCHECK_NE(message, MessageTemplate::kNone);
      impl()->ReportMessageAt(octal, message);
      scanner()->clear_octal_position();
      if (message == MessageTemplate::kStrictDecimalWithLeadingZero) {
        impl()->CountUsage(v8::Isolate::kDecimalWithLeadingZeroInStrictMode);
      }
    }
  }

  // Checks if an octal literal or an invalid hex or unicode escape sequence
  // appears in the current template literal token. In the presence of such,
  // either returns false or reports an error, depending on should_throw.
  // Otherwise returns true.
  inline bool CheckTemplateEscapes(bool should_throw) {
    DCHECK(Token::IsTemplate(scanner()->current_token()));
    if (!scanner()->has_invalid_template_escape()) return true;

    // Handle error case(s)
    if (should_throw) {
      impl()->ReportMessageAt(scanner()->invalid_template_escape_location(),
                              scanner()->invalid_template_escape_message());
    }
    scanner()->clear_invalid_template_escape_message();
    return should_throw;
  }

  ExpressionT ParsePossibleDestructuringSubPattern(AccumulationScope* scope);
  void ClassifyParameter(IdentifierT parameter, int beg_pos, int end_pos);
  void ClassifyArrowParameter(AccumulationScope* accumulation_scope,
                              int position, ExpressionT parameter);

  // Checking the name of a function literal. This has to be done after parsing
  // the function, since the function can declare itself strict.
  void CheckFunctionName(LanguageMode language_mode, IdentifierT function_name,
                         FunctionNameValidity function_name_validity,
                         const Scanner::Location& function_name_loc) {
    if (impl()->IsNull(function_name)) return;
    if (function_name_validity == kSkipFunctionNameCheck) return;
    // The function name needs to be checked in strict mode.
    if (is_sloppy(language_mode)) return;

    if (impl()->IsEvalOrArguments(function_name)) {
      impl()->ReportMessageAt(function_name_loc,
                              MessageTemplate::kStrictEvalArguments);
      return;
    }
    if (function_name_validity == kFunctionNameIsStrictReserved) {
      impl()->ReportMessageAt(function_name_loc,
                              MessageTemplate::kUnexpectedStrictReserved);
      return;
    }
  }

  typename Types::Factory* factory() { return &ast_node_factory_; }

  DeclarationScope* GetReceiverScope() const {
    return scope()->GetReceiverScope();
  }
  LanguageMode language_mode() { return scope()->language_mode(); }
  void RaiseLanguageMode(LanguageMode mode) {
    LanguageMode old = scope()->language_mode();
    impl()->SetLanguageMode(scope(), old > mode ? old : mode);
  }
  bool is_generator() const {
    return IsGeneratorFunction(function_state_->kind());
  }
  bool is_async_function() const {
    return IsAsyncFunction(function_state_->kind());
  }
  bool is_async_generator() const {
    return IsAsyncGeneratorFunction(function_state_->kind());
  }
  bool is_resumable() const {
    return IsResumableFunction(function_state_->kind());
  }
  bool is_await_allowed() const {
    return is_async_function() || IsModule(function_state_->kind());
  }
  bool is_await_as_identifier_disallowed() {
    return flags().is_module() ||
           IsAwaitAsIdentifierDisallowed(function_state_->kind());
  }
  bool is_using_allowed() const {
    // UsingDeclaration and AwaitUsingDeclaration are Syntax Errors if the goal
    // symbol is Script. UsingDeclaration and AwaitUsingDeclaration are not
    // contained, either directly or indirectly, within a Block, CaseBlock,
    // ForStatement, ForInOfStatement, FunctionBody, GeneratorBody,
    // AsyncGeneratorBody, AsyncFunctionBody, ClassStaticBlockBody, or
    // ClassBody. Unless the current scope's ScopeType is ScriptScope, the
    // current position is directly or indirectly within one of the productions
    // listed above since they open a new scope.
    return scope()->scope_type() != SCRIPT_SCOPE;
  }
  bool IfNextUsingKeyword(Token::Value token_after_using) {
    // If the token after `using` is `of` or `in`, `using` is an identifier
    // and not a declaration token.
    // `of`: for ( [lookahead ≠ using of] ForDeclaration[?Yield, ?Await, +Using]
    //       of AssignmentExpression[+In, ?Yield, ?Await] )
    // `in`: for ( ForDeclaration[?Yield, ?Await, ~Using] in
    //       Expression[+In, ?Yield, ?Await] )
    // If the token after `using` is `{` or `[`, it
    // shows a pattern after `using` which is not applicable.
    // `{` or `[`: using [no LineTerminator here] [lookahead ≠ await]
    // ForBinding[?Yield, ?Await, ~Pattern]
    return (v8_flags.js_explicit_resource_management &&
            token_after_using != Token::kLeftBracket &&
            token_after_using != Token::kLeftBrace &&
            token_after_using != Token::kOf && token_after_using != Token::kIn);
  }
  bool IfStartsWithUsingKeyword() {
    return ((peek() == Token::kUsing && IfNextUsingKeyword(PeekAhead())) ||
            (peek() == Token::kAwait && PeekAhead() == Token::kUsing &&
             IfNextUsingKeyword(PeekAheadAhead())));
  }
  FunctionState* AddOneSuspendPointIfBlockContainsAwaitUsing(
      Scope* scope, FunctionState* function_state) {
    if (scope->has_await_using_declaration()) {
      // Since, we handle async disposal of resources by promise chaining, just
      // one suspend point is needed at the end of the block that contains at
      // least one `await using`. This suspend point will be placed in the
      // `finally` block of rewritten block.
      function_state->AddSuspend();
    }
    return function_state;
  }
  const PendingCompilationErrorHandler* pending_error_handler() const {
    return pending_error_handler_;
  }
  PendingCompilationErrorHandler* pending_error_handler() {
    return pending_error_handler_;
  }

  // Report syntax errors.
  template <typename... Ts>
  V8_NOINLINE void ReportMessage(MessageTemplate message, const Ts&... args) {
    ReportMessageAt(scanner()->location(), message, args...);
  }

  template <typename... Ts>
  V8_NOINLINE void ReportMessageAt(Scanner::Location source_location,
                                   MessageTemplate message, const Ts&... args) {
    impl()->pending_error_handler()->ReportMessageAt(
        source_location.beg_pos, source_location.end_pos, message, args...);
    scanner()->set_parser_error();
  }

  V8_NOINLINE void ReportMessageAt(Scanner::Location source_location,
                                   MessageTemplate message,
                                   const PreParserIdentifier& arg0) {
    ReportMessageAt(source_location, message,
                    impl()->PreParserIdentifierToAstRawString(arg0));
  }

  V8_NOINLINE void ReportUnexpectedToken(Token::Value token);

  void ValidateFormalParameters(LanguageMode language_mode,
                                const FormalParametersT& parameters,
                                bool allow_duplicates) {
    if (!allow_duplicates) parameters.ValidateDuplicate(impl());
    if (is_strict(language_mode)) parameters.ValidateStrictMode(impl());
  }

  // Needs to be called if the reference needs to be available from the current
  // point. It causes the receiver to be context allocated if necessary.
  // Returns the receiver variable that we're referencing.
  V8_INLINE Variable* UseThis() {
    DeclarationScope* closure_scope = scope()->GetClosureScope();
    DeclarationScope* receiver_scope = closure_scope->GetReceiverScope();
    Variable* var = receiver_scope->receiver();
    var->set_is_used();
    if (closure_scope == receiver_scope) {
      // It's possible that we're parsing the head of an arrow function, in
      // which case we haven't realized yet that closure_scope !=
      // receiver_scope. Mark through the ExpressionScope for now.
      expression_scope()->RecordThisUse();
    } else {
      closure_scope->set_has_this_reference();
      var->ForceContextAllocation();
    }
    return var;
  }

  V8_INLINE IdentifierT ParseAndClassifyIdentifier(Token::Value token);

  // Similar logic to ParseAndClassifyIdentifier but the identifier is
  // already parsed in prop_info. Returns false if this is an invalid
  // identifier or an invalid use of the "arguments" keyword.
  V8_INLINE bool ClassifyPropertyIdentifier(Token::Value token,
                                            ParsePropertyInfo* prop_info);
  // Parses an identifier or a strict mode future reserved word. Allows passing
  // in function_kind for the case of parsing the identifier in a function
  // expression, where the relevant "function_kind" bit is of the function being
  // parsed, not the containing function.
  V8_INLINE IdentifierT ParseIdentifier(FunctionKind function_kind);
  V8_INLINE IdentifierT ParseIdentifier() {
    return ParseIdentifier(function_state_->kind());
  }
  // Same as above but additionally disallows 'eval' and 'arguments' in strict
  // mode.
  IdentifierT ParseNonRestrictedIdentifier();

  // This method should be used to ambiguously parse property names that can
  // become destructuring identifiers.
  V8_INLINE IdentifierT ParsePropertyName();

  ExpressionT ParsePropertyOrPrivatePropertyName();

  const AstRawString* GetNextSymbolForRegExpLiteral() const {
    return scanner()->NextSymbol(ast_value_factory());
  }
  bool ValidateRegExpFlags(RegExpFlags flags);
  bool ValidateRegExpLiteral(const AstRawString* pattern, RegExpFlags flags,
                             RegExpError* regexp_error);
  ExpressionT ParseRegExpLiteral();

  ExpressionT ParseBindingPattern();
  ExpressionT ParsePrimaryExpression();

  // Use when parsing an expression that is known to not be a pattern or part of
  // a pattern.
  V8_INLINE ExpressionT ParseExpression();
  V8_INLINE ExpressionT ParseAssignmentExpression();
  V8_INLINE ExpressionT ParseConditionalChainAssignmentExpression();

  // These methods do not wrap the parsing of the expression inside a new
  // expression_scope; they use the outer expression_scope instead. They should
  // be used whenever we're parsing something with the "cover" grammar that
  // recognizes both patterns and non-patterns (which roughly corresponds to
  // what's inside the parentheses generated by the symbol
  // "CoverParenthesizedExpressionAndArrowParameterList" in the ES 2017
  // specification).
  ExpressionT ParseExpressionCoverGrammar();
  ExpressionT ParseAssignmentExpressionCoverGrammar();
  ExpressionT ParseAssignmentExpressionCoverGrammarContinuation(
      int lhs_beg_pos, ExpressionT expression);
  ExpressionT ParseConditionalChainAssignmentExpressionCoverGrammar();

  ExpressionT ParseArrowParametersWithRest(ExpressionListT* list,
                                           AccumulationScope* scope,
                                           int seen_variables);

  ExpressionT ParseArrayLiteral();

  inline static bool IsAccessor(ParsePropertyKind kind) {
    return base::IsInRange(kind, ParsePropertyKind::kAccessorGetter,
                           ParsePropertyKind::kAccessorSetter);
  }

  ExpressionT ParseProperty(ParsePropertyInfo* prop_info);
  ExpressionT ParseObjectLiteral();
  V8_INLINE bool VerifyCanHaveAutoAccessorOrThrow(ParsePropertyInfo* prop_info,
                                                  ExpressionT name_expression,
                                                  int name_token_position);
  V8_INLINE bool ParseCurrentSymbolAsClassFieldOrMethod(
      ParsePropertyInfo* prop_info, ExpressionT* name_expression);
  V8_INLINE bool ParseAccessorPropertyOrAutoAccessors(
      ParsePropertyInfo* prop_info, ExpressionT* name_expression,
      int* name_token_position);
  ClassLiteralPropertyT ParseClassPropertyDefinition(
      ClassInfo* class_info, ParsePropertyInfo* prop_info, bool has_extends);
  void CheckClassFieldName(IdentifierT name, bool is_static);
  void CheckClassMethodName(IdentifierT name, ParsePropertyKind type,
                            ParseFunctionFlags flags, bool is_static,
                            bool* has_seen_constructor);
  ExpressionT ParseMemberInitializer(ClassInfo* class_info, int beg_pos,
                                     int info_id, bool is_static);
  BlockT ParseClassStaticBlock(ClassInfo* class_info);
  ObjectLiteralPropertyT ParseObjectPropertyDefinition(
      ParsePropertyInfo* prop_info, bool* has_seen_proto);
  void ParseArguments(
      ExpressionListT* args, bool* has_spread,
      ParsingArrowHeadFlag maybe_arrow = kCertainlyNotArrowHead);

  ExpressionT ParseYieldExpression();
  V8_INLINE ExpressionT ParseConditionalExpression();
  ExpressionT ParseConditionalChainExpression(ExpressionT condition,
                                              int condition_pos);
  ExpressionT ParseConditionalContinuation(ExpressionT expression, int pos);
  ExpressionT ParseLogicalExpression();
  ExpressionT ParseCoalesceExpression(ExpressionT expression);
  ExpressionT ParseBinaryContinuation(ExpressionT x, int prec, int prec1);
  V8_INLINE ExpressionT ParseBinaryExpression(int prec);
  ExpressionT ParseUnaryOrPrefixExpression();
  ExpressionT ParseAwaitExpression();
  V8_INLINE ExpressionT ParseUnaryExpression();
  V8_INLINE ExpressionT ParsePostfixExpression();
  V8_NOINLINE ExpressionT ParsePostfixContinuation(ExpressionT expression,
                                                   int lhs_beg_pos);
  V8_INLINE ExpressionT ParseLeftHandSideExpression();
  ExpressionT ParseLeftHandSideContinuation(ExpressionT expression);
  ExpressionT ParseMemberWithPresentNewPrefixesExpression();
  ExpressionT ParseFunctionExpression();
  V8_INLINE ExpressionT ParseMemberExpression();
  V8_INLINE ExpressionT
  ParseMemberExpressionContinuation(ExpressionT expression) {
    if (!Token::IsMember(peek())) return expression;
    return DoParseMemberExpressionContinuation(expression);
  }
  ExpressionT DoParseMemberExpressionContinuation(ExpressionT expression);

  ExpressionT ParseArrowFunctionLiteral(const FormalParametersT& parameters,
                                        int function_literal_id,
                                        bool could_be_immediately_invoked);
  ExpressionT ParseAsyncFunctionLiteral();
  ExpressionT ParseClassExpression(Scope* outer_scope);
  ExpressionT ParseClassLiteral(Scope* outer_scope, IdentifierT name,
                                Scanner::Location class_name_location,
                                bool name_is_strict_reserved,
                                int class_token_pos);
  void ParseClassLiteralBody(ClassInfo& class_info, IdentifierT name,
                             int class_token_pos, Token::Value end_token);

  ExpressionT ParseTemplateLiteral(ExpressionT tag, int start, bool tagged);
  ExpressionT ParseSuperExpression();
  ExpressionT ParseImportExpressions();
  ExpressionT ParseNewTargetExpression();

  V8_INLINE void ParseFormalParameter(FormalParametersT* parameters);
  void ParseFormalParameterList(FormalParametersT* parameters);
  void CheckArityRestrictions(int param_count, FunctionKind function_type,
                              bool has_rest, int formals_start_pos,
                              int formals_end_pos);

  void ParseVariableDeclarations(VariableDeclarationContext var_context,
                                 DeclarationParsingResult* parsing_result,
                                 ZonePtrList<const AstRawString>* names);
  StatementT ParseAsyncFunctionDeclaration(
      ZonePtrList<const AstRawString>* names, bool default_export);
  StatementT ParseFunctionDeclaration();
  StatementT ParseHoistableDeclaration(ZonePtrList<const AstRawString>* names,
                                       bool default_export);
  StatementT ParseHoistableDeclaration(int pos, ParseFunctionFlags flags,
                                       ZonePtrList<const AstRawString>* names,
                                       bool default_export);
  StatementT ParseClassDeclaration(ZonePtrList<const AstRawString>* names,
                                   bool default_export);
  StatementT ParseNativeDeclaration();

  // Whether we're parsing a single-expression arrow function or something else.
  enum class FunctionBodyType { kExpression, kBlock };
  // Consumes the ending }.
  void ParseFunctionBody(StatementListT* body, IdentifierT function_name,
                         int pos, const FormalParametersT& parameters,
                         FunctionKind kind,
                         FunctionSyntaxKind function_syntax_kind,
                         FunctionBodyType body_type);

  // Check if the scope has conflicting var/let declarations from different
  // scopes. This covers for example
  //
  // function f() { { { var x; } let x; } }
  // function g() { { var x; let x; } }
  //
  // The var declarations are hoisted to the function scope, but originate from
  // a scope where the name has also been let bound or the var declaration is
  // hoisted over such a scope.
  void CheckConflictingVarDeclarations(DeclarationScope* scope) {
    bool allowed_catch_binding_var_redeclaration = false;
    Declaration* decl = scope->CheckConflictingVarDeclarations(
        &allowed_catch_binding_var_redeclaration);
    if (allowed_catch_binding_var_redeclaration) {
      impl()->CountUsage(v8::Isolate::kVarRedeclaredCatchBinding);
    }
    if (decl != nullptr) {
      // In ES6, conflicting variable bindings are early errors.
      const AstRawString* name = decl->var()->raw_name();
      int position = decl->position();
      Scanner::Location location =
          position == kNoSourcePosition
              ? Scanner::Location::invalid()
              : Scanner::Location(position, position + 1);
      impl()->ReportMessageAt(location, MessageTemplate::kVarRedeclaration,
                              name);
    }
  }

  // TODO(nikolaos, marja): The first argument should not really be passed
  // by value. The method is expected to add the parsed statements to the
  // list. This works because in the case of the parser, StatementListT is
  // a pointer whereas the preparser does not really modify the body.
  V8_INLINE void ParseStatementList(StatementListT* body,
                                    Token::Value end_token);
  StatementT ParseStatementListItem();

  StatementT ParseStatement(ZonePtrList<const AstRawString>* labels,
                            ZonePtrList<const AstRawString>* own_labels) {
    return ParseStatement(labels, own_labels,
                          kDisallowLabelledFunctionStatement);
  }
  StatementT ParseStatement(ZonePtrList<const AstRawString>* labels,
                            ZonePtrList<const AstRawString>* own_labels,
                            AllowLabelledFunctionStatement allow_function);
  BlockT ParseBlock(ZonePtrList<const AstRawString>* labels,
                    Scope* block_scope);
  BlockT ParseBlock(ZonePtrList<const AstRawString>* labels);

  // Parse a SubStatement in strict mode, or with an extra block scope in
  // sloppy mode to handle
  // ES#sec-functiondeclarations-in-ifstatement-statement-clauses
  StatementT ParseScopedStatement(ZonePtrList<const AstRawString>* labels);

  StatementT ParseVariableStatement(VariableDeclarationContext var_context,
                                    ZonePtrList<const AstRawString>* names);

  // Magical syntax support.
  ExpressionT ParseV8Intrinsic();

  StatementT ParseDebuggerStatement();

  StatementT ParseExpressionOrLabelledStatement(
      ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels,
      AllowLabelledFunctionStatement allow_function);
  StatementT ParseIfStatement(ZonePtrList<const AstRawString>* labels);
  StatementT ParseContinueStatement();
  StatementT ParseBreakStatement(ZonePtrList<const AstRawString>* labels);
  StatementT ParseReturnStatement();
  StatementT ParseWithStatement(ZonePtrList<const AstRawString>* labels);
  StatementT ParseDoWhileStatement(ZonePtrList<const AstRawString>* labels,
                                   ZonePtrList<const AstRawString>* own_labels);
  StatementT ParseWhileStatement(ZonePtrList<const AstRawString>* labels,
                                 ZonePtrList<const AstRawString>* own_labels);
  StatementT ParseThrowStatement();
  StatementT ParseSwitchStatement(ZonePtrList<const AstRawString>* labels);
  V8_INLINE StatementT ParseTryStatement();
  StatementT ParseForStatement(ZonePtrList<const AstRawString>* labels,
                               ZonePtrList<const AstRawString>* own_labels);
  StatementT ParseForEachStatementWithDeclarations(
      int stmt_pos, ForInfo* for_info, ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels, Scope* inner_block_scope);
  StatementT ParseForEachStatementWithoutDeclarations(
      int stmt_pos, ExpressionT expression, int lhs_beg_pos, int lhs_end_pos,
      ForInfo* for_info, ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels);

  // Parse a C-style for loop: 'for (<init>; <cond>; <next>) { ... }'
  // "for (<init>;" is assumed to have been parser already.
  ForStatementT ParseStandardForLoop(
      int stmt_pos, ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels, ExpressionT* cond,
      StatementT* next, StatementT* body);
  // Same as the above, but handles those cases where <init> is a
  // lexical variable declaration.
  StatementT ParseStandardForLoopWithLexicalDeclarations(
      int stmt_pos, StatementT init, ForInfo* for_info,
      ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels);
  StatementT ParseForAwaitStatement(
      ZonePtrList<const AstRawString>* labels,
      ZonePtrList<const AstRawString>* own_labels);

  V8_INLINE bool IsLet(const AstRawString* identifier) const {
    return identifier == ast_value_factory()->let_string();
  }

  bool IsNextLetKeyword();

  // Checks if the expression is a valid reference expression (e.g., on the
  // left-hand side of assignments). Although ruled out by ECMA as early errors,
  // we allow calls for web compatibility and rewrite them to a runtime throw.
  // Modern language features can be exempted from this hack by passing
  // early_error = true.
  ExpressionT RewriteInvalidReferenceExpression(ExpressionT expression,
                                                int beg_pos, int end_pos,
                                                MessageTemplate message,
                                                bool early_error);

  bool IsValidReferenceExpression(ExpressionT expression);

  bool IsAssignableIdentifier(ExpressionT expression) {
    if (!impl()->IsIdentifier(expression)) return false;
    if (is_strict(language_mode()) &&
        impl()->IsEvalOrArguments(impl()->AsIdentifier(expression))) {
      return false;
    }
    return true;
  }

  enum SubFunctionKind { kFunction, kNonStaticMethod, kStaticMethod };

  FunctionKind FunctionKindForImpl(SubFunctionKind sub_function_kind,
                                   ParseFunctionFlags flags) {
    static const FunctionKind kFunctionKinds[][2][2] = {
        {
            // SubFunctionKind::kNormalFunction
            {// is_generator=false
             FunctionKind::kNormalFunction, FunctionKind::kAsyncFunction},
            {// is_generator=true
             FunctionKind::kGeneratorFunction,
             FunctionKind::kAsyncGeneratorFunction},
        },
        {
            // SubFunctionKind::kNonStaticMethod
            {// is_generator=false
             FunctionKind::kConciseMethod, FunctionKind::kAsyncConciseMethod},
            {// is_generator=true
             FunctionKind::kConciseGeneratorMethod,
             FunctionKind::kAsyncConciseGeneratorMethod},
        },
        {
            // SubFunctionKind::kStaticMethod
            {// is_generator=false
             FunctionKind::kStaticConciseMethod,
             FunctionKind::kStaticAsyncConciseMethod},
            {// is_generator=true
             FunctionKind::kStaticConciseGeneratorMethod,
             FunctionKind::kStaticAsyncConciseGeneratorMethod},
        }};
    return kFunctionKinds[sub_function_kind]
                         [(flags & ParseFunctionFlag::kIsGenerator) != 0]
                         [(flags & ParseFunctionFlag::kIsAsync) != 0];
  }

  inline FunctionKind FunctionKindFor(ParseFunctionFlags flags) {
    return FunctionKindForImpl(SubFunctionKind::kFunction, flags);
  }

  inline FunctionKind MethodKindFor(bool is_static, ParseFunctionFlags flags) {
    return FunctionKindForImpl(is_static ? SubFunctionKind::kStaticMethod
                                         : SubFunctionKind::kNonStaticMethod,
                               flags);
  }

  // Keep track of eval() calls since they disable all local variable
  // optimizations. This checks if expression is an eval call, and if yes,
  // forwards the information to scope.
  bool CheckPossibleEvalCall(ExpressionT expression, bool is_optional_call,
                             Scope* scope) {
    if (impl()->IsIdentifier(expression) &&
        impl()->IsEval(impl()->AsIdentifier(expression)) && !is_optional_call) {
      function_state_->RecordFunctionOrEvalCall();
      scope->RecordEvalCall();
      return true;
    }
    return false;
  }

  // Convenience method which determines the type of return statement to emit
  // depending on the current function type.
  inline StatementT BuildReturnStatement(
      ExpressionT expr, int pos,
      int end_pos = ReturnStatement::kFunctionLiteralReturnPosition) {
    if (impl()->IsNull(expr)) {
      expr = factory()->NewUndefinedLiteral(kNoSourcePosition);
    } else if (is_async_generator()) {
      // In async generators, if there is an explicit operand to the return
      // statement, await the operand.
      expr = factory()->NewAwait(expr, kNoSourcePosition);
      function_state_->AddSuspend();
    }
    if (is_async_function()) {
      return factory()->NewAsyncReturnStatement(expr, pos, end_pos);
    }
    return factory()->NewReturnStatement(expr, pos, end_pos);
  }

  SourceTextModuleDescriptor* module() const {
    return scope()->AsModuleScope()->module();
  }
  Scope* scope() const { return scope_; }

  // Stack of expression expression_scopes.
  // The top of the stack is always pointed to by expression_scope().
  V8_INLINE ExpressionScope* expression_scope() const {
    DCHECK_NOT_NULL(expression_scope_);
    return expression_scope_;
  }

  bool MaybeParsingArrowhead() const {
    return expression_scope_ != nullptr &&
           expression_scope_->has_possible_arrow_parameter_in_scope_chain();
  }

  class V8_NODISCARD AcceptINScope final {
   public:
    AcceptINScope(ParserBase* parser, bool accept_IN)
        : parser_(parser), previous_accept_IN_(parser->accept_IN_) {
      parser_->accept_IN_ = accept_IN;
    }

    ~AcceptINScope() { parser_->accept_IN_ = previous_accept_IN_; }

   private:
    ParserBase* parser_;
    bool previous_accept_IN_;
  };

  class V8_NODISCARD ParameterParsingScope {
   public:
    ParameterParsingScope(Impl* parser, FormalParametersT* parameters)
        : parser_(parser), parent_parameters_(parser_->parameters_) {
      parser_->parameters_ = parameters;
    }

    ~ParameterParsingScope() { parser_->parameters_ = parent_parameters_; }

   private:
    Impl* parser_;
    FormalParametersT* parent_parameters_;
  };

  class V8_NODISCARD FunctionParsingScope {
   public:
    explicit FunctionParsingScope(Impl* parser)
        : parser_(parser), expression_scope_(parser_->expression_scope_) {
      parser_->expression_scope_ = nullptr;
    }

    ~FunctionParsingScope() { parser_->expression_scope_ = expression_scope_; }

   private:
    Impl* parser_;
    ExpressionScope* expression_scope_;
  };

  std::vector<void*>* pointer_buffer() { return &pointer_buffer_; }
  std::vector<std::pair<VariableProxy*, int>>* variable_buffer() {
    return &variable_buffer_;
  }

  // Parser base's protected field members.

  Scope* scope_;                   // Scope stack.
  // Stack of scopes for object literals we're currently parsing.
  Scope* object_literal_scope_ = nullptr;
  Scope* original_scope_;  // The top scope for the current parsing item.
  FunctionState* function_state_;  // Function state stack.
  FuncNameInferrer fni_;
  AstValueFactory* ast_value_factory_;  // Not owned.
  typename Types::Factory ast_node_factory_;
  RuntimeCallStats* runtime_call_stats_;
  internal::V8FileLogger* v8_file_logger_;
  bool parsing_on_main_thread_;
  uintptr_t stack_limit_;
  PendingCompilationErrorHandler* pending_error_handler_;

  // Parser base's private field members.
  void set_has_module_in_scope_chain() { has_module_in_scope_chain_ = true; }

 private:
  Zone* zone_;
  ExpressionScope* expression_scope_;

  std::vector<void*> pointer_buffer_;
  std::vector<std::pair<VariableProxy*, int>> variable_buffer_;

  Scanner* scanner_;

  const UnoptimizedCompileFlags flags_;
  int info_id_;

  bool has_module_in_scope_chain_ : 1;

  FunctionLiteral::EagerCompileHint default_eager_compile_hint_;

  // This struct is used to move information about the next arrow function from
  // the place where the arrow head was parsed to where the body will be parsed.
  // Nothing can be parsed between the head and the body, so it will be consumed
  // immediately after it's produced.
  // Preallocating the struct as part of the parser minimizes the cost of
  // supporting arrow functions on non-arrow expressions.
  struct NextArrowFunctionInfo {
    Scanner::Location strict_parameter_error_location =
        Scanner::Location::invalid();
    MessageTemplate strict_parameter_error_message = MessageTemplate::kNone;
    DeclarationScope* scope = nullptr;
    int function_literal_id = -1;
    bool could_be_immediately_invoked = false;

    bool HasInitialState() const { return scope == nullptr; }

    void Reset() {
      scope = nullptr;
      function_literal_id = -1;
      ClearStrictParameterError();
      could_be_immediately_invoked = false;
      DCHECK(HasInitialState());
    }

    // Tracks strict-mode parameter violations of sloppy-mode arrow heads in
    // case the function ends up becoming strict mode. Only one global place to
    // track this is necessary since arrow functions with none-simple parameters
    // cannot become strict-mode later on.
    void ClearStrictParameterError() {
      strict_parameter_error_location = Scanner::Location::invalid();
      strict_parameter_error_message = MessageTemplate::kNone;
    }
  };

  FormalParametersT* parameters_;
  NextArrowFunctionInfo next_arrow_function_info_;

  // The position of the token following the start parenthesis in the production
  // PrimaryExpression :: '(' Expression ')'
  int position_after_last_primary_expression_open_parenthesis_ = -1;

  bool accept_IN_ = true;
  bool allow_eval_cache_ = true;
};

template <typename Impl>
ParserBase<Impl>::FunctionState::FunctionState(
    FunctionState** function_state_stack, Scope** scope_stack,
    DeclarationScope* scope)
    : BlockState(scope_stack, scope),
      expected_property_count_(0),
      suspend_count_(0),
      function_state_stack_(function_state_stack),
      outer_function_state_(*function_state_stack),
      scope_(scope),
      dont_optimize_reason_(BailoutReason::kNoReason),
      next_function_is_likely_called_(false),
      previous_function_was_likely_called_(false),
      contains_function_or_eval_(false) {
  *function_state_stack = this;
  if (outer_function_state_) {
    outer_function_state_->previous_function_was_likely_called_ =
        outer_function_state_->next_function_is_likely_called_;
    outer_function_state_->next_function_is_likely_called_ = false;
  }
}

template <typename Impl>
ParserBase<Impl>::FunctionState::~FunctionState() {
  *function_state_stack_ = outer_function_state_;
}

template <typename Impl>
void ParserBase<Impl>::ReportUnexpectedToken(Token::Value token) {
  return impl()->ReportUnexpectedTokenAt(scanner_->location(), token);
}

template <typename Impl>
bool ParserBase<Impl>::ClassifyPropertyIdentifier(
    Token::Value next, ParsePropertyInfo* prop_info) {
  // Updates made here must be reflected on ParseAndClassifyIdentifier.
  if (V8_LIKELY(base::IsInRange(next, Token::kIdentifier, Token::kAsync))) {
    if (V8_UNLIKELY(impl()->IsArguments(prop_info->name) &&
                    scope()->ShouldBanArguments())) {
      ReportMessage(
          MessageTemplate::kArgumentsDisallowedInInitializerAndStaticBlock);
      return false;
    }
    return true;
  }

  if (!Token::IsValidIdentifier(next, language_mode(), is_generator(),
                                is_await_as_identifier_disallowed())) {
    ReportUnexpectedToken(next);
    return false;
  }

  DCHECK(!prop_info->is_computed_name);

  if (next == Token::kAwait) {
    DCHECK(!is_async_function());
    expression_scope()->RecordAsyncArrowParametersError(
        scanner()->peek_location(), MessageTemplate::kAwaitBindingIdentifier);
  }
  return true;
}

template <typename Impl>
typename ParserBase<Impl>::IdentifierT
ParserBase<Impl>::ParseAndClassifyIdentifier(Token::Value next) {
  // Updates made here must be reflected on ClassifyPropertyIdentifier.
  DCHECK_EQ(scanner()->current_token(), next);
  if (V8_LIKELY(base::IsInRange(next, Token::kIdentifier, Token::kAsync))) {
    IdentifierT name = impl()->GetIdentifier();
    if (V8_UNLIKELY(impl()->IsArguments(name) &&
                    scope()->ShouldBanArguments())) {
      ReportMessage(
          MessageTemplate::kArgumentsDisallowedInInitializerAndStaticBlock);
      return impl()->EmptyIdentifierString();
    }
    return name;
  }

  if (!Token::IsValidIdentifier(next, language_mode(), is_generator(),
                                is_await_as_identifier_disallowed())) {
    ReportUnexpectedToken(next);
    return impl()->EmptyIdentifierString();
  }

  if (next == Token::kAwait) {
    expression_scope()->RecordAsyncArrowParametersError(
        scanner()->location(), MessageTemplate::kAwaitBindingIdentifier);
    return impl()->GetIdentifier();
  }

  DCHECK(Token::IsStrictReservedWord(next));
  expression_scope()->RecordStrictModeParameterError(
      scanner()->location(), MessageTemplate::kUnexpectedStrictReserved);
  return impl()->GetIdentifier();
}

template <class Impl>
typename ParserBase<Impl>::IdentifierT ParserBase<Impl>::ParseIdentifier(
    FunctionKind function_kind) {
  Token::Value next = Next();

  if (!Token::IsValidIdentifier(
          next, language_mode(), IsGeneratorFunction(function_kind),
          flags().is_module() ||
              IsAwaitAsIdentifierDisallowed(function_kind))) {
    ReportUnexpectedToken(next);
    return impl()->EmptyIdentifierString();
  }

  return impl()->GetIdentifier();
}

template <typename Impl>
typename ParserBase<Impl>::IdentifierT
ParserBase<Impl>::ParseNonRestrictedIdentifier() {
  IdentifierT result = ParseIdentifier();

  if (is_strict(language_mode()) &&
      V8_UNLIKELY(impl()->IsEvalOrArguments(result))) {
    impl()->ReportMessageAt(scanner()->location(),
                            MessageTemplate::kStrictEvalArguments);
  }

  return result;
}

template <typename Impl>
typename ParserBase<Impl>::IdentifierT ParserBase<Impl>::ParsePropertyName() {
  Token::Value next = Next();
  if (V8_LIKELY(Token::IsPropertyName(next))) {
    if (peek() == Token::kColon) return impl()->GetSymbol();
    return impl()->GetIdentifier();
  }

  ReportUnexpectedToken(next);
  return impl()->EmptyIdentifierString();
}

template <typename Impl>
bool ParserBase<Impl>::IsExtraordinaryPrivateNameAccessAllowed() const {
  if (flags().parsing_while_debugging() != ParsingWhileDebugging::kYes &&
      !flags().is_repl_mode()) {
    return false;
  }
  Scope* current_scope = scope();
  while (current_scope != nullptr) {
    switch (current_scope->scope_type()) {
      case CLASS_SCOPE:
      case CATCH_SCOPE:
      case BLOCK_SCOPE:
      case WITH_SCOPE:
      case SHADOW_REALM_SCOPE:
        return false;
      // Top-level scopes.
      case REPL_MODE_SCOPE:
      case SCRIPT_SCOPE:
      case MODULE_SCOPE:
        return true;
      // Top-level wrapper function scopes.
      case FUNCTION_SCOPE:
        return info_id_ == kFunctionLiteralIdTopLevel;
      // Used by debug-evaluate. If the outer scope is top-level,
      // extraordinary private name access is allowed.
      case EVAL_SCOPE:
        current_scope = current_scope->outer_scope();
        DCHECK_NOT_NULL(current_scope);
        break;
    }
  }
  UNREACHABLE();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParsePropertyOrPrivatePropertyName() {
  int pos = position();
  IdentifierT name;
  ExpressionT key;
  Token::Value next = Next();
  if (V8_LIKELY(Token::IsPropertyName(next))) {
    name = impl()->GetSymbol();
    key = factory()->NewStringLiteral(name, pos);
  } else if (next == Token::kPrivateName) {
    // In the case of a top level function, we completely skip
    // analysing it's scope, meaning, we don't have a chance to
    // resolve private names and find that they are not enclosed in a
    // class body.
    //
    // Here, we check if this is a new private name reference in a top
    // level function and throw an error if so.
    PrivateNameScopeIterator private_name_scope_iter(scope());
    // Parse the identifier so that we can display it in the error message
    name = impl()->GetIdentifier();
    // In debug-evaluate, we relax the private name resolution to enable
    // evaluation of obj.#member outside the class bodies in top-level scopes.
    if (private_name_scope_iter.Done() &&
        !IsExtraordinaryPrivateNameAccessAllowed()) {
      impl()->ReportMessageAt(Scanner::Location(pos, pos + 1),
                              MessageTemplate::kInvalidPrivateFieldResolution,
                              impl()->GetRawNameFromIdentifier(name));
      return impl()->FailureExpression();
    }
    key =
        impl()->ExpressionFromPrivateName(&private_name_scope_iter, name, pos);
  } else {
    ReportUnexpectedToken(next);
    return impl()->FailureExpression();
  }
  impl()->PushLiteralName(name);
  return key;
}

template <typename Impl>
bool ParserBase<Impl>::ValidateRegExpFlags(RegExpFlags flags) {
  return RegExp::VerifyFlags(flags);
}

template <typename Impl>
bool ParserBase<Impl>::ValidateRegExpLiteral(const AstRawString* pattern,
                                             RegExpFlags flags,
                                             RegExpError* regexp_error) {
  // TODO(jgruber): If already validated in the preparser, skip validation in
  // the parser.
  DisallowGarbageCollection no_gc;
  ZoneScope zone_scope(zone());  // Free regexp parser memory after use.
  const unsigned char* d = pattern->raw_data();
  if (pattern->is_one_byte()) {
    return RegExp::VerifySyntax(zone(), stack_limit(),
                                static_cast<const uint8_t*>(d),
                                pattern->length(), flags, regexp_error, no_gc);
  } else {
    return RegExp::VerifySyntax(zone(), stack_limit(),
                                reinterpret_cast<const uint16_t*>(d),
                                pattern->length(), flags, regexp_error, no_gc);
  }
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseRegExpLiteral() {
  int pos = peek_position();
  if (!scanner()->ScanRegExpPattern()) {
    Next();
    ReportMessage(MessageTemplate::kUnterminatedRegExp);
    return impl()->FailureExpression();
  }

  const AstRawString* pattern = GetNextSymbolForRegExpLiteral();
  std::optional<RegExpFlags> flags = scanner()->ScanRegExpFlags();
  const AstRawString* flags_as_ast_raw_string = GetNextSymbolForRegExpLiteral();
  if (!flags.has_value() || !ValidateRegExpFlags(flags.value())) {
    Next();
    ReportMessage(MessageTemplate::kMalformedRegExpFlags);
    return impl()->FailureExpression();
  }
  Next();
  RegExpError regexp_error;
  if (!ValidateRegExpLiteral(pattern, flags.value(), &regexp_error)) {
    if (RegExpErrorIsStackOverflow(regexp_error)) set_stack_overflow();
    ReportMessage(MessageTemplate::kMalformedRegExp, pattern,
                  flags_as_ast_raw_string, RegExpErrorString(regexp_error));
    return impl()->FailureExpression();
  }
  return factory()->NewRegExpLiteral(pattern, flags.value(), pos);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseBindingPattern() {
  // Pattern ::
  //   Identifier
  //   ArrayLiteral
  //   ObjectLiteral

  int beg_pos = peek_position();
  Token::Value token = peek();
  ExpressionT result;

  if (Token::IsAnyIdentifier(token)) {
    IdentifierT name = ParseAndClassifyIdentifier(Next());
    if (V8_UNLIKELY(is_strict(language_mode()) &&
                    impl()->IsEvalOrArguments(name))) {
      impl()->ReportMessageAt(scanner()->location(),
                              MessageTemplate::kStrictEvalArguments);
      return impl()->FailureExpression();
    }
    return impl()->ExpressionFromIdentifier(name, beg_pos);
  }

  CheckStackOverflow();

  if (token == Token::kLeftBracket) {
    result = ParseArrayLiteral();
  } else if (token == Token::kLeftBrace) {
    result = ParseObjectLiteral();
  } else {
    ReportUnexpectedToken(Next());
    return impl()->FailureExpression();
  }

  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParsePrimaryExpression() {
  CheckStackOverflow();

  // PrimaryExpression ::
  //   'this'
  //   'null'
  //   'true'
  //   'false'
  //   Identifier
  //   Number
  //   String
  //   ArrayLiteral
  //   ObjectLiteral
  //   RegExpLiteral
  //   ClassLiteral
  //   '(' Expression ')'
  //   TemplateLiteral
  //   do Block
  //   AsyncFunctionLiteral

  int beg_pos = peek_position();
  Token::Value token = peek();

  if (Token::IsAnyIdentifier(token)) {
    Consume(token);

    FunctionKind kind = FunctionKind::kArrowFunction;

    if (V8_UNLIKELY(token == Token::kAsync &&
                    !scanner()->HasLineTerminatorBeforeNext() &&
                    !scanner()->literal_contains_escapes())) {
      // async function ...
      if (peek() == Token::kFunction) return ParseAsyncFunctionLiteral();

      // async Identifier => ...
      if (peek_any_identifier() && PeekAhead() == Token::kArrow) {
        token = Next();
        beg_pos = position();
        kind = FunctionKind::kAsyncArrowFunction;
      }
    }

    if (V8_UNLIKELY(peek() == Token::kArrow)) {
      ArrowHeadParsingScope parsing_scope(impl(), kind, PeekNextInfoId());
      IdentifierT name = ParseAndClassifyIdentifier(token);
      ClassifyParameter(name, beg_pos, end_position());
      ExpressionT result =
          impl()->ExpressionFromIdentifier(name, beg_pos, InferName::kNo);
      parsing_scope.SetInitializers(0, peek_position());
      next_arrow_function_info_.scope = parsing_scope.ValidateAndCreateScope();
      next_arrow_function_info_.function_literal_id =
          parsing_scope.function_literal_id();
      next_arrow_function_info_.could_be_immediately_invoked =
          position_after_last_primary_expression_open_parenthesis_ == beg_pos;
      return result;
    }

    IdentifierT name = ParseAndClassifyIdentifier(token);
    return impl()->ExpressionFromIdentifier(name, beg_pos);
  }

  if (Token::IsLiteral(token)) {
    return impl()->ExpressionFromLiteral(Next(), beg_pos);
  }

  switch (token) {
    case Token::kNew:
      return ParseMemberWithPresentNewPrefixesExpression();

    case Token::kThis: {
      Consume(Token::kThis);
      // Not necessary for this.x, this.x(), this?.x and this?.x() to
      // store the source position for ThisExpression.
      if (peek() == Token::kPeriod || peek() == Token::kQuestionPeriod) {
        return impl()->ThisExpression();
      }
      return impl()->NewThisExpression(beg_pos);
    }

    case Token::kAssignDiv:
    case Token::kDiv:
      return ParseRegExpLiteral();

    case Token::kFunction:
      return ParseFunctionExpression();

    case Token::kSuper: {
      return ParseSuperExpression();
    }
    case Token::kImport:
      return ParseImportExpressions();

    case Token::kLeftBracket:
      return ParseArrayLiteral();

    case Token::kLeftBrace:
      return ParseObjectLiteral();

    case Token::kLeftParen: {
      Consume(Token::kLeftParen);

      if (Check(Token::kRightParen)) {
        // clear last next_arrow_function_info tracked strict parameters error.
        next_arrow_function_info_.ClearStrictParameterError();

        // ()=>x.  The continuation that consumes the => is in
        // ParseAssignmentExpressionCoverGrammar.
        if (peek() != Token::kArrow) ReportUnexpectedToken(Token::kRightParen);
        next_arrow_function_info_.scope =
            NewFunctionScope(FunctionKind::kArrowFunction);
        next_arrow_function_info_.function_literal_id = PeekNextInfoId();
        next_arrow_function_info_.could_be_immediately_invoked =
            position_after_last_primary_expression_open_parenthesis_ == beg_pos;
        return factory()->NewEmptyParentheses(beg_pos);
      }
      Scope::Snapshot scope_snapshot(scope());
      bool could_be_immediately_invoked_arrow_function =
          position_after_last_primary_expression_open_parenthesis_ == beg_pos;
      ArrowHeadParsingScope maybe_arrow(impl(), FunctionKind::kArrowFunction,
                                        PeekNextInfoId());
      position_after_last_primary_expression_open_parenthesis_ =
          peek_position();
      // Heuristically try to detect immediately called functions before
      // seeing the call parentheses.
      if (peek() == Token::kFunction ||
          (peek() == Token::kAsync && PeekAhead() == Token::kFunction)) {
        function_state_->set_next_function_is_likely_called();
      }
      AcceptINScope scope(this, true);
      ExpressionT expr = ParseExpressionCoverGrammar();
      expr->mark_parenthesized();
      Expect(Token::kRightParen);

      if (peek() == Token::kArrow) {
        next_arrow_function_info_.scope = maybe_arrow.ValidateAndCreateScope();
        next_arrow_function_info_.function_literal_id =
            maybe_arrow.function_literal_id();
        next_arrow_function_info_.could_be_immediately_invoked =
            could_be_immediately_invoked_arrow_function;
        scope_snapshot.Reparent(next_arrow_function_info_.scope);
      } else {
        maybe_arrow.ValidateExpression();
      }

      return expr;
    }

    case Token::kClass: {
      return ParseClassExpression(scope());
    }

    case Token::kTemplateSpan:
    case Token::kTemplateTail:
      return ParseTemplateLiteral(impl()->NullExpression(), beg_pos, false);

    case Token::kMod:
      if (flags().allow_natives_syntax() || impl()->ParsingExtension()) {
        return ParseV8Intrinsic();
      }
      break;

    default:
      break;
  }

  ReportUnexpectedToken(Next());
  return impl()->FailureExpression();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseExpression() {
  ExpressionParsingScope expression_scope(impl());
  AcceptINScope scope(this, true);
  ExpressionT result = ParseExpressionCoverGrammar();
  expression_scope.ValidateExpression();
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseConditionalChainAssignmentExpression() {
  ExpressionParsingScope expression_scope(impl());
  ExpressionT result = ParseConditionalChainAssignmentExpressionCoverGrammar();
  expression_scope.ValidateExpression();
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseAssignmentExpression() {
  ExpressionParsingScope expression_scope(impl());
  ExpressionT result = ParseAssignmentExpressionCoverGrammar();
  expression_scope.ValidateExpression();
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseExpressionCoverGrammar() {
  // Expression ::
  //   AssignmentExpression
  //   Expression ',' AssignmentExpression

  ExpressionListT list(pointer_buffer());
  ExpressionT expression;
  AccumulationScope accumulation_scope(expression_scope());
  int variable_index = 0;
  while (true) {
    if (V8_UNLIKELY(peek() == Token::kEllipsis)) {
      return ParseArrowParametersWithRest(&list, &accumulation_scope,
                                          variable_index);
    }

    int expr_pos = peek_position();
    expression = ParseAssignmentExpressionCoverGrammar();

    ClassifyArrowParameter(&accumulation_scope, expr_pos, expression);
    list.Add(expression);

    variable_index =
        expression_scope()->SetInitializers(variable_index, peek_position());

    if (!Check(Token::kComma)) break;

    if (peek() == Token::kRightParen && PeekAhead() == Token::kArrow) {
      // a trailing comma is allowed at the end of an arrow parameter list
      break;
    }

    // Pass on the 'set_next_function_is_likely_called' flag if we have
    // several function literals separated by comma.
    if (peek() == Token::kFunction &&
        function_state_->previous_function_was_likely_called()) {
      function_state_->set_next_function_is_likely_called();
    }
  }

  // Return the single element if the list is empty. We need to do this because
  // callers of this function care about the type of the result if there was
  // only a single assignment expression. The preparser would lose this
  // information otherwise.
  if (list.length() == 1) return expression;
  return impl()->ExpressionListToExpression(list);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseArrowParametersWithRest(
    typename ParserBase<Impl>::ExpressionListT* list,
    AccumulationScope* accumulation_scope, int seen_variables) {
  Consume(Token::kEllipsis);

  Scanner::Location ellipsis = scanner()->location();
  int pattern_pos = peek_position();
  ExpressionT pattern = ParseBindingPattern();
  ClassifyArrowParameter(accumulation_scope, pattern_pos, pattern);

  expression_scope()->RecordNonSimpleParameter();

  if (V8_UNLIKELY(peek() == Token::kAssign)) {
    ReportMessage(MessageTemplate::kRestDefaultInitializer);
    return impl()->FailureExpression();
  }

  ExpressionT spread =
      factory()->NewSpread(pattern, ellipsis.beg_pos, pattern_pos);
  if (V8_UNLIKELY(peek() == Token::kComma)) {
    ReportMessage(MessageTemplate::kParamAfterRest);
    return impl()->FailureExpression();
  }

  expression_scope()->SetInitializers(seen_variables, peek_position());

  // 'x, y, ...z' in CoverParenthesizedExpressionAndArrowParameterList only
  // as the formal parameters of'(x, y, ...z) => foo', and is not itself a
  // valid expression.
  if (peek() != Token::kRightParen || PeekAhead() != Token::kArrow) {
    impl()->ReportUnexpectedTokenAt(ellipsis, Token::kEllipsis);
    return impl()->FailureExpression();
  }

  list->Add(spread);
  return impl()->ExpressionListToExpression(*list);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseArrayLiteral() {
  // ArrayLiteral ::
  //   '[' Expression? (',' Expression?)* ']'

  int pos = peek_position();
  ExpressionListT values(pointer_buffer());
  int first_spread_index = -1;
  Consume(Token::kLeftBracket);

  AccumulationScope accumulation_scope(expression_scope());

  while (!Check(Token::kRightBracket)) {
    ExpressionT elem;
    if (peek() == Token::kComma) {
      elem = factory()->NewTheHoleLiteral();
    } else if (Check(Token::kEllipsis)) {
      int start_pos = position();
      int expr_pos = peek_position();
      AcceptINScope scope(this, true);
      ExpressionT argument =
          ParsePossibleDestructuringSubPattern(&accumulation_scope);
      elem = factory()->NewSpread(argument, start_pos, expr_pos);

      if (first_spread_index < 0) {
        first_spread_index = values.length();
      }

      if (argument->IsAssignment()) {
        expression_scope()->RecordPatternError(
            Scanner::Location(start_pos, end_position()),
            MessageTemplate::kInvalidDestructuringTarget);
      }

      if (peek() == Token::kComma) {
        expression_scope()->RecordPatternError(
            Scanner::Location(start_pos, end_position()),
            MessageTemplate::kElementAfterRest);
      }
    } else {
      AcceptINScope scope(this, true);
      elem = ParsePossibleDestructuringSubPattern(&accumulation_scope);
    }
    values.Add(elem);
    if (peek() != Token::kRightBracket) {
      Expect(Token::kComma);
      if (elem->IsFailureExpression()) return elem;
    }
  }

  return factory()->NewArrayLiteral(values, first_spread_index, pos);
}

template <class Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseProperty(
    ParsePropertyInfo* prop_info) {
  DCHECK_EQ(prop_info->kind, ParsePropertyKind::kNotSet);
  DCHECK_EQ(prop_info->function_flags, ParseFunctionFlag::kIsNormal);
  DCHECK(!prop_info->is_computed_name);

  if (Check(Token::kAsync)) {
    Token::Value token = peek();
    if ((token != Token::kMul &&
         prop_info->ParsePropertyKindFromToken(token)) ||
        scanner()->HasLineTerminatorBeforeNext()) {
      prop_info->name = impl()->GetIdentifier();
      impl()->PushLiteralName(prop_info->name);
      return factory()->NewStringLiteral(prop_info->name, position());
    }
    if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
      impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
    }
    prop_info->function_flags = ParseFunctionFlag::kIsAsync;
    prop_info->kind = ParsePropertyKind::kMethod;
  }

  if (Check(Token::kMul)) {
    prop_info->function_flags |= ParseFunctionFlag::kIsGenerator;
    prop_info->kind = ParsePropertyKind::kMethod;
  }

  if (prop_info->kind == ParsePropertyKind::kNotSet &&
      base::IsInRange(peek(), Token::kGet, Token::kSet)) {
    Token::Value token = Next();
    if (prop_info->ParsePropertyKindFromToken(peek())) {
      prop_info->name = impl()->GetIdentifier();
      impl()->PushLiteralName(prop_info->name);
      return factory()->NewStringLiteral(prop_info->name, position());
    }
    if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
      impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
    }
    if (token == Token::kGet) {
      prop_info->kind = ParsePropertyKind::kAccessorGetter;
    } else if (token == Token::kSet) {
      prop_info->kind = ParsePropertyKind::kAccessorSetter;
    }
  }

  int pos = peek_position();

  // For non computed property names we normalize the name a bit:
  //
  //   "12" -> 12
  //   12.3 -> "12.3"
  //   12.30 -> "12.3"
  //   identifier -> "identifier"
  //
  // This is important because we use the property name as a key in a hash
  // table when we compute constant properties.
  bool is_array_index;
  uint32_t index;
  switch (peek()) {
    case Token::kPrivateName:
      prop_info->is_private = true;
      is_array_index = false;
      Consume(Token::kPrivateName);
      if (prop_info->kind == ParsePropertyKind::kNotSet) {
        prop_info->ParsePropertyKindFromToken(peek());
      }
      prop_info->name = impl()->GetIdentifier();
      if (V8_UNLIKELY(prop_info->position ==
                      PropertyPosition::kObjectLiteral)) {
        ReportUnexpectedToken(Token::kPrivateName);
        prop_info->kind = ParsePropertyKind::kNotSet;
        return impl()->FailureExpression();
      }
      break;

    case Token::kString:
      Consume(Token::kString);
      prop_info->name = peek() == Token::kColon ? impl()->GetSymbol()
                                                : impl()->GetIdentifier();
      is_array_index = impl()->IsArrayIndex(prop_info->name, &index);
      break;

    case Token::kSmi:
      Consume(Token::kSmi);
      index = scanner()->smi_value();
      is_array_index = true;
      // Token::kSmi were scanned from their canonical representation.
      prop_info->name = impl()->GetSymbol();
      break;

    case Token::kNumber: {
      Consume(Token::kNumber);
      prop_info->name = impl()->GetNumberAsSymbol();
      is_array_index = impl()->IsArrayIndex(prop_info->name, &index);
      break;
    }

    case Token::kBigInt: {
      Consume(Token::kBigInt);
      prop_info->name = impl()->GetBigIntAsSymbol();
      is_array_index = impl()->IsArrayIndex(prop_info->name, &index);
      break;
    }

    case Token::kLeftBracket: {
      prop_info->name = impl()->NullIdentifier();
      prop_info->is_computed_name = true;
      Consume(Token::kLeftBracket);
      AcceptINScope scope(this, true);
      ExpressionT expression = ParseAssignmentExpression();
      Expect(Token::kRightBracket);
      if (prop_info->kind == ParsePropertyKind::kNotSet) {
        prop_info->ParsePropertyKindFromToken(peek());
      }
      return expression;
    }

    case Token::kEllipsis:
      if (prop_info->kind == ParsePropertyKind::kNotSet) {
        prop_info->name = impl()->NullIdentifier();
        Consume(Token::kEllipsis);
        AcceptINScope scope(this, true);
        int start_pos = peek_position();
        ExpressionT expression =
            ParsePossibleDestructuringSubPattern(prop_info->accumulation_scope);
        prop_info->kind = ParsePropertyKind::kSpread;

        if (!IsValidReferenceExpression(expression)) {
          expression_scope()->RecordDeclarationError(
              Scanner::Location(start_pos, end_position()),
              MessageTemplate::kInvalidRestBindingPattern);
          expression_scope()->RecordPatternError(
              Scanner::Location(start_pos, end_position()),
              MessageTemplate::kInvalidRestAssignmentPattern);
        }

        if (peek() != Token::kRightBrace) {
          expression_scope()->RecordPatternError(
              scanner()->location(), MessageTemplate::kElementAfterRest);
        }
        return expression;
      }
      [[fallthrough]];

    default:
      prop_info->name = ParsePropertyName();
      is_array_index = false;
      break;
  }

  if (prop_info->kind == ParsePropertyKind::kNotSet) {
    prop_info->ParsePropertyKindFromToken(peek());
  }
  impl()->PushLiteralName(prop_info->name);
  return is_array_index ? factory()->NewNumberLiteral(index, pos)
                        : factory()->NewStringLiteral(prop_info->name, pos);
}

template <typename Impl>
bool ParserBase<Impl>::VerifyCanHaveAutoAccessorOrThrow(
    ParsePropertyInfo* prop_info, ExpressionT name_expression,
    int name_token_position) {
  switch (prop_info->kind) {
    case ParsePropertyKind::kAssign:
    case ParsePropertyKind::kClassField:
    case ParsePropertyKind::kShorthandOrClassField:
    case ParsePropertyKind::kNotSet:
      prop_info->kind = ParsePropertyKind::kAutoAccessorClassField;
      return true;
    default:
      impl()->ReportUnexpectedTokenAt(
          Scanner::Location(name_token_position, name_expression->position()),
          Token::kAccessor);
      return false;
  }
}

template <typename Impl>
bool ParserBase<Impl>::ParseCurrentSymbolAsClassFieldOrMethod(
    ParsePropertyInfo* prop_info, ExpressionT* name_expression) {
  if (peek() == Token::kLeftParen) {
    prop_info->kind = ParsePropertyKind::kMethod;
    prop_info->name = impl()->GetIdentifier();
    *name_expression = factory()->NewStringLiteral(prop_info->name, position());
    return true;
  }
  if (peek() == Token::kAssign || peek() == Token::kSemicolon ||
      peek() == Token::kRightBrace) {
    prop_info->name = impl()->GetIdentifier();
    *name_expression = factory()->NewStringLiteral(prop_info->name, position());
    return true;
  }
  return false;
}

template <typename Impl>
bool ParserBase<Impl>::ParseAccessorPropertyOrAutoAccessors(
    ParsePropertyInfo* prop_info, ExpressionT* name_expression,
    int* name_token_position) {
  // accessor [no LineTerminator here] ClassElementName[?Yield, ?Await]
  // Initializer[~In, ?Yield, ?Await]opt ;
  Consume(Token::kAccessor);
  *name_token_position = scanner()->peek_location().beg_pos;
  // If there is a line terminator here, it cannot be an auto-accessor.
  if (scanner()->HasLineTerminatorBeforeNext()) {
    prop_info->kind = ParsePropertyKind::kClassField;
    prop_info->name = impl()->GetIdentifier();
    *name_expression = factory()->NewStringLiteral(prop_info->name, position());
    return true;
  }
  if (ParseCurrentSymbolAsClassFieldOrMethod(prop_info, name_expression)) {
    return true;
  }
  *name_expression = ParseProperty(prop_info);
  return VerifyCanHaveAutoAccessorOrThrow(prop_info, *name_expression,
                                          *name_token_position);
}

template <typename Impl>
typename ParserBase<Impl>::ClassLiteralPropertyT
ParserBase<Impl>::ParseClassPropertyDefinition(ClassInfo* class_info,
                                               ParsePropertyInfo* prop_info,
                                               bool has_extends) {
  DCHECK_NOT_NULL(class_info);
  DCHECK_EQ(prop_info->position, PropertyPosition::kClassLiteral);

  int next_info_id = PeekNextInfoId();

  Token::Value name_token = peek();
  int property_beg_pos = peek_position();
  int name_token_position = property_beg_pos;
  ExpressionT name_expression;
  if (name_token == Token::kStatic) {
    Consume(Token::kStatic);
    name_token_position = scanner()->peek_location().beg_pos;
    if (!ParseCurrentSymbolAsClassFieldOrMethod(prop_info, &name_expression)) {
      prop_info->is_static = true;
      if (v8_flags.js_decorators && peek() == Token::kAccessor) {
        if (!ParseAccessorPropertyOrAutoAccessors(prop_info, &name_expression,
                                                  &name_token_position)) {
          return impl()->NullLiteralProperty();
        }
      } else {
        name_expression = ParseProperty(prop_info);
      }
    }
  } else if (v8_flags.js_decorators && name_token == Token::kAccessor) {
    if (!ParseAccessorPropertyOrAutoAccessors(prop_info, &name_expression,
                                              &name_token_position)) {
      return impl()->NullLiteralProperty();
    }
  } else {
    name_expression = ParseProperty(prop_info);
  }

  switch (prop_info->kind) {
    case ParsePropertyKind::kAssign:
    case ParsePropertyKind::kAutoAccessorClassField:
    case ParsePropertyKind::kClassField:
    case ParsePropertyKind::kShorthandOrClassField:
    case ParsePropertyKind::kNotSet: {  // This case is a name followed by a
                                        // name or other property. Here we have
                                        // to assume that's an uninitialized
                                        // field followed by a linebreak
                                        // followed by a property, with ASI
                                        // adding the semicolon. If not, there
                                        // will be a syntax error after parsing
                                        // the first name as an uninitialized
                                        // field.
      DCHECK_IMPLIES(prop_info->is_computed_name, !prop_info->is_private);

      if (prop_info->is_computed_name) {
        if (!has_error() && next_info_id != PeekNextInfoId() &&
            !(prop_info->is_static ? class_info->has_static_elements()
                                   : class_info->has_instance_members())) {
          impl()->ReindexComputedMemberName(name_expression);
        }
      } else {
        CheckClassFieldName(prop_info->name, prop_info->is_static);
      }

      ExpressionT value = ParseMemberInitializer(
          class_info, property_beg_pos, next_info_id, prop_info->is_static);
      ExpectSemicolon();

      ClassLiteralPropertyT result;
      if (prop_info->kind == ParsePropertyKind::kAutoAccessorClassField) {
        // Declare the auto-accessor synthetic getter and setter here where we
        // have access to the property position in parsing and preparsing.
        result = impl()->NewClassLiteralPropertyWithAccessorInfo(
            scope()->AsClassScope(), class_info, prop_info->name,
            name_expression, value, prop_info->is_static,
            prop_info->is_computed_name, prop_info->is_private,
            property_beg_pos);
      } else {
        prop_info->kind = ParsePropertyKind::kClassField;
        result = factory()->NewClassLiteralProperty(
            name_expression, value, ClassLiteralProperty::FIELD,
            prop_info->is_static, prop_info->is_computed_name,
            prop_info->is_private);
      }
      impl()->SetFunctionNameFromPropertyName(result, prop_info->name);

      return result;
    }
    case ParsePropertyKind::kMethod: {
      // MethodDefinition
      //    PropertyName '(' StrictFormalParameters ')' '{' FunctionBody '}'
      //    '*' PropertyName '(' StrictFormalParameters ')' '{' FunctionBody '}'
      //    async PropertyName '(' StrictFormalParameters ')'
      //        '{' FunctionBody '}'
      //    async '*' PropertyName '(' StrictFormalParameters ')'
      //        '{' FunctionBody '}'

      if (!prop_info->is_computed_name) {
        CheckClassMethodName(prop_info->name, ParsePropertyKind::kMethod,
                             prop_info->function_flags, prop_info->is_static,
                             &class_info->has_seen_constructor);
      }

      FunctionKind kind =
          MethodKindFor(prop_info->is_static, prop_info->function_flags);

      if (!prop_info->is_static && impl()->IsConstructor(prop_info->name)) {
        class_info->has_seen_constructor = true;
        kind = has_extends ? FunctionKind::kDerivedConstructor
                           : FunctionKind::kBaseConstructor;
      }

      ExpressionT value = impl()->ParseFunctionLiteral(
          prop_info->name, scanner()->location(), kSkipFunctionNameCheck, kind,
          name_token_position, FunctionSyntaxKind::kAccessorOrMethod,
          language_mode(), nullptr);

      ClassLiteralPropertyT result = factory()->NewClassLiteralProperty(
          name_expression, value, ClassLiteralProperty::METHOD,
          prop_info->is_static, prop_info->is_computed_name,
          prop_info->is_private);
      impl()->SetFunctionNameFromPropertyName(result, prop_info->name);
      return result;
    }

    case ParsePropertyKind::kAccessorGetter:
    case ParsePropertyKind::kAccessorSetter: {
      DCHECK_EQ(prop_info->function_flags, ParseFunctionFlag::kIsNormal);
      bool is_get = prop_info->kind == ParsePropertyKind::kAccessorGetter;

      if (!prop_info->is_computed_name) {
        CheckClassMethodName(prop_info->name, prop_info->kind,
                             ParseFunctionFlag::kIsNormal, prop_info->is_static,
                             &class_info->has_seen_constructor);
        // Make sure the name expression is a string since we need a Name for
        // Runtime_DefineAccessorPropertyUnchecked and since we can determine
        // this statically we can skip the extra runtime check.
        name_expression = factory()->NewStringLiteral(
            prop_info->name, name_expression->position());
      }

      FunctionKind kind;
      if (prop_info->is_static) {
        kind = is_get ? FunctionKind::kStaticGetterFunction
                      : FunctionKind::kStaticSetterFunction;
      } else {
        kind = is_get ? FunctionKind::kGetterFunction
                      : FunctionKind::kSetterFunction;
      }

      FunctionLiteralT value = impl()->ParseFunctionLiteral(
          prop_info->name, scanner()->location(), kSkipFunctionNameCheck, kind,
          name_token_position, FunctionSyntaxKind::kAccessorOrMethod,
          language_mode(), nullptr);

      ClassLiteralProperty::Kind property_kind =
          is_get ? ClassLiteralProperty::GETTER : ClassLiteralProperty::SETTER;
      ClassLiteralPropertyT result = factory()->NewClassLiteralProperty(
          name_expression, value, property_kind, prop_info->is_static,
          prop_info->is_computed_name, prop_info->is_private);
      const AstRawString* prefix =
          is_get ? ast_value_factory()->get_space_string()
                 : ast_value_factory()->set_space_string();
      impl()->SetFunctionNameFromPropertyName(result, prop_info->name, prefix);
      return result;
    }
    case ParsePropertyKind::kValue:
    case ParsePropertyKind::kShorthand:
    case ParsePropertyKind::kSpread:
      impl()->ReportUnexpectedTokenAt(
          Scanner::Location(name_token_position, name_expression->position()),
          name_token);
      return impl()->NullLiteralProperty();
  }
  UNREACHABLE();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseMemberInitializer(
    ClassInfo* class_info, int beg_pos, int info_id, bool is_static) {
  FunctionParsingScope body_parsing_scope(impl());
  DeclarationScope* initializer_scope =
      is_static
          ? class_info->EnsureStaticElementsScope(this, beg_pos, info_id)
          : class_info->EnsureInstanceMembersScope(this, beg_pos, info_id);

  if (Check(Token::kAssign)) {
    FunctionState initializer_state(&function_state_, &scope_,
                                    initializer_scope);

    AcceptINScope scope(this, true);
    auto result = ParseAssignmentExpression();
    initializer_scope->set_end_position(end_position());
    return result;
  }
  initializer_scope->set_end_position(end_position());
  return factory()->NewUndefinedLiteral(kNoSourcePosition);
}

template <typename Impl>
typename ParserBase<Impl>::BlockT ParserBase<Impl>::ParseClassStaticBlock(
    ClassInfo* class_info) {
  Consume(Token::kStatic);

  DeclarationScope* initializer_scope =
      class_info->EnsureStaticElementsScope(this, position(), PeekNextInfoId());

  FunctionState initializer_state(&function_state_, &scope_, initializer_scope);
  FunctionParsingScope body_parsing_scope(impl());
  AcceptINScope accept_in(this, true);

  // Each static block has its own var and lexical scope, so make a new var
  // block scope instead of using the synthetic members initializer function
  // scope.
  DeclarationScope* static_block_var_scope = NewVarblockScope();
  BlockT static_block = ParseBlock(nullptr, static_block_var_scope);
  CheckConflictingVarDeclarations(static_block_var_scope);
  initializer_scope->set_end_position(end_position());
  return static_block;
}

template <typename Impl>
typename ParserBase<Impl>::ObjectLiteralPropertyT
ParserBase<Impl>::ParseObjectPropertyDefinition(ParsePropertyInfo* prop_info,
                                                bool* has_seen_proto) {
  DCHECK_EQ(prop_info->position, PropertyPosition::kObjectLiteral);
  Token::Value name_token = peek();
  Scanner::Location next_loc = scanner()->peek_location();

  ExpressionT name_expression = ParseProperty(prop_info);

  DCHECK_IMPLIES(name_token == Token::kPrivateName, has_error());

  IdentifierT name = prop_info->name;
  ParseFunctionFlags function_flags = prop_info->function_flags;

  switch (prop_info->kind) {
    case ParsePropertyKind::kSpread:
      DCHECK_EQ(function_flags, ParseFunctionFlag::kIsNormal);
      DCHECK(!prop_info->is_computed_name);
      DCHECK_EQ(Token::kEllipsis, name_token);

      prop_info->is_computed_name = true;
      prop_info->is_rest = true;

      return factory()->NewObjectLiteralProperty(
          factory()->NewTheHoleLiteral(), name_expression,
          ObjectLiteralProperty::SPREAD, true);

    case ParsePropertyKind::kValue: {
      DCHECK_EQ(function_flags, ParseFunctionFlag::kIsNormal);

      if (!prop_info->is_computed_name &&
          scanner()->CurrentLiteralEquals("__proto__")) {
        if (*has_seen_proto) {
          expression_scope()->RecordExpressionError(
              scanner()->location(), MessageTemplate::kDuplicateProto);
        }
        *has_seen_proto = true;
      }
      Consume(Token::kColon);
      AcceptINScope scope(this, true);
      ExpressionT value =
          ParsePossibleDestructuringSubPattern(prop_info->accumulation_scope);

      ObjectLiteralPropertyT result = factory()->NewObjectLiteralProperty(
          name_expression, value, prop_info->is_computed_name);
      impl()->SetFunctionNameFromPropertyName(result, name);
      return result;
    }

    case ParsePropertyKind::kAssign:
    case ParsePropertyKind::kShorthandOrClassField:
    case ParsePropertyKind::kShorthand: {
      // PropertyDefinition
      //    IdentifierReference
      //    CoverInitializedName
      //
      // CoverInitializedName
      //    IdentifierReference Initializer?
      DCHECK_EQ(function_flags, ParseFunctionFlag::kIsNormal);

      if (!ClassifyPropertyIdentifier(name_token, prop_info)) {
        return impl()->NullLiteralProperty();
      }

      ExpressionT lhs =
          impl()->ExpressionFromIdentifier(name, next_loc.beg_pos);
      if (!IsAssignableIdentifier(lhs)) {
        expression_scope()->RecordPatternError(
            next_loc, MessageTemplate::kStrictEvalArguments);
      }

      ExpressionT value;
      if (peek() == Token::kAssign) {
        Consume(Token::kAssign);
        {
          AcceptINScope scope(this, true);
          ExpressionT rhs = ParseAssignmentExpression();
          value = factory()->NewAssignment(Token::kAssign, lhs, rhs,
                                           kNoSourcePosition);
          impl()->SetFunctionNameFromIdentifierRef(rhs, lhs);
        }
        expression_scope()->RecordExpressionError(
            Scanner::Location(next_loc.beg_pos, end_position()),
            MessageTemplate::kInvalidCoverInitializedName);
      } else {
        value = lhs;
      }

      ObjectLiteralPropertyT result = factory()->NewObjectLiteralProperty(
          name_expression, value, ObjectLiteralProperty::COMPUTED, false);
      impl()->SetFunctionNameFromPropertyName(result, name);
      return result;
    }

    case ParsePropertyKind::kMethod: {
      // MethodDefinition
      //    PropertyName '(' StrictFormalParameters ')' '{' FunctionBody '}'
      //    '*' PropertyName '(' StrictFormalParameters ')' '{' FunctionBody '}'

      expression_scope()->RecordPatternError(
          Scanner::Location(next_loc.beg_pos, end_position()),
          MessageTemplate::kInvalidDestructuringTarget);

      std::unique_ptr<BlockState> block_state;
      if (object_literal_scope_ != nullptr) {
        DCHECK_EQ(object_literal_scope_->outer_scope(), scope_);
        block_state.reset(new BlockState(&scope_, object_literal_scope_));
      }
      constexpr bool kIsStatic = false;
      FunctionKind kind = MethodKindFor(kIsStatic, function_flags);

      ExpressionT value = impl()->ParseFunctionLiteral(
          name, scanner()->location(), kSkipFunctionNameCheck, kind,
          next_loc.beg_pos, FunctionSyntaxKind::kAccessorOrMethod,
          language_mode(), nullptr);

      ObjectLiteralPropertyT result = factory()->NewObjectLiteralProperty(
          name_expression, value, ObjectLiteralProperty::COMPUTED,
          prop_info->is_computed_name);
      impl()->SetFunctionNameFromPropertyName(result, name);
      return result;
    }

    case ParsePropertyKind::kAccessorGetter:
    case ParsePropertyKind::kAccessorSetter: {
      DCHECK_EQ(function_flags, ParseFunctionFlag::kIsNormal);
      bool is_get = prop_info->kind == ParsePropertyKind::kAccessorGetter;

      expression_scope()->RecordPatternError(
          Scanner::Location(next_loc.beg_pos, end_position()),
          MessageTemplate::kInvalidDestructuringTarget);

      if (!prop_info->is_computed_name) {
        // Make sure the name expression is a string since we need a Name for
        // Runtime_DefineAccessorPropertyUnchecked and since we can determine
        // this statically we can skip the extra runtime check.
        name_expression =
            factory()->NewStringLiteral(name, name_expression->position());
      }

      std::unique_ptr<BlockState> block_state;
      if (object_literal_scope_ != nullptr) {
        DCHECK_EQ(object_literal_scope_->outer_scope(), scope_);
        block_state.reset(new BlockState(&scope_, object_literal_scope_));
      }

      FunctionKind kind = is_get ? FunctionKind::kGetterFunction
                                 : FunctionKind::kSetterFunction;

      FunctionLiteralT value = impl()->ParseFunctionLiteral(
          name, scanner()->location(), kSkipFunctionNameCheck, kind,
          next_loc.beg_pos, FunctionSyntaxKind::kAccessorOrMethod,
          language_mode(), nullptr);

      ObjectLiteralPropertyT result = factory()->NewObjectLiteralProperty(
          name_expression, value,
          is_get ? ObjectLiteralProperty::GETTER
                 : ObjectLiteralProperty::SETTER,
          prop_info->is_computed_name);
      const AstRawString* prefix =
          is_get ? ast_value_factory()->get_space_string()
                 : ast_value_factory()->set_space_string();
      impl()->SetFunctionNameFromPropertyName(result, name, prefix);
      return result;
    }

    case ParsePropertyKind::kAutoAccessorClassField:
    case ParsePropertyKind::kClassField:
    case ParsePropertyKind::kNotSet:
      ReportUnexpectedToken(Next());
      return impl()->NullLiteralProperty();
  }
  UNREACHABLE();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseObjectLiteral() {
  // ObjectLiteral ::
  // '{' (PropertyDefinition (',' PropertyDefinition)* ','? )? '}'

  int pos = peek_position();
  ObjectPropertyListT properties(pointer_buffer());
  int number_of_boilerplate_properties = 0;

  bool has_computed_names = false;
  bool has_rest_property = false;
  bool has_seen_proto = false;

  Consume(Token::kLeftBrace);
  AccumulationScope accumulation_scope(expression_scope());

  // If methods appear inside the object literal, we'll enter this scope.
  Scope* block_scope = NewBlockScopeForObjectLiteral();
  block_scope->set_start_position(pos);
  BlockState object_literal_scope_state(&object_literal_scope_, block_scope);

  while (!Check(Token::kRightBrace)) {
    FuncNameInferrerState fni_state(&fni_);

    ParsePropertyInfo prop_info(this, &accumulation_scope);
    prop_info.position = PropertyPosition::kObjectLiteral;
    ObjectLiteralPropertyT property =
        ParseObjectPropertyDefinition(&prop_info, &has_seen_proto);
    if (impl()->IsNull(property)) return impl()->FailureExpression();

    if (prop_info.is_computed_name) {
      has_computed_names = true;
    }

    if (prop_info.is_rest) {
      has_rest_property = true;
    }

    if (impl()->IsBoilerplateProperty(property) && !has_computed_names) {
      // Count CONSTANT or COMPUTED properties to maintain the enumeration
      // order.
      number_of_boilerplate_properties++;
    }

    properties.Add(property);

    if (peek() != Token::kRightBrace) {
      Expect(Token::kComma);
    }

    fni_.Infer();
  }

  Variable* home_object = nullptr;
  if (block_scope->needs_home_object()) {
    home_object = block_scope->DeclareHomeObjectVariable(ast_value_factory());
    block_scope->set_end_position(end_position());
  } else {
    block_scope = block_scope->FinalizeBlockScope();
    DCHECK_NULL(block_scope);
  }

  // In pattern rewriter, we rewrite rest property to call out to a
  // runtime function passing all the other properties as arguments to
  // this runtime function. Here, we make sure that the number of
  // properties is less than number of arguments allowed for a runtime
  // call.
  if (has_rest_property && properties.length() > Code::kMaxArguments) {
    expression_scope()->RecordPatternError(Scanner::Location(pos, position()),
                                           MessageTemplate::kTooManyArguments);
  }

  return impl()->InitializeObjectLiteral(
      factory()->NewObjectLiteral(properties, number_of_boilerplate_properties,
                                  pos, has_rest_property, home_object));
}

template <typename Impl>
void ParserBase<Impl>::ParseArguments(
    typename ParserBase<Impl>::ExpressionListT* args, bool* has_spread,
    ParsingArrowHeadFlag maybe_arrow) {
  // Arguments ::
  //   '(' (AssignmentExpression)*[','] ')'

  *has_spread = false;
  Consume(Token::kLeftParen);
  AccumulationScope accumulation_scope(expression_scope());

  int variable_index = 0;
  while (peek() != Token::kRightParen) {
    int start_pos = peek_position();
    bool is_spread = Check(Token::kEllipsis);
    int expr_pos = peek_position();

    AcceptINScope scope(this, true);
    ExpressionT argument = ParseAssignmentExpressionCoverGrammar();

    if (V8_UNLIKELY(maybe_arrow == kMaybeArrowHead)) {
      ClassifyArrowParameter(&accumulation_scope, expr_pos, argument);
      if (is_spread) {
        expression_scope()->RecordNonSimpleParameter();
        if (argument->IsAssignment()) {
          expression_scope()->RecordAsyncArrowParametersError(
              scanner()->location(), MessageTemplate::kRestDefaultInitializer);
        }
        if (peek() == Token::kComma) {
          expression_scope()->RecordAsyncArrowParametersError(
              scanner()->peek_location(), MessageTemplate::kParamAfterRest);
        }
      }
    }
    if (is_spread) {
      *has_spread = true;
      argument = factory()->NewSpread(argument, start_pos, expr_pos);
    }
    args->Add(argument);

    variable_index =
        expression_scope()->SetInitializers(variable_index, peek_position());

    if (!Check(Token::kComma)) break;
  }

  if (args->length() > Code::kMaxArguments) {
    ReportMessage(MessageTemplate::kTooManyArguments);
    return;
  }

  Scanner::Location location = scanner_->location();
  if (!Check(Token::kRightParen)) {
    impl()->ReportMessageAt(location, MessageTemplate::kUnterminatedArgList);
  }
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseConditionalChainAssignmentExpressionCoverGrammar() {
  // AssignmentExpression ::
  //   ArrowFunction
  //   YieldExpression
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression
  int lhs_beg_pos = peek_position();

  if (peek() == Token::kYield && is_generator()) {
    return ParseYieldExpression();
  }

  FuncNameInferrerState fni_state(&fni_);

  DCHECK_IMPLIES(!has_error(), next_arrow_function_info_.HasInitialState());

  ExpressionT expression = ParseLogicalExpression();

  Token::Value op = peek();

  if (!Token::IsArrowOrAssignmentOp(op) || peek() == Token::kConditional)
    return expression;

  return ParseAssignmentExpressionCoverGrammarContinuation(lhs_beg_pos,
                                                           expression);
}

// Precedence = 2
template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseAssignmentExpressionCoverGrammar() {
  // AssignmentExpression ::
  //   ConditionalExpression
  //   ArrowFunction
  //   YieldExpression
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression
  int lhs_beg_pos = peek_position();

  if (peek() == Token::kYield && is_generator()) {
    return ParseYieldExpression();
  }

  FuncNameInferrerState fni_state(&fni_);

  DCHECK_IMPLIES(!has_error(), next_arrow_function_info_.HasInitialState());

  ExpressionT expression = ParseConditionalExpression();

  Token::Value op = peek();

  if (!Token::IsArrowOrAssignmentOp(op)) return expression;

  return ParseAssignmentExpressionCoverGrammarContinuation(lhs_beg_pos,
                                                           expression);
}

// Precedence = 2
template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseAssignmentExpressionCoverGrammarContinuation(
    int lhs_beg_pos, ExpressionT expression) {
  // AssignmentExpression ::
  //   ConditionalExpression
  //   ArrowFunction
  //   YieldExpression
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression
  Token::Value op = peek();

  // Arrow functions.
  if (V8_UNLIKELY(op == Token::kArrow)) {
    Scanner::Location loc(lhs_beg_pos, end_position());

    if (!impl()->IsIdentifier(expression) && !expression->is_parenthesized()) {
      impl()->ReportMessageAt(
          Scanner::Location(expression->position(), position()),
          MessageTemplate::kMalformedArrowFunParamList);
      return impl()->FailureExpression();
    }

    DeclarationScope* scope = next_arrow_function_info_.scope;
    int function_literal_id = next_arrow_function_info_.function_literal_id;
    scope->set_start_position(lhs_beg_pos);

    FormalParametersT parameters(scope);
    parameters.set_strict_parameter_error(
        next_arrow_function_info_.strict_parameter_error_location,
        next_arrow_function_info_.strict_parameter_error_message);
    parameters.is_simple = scope->has_simple_parameters();
    bool could_be_immediately_invoked =
        next_arrow_function_info_.could_be_immediately_invoked;
    next_arrow_function_info_.Reset();

    impl()->DeclareArrowFunctionFormalParameters(&parameters, expression, loc);
    // function_literal_id was reserved for the arrow function, but not actaully
    // allocated. This comparison allocates a function literal id for the arrow
    // function, and checks whether it's still the function id we wanted. If
    // not, we'll reindex the arrow function formal parameters to shift them all
    // 1 down to make space for the arrow function.
    if (function_literal_id != GetNextInfoId()) {
      impl()->ReindexArrowFunctionFormalParameters(&parameters);
    }

    expression = ParseArrowFunctionLiteral(parameters, function_literal_id,
                                           could_be_immediately_invoked);

    return expression;
  }

  if (V8_LIKELY(impl()->IsAssignableIdentifier(expression))) {
    if (expression->is_parenthesized()) {
      expression_scope()->RecordDeclarationError(
          Scanner::Location(lhs_beg_pos, end_position()),
          MessageTemplate::kInvalidDestructuringTarget);
    }
    expression_scope()->MarkIdentifierAsAssigned();
  } else if (expression->IsProperty()) {
    expression_scope()->RecordDeclarationError(
        Scanner::Location(lhs_beg_pos, end_position()),
        MessageTemplate::kInvalidPropertyBindingPattern);
    expression_scope()->ValidateAsExpression();
  } else if (expression->IsPattern() && op == Token::kAssign) {
    // Destructuring assignmment.
    if (expression->is_parenthesized()) {
      Scanner::Location loc(lhs_beg_pos, end_position());
      if (expression_scope()->IsCertainlyDeclaration()) {
        impl()->ReportMessageAt(loc,
                                MessageTemplate::kInvalidDestructuringTarget);
      } else {
        // Syntax Error if LHS is neither object literal nor an array literal
        // (Parenthesized literals are
        // CoverParenthesizedExpressionAndArrowParameterList).
        // #sec-assignment-operators-static-semantics-early-errors
        impl()->ReportMessageAt(loc, MessageTemplate::kInvalidLhsInAssignment);
      }
    }
    expression_scope()->ValidateAsPattern(expression, lhs_beg_pos,
                                          end_position());
  } else {
    DCHECK(!IsValidReferenceExpression(expression));
    // For web compatibility reasons, throw early errors only for logical
    // assignment, not for regular assignment.
    const bool early_error = Token::IsLogicalAssignmentOp(op);
    expression = RewriteInvalidReferenceExpression(
        expression, lhs_beg_pos, end_position(),
        MessageTemplate::kInvalidLhsInAssignment, early_error);
  }

  Consume(op);
  int op_position = position();

  ExpressionT right = ParseAssignmentExpression();

  // Anonymous function name inference applies to =, ||=, &&=, and ??=.
  if (op == Token::kAssign || Token::IsLogicalAssignmentOp(op)) {
    impl()->CheckAssigningFunctionLiteralToProperty(expression, right);

    // Check if the right hand side is a call to avoid inferring a
    // name if we're dealing with "a = function(){...}();"-like
    // expression.
    if (right->IsCall() || right->IsCallNew()) {
      fni_.RemoveLastFunction();
    } else {
      fni_.Infer();
    }

    impl()->SetFunctionNameFromIdentifierRef(right, expression);
  } else {
    fni_.RemoveLastFunction();
  }

  if (op == Token::kAssign) {
    // We try to estimate the set of properties set by constructors. We define a
    // new property whenever there is an assignment to a property of 'this'. We
    // should probably only add properties if we haven't seen them before.
    // Otherwise we'll probably overestimate the number of properties.
    if (impl()->IsThisProperty(expression)) function_state_->AddProperty();
  } else {
    // Only initializers (i.e. no compound assignments) are allowed in patterns.
    expression_scope()->RecordPatternError(
        Scanner::Location(lhs_beg_pos, end_position()),
        MessageTemplate::kInvalidDestructuringTarget);
  }

  return factory()->NewAssignment(op, expression, right, op_position);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseYieldExpression() {
  // YieldExpression ::
  //   'yield' ([no line terminator] '*'? AssignmentExpression)?
  int pos = peek_position();
  expression_scope()->RecordParameterInitializerError(
      scanner()->peek_location(), MessageTemplate::kYieldInParameter);
  Consume(Token::kYield);
  if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
    impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
  }

  CheckStackOverflow();

  // The following initialization is necessary.
  ExpressionT expression = impl()->NullExpression();
  bool delegating = false;  // yield*
  if (!scanner()->HasLineTerminatorBeforeNext()) {
    if (Check(Token::kMul)) delegating = true;
    switch (peek()) {
      case Token::kEos:
      case Token::kSemicolon:
      case Token::kRightBrace:
      case Token::kRightBracket:
      case Token::kRightParen:
      case Token::kColon:
      case Token::kComma:
      case Token::kIn:
        // The above set of tokens is the complete set of tokens that can appear
        // after an AssignmentExpression, and none of them can start an
        // AssignmentExpression.  This allows us to avoid looking for an RHS for
        // a regular yield, given only one look-ahead token.
        if (!delegating) break;
        // Delegating yields require an RHS; fall through.
        [[fallthrough]];
      default:
        expression = ParseAssignmentExpressionCoverGrammar();
        break;
    }
  }

  if (delegating) {
    ExpressionT yieldstar = factory()->NewYieldStar(expression, pos);
    impl()->RecordSuspendSourceRange(yieldstar, PositionAfterSemicolon());
    function_state_->AddSuspend();
    if (IsAsyncGeneratorFunction(function_state_->kind())) {
      // return, iterator_close and delegated_iterator_output suspend ids.
      function_state_->AddSuspend();
      function_state_->AddSuspend();
      function_state_->AddSuspend();
    }
    return yieldstar;
  }

  // Hackily disambiguate o from o.next and o [Symbol.iterator]().
  // TODO(verwaest): Come up with a better solution.
  ExpressionT yield =
      factory()->NewYield(expression, pos, Suspend::kOnExceptionThrow);
  impl()->RecordSuspendSourceRange(yield, PositionAfterSemicolon());
  function_state_->AddSuspend();
  return yield;
}

// Precedence = 3
template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseConditionalExpression() {
  // ConditionalExpression ::
  //   LogicalExpression
  //   LogicalExpression '?' AssignmentExpression ':' AssignmentExpression
  //
  int pos = peek_position();
  ExpressionT expression = ParseLogicalExpression();
  return peek() == Token::kConditional
             ? ParseConditionalChainExpression(expression, pos)
             : expression;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseLogicalExpression() {
  // LogicalExpression ::
  //   LogicalORExpression
  //   CoalesceExpression

  // Both LogicalORExpression and CoalesceExpression start with BitwiseOR.
  // Parse for binary expressions >= 6 (BitwiseOR);
  ExpressionT expression = ParseBinaryExpression(6);
  if (peek() == Token::kAnd || peek() == Token::kOr) {
    // LogicalORExpression, pickup parsing where we left off.
    int prec1 = Token::Precedence(peek(), accept_IN_);
    expression = ParseBinaryContinuation(expression, 4, prec1);
  } else if (V8_UNLIKELY(peek() == Token::kNullish)) {
    expression = ParseCoalesceExpression(expression);
  }
  return expression;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseCoalesceExpression(ExpressionT expression) {
  // CoalesceExpression ::
  //   CoalesceExpressionHead ?? BitwiseORExpression
  //
  //   CoalesceExpressionHead ::
  //     CoalesceExpression
  //     BitwiseORExpression

  // We create a binary operation for the first nullish, otherwise collapse
  // into an nary expresion.
  bool first_nullish = true;
  while (peek() == Token::kNullish) {
    SourceRange right_range;
    int pos;
    ExpressionT y;
    {
      SourceRangeScope right_range_scope(scanner(), &right_range);
      Consume(Token::kNullish);
      pos = peek_position();
      // Parse BitwiseOR or higher.
      y = ParseBinaryExpression(6);
    }
    if (first_nullish) {
      expression =
          factory()->NewBinaryOperation(Token::kNullish, expression, y, pos);
      impl()->RecordBinaryOperationSourceRange(expression, right_range);
      first_nullish = false;
    } else {
      impl()->CollapseNaryExpression(&expression, y, Token::kNullish, pos,
                                     right_range);
    }
  }
  return expression;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseConditionalChainExpression(ExpressionT condition,
                                                  int condition_pos) {
  // ConditionalChainExpression ::
  // ConditionalExpression_1 ? AssignmentExpression_1 :
  // ConditionalExpression_2 ? AssignmentExpression_2 :
  // ConditionalExpression_3 ? AssignmentExpression_3 :
  // ...
  // ConditionalExpression_n ? AssignmentExpression_n

  ExpressionT expr = impl()->NullExpression();
  ExpressionT else_expression = impl()->NullExpression();
  bool else_found = false;
  ZoneVector<int> else_ranges_beg_pos(impl()->zone());
  do {
    SourceRange then_range;
    ExpressionT then_expression;
    {
      SourceRangeScope range_scope(scanner(), &then_range);
      Consume(Token::kConditional);
      // In parsing the first assignment expression in conditional
      // expressions we always accept the 'in' keyword; see ECMA-262,
      // section 11.12, page 58.
      AcceptINScope scope(this, true);
      then_expression = ParseAssignmentExpression();
    }

    else_ranges_beg_pos.emplace_back(scanner()->peek_location().beg_pos);
    int condition_or_else_pos = peek_position();
    SourceRange condition_or_else_range = SourceRange();
    ExpressionT condition_or_else_expression;
    {
      SourceRangeScope condition_or_else_range_scope(scanner(),
                                                     &condition_or_else_range);
      Expect(Token::kColon);
      condition_or_else_expression =
          ParseConditionalChainAssignmentExpression();
    }

    else_found = (peek() != Token::kConditional);

    if (else_found) {
      else_expression = condition_or_else_expression;

      if (impl()->IsNull(expr)) {
        // When we have a single conditional expression, we don't create a
        // conditional chain expression. Instead, we just return a conditional
        // expression.
        SourceRange else_range = condition_or_else_range;
        expr = factory()->NewConditional(condition, then_expression,
                                         else_expression, condition_pos);
        impl()->RecordConditionalSourceRange(expr, then_range, else_range);
        return expr;
      }
    }

    if (impl()->IsNull(expr)) {
      // For the first conditional expression, we create a conditional chain.
      expr = factory()->NewConditionalChain(1, condition_pos);
    }

    impl()->CollapseConditionalChain(&expr, condition, then_expression,
                                     else_expression, condition_pos,
                                     then_range);

    if (!else_found) {
      condition = condition_or_else_expression;
      condition_pos = condition_or_else_pos;
    }
  } while (!else_found);

  int end_pos = scanner()->location().end_pos;
  for (const auto& else_range_beg_pos : else_ranges_beg_pos) {
    impl()->AppendConditionalChainElse(
        &expr, SourceRange(else_range_beg_pos, end_pos));
  }

  return expr;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseConditionalContinuation(ExpressionT expression,
                                               int pos) {
  SourceRange then_range, else_range;

  ExpressionT left;
  {
    SourceRangeScope range_scope(scanner(), &then_range);
    Consume(Token::kConditional);
    // In parsing the first assignment expression in conditional
    // expressions we always accept the 'in' keyword; see ECMA-262,
    // section 11.12, page 58.
    AcceptINScope scope(this, true);
    left = ParseAssignmentExpression();
  }
  ExpressionT right;
  {
    SourceRangeScope range_scope(scanner(), &else_range);
    Expect(Token::kColon);
    right = ParseAssignmentExpression();
  }
  ExpressionT expr = factory()->NewConditional(expression, left, right, pos);
  impl()->RecordConditionalSourceRange(expr, then_range, else_range);
  return expr;
}

// Precedence >= 4
template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseBinaryContinuation(ExpressionT x, int prec, int prec1) {
  do {
    // prec1 >= 4
    while (Token::Precedence(peek(), accept_IN_) == prec1) {
      SourceRange right_range;
      int pos = peek_position();
      ExpressionT y;
      Token::Value op;
      {
        SourceRangeScope right_range_scope(scanner(), &right_range);
        op = Next();

        const bool is_right_associative = op == Token::kExp;
        const int next_prec = is_right_associative ? prec1 : prec1 + 1;
        y = ParseBinaryExpression(next_prec);
      }

      // For now we distinguish between comparisons and other binary
      // operations.  (We could combine the two and get rid of this
      // code and AST node eventually.)
      if (Token::IsCompareOp(op)) {
        // We have a comparison.
        Token::Value cmp = op;
        switch (op) {
          case Token::kNotEq:
            cmp = Token::kEq;
            break;
          case Token::kNotEqStrict:
            cmp = Token::kEqStrict;
            break;
          default: break;
        }
        x = factory()->NewCompareOperation(cmp, x, y, pos);
        if (cmp != op) {
          // The comparison was negated - add a kNot.
          x = factory()->NewUnaryOperation(Token::kNot, x, pos);
        }
      } else if (!impl()->ShortcutLiteralBinaryExpression(&x, y, op, pos) &&
                 !impl()->CollapseNaryExpression(&x, y, op, pos, right_range)) {
        // We have a "normal" binary operation.
        x = factory()->NewBinaryOperation(op, x, y, pos);
        if (op == Token::kOr || op == Token::kAnd) {
          impl()->RecordBinaryOperationSourceRange(x, right_range);
        }
      }
    }
    --prec1;
  } while (prec1 >= prec);

  return x;
}

// Precedence >= 4
template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseBinaryExpression(
    int prec) {
  DCHECK_GE(prec, 4);

  // "#foo in ShiftExpression" needs to be parsed separately, since private
  // identifiers are not valid PrimaryExpressions.
  if (V8_UNLIKELY(peek() == Token::kPrivateName)) {
    ExpressionT x = ParsePropertyOrPrivatePropertyName();
    int prec1 = Token::Precedence(peek(), accept_IN_);
    if (peek() != Token::kIn || prec1 < prec) {
      ReportUnexpectedToken(Token::kPrivateName);
      return impl()->FailureExpression();
    }
    return ParseBinaryContinuation(x, prec, prec1);
  }

  ExpressionT x = ParseUnaryExpression();
  int prec1 = Token::Precedence(peek(), accept_IN_);
  if (prec1 >= prec) {
    return ParseBinaryContinuation(x, prec, prec1);
  }
  return x;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseUnaryOrPrefixExpression() {
  Token::Value op = Next();
  int pos = position();

  // Assume "! function ..." indicates the function is likely to be called.
  if (op == Token::kNot && peek() == Token::kFunction) {
    function_state_->set_next_function_is_likely_called();
  }

  CheckStackOverflow();

  int expression_position = peek_position();
  ExpressionT expression = ParseUnaryExpression();

  if (Token::IsUnaryOp(op)) {
    if (op == Token::kDelete) {
      if (impl()->IsIdentifier(expression) && is_strict(language_mode())) {
        // "delete identifier" is a syntax error in strict mode.
        ReportMessage(MessageTemplate::kStrictDelete);
        return impl()->FailureExpression();
      }

      if (impl()->IsPrivateReference(expression)) {
        ReportMessage(MessageTemplate::kDeletePrivateField);
        return impl()->FailureExpression();
      }
    }

    if (peek() == Token::kExp) {
      impl()->ReportMessageAt(
          Scanner::Location(pos, peek_end_position()),
          MessageTemplate::kUnexpectedTokenUnaryExponentiation);
      return impl()->FailureExpression();
    }

    // Allow the parser's implementation to rewrite the expression.
    return impl()->BuildUnaryExpression(expression, op, pos);
  }

  DCHECK(Token::IsCountOp(op));

  if (V8_LIKELY(IsValidReferenceExpression(expression))) {
    if (impl()->IsIdentifier(expression)) {
      expression_scope()->MarkIdentifierAsAssigned();
    }
  } else {
    const bool early_error = false;
    expression = RewriteInvalidReferenceExpression(
        expression, expression_position, end_position(),
        MessageTemplate::kInvalidLhsInPrefixOp, early_error);
  }

  return factory()->NewCountOperation(op, true /* prefix */, expression,
                                      position());
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseAwaitExpression() {
  expression_scope()->RecordParameterInitializerError(
      scanner()->peek_location(),
      MessageTemplate::kAwaitExpressionFormalParameter);
  int await_pos = peek_position();
  Consume(Token::kAwait);
  if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
    impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
  }

  CheckStackOverflow();

  ExpressionT value = ParseUnaryExpression();

  // 'await' is a unary operator according to the spec, even though it's treated
  // specially in the parser.
  if (peek() == Token::kExp) {
    impl()->ReportMessageAt(
        Scanner::Location(await_pos, peek_end_position()),
        MessageTemplate::kUnexpectedTokenUnaryExponentiation);
    return impl()->FailureExpression();
  }

  ExpressionT expr = factory()->NewAwait(value, await_pos);
  function_state_->AddSuspend();
  impl()->RecordSuspendSourceRange(expr, PositionAfterSemicolon());
  return expr;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseUnaryExpression() {
  // UnaryExpression ::
  //   PostfixExpression
  //   'delete' UnaryExpression
  //   'void' UnaryExpression
  //   'typeof' UnaryExpression
  //   '++' UnaryExpression
  //   '--' UnaryExpression
  //   '+' UnaryExpression
  //   '-' UnaryExpression
  //   '~' UnaryExpression
  //   '!' UnaryExpression
  //   [+Await] AwaitExpression[?Yield]

  Token::Value op = peek();
  if (Token::IsUnaryOrCountOp(op)) return ParseUnaryOrPrefixExpression();
  if (is_await_allowed() && op == Token::kAwait) {
    return ParseAwaitExpression();
  }
  return ParsePostfixExpression();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParsePostfixExpression() {
  // PostfixExpression ::
  //   LeftHandSideExpression ('++' | '--')?

  int lhs_beg_pos = peek_position();
  ExpressionT expression = ParseLeftHandSideExpression();
  if (V8_LIKELY(!Token::IsCountOp(peek()) ||
                scanner()->HasLineTerminatorBeforeNext())) {
    return expression;
  }
  return ParsePostfixContinuation(expression, lhs_beg_pos);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParsePostfixContinuation(ExpressionT expression,
                                           int lhs_beg_pos) {
  if (V8_UNLIKELY(!IsValidReferenceExpression(expression))) {
    const bool early_error = false;
    expression = RewriteInvalidReferenceExpression(
        expression, lhs_beg_pos, end_position(),
        MessageTemplate::kInvalidLhsInPostfixOp, early_error);
  }
  if (impl()->IsIdentifier(expression)) {
    expression_scope()->MarkIdentifierAsAssigned();
  }

  Token::Value next = Next();
  return factory()->NewCountOperation(next, false /* postfix */, expression,
                                      position());
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseLeftHandSideExpression() {
  // LeftHandSideExpression ::
  //   (NewExpression | MemberExpression) ...

  ExpressionT result = ParseMemberExpression();
  if (!Token::IsPropertyOrCall(peek())) return result;
  return ParseLeftHandSideContinuation(result);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseLeftHandSideContinuation(ExpressionT result) {
  DCHECK(Token::IsPropertyOrCall(peek()));

  if (V8_UNLIKELY(peek() == Token::kLeftParen && impl()->IsIdentifier(result) &&
                  scanner()->current_token() == Token::kAsync &&
                  !scanner()->HasLineTerminatorBeforeNext() &&
                  !scanner()->literal_contains_escapes())) {
    DCHECK(impl()->IsAsync(impl()->AsIdentifier(result)));
    int pos = position();

    ArrowHeadParsingScope maybe_arrow(impl(), FunctionKind::kAsyncArrowFunction,
                                      PeekNextInfoId());
    Scope::Snapshot scope_snapshot(scope());

    ExpressionListT args(pointer_buffer());
    bool has_spread;
    ParseArguments(&args, &has_spread, kMaybeArrowHead);
    if (V8_LIKELY(peek() == Token::kArrow)) {
      fni_.RemoveAsyncKeywordFromEnd();
      next_arrow_function_info_.scope = maybe_arrow.ValidateAndCreateScope();
      next_arrow_function_info_.function_literal_id =
          maybe_arrow.function_literal_id();
      scope_snapshot.Reparent(next_arrow_function_info_.scope);
      // async () => ...
      if (!args.length()) return factory()->NewEmptyParentheses(pos);
      // async ( Arguments ) => ...
      result = impl()->ExpressionListToExpression(args);
      result->mark_parenthesized();
      return result;
    }

    result = factory()->NewCall(result, args, pos, has_spread);

    maybe_arrow.ValidateExpression();

    fni_.RemoveLastFunction();
    if (!Token::IsPropertyOrCall(peek())) return result;
  }

  bool optional_chaining = false;
  bool is_optional = false;
  int optional_link_begin;
  do {
    switch (peek()) {
      case Token::kQuestionPeriod: {
        if (is_optional) {
          ReportUnexpectedToken(peek());
          return impl()->FailureExpression();
        }
        // Include the ?. in the source range position.
        optional_link_begin = scanner()->peek_location().beg_pos;
        Consume(Token::kQuestionPeriod);
        is_optional = true;
        optional_chaining = true;
        if (Token::IsPropertyOrCall(peek())) continue;
        int pos = position();
        ExpressionT key = ParsePropertyOrPrivatePropertyName();
        result = factory()->NewProperty(result, key, pos, is_optional);
        break;
      }

      /* Property */
      case Token::kLeftBracket: {
        Consume(Token::kLeftBracket);
        int pos = position();
        AcceptINScope scope(this, true);
        ExpressionT index = ParseExpressionCoverGrammar();
        result = factory()->NewProperty(result, index, pos, is_optional);
        Expect(Token::kRightBracket);
        break;
      }

      /* Property */
      case Token::kPeriod: {
        if (is_optional) {
          ReportUnexpectedToken(Next());
          return impl()->FailureExpression();
        }
        Consume(Token::kPeriod);
        int pos = position();
        ExpressionT key = ParsePropertyOrPrivatePropertyName();
        result = factory()->NewProperty(result, key, pos, is_optional);
        break;
      }

      /* Call */
      case Token::kLeftParen: {
        int pos;
        if (Token::IsCallable(scanner()->current_token())) {
          // For call of an identifier we want to report position of
          // the identifier as position of the call in the stack trace.
          pos = position();
        } else {
          // For other kinds of calls we record position of the parenthesis as
          // position of the call. Note that this is extremely important for
          // expressions of the form function(){...}() for which call position
          // should not point to the closing brace otherwise it will intersect
          // with positions recorded for function literal and confuse debugger.
          pos = peek_position();
          // Also the trailing parenthesis are a hint that the function will
          // be called immediately. If we happen to have parsed a preceding
          // function literal eagerly, we can also compile it eagerly.
          if (result->IsFunctionLiteral()) {
            result->AsFunctionLiteral()->SetShouldEagerCompile();
          }
        }
        bool has_spread;
        ExpressionListT args(pointer_buffer());
        ParseArguments(&args, &has_spread);

        // Keep track of eval() calls since they disable all local variable
        // optimizations.
        // The calls that need special treatment are the
        // direct eval calls. These calls are all of the form eval(...), with
        // no explicit receiver.
        // These calls are marked as potentially direct eval calls. Whether
        // they are actually direct calls to eval is determined at run time.
        int eval_scope_info_index = 0;
        if (CheckPossibleEvalCall(result, is_optional, scope())) {
          eval_scope_info_index = GetNextInfoId();
        }

        result = factory()->NewCall(result, args, pos, has_spread,
                                    eval_scope_info_index, is_optional);

        fni_.RemoveLastFunction();
        break;
      }

      default:
        // Template literals in/after an Optional Chain not supported:
        if (optional_chaining) {
          impl()->ReportMessageAt(scanner()->peek_location(),
                                  MessageTemplate::kOptionalChainingNoTemplate);
          return impl()->FailureExpression();
        }
        /* Tagged Template */
        DCHECK(Token::IsTemplate(peek()));
        result = ParseTemplateLiteral(result, position(), true);
        break;
    }
    if (is_optional) {
      SourceRange chain_link_range(optional_link_begin, end_position());
      impl()->RecordExpressionSourceRange(result, chain_link_range);
      is_optional = false;
    }
  } while (Token::IsPropertyOrCall(peek()));
  if (optional_chaining) return factory()->NewOptionalChain(result);
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseMemberWithPresentNewPrefixesExpression() {
  // NewExpression ::
  //   ('new')+ MemberExpression
  //
  // NewTarget ::
  //   'new' '.' 'target'

  // The grammar for new expressions is pretty warped. We can have several 'new'
  // keywords following each other, and then a MemberExpression. When we see '('
  // after the MemberExpression, it's associated with the rightmost unassociated
  // 'new' to create a NewExpression with arguments. However, a NewExpression
  // can also occur without arguments.

  // Examples of new expression:
  // new foo.bar().baz means (new (foo.bar)()).baz
  // new foo()() means (new foo())()
  // new new foo()() means (new (new foo())())
  // new new foo means new (new foo)
  // new new foo() means new (new foo())
  // new new foo().bar().baz means (new (new foo()).bar()).baz
  // new super.x means new (super.x)
  Consume(Token::kNew);
  int new_pos = position();
  ExpressionT result;

  CheckStackOverflow();

  if (peek() == Token::kImport && PeekAhead() == Token::kLeftParen) {
    impl()->ReportMessageAt(scanner()->peek_location(),
                            MessageTemplate::kImportCallNotNewExpression);
    return impl()->FailureExpression();
  } else if (peek() == Token::kPeriod) {
    result = ParseNewTargetExpression();
    return ParseMemberExpressionContinuation(result);
  } else {
    result = ParseMemberExpression();
    if (result->IsSuperCallReference()) {
      // new super() is never allowed
      impl()->ReportMessageAt(scanner()->location(),
                              MessageTemplate::kUnexpectedSuper);
      return impl()->FailureExpression();
    }
  }
  if (peek() == Token::kLeftParen) {
    // NewExpression with arguments.
    {
      ExpressionListT args(pointer_buffer());
      bool has_spread;
      ParseArguments(&args, &has_spread);

      result = factory()->NewCallNew(result, args, new_pos, has_spread);
    }
    // The expression can still continue with . or [ after the arguments.
    return ParseMemberExpressionContinuation(result);
  }

  if (peek() == Token::kQuestionPeriod) {
    impl()->ReportMessageAt(scanner()->peek_location(),
                            MessageTemplate::kOptionalChainingNoNew);
    return impl()->FailureExpression();
  }

  // NewExpression without arguments.
  ExpressionListT args(pointer_buffer());
  return factory()->NewCallNew(result, args, new_pos, false);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseFunctionExpression() {
  Consume(Token::kFunction);
  int function_token_position = position();

  FunctionKind function_kind = Check(Token::kMul)
                                   ? FunctionKind::kGeneratorFunction
                                   : FunctionKind::kNormalFunction;
  IdentifierT name = impl()->NullIdentifier();
  bool is_strict_reserved_name = Token::IsStrictReservedWord(peek());
  Scanner::Location function_name_location = Scanner::Location::invalid();
  FunctionSyntaxKind function_syntax_kind =
      FunctionSyntaxKind::kAnonymousExpression;
  if (impl()->ParsingDynamicFunctionDeclaration()) {
    // We don't want dynamic functions to actually declare their name
    // "anonymous". We just want that name in the toString().
    Consume(Token::kIdentifier);
    DCHECK_IMPLIES(!has_error(),
                   scanner()->CurrentSymbol(ast_value_factory()) ==
                       ast_value_factory()->anonymous_string());
  } else if (peek_any_identifier()) {
    name = ParseIdentifier(function_kind);
    function_name_location = scanner()->location();
    function_syntax_kind = FunctionSyntaxKind::kNamedExpression;
  }
  FunctionLiteralT result = impl()->ParseFunctionLiteral(
      name, function_name_location,
      is_strict_reserved_name ? kFunctionNameIsStrictReserved
                              : kFunctionNameValidityUnknown,
      function_kind, function_token_position, function_syntax_kind,
      language_mode(), nullptr);
  // TODO(verwaest): FailureFunctionLiteral?
  if (impl()->IsNull(result)) return impl()->FailureExpression();
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseMemberExpression() {
  // MemberExpression ::
  //   (PrimaryExpression | FunctionLiteral | ClassLiteral)
  //     ('[' Expression ']' | '.' Identifier | Arguments | TemplateLiteral)*
  //
  // CallExpression ::
  //   (SuperCall | ImportCall)
  //     ('[' Expression ']' | '.' Identifier | Arguments | TemplateLiteral)*
  //
  // The '[' Expression ']' and '.' Identifier parts are parsed by
  // ParseMemberExpressionContinuation, and everything preceeding it is merged
  // into ParsePrimaryExpression.

  // Parse the initial primary or function expression.
  ExpressionT result = ParsePrimaryExpression();
  return ParseMemberExpressionContinuation(result);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseImportExpressions() {
  // ImportCall[Yield, Await] :
  //   import ( AssignmentExpression[+In, ?Yield, ?Await] )
  //   import . source ( AssignmentExpression[+In, ?Yield, ?Await] )
  //
  // ImportMeta : import . meta

  Consume(Token::kImport);
  int pos = position();

  ModuleImportPhase phase = ModuleImportPhase::kEvaluation;

  // Distinguish import meta and import phase calls.
  if (Check(Token::kPeriod)) {
    if (v8_flags.js_source_phase_imports &&
        CheckContextualKeyword(ast_value_factory()->source_string())) {
      phase = ModuleImportPhase::kSource;
    } else {
      ExpectContextualKeyword(ast_value_factory()->meta_string(), "import.meta",
                              pos);
      if (!flags().is_module() && !IsParsingWhileDebugging()) {
        impl()->ReportMessageAt(scanner()->location(),
                                MessageTemplate::kImportMetaOutsideModule);
        return impl()->FailureExpression();
      }
      return impl()->ImportMetaExpression(pos);
    }
  }

  if (V8_UNLIKELY(peek() != Token::kLeftParen)) {
    if (!flags().is_module()) {
      impl()->ReportMessageAt(scanner()->location(),
                              MessageTemplate::kImportOutsideModule);
    } else {
      ReportUnexpectedToken(Next());
    }
    return impl()->FailureExpression();
  }

  Consume(Token::kLeftParen);
  if (peek() == Token::kRightParen) {
    impl()->ReportMessageAt(scanner()->location(),
                            MessageTemplate::kImportMissingSpecifier);
    return impl()->FailureExpression();
  }

  AcceptINScope scope(this, true);
  ExpressionT specifier = ParseAssignmentExpressionCoverGrammar();

  DCHECK_IMPLIES(phase == ModuleImportPhase::kSource,
                 v8_flags.js_source_phase_imports);
  // TODO(42204365): Enable import attributes with source phase import once
  // specified.
  const bool check_import_attributes = (v8_flags.harmony_import_assertions ||
                                        v8_flags.harmony_import_attributes) &&
                                       phase == ModuleImportPhase::kEvaluation;
  if (check_import_attributes && Check(Token::kComma)) {
    if (Check(Token::kRightParen)) {
      // A trailing comma allowed after the specifier.
      return factory()->NewImportCallExpression(specifier, phase, pos);
    } else {
      ExpressionT import_options = ParseAssignmentExpressionCoverGrammar();
      Check(Token::kComma);  // A trailing comma is allowed after the import
                             // assertions.
      Expect(Token::kRightParen);
      return factory()->NewImportCallExpression(specifier, phase,
                                                import_options, pos);
    }
  }

  Expect(Token::kRightParen);
  return factory()->NewImportCallExpression(specifier, phase, pos);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseSuperExpression() {
  Consume(Token::kSuper);
  int pos = position();

  DeclarationScope* scope = GetReceiverScope();
  FunctionKind kind = scope->function_kind();
  if (IsConciseMethod(kind) || IsAccessorFunction(kind) ||
      IsClassConstructor(kind)) {
    if (Token::IsProperty(peek())) {
      if (peek() == Token::kPeriod && PeekAhead() == Token::kPrivateName) {
        Consume(Token::kPeriod);
        Consume(Token::kPrivateName);

        impl()->ReportMessage(MessageTemplate::kUnexpectedPrivateField);
        return impl()->FailureExpression();
      }
      if (peek() == Token::kQuestionPeriod) {
        Consume(Token::kQuestionPeriod);
        impl()->ReportMessage(MessageTemplate::kOptionalChainingNoSuper);
        return impl()->FailureExpression();
      }
      scope->RecordSuperPropertyUsage();
      UseThis();
      return impl()->NewSuperPropertyReference(pos);
    }
    // super() is only allowed in derived constructor. new super() is never
    // allowed; it's reported as an error by
    // ParseMemberWithPresentNewPrefixesExpression.
    if (peek() == Token::kLeftParen && IsDerivedConstructor(kind)) {
      // TODO(rossberg): This might not be the correct FunctionState for the
      // method here.
      expression_scope()->RecordThisUse();
      UseThis();
      return impl()->NewSuperCallReference(pos);
    }
  }

  impl()->ReportMessageAt(scanner()->location(),
                          MessageTemplate::kUnexpectedSuper);
  return impl()->FailureExpression();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseNewTargetExpression() {
  int pos = position();
  Consume(Token::kPeriod);
  ExpectContextualKeyword(ast_value_factory()->target_string(), "new.target",
                          pos);

  if (!GetReceiverScope()->is_function_scope()) {
    impl()->ReportMessageAt(scanner()->location(),
                            MessageTemplate::kUnexpectedNewTarget);
    return impl()->FailureExpression();
  }

  return impl()->NewTargetExpression(pos);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::DoParseMemberExpressionContinuation(ExpressionT expression) {
  DCHECK(Token::IsMember(peek()));
  // Parses this part of MemberExpression:
  // ('[' Expression ']' | '.' Identifier | TemplateLiteral)*
  do {
    switch (peek()) {
      case Token::kLeftBracket: {
        Consume(Token::kLeftBracket);
        int pos = position();
        AcceptINScope scope(this, true);
        ExpressionT index = ParseExpressionCoverGrammar();
        expression = factory()->NewProperty(expression, index, pos);
        impl()->PushPropertyName(index);
        Expect(Token::kRightBracket);
        break;
      }
      case Token::kPeriod: {
        Consume(Token::kPeriod);
        int pos = peek_position();
        ExpressionT key = ParsePropertyOrPrivatePropertyName();
        expression = factory()->NewProperty(expression, key, pos);
        break;
      }
      default: {
        DCHECK(Token::IsTemplate(peek()));
        int pos;
        if (scanner()->current_token() == Token::kIdentifier) {
          pos = position();
        } else {
          pos = peek_position();
          if (expression->IsFunctionLiteral()) {
            // If the tag function looks like an IIFE, set_parenthesized() to
            // force eager compilation.
            expression->AsFunctionLiteral()->SetShouldEagerCompile();
          }
        }
        expression = ParseTemplateLiteral(expression, pos, true);
        break;
      }
    }
  } while (Token::IsMember(peek()));
  return expression;
}

template <typename Impl>
void ParserBase<Impl>::ParseFormalParameter(FormalParametersT* parameters) {
  // FormalParameter[Yield,GeneratorParameter] :
  //   BindingElement[?Yield, ?GeneratorParameter]
  FuncNameInferrerState fni_state(&fni_);
  int pos = peek_position();
  auto declaration_it = scope()->declarations()->end();
  ExpressionT pattern = ParseBindingPattern();
  if (impl()->IsIdentifier(pattern)) {
    ClassifyParameter(impl()->AsIdentifier(pattern), pos, end_position());
  } else {
    parameters->is_simple = false;
  }

  ExpressionT initializer = impl()->NullExpression();
  if (Check(Token::kAssign)) {
    parameters->is_simple = false;

    if (parameters->has_rest) {
      ReportMessage(MessageTemplate::kRestDefaultInitializer);
      return;
    }

    AcceptINScope accept_in_scope(this, true);
    initializer = ParseAssignmentExpression();
    impl()->SetFunctionNameFromIdentifierRef(initializer, pattern);
  }

  auto declaration_end = scope()->declarations()->end();
  int initializer_end = end_position();
  for (; declaration_it != declaration_end; ++declaration_it) {
    Variable* var = declaration_it->var();

    // The first time a variable is initialized (i.e. when the initializer
    // position is unset), clear its maybe_assigned flag as it is not a true
    // assignment. Since this is done directly on the Variable objects, it has
    // no effect on VariableProxy objects appearing on the left-hand side of
    // true assignments, so x will be still be marked as maybe_assigned for:
    // (x = 1, y = (x = 2)) => {}
    // and even:
    // (x = (x = 2)) => {}.
    if (var->initializer_position() == kNoSourcePosition)
      var->clear_maybe_assigned();
    var->set_initializer_position(initializer_end);
  }

  impl()->AddFormalParameter(parameters, pattern, initializer, end_position(),
                             parameters->has_rest);
}

template <typename Impl>
void ParserBase<Impl>::ParseFormalParameterList(FormalParametersT* parameters) {
  // FormalParameters[Yield] :
  //   [empty]
  //   FunctionRestParameter[?Yield]
  //   FormalParameterList[?Yield]
  //   FormalParameterList[?Yield] ,
  //   FormalParameterList[?Yield] , FunctionRestParameter[?Yield]
  //
  // FormalParameterList[Yield] :
  //   FormalParameter[?Yield]
  //   FormalParameterList[?Yield] , FormalParameter[?Yield]
  ParameterParsingScope scope(impl(), parameters);

  DCHECK_EQ(0, parameters->arity);

  if (peek() != Token::kRightParen) {
    while (true) {
      // Add one since we're going to be adding a parameter.
      if (parameters->arity + 1 > Code::kMaxArguments) {
        ReportMessage(MessageTemplate::kTooManyParameters);
        return;
      }
      parameters->has_rest = Check(Token::kEllipsis);
      ParseFormalParameter(parameters);

      if (parameters->has_rest) {
        parameters->is_simple = false;
        if (peek() == Token::kComma) {
          impl()->ReportMessageAt(scanner()->peek_location(),
                                  MessageTemplate::kParamAfterRest);
          return;
        }
        break;
      }
      if (!Check(Token::kComma)) break;
      if (peek() == Token::kRightParen) {
        // allow the trailing comma
        break;
      }
    }
  }

  impl()->DeclareFormalParameters(parameters);
}

template <typename Impl>
void ParserBase<Impl>::ParseVariableDeclarations(
    VariableDeclarationContext var_context,
    DeclarationParsingResult* parsing_result,
    ZonePtrList<const AstRawString>* names) {
  // VariableDeclarations ::
  //   ('var' | 'const' | 'let' | 'using' | 'await using') (Identifier ('='
  //   AssignmentExpression)?)+[',']
  //
  // ES6:
  // FIXME(marja, nikolaos): Add an up-to-date comment about ES6 variable
  // declaration syntax.

  DCHECK_NOT_NULL(parsing_result);
  parsing_result->descriptor.kind = NORMAL_VARIABLE;
  parsing_result->descriptor.declaration_pos = peek_position();
  parsing_result->descriptor.initialization_pos = peek_position();

  switch (peek()) {
    case Token::kVar:
      parsing_result->descriptor.mode = VariableMode::kVar;
      Consume(Token::kVar);
      break;
    case Token::kConst:
      Consume(Token::kConst);
      DCHECK_NE(var_context, kStatement);
      parsing_result->descriptor.mode = VariableMode::kConst;
      break;
    case Token::kLet:
      Consume(Token::kLet);
      DCHECK_NE(var_context, kStatement);
      parsing_result->descriptor.mode = VariableMode::kLet;
      break;
    case Token::kUsing:
      // using [no LineTerminator here] [lookahead ≠ await] BindingList[?In,
      // ?Yield, ?Await, ~Pattern] ;
      Consume(Token::kUsing);
      DCHECK(v8_flags.js_explicit_resource_management);
      DCHECK_NE(var_context, kStatement);
      DCHECK(is_using_allowed());
      DCHECK(peek() != Token::kAwait);
      DCHECK(!scanner()->HasLineTerminatorBeforeNext());
      DCHECK(peek() != Token::kLeftBracket && peek() != Token::kLeftBrace);
      parsing_result->descriptor.mode = VariableMode::kUsing;
      break;
    case Token::kAwait:
      // CoverAwaitExpressionAndAwaitUsingDeclarationHead[?Yield] [no
      // LineTerminator here] BindingList[?In, ?Yield, +Await, ~Pattern];
      Consume(Token::kAwait);
      DCHECK(v8_flags.js_explicit_resource_management);
      DCHECK_NE(var_context, kStatement);
      DCHECK(is_using_allowed());
      DCHECK(is_await_allowed());
      Consume(Token::kUsing);
      DCHECK(!scanner()->HasLineTerminatorBeforeNext());
      DCHECK(peek() != Token::kLeftBracket && peek() != Token::kLeftBrace);
      parsing_result->descriptor.mode = VariableMode::kAwaitUsing;
      break;
    default:
      UNREACHABLE();  // by current callers
      break;
  }

  VariableDeclarationParsingScope declaration(
      impl(), parsing_result->descriptor.mode, names);
  Scope* target_scope = IsLexicalVariableMode(parsing_result->descriptor.mode)
                            ? scope()
                            : scope()->GetDeclarationScope();

  auto declaration_it = target_scope->declarations()->end();

  int bindings_start = peek_position();
  do {
    // Parse binding pattern.
    FuncNameInferrerState fni_state(&fni_);

    int decl_pos = peek_position();

    IdentifierT name;
    ExpressionT pattern;
    // Check for an identifier first, so that we can elide the pattern in cases
    // where there is no initializer (and so no proxy needs to be created).
    if (V8_LIKELY(Token::IsAnyIdentifier(peek()))) {
      name = ParseAndClassifyIdentifier(Next());
      if (V8_UNLIKELY(is_strict(language_mode()) &&
                      impl()->IsEvalOrArguments(name))) {
        impl()->ReportMessageAt(scanner()->location(),
                                MessageTemplate::kStrictEvalArguments);
        return;
      }
      if (peek() == Token::kAssign ||
          (var_context == kForStatement && PeekInOrOf()) ||
          parsing_result->descriptor.mode == VariableMode::kLet) {
        // Assignments need the variable expression for the assignment LHS, and
        // for of/in will need it later, so create the expression now.
        pattern = impl()->ExpressionFromIdentifier(name, decl_pos);
      } else {
        // Otherwise, elide the variable expression and just declare it.
        impl()->DeclareIdentifier(name, decl_pos);
        pattern = impl()->NullExpression();
      }
    } else if (parsing_result->descriptor.mode != VariableMode::kUsing &&
               parsing_result->descriptor.mode != VariableMode::kAwaitUsing) {
      name = impl()->NullIdentifier();
      pattern = ParseBindingPattern();
      DCHECK(!impl()->IsIdentifier(pattern));
    }

    Scanner::Location variable_loc = scanner()->location();

    ExpressionT value = impl()->NullExpression();
    int value_beg_pos = kNoSourcePosition;
    if (Check(Token::kAssign)) {
      DCHECK(!impl()->IsNull(pattern));
      {
        value_beg_pos = peek_position();
        AcceptINScope scope(this, var_context != kForStatement);
        value = ParseAssignmentExpression();
      }
      variable_loc.end_pos = end_position();

      if (!parsing_result->first_initializer_loc.IsValid()) {
        parsing_result->first_initializer_loc = variable_loc;
      }

      // Don't infer if it is "a = function(){...}();"-like expression.
      if (impl()->IsIdentifier(pattern)) {
        if (!value->IsCall() && !value->IsCallNew()) {
          fni_.Infer();
        } else {
          fni_.RemoveLastFunction();
        }
      }

      impl()->SetFunctionNameFromIdentifierRef(value, pattern);
    } else {
#ifdef DEBUG
      // We can fall through into here on error paths, so don't DCHECK those.
      if (!has_error()) {
        // We should never get identifier patterns for the non-initializer path,
        // as those expressions should be elided.
        DCHECK_EQ(!impl()->IsNull(name),
                  Token::IsAnyIdentifier(scanner()->current_token()));
        DCHECK_IMPLIES(impl()->IsNull(pattern), !impl()->IsNull(name));
        // The only times we have a non-null pattern are:
        //   1. This is a destructuring declaration (with no initializer, which
        //      is immediately an error),
        //   2. This is a declaration in a for in/of loop, or
        //   3. This is a let (which has an implicit undefined initializer)
        DCHECK_IMPLIES(
            !impl()->IsNull(pattern),
            !impl()->IsIdentifier(pattern) ||
                (var_context == kForStatement && PeekInOrOf()) ||
                parsing_result->descriptor.mode == VariableMode::kLet);
      }
#endif

      if (var_context != kForStatement || !PeekInOrOf()) {
        // ES6 'const' and binding patterns require initializers.
        if (parsing_result->descriptor.mode == VariableMode::kConst ||
            impl()->IsNull(name)) {
          impl()->ReportMessageAt(
              Scanner::Location(decl_pos, end_position()),
              MessageTemplate::kDeclarationMissingInitializer,
              impl()->IsNull(name) ? "destructuring" : "const");
          return;
        }
        // 'let x' initializes 'x' to undefined.
        if (parsing_result->descriptor.mode == VariableMode::kLet) {
          value = factory()->NewUndefinedLiteral(position());
        }
      }
    }

    int initializer_position = end_position();
    auto declaration_end = target_scope->declarations()->end();
    for (; declaration_it != declaration_end; ++declaration_it) {
      declaration_it->var()->set_initializer_position(initializer_position);
    }

    // Patterns should be elided iff. they don't have an initializer.
    DCHECK_IMPLIES(impl()->IsNull(pattern),
                   impl()->IsNull(value) ||
                       (var_context == kForStatement && PeekInOrOf()));

    typename DeclarationParsingResult::Declaration decl(pattern, value);
    decl.value_beg_pos = value_beg_pos;

    parsing_result->declarations.push_back(decl);
  } while (Check(Token::kComma));

  parsing_result->bindings_loc =
      Scanner::Location(bindings_start, end_position());
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseFunctionDeclaration() {
  Consume(Token::kFunction);

  int pos = position();
  ParseFunctionFlags flags = ParseFunctionFlag::kIsNormal;
  if (Check(Token::kMul)) {
    impl()->ReportMessageAt(
        scanner()->location(),
        MessageTemplate::kGeneratorInSingleStatementContext);
    return impl()->NullStatement();
  }
  return ParseHoistableDeclaration(pos, flags, nullptr, false);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseHoistableDeclaration(
    ZonePtrList<const AstRawString>* names, bool default_export) {
  Consume(Token::kFunction);

  int pos = position();
  ParseFunctionFlags flags = ParseFunctionFlag::kIsNormal;
  if (Check(Token::kMul)) {
    flags |= ParseFunctionFlag::kIsGenerator;
  }
  return ParseHoistableDeclaration(pos, flags, names, default_export);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseHoistableDeclaration(
    int pos, ParseFunctionFlags flags, ZonePtrList<const AstRawString>* names,
    bool default_export) {
  CheckStackOverflow();

  // FunctionDeclaration ::
  //   'function' Identifier '(' FormalParameters ')' '{' FunctionBody '}'
  //   'function' '(' FormalParameters ')' '{' FunctionBody '}'
  // GeneratorDeclaration ::
  //   'function' '*' Identifier '(' FormalParameters ')' '{' FunctionBody '}'
  //   'function' '*' '(' FormalParameters ')' '{' FunctionBody '}'
  //
  // The anonymous forms are allowed iff [default_export] is true.
  //
  // 'function' and '*' (if present) have been consumed by the caller.

  DCHECK_IMPLIES((flags & ParseFunctionFlag::kIsAsync) != 0,
                 (flags & ParseFunctionFlag::kIsGenerator) == 0);

  if ((flags & ParseFunctionFlag::kIsAsync) != 0 && Check(Token::kMul)) {
    // Async generator
    flags |= ParseFunctionFlag::kIsGenerator;
  }

  IdentifierT name;
  FunctionNameValidity name_validity;
  IdentifierT variable_name;
  if (peek() == Token::kLeftParen) {
    if (default_export) {
      impl()->GetDefaultStrings(&name, &variable_name);
      name_validity = kSkipFunctionNameCheck;
    } else {
      ReportMessage(MessageTemplate::kMissingFunctionName);
      return impl()->NullStatement();
    }
  } else {
    bool is_strict_reserved = Token::IsStrictReservedWord(peek());
    name = ParseIdentifier();
    name_validity = is_strict_reserved ? kFunctionNameIsStrictReserved
                                       : kFunctionNameValidityUnknown;
    variable_name = name;
  }

  FuncNameInferrerState fni_state(&fni_);
  impl()->PushEnclosingName(name);

  FunctionKind function_kind = FunctionKindFor(flags);

  FunctionLiteralT function = impl()->ParseFunctionLiteral(
      name, scanner()->location(), name_validity, function_kind, pos,
      FunctionSyntaxKind::kDeclaration, language_mode(), nullptr);

  // In ES6, a function behaves as a lexical binding, except in
  // a script scope, or the initial scope of eval or another function.
  VariableMode mode =
      (!scope()->is_declaration_scope() || scope()->is_module_scope())
          ? VariableMode::kLet
          : VariableMode::kVar;
  // Async functions don't undergo sloppy mode block scoped hoisting, and don't
  // allow duplicates in a block. Both are represented by the
  // sloppy_block_functions_. Don't add them to the map for async functions.
  // Generators are also supposed to be prohibited; currently doing this behind
  // a flag and UseCounting violations to assess web compatibility.
  VariableKind kind = is_sloppy(language_mode()) &&
                              !scope()->is_declaration_scope() &&
                              flags == ParseFunctionFlag::kIsNormal
                          ? SLOPPY_BLOCK_FUNCTION_VARIABLE
                          : NORMAL_VARIABLE;

  return impl()->DeclareFunction(variable_name, function, mode, kind, pos,
                                 end_position(), names);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseClassDeclaration(
    ZonePtrList<const AstRawString>* names, bool default_export) {
  // ClassDeclaration ::
  //   'class' Identifier ('extends' LeftHandExpression)? '{' ClassBody '}'
  //   'class' ('extends' LeftHandExpression)? '{' ClassBody '}'
  //
  // The anonymous form is allowed iff [default_export] is true.
  //
  // 'class' is expected to be consumed by the caller.
  //
  // A ClassDeclaration
  //
  //   class C { ... }
  //
  // has the same semantics as:
  //
  //   let C = class C { ... };
  //
  // so rewrite it as such.

  int class_token_pos = position();
  IdentifierT name = impl()->EmptyIdentifierString();
  bool is_strict_reserved = Token::IsStrictReservedWord(peek());
  IdentifierT variable_name = impl()->NullIdentifier();
  if (default_export &&
      (peek() == Token::kExtends || peek() == Token::kLeftBrace)) {
    impl()->GetDefaultStrings(&name, &variable_name);
  } else {
    name = ParseIdentifier();
    variable_name = name;
  }

  ExpressionParsingScope no_expression_scope(impl());
  ExpressionT value = ParseClassLiteral(scope(), name, scanner()->location(),
                                        is_strict_reserved, class_token_pos);
  no_expression_scope.ValidateExpression();
  int end_pos = position();
  return impl()->DeclareClass(variable_name, value, names, class_token_pos,
                              end_pos);
}

// Language extension which is only enabled for source files loaded
// through the API's extension mechanism.  A native function
// declaration is resolved by looking up the function through a
// callback provided by the extension.
template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseNativeDeclaration() {
  function_state_->DisableOptimization(BailoutReason::kNativeFunctionLiteral);

  int pos = peek_position();
  Consume(Token::kFunction);
  // Allow "eval" or "arguments" for backward compatibility.
  IdentifierT name = ParseIdentifier();
  Expect(Token::kLeftParen);
  if (peek() != Token::kRightParen) {
    do {
      ParseIdentifier();
    } while (Check(Token::kComma));
  }
  Expect(Token::kRightParen);
  Expect(Token::kSemicolon);
  return impl()->DeclareNative(name, pos);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseAsyncFunctionDeclaration(
    ZonePtrList<const AstRawString>* names, bool default_export) {
  // AsyncFunctionDeclaration ::
  //   async [no LineTerminator here] function BindingIdentifier[Await]
  //       ( FormalParameters[Await] ) { AsyncFunctionBody }
  DCHECK_EQ(scanner()->current_token(), Token::kAsync);
  if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
    impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
  }
  int pos = position();
  DCHECK(!scanner()->HasLineTerminatorBeforeNext());
  Consume(Token::kFunction);
  ParseFunctionFlags flags = ParseFunctionFlag::kIsAsync;
  return ParseHoistableDeclaration(pos, flags, names, default_export);
}

template <typename Impl>
void ParserBase<Impl>::ParseFunctionBody(
    StatementListT* body, IdentifierT function_name, int pos,
    const FormalParametersT& parameters, FunctionKind kind,
    FunctionSyntaxKind function_syntax_kind, FunctionBodyType body_type) {
  CheckStackOverflow();

  if (IsResumableFunction(kind)) impl()->PrepareGeneratorVariables();

  DeclarationScope* function_scope = parameters.scope;
  DeclarationScope* inner_scope = function_scope;

  // Building the parameter initialization block declares the parameters.
  // TODO(verwaest): Rely on ArrowHeadParsingScope instead.
  if (V8_UNLIKELY(!parameters.is_simple)) {
    if (has_error()) return;
    body->Add(impl()->BuildParameterInitializationBlock(parameters));
    if (has_error()) return;

    inner_scope = NewVarblockScope();
    inner_scope->set_start_position(position());
  }

  StatementListT inner_body(pointer_buffer());

  {
    BlockState block_state(&scope_, inner_scope);

    if (body_type == FunctionBodyType::kExpression) {
      ExpressionT expression = ParseAssignmentExpression();
      inner_body.Add(BuildReturnStatement(expression, expression->position()));
    } else {
      DCHECK(accept_IN_);
      DCHECK_EQ(FunctionBodyType::kBlock, body_type);
      // If we are parsing the source as if it is wrapped in a function, the
      // source ends without a closing brace.
      Token::Value closing_token =
          function_syntax_kind == FunctionSyntaxKind::kWrapped
              ? Token::kEos
              : Token::kRightBrace;

      if (IsAsyncGeneratorFunction(kind)) {
        impl()->ParseAndRewriteAsyncGeneratorFunctionBody(pos, kind,
                                                          &inner_body);
      } else if (IsGeneratorFunction(kind)) {
        impl()->ParseGeneratorFunctionBody(pos, kind, &inner_body);
      } else {
        ParseStatementList(&inner_body, closing_token);
        if (IsAsyncFunction(kind)) {
          inner_scope->set_end_position(end_position());
          function_state_ = AddOneSuspendPointIfBlockContainsAwaitUsing(
              inner_scope, function_state_);
        }
      }
      if (IsDerivedConstructor(kind)) {
        // Derived constructors are implemented by returning `this` when the
        // original return value is undefined, so always use `this`.
        ExpressionParsingScope expression_scope(impl());
        UseThis();
        expression_scope.ValidateExpression();
      }
      Expect(closing_token);
    }
  }

  scope()->set_end_position(end_position());

  bool allow_duplicate_parameters = false;

  CheckConflictingVarDeclarations(inner_scope);

  if (V8_LIKELY(parameters.is_simple)) {
    DCHECK_EQ(inner_scope, function_scope);
    if (is_sloppy(function_scope->language_mode())) {
      impl()->InsertSloppyBlockFunctionVarBindings(function_scope);
    }
    allow_duplicate_parameters =
        is_sloppy(function_scope->language_mode()) && !IsConciseMethod(kind);
  } else {
    DCHECK_NOT_NULL(inner_scope);
    DCHECK_EQ(function_scope, scope());
    DCHECK_EQ(function_scope, inner_scope->outer_scope());
    impl()->SetLanguageMode(function_scope, inner_scope->language_mode());

    if (is_sloppy(inner_scope->language_mode())) {
      impl()->InsertSloppyBlockFunctionVarBindings(inner_scope);
    }

    inner_scope->set_end_position(end_position());
    if (inner_scope->FinalizeBlockScope() != nullptr) {
      BlockT inner_block = factory()->NewBlock(true, inner_body);
      inner_body.Rewind();
      inner_body.Add(inner_block);
      inner_block->set_scope(inner_scope);
      impl()->RecordBlockSourceRange(inner_block, scope()->end_position());
      if (!impl()->HasCheckedSyntax()) {
        const AstRawString* conflict = inner_scope->FindVariableDeclaredIn(
            function_scope, VariableMode::kLastLexicalVariableMode);
        if (conflict != nullptr) {
          impl()->ReportVarRedeclarationIn(conflict, inner_scope);
        }
      }

      // According to ES#sec-functiondeclarationinstantiation step 27,28
      // when hasParameterExpressions is true, we need bind var declared
      // arguments to "arguments exotic object", so we here first declare
      // "arguments exotic object", then var declared arguments will be
      // initialized with "arguments exotic object"
      if (!IsArrowFunction(kind)) {
        function_scope->DeclareArguments(ast_value_factory());
      }

      impl()->InsertShadowingVarBindingInitializers(inner_block);
    }
  }

  ValidateFormalParameters(language_mode(), parameters,
                           allow_duplicate_parameters);

  if (!IsArrowFunction(kind)) {
    function_scope->DeclareArguments(ast_value_factory());
  }

  impl()->DeclareFunctionNameVar(function_name, function_syntax_kind,
                                 function_scope);

  inner_body.MergeInto(body);
}

template <typename Impl>
void ParserBase<Impl>::CheckArityRestrictions(int param_count,
                                              FunctionKind function_kind,
                                              bool has_rest,
                                              int formals_start_pos,
                                              int formals_end_pos) {
  if (impl()->HasCheckedSyntax()) return;
  if (IsGetterFunction(function_kind)) {
    if (param_count != 0) {
      impl()->ReportMessageAt(
          Scanner::Location(formals_start_pos, formals_end_pos),
          MessageTemplate::kBadGetterArity);
    }
  } else if (IsSetterFunction(function_kind)) {
    if (param_count != 1) {
      impl()->ReportMessageAt(
          Scanner::Location(formals_start_pos, formals_end_pos),
          MessageTemplate::kBadSetterArity);
    }
    if (has_rest) {
      impl()->ReportMessageAt(
          Scanner::Location(formals_start_pos, formals_end_pos),
          MessageTemplate::kBadSetterRestParameter);
    }
  }
}

template <typename Impl>
bool ParserBase<Impl>::IsNextLetKeyword() {
  DCHECK_EQ(Token::kLet, peek());
  Token::Value next_next = PeekAhead();
  switch (next_next) {
    case Token::kLeftBrace:
    case Token::kLeftBracket:
    case Token::kIdentifier:
    case Token::kStatic:
    case Token::kLet:  // `let let;` is disallowed by static semantics, but the
                       // token must be first interpreted as a keyword in order
                       // for those semantics to apply. This ensures that ASI is
                       // not honored when a LineTerminator separates the
                       // tokens.
    case Token::kYield:
    case Token::kAwait:
    case Token::kGet:
    case Token::kSet:
    case Token::kOf:
    case Token::kUsing:
    case Token::kAccessor:
    case Token::kAsync:
      return true;
    case Token::kFutureStrictReservedWord:
    case Token::kEscapedStrictReservedWord:
      // The early error rule for future reserved keywords
      // (ES#sec-identifiers-static-semantics-early-errors) uses the static
      // semantics StringValue of IdentifierName, which normalizes escape
      // sequences. So, both escaped and unescaped future reserved keywords are
      // allowed as identifiers in sloppy mode.
      return is_sloppy(language_mode());
    default:
      return false;
  }
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseArrowFunctionLiteral(
    const FormalParametersT& formal_parameters, int function_literal_id,
    bool could_be_immediately_invoked) {
  RCS_SCOPE(runtime_call_stats_,
            Impl::IsPreParser()
                ? RuntimeCallCounterId::kPreParseArrowFunctionLiteral
                : RuntimeCallCounterId::kParseArrowFunctionLiteral,
            RuntimeCallStats::kThreadSpecific);
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.log_function_events)) timer.Start();

  DCHECK_IMPLIES(!has_error(), peek() == Token::kArrow);
  if (!impl()->HasCheckedSyntax() && scanner_->HasLineTerminatorBeforeNext()) {
    // No line terminator allowed between the parameters and the arrow:
    // ArrowFunction[In, Yield, Await] :
    //   ArrowParameters[?Yield, ?Await] [no LineTerminator here] =>
    //   ConciseBody[?In]
    // If the next token is not `=>`, it's a syntax error anyway.
    impl()->ReportUnexpectedTokenAt(scanner_->peek_location(), Token::kArrow);
    return impl()->FailureExpression();
  }

  int expected_property_count = 0;
  int suspend_count = 0;

  FunctionKind kind = formal_parameters.scope->function_kind();
  FunctionLiteral::EagerCompileHint eager_compile_hint =
      could_be_immediately_invoked ? FunctionLiteral::kShouldEagerCompile
                                   : default_eager_compile_hint_;

  int compile_hint_position = formal_parameters.scope->start_position();
  eager_compile_hint =
      impl()->GetEmbedderCompileHint(eager_compile_hint, compile_hint_position);

  bool can_preparse = impl()->parse_lazily() &&
                      eager_compile_hint == FunctionLiteral::kShouldLazyCompile;
  // TODO(marja): consider lazy-parsing inner arrow functions too. is_this
  // handling in Scope::ResolveVariable needs to change.
  bool is_lazy_top_level_function =
      can_preparse && impl()->AllowsLazyParsingWithoutUnresolvedVariables();
  bool has_braces = true;
  ProducedPreparseData* produced_preparse_data = nullptr;
  StatementListT body(pointer_buffer());
  {
    FunctionState function_state(&function_state_, &scope_,
                                 formal_parameters.scope);

    Consume(Token::kArrow);

    if (peek() == Token::kLeftBrace) {
      // Multiple statement body
      DCHECK_EQ(scope(), formal_parameters.scope);

      if (is_lazy_top_level_function) {
        // FIXME(marja): Arrow function parameters will be parsed even if the
        // body is preparsed; move relevant parts of parameter handling to
        // simulate consistent parameter handling.

        // Building the parameter initialization block declares the parameters.
        // TODO(verwaest): Rely on ArrowHeadParsingScope instead.
        if (!formal_parameters.is_simple) {
          impl()->BuildParameterInitializationBlock(formal_parameters);
          if (has_error()) return impl()->FailureExpression();
        }

        // For arrow functions, we don't need to retrieve data about function
        // parameters.
        int dummy_num_parameters = -1;
        int dummy_function_length = -1;
        DCHECK(IsArrowFunction(kind));
        bool did_preparse_successfully = impl()->SkipFunction(
            nullptr, kind, FunctionSyntaxKind::kAnonymousExpression,
            formal_parameters.scope, &dummy_num_parameters,
            &dummy_function_length, &produced_preparse_data);

        DCHECK_NULL(produced_preparse_data);

        if (did_preparse_successfully) {
          // Validate parameter names. We can do this only after preparsing the
          // function, since the function can declare itself strict.
          ValidateFormalParameters(language_mode(), formal_parameters, false);
        } else {
          // In case we did not sucessfully preparse the function because of an
          // unidentified error we do a full reparse to return the error.
          // Parse again in the outer scope, since the language mode may change.
          BlockState block_state(&scope_, scope()->outer_scope());
          ExpressionT expression = ParseConditionalExpression();
          // Reparsing the head may have caused a stack overflow.
          if (has_error()) return impl()->FailureExpression();

          DeclarationScope* function_scope = next_arrow_function_info_.scope;
          FunctionState inner_function_state(&function_state_, &scope_,
                                             function_scope);
          Scanner::Location loc(function_scope->start_position(),
                                end_position());
          FormalParametersT parameters(function_scope);
          parameters.is_simple = function_scope->has_simple_parameters();
          impl()->DeclareArrowFunctionFormalParameters(&parameters, expression,
                                                       loc);
          next_arrow_function_info_.Reset();

          Consume(Token::kArrow);
          Consume(Token::kLeftBrace);

          AcceptINScope scope(this, true);
          FunctionParsingScope body_parsing_scope(impl());
          ParseFunctionBody(&body, impl()->NullIdentifier(), kNoSourcePosition,
                            parameters, kind,
                            FunctionSyntaxKind::kAnonymousExpression,
                            FunctionBodyType::kBlock);
          CHECK(has_error());
          return impl()->FailureExpression();
        }
      } else {
        Consume(Token::kLeftBrace);
        AcceptINScope scope(this, true);
        FunctionParsingScope body_parsing_scope(impl());
        ParseFunctionBody(&body, impl()->NullIdentifier(), kNoSourcePosition,
                          formal_parameters, kind,
                          FunctionSyntaxKind::kAnonymousExpression,
                          FunctionBodyType::kBlock);
        expected_property_count = function_state.expected_property_count();
      }
    } else {
      // Single-expression body
      has_braces = false;
      FunctionParsingScope body_parsing_scope(impl());
      ParseFunctionBody(&body, impl()->NullIdentifier(), kNoSourcePosition,
                        formal_parameters, kind,
                        FunctionSyntaxKind::kAnonymousExpression,
                        FunctionBodyType::kExpression);
      expected_property_count = function_state.expected_property_count();
    }

    formal_parameters.scope->set_end_position(end_position());

    // Validate strict mode.
    if (is_strict(language_mode())) {
      CheckStrictOctalLiteral(formal_parameters.scope->start_position(),
                              end_position());
    }
    suspend_count = function_state.suspend_count();
  }

  FunctionLiteralT function_literal = factory()->NewFunctionLiteral(
      impl()->EmptyIdentifierString(), formal_parameters.scope, body,
      expected_property_count, formal_parameters.num_parameters(),
      formal_parameters.function_length,
      FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAnonymousExpression, eager_compile_hint,
      formal_parameters.scope->start_position(), has_braces,
      function_literal_id, produced_preparse_data);

  function_literal->set_suspend_count(suspend_count);
  function_literal->set_function_token_position(
      formal_parameters.scope->start_position());

  impl()->RecordFunctionLiteralSourceRange(function_literal);
  impl()->AddFunctionForNameInference(function_literal);

  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    Scope* scope = formal_parameters.scope;
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name =
        is_lazy_top_level_function ? "preparse-no-resolution" : "parse";
    const char* name = "arrow function";
    v8_file_logger_->FunctionEvent(event_name, flags().script_id(), ms,
                                   scope->start_position(),
                                   scope->end_position(), name, strlen(name));
  }

  return function_literal;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseClassExpression(
    Scope* outer_scope) {
  Consume(Token::kClass);
  int class_token_pos = position();
  IdentifierT name = impl()->EmptyIdentifierString();
  bool is_strict_reserved_name = false;
  Scanner::Location class_name_location = Scanner::Location::invalid();
  if (peek_any_identifier()) {
    name = ParseAndClassifyIdentifier(Next());
    class_name_location = scanner()->location();
    is_strict_reserved_name =
        Token::IsStrictReservedWord(scanner()->current_token());
  }
  return ParseClassLiteral(outer_scope, name, class_name_location,
                           is_strict_reserved_name, class_token_pos);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseClassLiteral(
    Scope* outer_scope, IdentifierT name, Scanner::Location class_name_location,
    bool name_is_strict_reserved, int class_token_pos) {
  bool is_anonymous = impl()->IsEmptyIdentifier(name);

  // All parts of a ClassDeclaration and ClassExpression are strict code.
  if (!impl()->HasCheckedSyntax() && !is_anonymous) {
    if (name_is_strict_reserved) {
      impl()->ReportMessageAt(class_name_location,
                              MessageTemplate::kUnexpectedStrictReserved);
      return impl()->FailureExpression();
    }
    if (impl()->IsEvalOrArguments(name)) {
      impl()->ReportMessageAt(class_name_location,
                              MessageTemplate::kStrictEvalArguments);
      return impl()->FailureExpression();
    }
  }

  ClassScope* class_scope = NewClassScope(outer_scope, is_anonymous);
  BlockState block_state(&scope_, class_scope);
  RaiseLanguageMode(LanguageMode::kStrict);

  BlockState object_literal_scope_state(&object_literal_scope_, nullptr);

  ClassInfo class_info(this);
  class_info.is_anonymous = is_anonymous;

  scope()->set_start_position(class_token_pos);
  if (Check(Token::kExtends)) {
    ClassScope::HeritageParsingScope heritage(class_scope);
    FuncNameInferrerState fni_state(&fni_);
    ExpressionParsingScope scope(impl());
    class_info.extends = ParseLeftHandSideExpression();
    scope.ValidateExpression();
  }

  Expect(Token::kLeftBrace);

  ParseClassLiteralBody(class_info, name, class_token_pos, Token::kRightBrace);

  VariableProxy* unresolvable = class_scope->ResolvePrivateNamesPartially();
  if (unresolvable != nullptr) {
    impl()->ReportMessageAt(Scanner::Location(unresolvable->position(),
                                              unresolvable->position() + 1),
                            MessageTemplate::kInvalidPrivateFieldResolution,
                            unresolvable->raw_name());
    return impl()->FailureExpression();
  }

  if (class_info.requires_brand) {
    class_scope->DeclareBrandVariable(
        ast_value_factory(), IsStaticFlag::kNotStatic, kNoSourcePosition);
  }

  if (class_scope->needs_home_object()) {
    class_info.home_object_variable =
        class_scope->DeclareHomeObjectVariable(ast_value_factory());
    class_info.static_home_object_variable =
        class_scope->DeclareStaticHomeObjectVariable(ast_value_factory());
  }

  bool should_save_class_variable_index =
      class_scope->should_save_class_variable_index();
  if (!class_info.is_anonymous || should_save_class_variable_index) {
    impl()->DeclareClassVariable(class_scope, name, &class_info,
                                 class_token_pos);
    if (should_save_class_variable_index) {
      class_scope->class_variable()->set_is_used();
      class_scope->class_variable()->ForceContextAllocation();
    }
  }

  return impl()->RewriteClassLiteral(class_scope, name, &class_info,
                                     class_token_pos);
}

template <typename Impl>
void ParserBase<Impl>::ParseClassLiteralBody(ClassInfo& class_info,
                                             IdentifierT name,
                                             int class_token_pos,
                                             Token::Value end_token) {
  bool has_extends = !impl()->IsNull(class_info.extends);

  while (peek() != end_token) {
    if (Check(Token::kSemicolon)) continue;

    // Either we're parsing a `static { }` initialization block or a property.
    if (peek() == Token::kStatic && PeekAhead() == Token::kLeftBrace) {
      BlockT static_block = ParseClassStaticBlock(&class_info);
      impl()->AddClassStaticBlock(static_block, &class_info);
      continue;
    }

    FuncNameInferrerState fni_state(&fni_);
    // If we haven't seen the constructor yet, it potentially is the next
    // property.
    bool is_constructor = !class_info.has_seen_constructor;
    ParsePropertyInfo prop_info(this);
    prop_info.position = PropertyPosition::kClassLiteral;

    ClassLiteralPropertyT property =
        ParseClassPropertyDefinition(&class_info, &prop_info, has_extends);

    if (has_error()) return;

    ClassLiteralProperty::Kind property_kind =
        ClassPropertyKindFor(prop_info.kind);

    if (!class_info.has_static_computed_names && prop_info.is_static &&
        prop_info.is_computed_name) {
      class_info.has_static_computed_names = true;
    }
    is_constructor &= class_info.has_seen_constructor;

    bool is_field = property_kind == ClassLiteralProperty::FIELD;

    if (V8_UNLIKELY(prop_info.is_private)) {
      DCHECK(!is_constructor);
      class_info.requires_brand |= (!is_field && !prop_info.is_static);
      class_info.has_static_private_methods_or_accessors |=
          (prop_info.is_static && !is_field);

      impl()->DeclarePrivateClassMember(scope()->AsClassScope(), prop_info.name,
                                        property, property_kind,
                                        prop_info.is_static, &class_info);
      impl()->InferFunctionName();
      continue;
    }

    if (V8_UNLIKELY(is_field)) {
      DCHECK(!prop_info.is_private);
      // If we're reparsing, we might not have a class scope. We only need a
      // class scope if we have a computed name though, and in that case we're
      // certain that the current scope must be a class scope.
      ClassScope* class_scope = nullptr;
      if (prop_info.is_computed_name) {
        class_info.computed_field_count++;
        class_scope = scope()->AsClassScope();
      }

      impl()->DeclarePublicClassField(class_scope, property,
                                      prop_info.is_static,
                                      prop_info.is_computed_name, &class_info);
      impl()->InferFunctionName();
      continue;
    }

    impl()->DeclarePublicClassMethod(name, property, is_constructor,
                                     &class_info);
    impl()->InferFunctionName();
  }

  Expect(end_token);
  scope()->set_end_position(end_position());
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParseAsyncFunctionLiteral() {
  // AsyncFunctionLiteral ::
  //   async [no LineTerminator here] function ( FormalParameters[Await] )
  //       { AsyncFunctionBody }
  //
  //   async [no LineTerminator here] function BindingIdentifier[Await]
  //       ( FormalParameters[Await] ) { AsyncFunctionBody }
  DCHECK_EQ(scanner()->current_token(), Token::kAsync);
  if (V8_UNLIKELY(scanner()->literal_contains_escapes())) {
    impl()->ReportUnexpectedToken(Token::kEscapedKeyword);
  }
  int pos = position();
  Consume(Token::kFunction);
  IdentifierT name = impl()->NullIdentifier();
  FunctionSyntaxKind syntax_kind = FunctionSyntaxKind::kAnonymousExpression;

  ParseFunctionFlags flags = ParseFunctionFlag::kIsAsync;
  if (Check(Token::kMul)) flags |= ParseFunctionFlag::kIsGenerator;
  const FunctionKind kind = FunctionKindFor(flags);
  bool is_strict_reserved = Token::IsStrictReservedWord(peek());

  if (impl()->ParsingDynamicFunctionDeclaration()) {
    // We don't want dynamic functions to actually declare their name
    // "anonymous". We just want that name in the toString().

    // Consuming token we did not peek yet, which could lead to a kIllegal token
    // in the case of a stackoverflow.
    Consume(Token::kIdentifier);
    DCHECK_IMPLIES(!has_error(),
                   scanner()->CurrentSymbol(ast_value_factory()) ==
                       ast_value_factory()->anonymous_string());
  } else if (peek_any_identifier()) {
    syntax_kind = FunctionSyntaxKind::kNamedExpression;
    name = ParseIdentifier(kind);
  }
  FunctionLiteralT result = impl()->ParseFunctionLiteral(
      name, scanner()->location(),
      is_strict_reserved ? kFunctionNameIsStrictReserved
                         : kFunctionNameValidityUnknown,
      kind, pos, syntax_kind, language_mode(), nullptr);
  if (impl()->IsNull(result)) return impl()->FailureExpression();
  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseTemplateLiteral(
    ExpressionT tag, int start, bool tagged) {
  // A TemplateLiteral is made up of 0 or more kTemplateSpan tokens (literal
  // text followed by a substitution expression), finalized by a single
  // kTemplateTail.
  //
  // In terms of draft language, kTemplateSpan may be either the TemplateHead or
  // TemplateMiddle productions, while kTemplateTail is either TemplateTail, or
  // NoSubstitutionTemplate.
  //
  // When parsing a TemplateLiteral, we must have scanned either an initial
  // kTemplateSpan, or a kTemplateTail.
  DCHECK(peek() == Token::kTemplateSpan || peek() == Token::kTemplateTail);

  if (tagged) {
    // TaggedTemplate expressions prevent the eval compilation cache from being
    // used. This flag is only used if an eval is being parsed.
    set_allow_eval_cache(false);
  }

  bool forbid_illegal_escapes = !tagged;

  // If we reach a kTemplateTail first, we are parsing a NoSubstitutionTemplate.
  // In this case we may simply consume the token and build a template with a
  // single kTemplateSpan and no expressions.
  if (peek() == Token::kTemplateTail) {
    Consume(Token::kTemplateTail);
    int pos = position();
    typename Impl::TemplateLiteralState ts = impl()->OpenTemplateLiteral(pos);
    bool is_valid = CheckTemplateEscapes(forbid_illegal_escapes);
    impl()->AddTemplateSpan(&ts, is_valid, true);
    return impl()->CloseTemplateLiteral(&ts, start, tag);
  }

  Consume(Token::kTemplateSpan);
  int pos = position();
  typename Impl::TemplateLiteralState ts = impl()->OpenTemplateLiteral(pos);
  bool is_valid = CheckTemplateEscapes(forbid_illegal_escapes);
  impl()->AddTemplateSpan(&ts, is_valid, false);
  Token::Value next;

  // If we open with a kTemplateSpan, we must scan the subsequent expression,
  // and repeat if the following token is a kTemplateSpan as well (in this
  // case, representing a TemplateMiddle).

  do {
    next = peek();

    int expr_pos = peek_position();
    AcceptINScope scope(this, true);
    ExpressionT expression = ParseExpressionCoverGrammar();
    impl()->AddTemplateExpression(&ts, expression);

    if (peek() != Token::kRightBrace) {
      impl()->ReportMessageAt(Scanner::Location(expr_pos, peek_position()),
                              MessageTemplate::kUnterminatedTemplateExpr);
      return impl()->FailureExpression();
    }

    // If we didn't die parsing that expression, our next token should be a
    // kTemplateSpan or kTemplateTail.
    next = scanner()->ScanTemplateContinuation();
    Next();
    pos = position();

    is_valid = CheckTemplateEscapes(forbid_illegal_escapes);
    impl()->AddTemplateSpan(&ts, is_valid, next == Token::kTemplateTail);
  } while (next == Token::kTemplateSpan);

  DCHECK_IMPLIES(!has_error(), next == Token::kTemplateTail);
  // Once we've reached a kTemplateTail, we can close the TemplateLiteral.
  return impl()->CloseTemplateLiteral(&ts, start, tag);
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::RewriteInvalidReferenceExpression(ExpressionT expression,
                                                    int beg_pos, int end_pos,
                                                    MessageTemplate message,
                                                    bool early_error) {
  DCHECK(!IsValidReferenceExpression(expression));
  if (impl()->IsIdentifier(expression)) {
    DCHECK(is_strict(language_mode()));
    DCHECK(impl()->IsEvalOrArguments(impl()->AsIdentifier(expression)));

    ReportMessageAt(Scanner::Location(beg_pos, end_pos),
                    MessageTemplate::kStrictEvalArguments);
    return impl()->FailureExpression();
  }
  if (expression->IsCall() && !expression->AsCall()->is_tagged_template() &&
      !early_error) {
    expression_scope()->RecordPatternError(
        Scanner::Location(beg_pos, end_pos),
        MessageTemplate::kInvalidDestructuringTarget);
    // If it is a call, make it a runtime error for legacy web compatibility.
    // Bug: https://bugs.chromium.org/p/v8/issues/detail?id=4480
    // Rewrite `expr' to `expr[throw ReferenceError]'.
    impl()->CountUsage(
        is_strict(language_mode())
            ? v8::Isolate::kAssigmentExpressionLHSIsCallInStrict
            : v8::Isolate::kAssigmentExpressionLHSIsCallInSloppy);
    ExpressionT error = impl()->NewThrowReferenceError(message, beg_pos);
    return factory()->NewProperty(expression, error, beg_pos);
  }
  // Tagged templates and other modern language features (which pass early_error
  // = true) are exempt from the web compatibility hack. Throw a regular early
  // error.
  ReportMessageAt(Scanner::Location(beg_pos, end_pos), message);
  return impl()->FailureExpression();
}

template <typename Impl>
void ParserBase<Impl>::ClassifyParameter(IdentifierT parameter, int begin,
                                         int end) {
  if (impl()->IsEvalOrArguments(parameter)) {
    expression_scope()->RecordStrictModeParameterError(
        Scanner::Location(begin, end), MessageTemplate::kStrictEvalArguments);
  }
}

template <typename Impl>
void ParserBase<Impl>::ClassifyArrowParameter(
    AccumulationScope* accumulation_scope, int position,
    ExpressionT parameter) {
  accumulation_scope->Accumulate();
  if (parameter->is_parenthesized() ||
      !(impl()->IsIdentifier(parameter) || parameter->IsPattern() ||
        parameter->IsAssignment())) {
    expression_scope()->RecordDeclarationError(
        Scanner::Location(position, end_position()),
        MessageTemplate::kInvalidDestructuringTarget);
  } else if (impl()->IsIdentifier(parameter)) {
    ClassifyParameter(impl()->AsIdentifier(parameter), position,
                      end_position());
  } else {
    expression_scope()->RecordNonSimpleParameter();
  }
}

template <typename Impl>
bool ParserBase<Impl>::IsValidReferenceExpression(ExpressionT expression) {
  return IsAssignableIdentifier(expression) || expression->IsProperty();
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT
ParserBase<Impl>::ParsePossibleDestructuringSubPattern(
    AccumulationScope* scope) {
  if (scope) scope->Accumulate();
  int begin = peek_position();
  ExpressionT result = ParseAssignmentExpressionCoverGrammar();

  if (IsValidReferenceExpression(result)) {
    // Parenthesized identifiers and property references are allowed as part of
    // a larger assignment pattern, even though parenthesized patterns
    // themselves are not allowed, e.g., "[(x)] = []". Only accumulate
    // assignment pattern errors if the parsed expression is more complex.
    if (impl()->IsIdentifier(result)) {
      if (result->is_parenthesized()) {
        expression_scope()->RecordDeclarationError(
            Scanner::Location(begin, end_position()),
            MessageTemplate::kInvalidDestructuringTarget);
      }
      IdentifierT identifier = impl()->AsIdentifier(result);
      ClassifyParameter(identifier, begin, end_position());
    } else {
      DCHECK(result->IsProperty());
      expression_scope()->RecordDeclarationError(
          Scanner::Location(begin, end_position()),
          MessageTemplate::kInvalidPropertyBindingPattern);
      if (scope != nullptr) scope->ValidateExpression();
    }
  } else if (result->is_parenthesized() ||
             (!result->IsPattern() && !result->IsAssignment())) {
    expression_scope()->RecordPatternError(
        Scanner::Location(begin, end_position()),
        MessageTemplate::kInvalidDestructuringTarget);
  }

  return result;
}

template <typename Impl>
typename ParserBase<Impl>::ExpressionT ParserBase<Impl>::ParseV8Intrinsic() {
  // CallRuntime ::
  //   '%' Identifier Arguments

  int pos = peek_position();
  Consume(Token::kMod);
  // Allow "eval" or "arguments" for backward compatibility.
  IdentifierT name = ParseIdentifier();
  if (peek() != Token::kLeftParen) {
    impl()->ReportUnexpectedToken(peek());
    return impl()->FailureExpression();
  }
  bool has_spread;
  ExpressionListT args(pointer_buffer());
  ParseArguments(&args, &has_spread);

  if (has_spread) {
    ReportMessageAt(Scanner::Location(pos, position()),
                    MessageTemplate::kIntrinsicWithSpread);
    return impl()->FailureExpression();
  }

  return impl()->NewV8Intrinsic(name, args, pos);
}

template <typename Impl>
void ParserBase<Impl>::ParseStatementList(StatementListT* body,
                                          Token::Value end_token) {
  // StatementList ::
  //   (StatementListItem)* <end_token>
  DCHECK_NOT_NULL(body);

  while (peek() == Token::kString) {
    bool use_strict = false;
#if V8_ENABLE_WEBASSEMBLY
    bool use_asm = false;
#endif  // V8_ENABLE_WEBASSEMBLY

    Scanner::Location token_loc = scanner()->peek_location();

    if (scanner()->NextLiteralExactlyEquals("use strict")) {
      use_strict = true;
#if V8_ENABLE_WEBASSEMBLY
    } else if (scanner()->NextLiteralExactlyEquals("use asm")) {
      use_asm = true;
#endif  // V8_ENABLE_WEBASSEMBLY
    }

    StatementT stat = ParseStatementListItem();
    if (impl()->IsNull(stat)) return;

    body->Add(stat);

    if (!impl()->IsStringLiteral(stat)) break;

    if (use_strict) {
      // Directive "use strict" (ES5 14.1).
      RaiseLanguageMode(LanguageMode::kStrict);
      if (!scope()->HasSimpleParameters()) {
        // TC39 deemed "use strict" directives to be an error when occurring
        // in the body of a function with non-simple parameter list, on
        // 29/7/2015. https://goo.gl/ueA7Ln
        impl()->ReportMessageAt(token_loc,
                                MessageTemplate::kIllegalLanguageModeDirective,
                                "use strict");
        return;
      }
#if V8_ENABLE_WEBASSEMBLY
    } else if (use_asm) {
      // Directive "use asm".
      impl()->SetAsmModule();
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      // Possibly an unknown directive.
      // Should not change mode, but will increment usage counters
      // as appropriate. Ditto usages below.
      RaiseLanguageMode(LanguageMode::kSloppy);
    }
  }

  while (peek() != end_token) {
    StatementT stat = ParseStatementListItem();
    if (impl()->IsNull(stat)) return;
    if (stat->IsEmptyStatement()) continue;
    body->Add(stat);
  }
  function_state_ =
      AddOneSuspendPointIfBlockContainsAwaitUsing(scope(), function_state_);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseStatementListItem() {
  // ECMA 262 6th Edition
  // StatementListItem[Yield, Return] :
  //   Statement[?Yield, ?Return]
  //   Declaration[?Yield]
  //
  // Declaration[Yield] :
  //   HoistableDeclaration[?Yield]
  //   ClassDeclaration[?Yield]
  //   LexicalDeclaration[In, ?Yield]
  //
  // HoistableDeclaration[Yield, Default] :
  //   FunctionDeclaration[?Yield, ?Default]
  //   GeneratorDeclaration[?Yield, ?Default]
  //
  // LexicalDeclaration[In, Yield, Await] :
  //   LetOrConst BindingList[?In, ?Yield, ?Await, +Pattern] ;
  //   UsingDeclaration[?In, ?Yield, ?Await, ~Pattern];
  //   [+Await] AwaitUsingDeclaration[?In, ?Yield];

  switch (peek()) {
    case Token::kFunction:
      return ParseHoistableDeclaration(nullptr, false);
    case Token::kClass:
      Consume(Token::kClass);
      return ParseClassDeclaration(nullptr, false);
    case Token::kVar:
    case Token::kConst:
      return ParseVariableStatement(kStatementListItem, nullptr);
    case Token::kLet:
      if (IsNextLetKeyword()) {
        return ParseVariableStatement(kStatementListItem, nullptr);
      }
      break;
    case Token::kUsing:
      if (!v8_flags.js_explicit_resource_management) break;
      if (!is_using_allowed()) break;
      if (!(scanner()->HasLineTerminatorAfterNext()) &&
          PeekAhead() != Token::kAwait && PeekAhead() != Token::kLeftBracket &&
          PeekAhead() != Token::kLeftBrace) {
        return ParseVariableStatement(kStatementListItem, nullptr);
      }
      break;
    case Token::kAwait:
      if (!v8_flags.js_explicit_resource_management) break;
      if (!is_await_allowed()) break;
      if (!is_using_allowed()) break;
      if (!(scanner()->HasLineTerminatorAfterNext()) &&
          PeekAhead() == Token::kUsing &&
          !(scanner()->HasLineTerminatorAfterNextNext()) &&
          PeekAheadAhead() != Token::kLeftBracket &&
          PeekAheadAhead() != Token::kLeftBrace &&
          Token::IsAnyIdentifier(PeekAheadAhead())) {
        return ParseVariableStatement(kStatementListItem, nullptr);
      }
      break;
    case Token::kAsync:
      if (PeekAhead() == Token::kFunction &&
          !scanner()->HasLineTerminatorAfterNext()) {
        Consume(Token::kAsync);
        return ParseAsyncFunctionDeclaration(nullptr, false);
      }
      break;
    default:
      break;
  }
  return ParseStatement(nullptr, nullptr, kAllowLabelledFunctionStatement);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels,
    AllowLabelledFunctionStatement allow_function) {
  // Statement ::
  //   Block
  //   VariableStatement
  //   EmptyStatement
  //   ExpressionStatement
  //   IfStatement
  //   IterationStatement
  //   ContinueStatement
  //   BreakStatement
  //   ReturnStatement
  //   WithStatement
  //   LabelledStatement
  //   SwitchStatement
  //   ThrowStatement
  //   TryStatement
  //   DebuggerStatement

  // {own_labels} is always a subset of {labels}.
  DCHECK_IMPLIES(labels == nullptr, own_labels == nullptr);

  // Note: Since labels can only be used by 'break' and 'continue'
  // statements, which themselves are only valid within blocks,
  // iterations or 'switch' statements (i.e., BreakableStatements),
  // labels can be simply ignored in all other cases; except for
  // trivial labeled break statements 'label: break label' which is
  // parsed into an empty statement.
  switch (peek()) {
    case Token::kLeftBrace:
      return ParseBlock(labels);
    case Token::kSemicolon:
      Next();
      return factory()->EmptyStatement();
    case Token::kIf:
      return ParseIfStatement(labels);
    case Token::kDo:
      return ParseDoWhileStatement(labels, own_labels);
    case Token::kWhile:
      return ParseWhileStatement(labels, own_labels);
    case Token::kFor:
      if (V8_UNLIKELY(is_await_allowed() && PeekAhead() == Token::kAwait)) {
        return ParseForAwaitStatement(labels, own_labels);
      }
      return ParseForStatement(labels, own_labels);
    case Token::kContinue:
      return ParseContinueStatement();
    case Token::kBreak:
      return ParseBreakStatement(labels);
    case Token::kReturn:
      return ParseReturnStatement();
    case Token::kThrow:
      return ParseThrowStatement();
    case Token::kTry: {
      // It is somewhat complicated to have labels on try-statements.
      // When breaking out of a try-finally statement, one must take
      // great care not to treat it as a fall-through. It is much easier
      // just to wrap the entire try-statement in a statement block and
      // put the labels there.
      if (labels == nullptr) return ParseTryStatement();
      StatementListT statements(pointer_buffer());
      BlockT result = factory()->NewBlock(false, true);
      Target target(this, result, labels, nullptr,
                    Target::TARGET_FOR_NAMED_ONLY);
      StatementT statement = ParseTryStatement();
      statements.Add(statement);
      result->InitializeStatements(statements, zone());
      return result;
    }
    case Token::kWith:
      return ParseWithStatement(labels);
    case Token::kSwitch:
      return ParseSwitchStatement(labels);
    case Token::kFunction:
      // FunctionDeclaration only allowed as a StatementListItem, not in
      // an arbitrary Statement position. Exceptions such as
      // ES#sec-functiondeclarations-in-ifstatement-statement-clauses
      // are handled by calling ParseScopedStatement rather than
      // ParseStatement directly.
      impl()->ReportMessageAt(scanner()->peek_location(),
                              is_strict(language_mode())
                                  ? MessageTemplate::kStrictFunction
                                  : MessageTemplate::kSloppyFunction);
      return impl()->NullStatement();
    case Token::kDebugger:
      return ParseDebuggerStatement();
    case Token::kVar:
      return ParseVariableStatement(kStatement, nullptr);
    case Token::kAsync:
      if (!impl()->HasCheckedSyntax() &&
          !scanner()->HasLineTerminatorAfterNext() &&
          PeekAhead() == Token::kFunction) {
        impl()->ReportMessageAt(
            scanner()->peek_location(),
            MessageTemplate::kAsyncFunctionInSingleStatementContext);
        return impl()->NullStatement();
      }
      [[fallthrough]];
    default:
      return ParseExpressionOrLabelledStatement(labels, own_labels,
                                                allow_function);
  }
}

template <typename Impl>
typename ParserBase<Impl>::BlockT ParserBase<Impl>::ParseBlock(
    ZonePtrList<const AstRawString>* labels, Scope* block_scope) {
  // Block ::
  //   '{' StatementList '}'

  // Parse the statements and collect escaping labels.
  BlockT body = factory()->NewBlock(false, labels != nullptr);
  StatementListT statements(pointer_buffer());

  CheckStackOverflow();

  {
    BlockState block_state(&scope_, block_scope);
    scope()->set_start_position(peek_position());
    Target target(this, body, labels, nullptr, Target::TARGET_FOR_NAMED_ONLY);

    Expect(Token::kLeftBrace);

    while (peek() != Token::kRightBrace) {
      StatementT stat = ParseStatementListItem();
      if (impl()->IsNull(stat)) return body;
      if (stat->IsEmptyStatement()) continue;
      statements.Add(stat);
    }

    Expect(Token::kRightBrace);

    int end_pos = end_position();
    scope()->set_end_position(end_pos);

    impl()->RecordBlockSourceRange(body, end_pos);
    body->set_scope(scope()->FinalizeBlockScope());
    function_state_ =
        AddOneSuspendPointIfBlockContainsAwaitUsing(scope(), function_state_);
  }

  body->InitializeStatements(statements, zone());
  return body;
}

template <typename Impl>
typename ParserBase<Impl>::BlockT ParserBase<Impl>::ParseBlock(
    ZonePtrList<const AstRawString>* labels) {
  return ParseBlock(labels, NewScope(BLOCK_SCOPE));
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseScopedStatement(
    ZonePtrList<const AstRawString>* labels) {
  if (is_strict(language_mode()) || peek() != Token::kFunction) {
    return ParseStatement(labels, nullptr);
  } else {
    // Make a block around the statement for a lexical binding
    // is introduced by a FunctionDeclaration.
    BlockState block_state(zone(), &scope_);
    scope()->set_start_position(scanner()->location().beg_pos);
    BlockT block = factory()->NewBlock(1, false);
    StatementT body = ParseFunctionDeclaration();
    block->statements()->Add(body, zone());
    scope()->set_end_position(end_position());
    block->set_scope(scope()->FinalizeBlockScope());
    return block;
  }
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseVariableStatement(
    VariableDeclarationContext var_context,
    ZonePtrList<const AstRawString>* names) {
  // VariableStatement ::
  //   VariableDeclarations ';'

  // The scope of a var declared variable anywhere inside a function
  // is the entire function (ECMA-262, 3rd, 10.1.3, and 12.2). Thus we can
  // transform a source-level var declaration into a (Function) Scope
  // declaration, and rewrite the source-level initialization into an assignment
  // statement. We use a block to collect multiple assignments.
  //
  // We mark the block as initializer block because we don't want the
  // rewriter to add a '.result' assignment to such a block (to get compliant
  // behavior for code such as print(eval('var x = 7')), and for cosmetic
  // reasons when pretty-printing. Also, unless an assignment (initialization)
  // is inside an initializer block, it is ignored.

  DeclarationParsingResult parsing_result;
  ParseVariableDeclarations(var_context, &parsing_result, names);
  ExpectSemicolon();
  return impl()->BuildInitializationBlock(&parsing_result);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseDebuggerStatement() {
  // In ECMA-262 'debugger' is defined as a reserved keyword. In some browser
  // contexts this is used as a statement which invokes the debugger as i a
  // break point is present.
  // DebuggerStatement ::
  //   'debugger' ';'

  int pos = peek_position();
  Consume(Token::kDebugger);
  ExpectSemicolon();
  return factory()->NewDebuggerStatement(pos);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseExpressionOrLabelledStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels,
    AllowLabelledFunctionStatement allow_function) {
  // ExpressionStatement | LabelledStatement ::
  //   Expression ';'
  //   Identifier ':' Statement
  //
  // ExpressionStatement[Yield] :
  //   [lookahead notin {{, function, class, let [}] Expression[In, ?Yield] ;

  int pos = peek_position();

  switch (peek()) {
    case Token::kFunction:
    case Token::kLeftBrace:
      UNREACHABLE();  // Always handled by the callers.
    case Token::kClass:
      ReportUnexpectedToken(Next());
      return impl()->NullStatement();
    case Token::kLet: {
      Token::Value next_next = PeekAhead();
      // "let" followed by either "[", "{" or an identifier means a lexical
      // declaration, which should not appear here.
      // However, ASI may insert a line break before an identifier or a brace.
      if (next_next != Token::kLeftBracket &&
          ((next_next != Token::kLeftBrace &&
            next_next != Token::kIdentifier) ||
           scanner_->HasLineTerminatorAfterNext())) {
        break;
      }
      impl()->ReportMessageAt(scanner()->peek_location(),
                              MessageTemplate::kUnexpectedLexicalDeclaration);
      return impl()->NullStatement();
    }
    default:
      break;
  }

  bool starts_with_identifier = peek_any_identifier();

  ExpressionT expr;
  {
    // Effectively inlines ParseExpression, so potential labels can be extracted
    // from expression_scope.
    ExpressionParsingScope expression_scope(impl());
    AcceptINScope scope(this, true);
    expr = ParseExpressionCoverGrammar();
    expression_scope.ValidateExpression();

    if (peek() == Token::kColon && starts_with_identifier &&
        impl()->IsIdentifier(expr)) {
      // The whole expression was a single identifier, and not, e.g.,
      // something starting with an identifier or a parenthesized identifier.
      DCHECK_EQ(expression_scope.variable_list()->length(), 1);
      VariableProxy* label = expression_scope.variable_list()->at(0).first;
      impl()->DeclareLabel(&labels, &own_labels, label->raw_name());

      // Remove the "ghost" variable that turned out to be a label from the top
      // scope. This way, we don't try to resolve it during the scope
      // processing.
      this->scope()->DeleteUnresolved(label);

      Consume(Token::kColon);
      // ES#sec-labelled-function-declarations Labelled Function Declarations
      if (peek() == Token::kFunction && is_sloppy(language_mode()) &&
          allow_function == kAllowLabelledFunctionStatement) {
        return ParseFunctionDeclaration();
      }
      return ParseStatement(labels, own_labels, allow_function);
    }
  }

  // We allow a native function declaration if we're parsing the source for an
  // extension. A native function declaration starts with "native function"
  // with no line-terminator between the two words.
  if (impl()->ParsingExtension() && peek() == Token::kFunction &&
      !scanner()->HasLineTerminatorBeforeNext() && impl()->IsNative(expr) &&
      !scanner()->literal_contains_escapes()) {
    return ParseNativeDeclaration();
  }

  // Parsed expression statement, followed by semicolon.
  ExpectSemicolon();
  if (expr->IsFailureExpression()) return impl()->NullStatement();
  return factory()->NewExpressionStatement(expr, pos);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseIfStatement(
    ZonePtrList<const AstRawString>* labels) {
  // IfStatement ::
  //   'if' '(' Expression ')' Statement ('else' Statement)?

  int pos = peek_position();
  Consume(Token::kIf);
  Expect(Token::kLeftParen);
  ExpressionT condition = ParseExpression();
  Expect(Token::kRightParen);

  SourceRange then_range, else_range;
  StatementT then_statement = impl()->NullStatement();
  {
    SourceRangeScope range_scope(scanner(), &then_range);
    // Make a copy of {labels} to avoid conflicts with any
    // labels that may be applied to the else clause below.
    auto labels_copy =
        labels == nullptr
            ? labels
            : zone()->template New<ZonePtrList<const AstRawString>>(*labels,
                                                                    zone());
    then_statement = ParseScopedStatement(labels_copy);
  }

  StatementT else_statement = impl()->NullStatement();
  if (Check(Token::kElse)) {
    else_statement = ParseScopedStatement(labels);
    else_range = SourceRange::ContinuationOf(then_range, end_position());
  } else {
    else_statement = factory()->EmptyStatement();
  }
  StatementT stmt =
      factory()->NewIfStatement(condition, then_statement, else_statement, pos);
  impl()->RecordIfStatementSourceRange(stmt, then_range, else_range);
  return stmt;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseContinueStatement() {
  // ContinueStatement ::
  //   'continue' Identifier? ';'

  int pos = peek_position();
  Consume(Token::kContinue);
  IdentifierT label = impl()->NullIdentifier();
  Token::Value tok = peek();
  if (!scanner()->HasLineTerminatorBeforeNext() &&
      !Token::IsAutoSemicolon(tok)) {
    // ECMA allows "eval" or "arguments" as labels even in strict mode.
    label = ParseIdentifier();
  }
  IterationStatementT target = LookupContinueTarget(label);
  if (impl()->IsNull(target)) {
    // Illegal continue statement.
    MessageTemplate message = MessageTemplate::kIllegalContinue;
    BreakableStatementT breakable_target = LookupBreakTarget(label);
    if (impl()->IsNull(label)) {
      message = MessageTemplate::kNoIterationStatement;
    } else if (impl()->IsNull(breakable_target)) {
      message = MessageTemplate::kUnknownLabel;
    }
    ReportMessage(message, label);
    return impl()->NullStatement();
  }
  ExpectSemicolon();
  StatementT stmt = factory()->NewContinueStatement(target, pos);
  impl()->RecordJumpStatementSourceRange(stmt, end_position());
  return stmt;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseBreakStatement(
    ZonePtrList<const AstRawString>* labels) {
  // BreakStatement ::
  //   'break' Identifier? ';'

  int pos = peek_position();
  Consume(Token::kBreak);
  IdentifierT label = impl()->NullIdentifier();
  Token::Value tok = peek();
  if (!scanner()->HasLineTerminatorBeforeNext() &&
      !Token::IsAutoSemicolon(tok)) {
    // ECMA allows "eval" or "arguments" as labels even in strict mode.
    label = ParseIdentifier();
  }
  // Parse labeled break statements that target themselves into
  // empty statements, e.g. 'l1: l2: l3: break l2;'
  if (!impl()->IsNull(label) &&
      impl()->ContainsLabel(labels, impl()->GetRawNameFromIdentifier(label))) {
    ExpectSemicolon();
    return factory()->EmptyStatement();
  }
  BreakableStatementT target = LookupBreakTarget(label);
  if (impl()->IsNull(target)) {
    // Illegal break statement.
    MessageTemplate message = MessageTemplate::kIllegalBreak;
    if (!impl()->IsNull(label)) {
      message = MessageTemplate::kUnknownLabel;
    }
    ReportMessage(message, label);
    return impl()->NullStatement();
  }
  ExpectSemicolon();
  StatementT stmt = factory()->NewBreakStatement(target, pos);
  impl()->RecordJumpStatementSourceRange(stmt, end_position());
  return stmt;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseReturnStatement() {
  // ReturnStatement ::
  //   'return' [no line terminator] Expression? ';'

  // Consume the return token. It is necessary to do that before
  // reporting any errors on it, because of the way errors are
  // reported (underlining).
  Consume(Token::kReturn);
  Scanner::Location loc = scanner()->location();

  switch (GetDeclarationScope()->scope_type()) {
    case SCRIPT_SCOPE:
    case REPL_MODE_SCOPE:
    case EVAL_SCOPE:
    case MODULE_SCOPE:
      impl()->ReportMessageAt(loc, MessageTemplate::kIllegalReturn);
      return impl()->NullStatement();
    case BLOCK_SCOPE:
      // Class static blocks disallow return. They are their own var scopes and
      // have a varblock scope.
      if (function_state_->kind() ==
          FunctionKind::kClassStaticInitializerFunction) {
        impl()->ReportMessageAt(loc, MessageTemplate::kIllegalReturn);
        return impl()->NullStatement();
      }
      break;
    default:
      break;
  }

  Token::Value tok = peek();
  ExpressionT return_value = impl()->NullExpression();
  if (!scanner()->HasLineTerminatorBeforeNext() &&
      !Token::IsAutoSemicolon(tok)) {
    return_value = ParseExpression();
  }
  ExpectSemicolon();

  int continuation_pos = end_position();
  StatementT stmt =
      BuildReturnStatement(return_value, loc.beg_pos, continuation_pos);
  impl()->RecordJumpStatementSourceRange(stmt, end_position());
  return stmt;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseWithStatement(
    ZonePtrList<const AstRawString>* labels) {
  // WithStatement ::
  //   'with' '(' Expression ')' Statement

  Consume(Token::kWith);
  int pos = position();

  if (is_strict(language_mode())) {
    ReportMessage(MessageTemplate::kStrictWith);
    return impl()->NullStatement();
  }

  Expect(Token::kLeftParen);
  ExpressionT expr = ParseExpression();
  Expect(Token::kRightParen);

  Scope* with_scope = NewScope(WITH_SCOPE);
  StatementT body = impl()->NullStatement();
  {
    BlockState block_state(&scope_, with_scope);
    with_scope->set_start_position(position());
    body = ParseStatement(labels, nullptr);
    with_scope->set_end_position(end_position());
  }
  return factory()->NewWithStatement(with_scope, expr, body, pos);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseDoWhileStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  // DoStatement ::
  //   'do' Statement 'while' '(' Expression ')' ';'
  typename FunctionState::LoopScope loop_scope(function_state_);

  auto loop = factory()->NewDoWhileStatement(peek_position());
  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  SourceRange body_range;
  StatementT body = impl()->NullStatement();

  Consume(Token::kDo);

  CheckStackOverflow();
  {
    SourceRangeScope range_scope(scanner(), &body_range);
    body = ParseStatement(nullptr, nullptr);
  }
  Expect(Token::kWhile);
  Expect(Token::kLeftParen);

  ExpressionT cond = ParseExpression();
  Expect(Token::kRightParen);

  // Allow do-statements to be terminated with and without
  // semi-colons. This allows code such as 'do;while(0)return' to
  // parse, which would not be the case if we had used the
  // ExpectSemicolon() functionality here.
  Check(Token::kSemicolon);

  loop->Initialize(cond, body);
  impl()->RecordIterationStatementSourceRange(loop, body_range);

  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseWhileStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  // WhileStatement ::
  //   'while' '(' Expression ')' Statement
  typename FunctionState::LoopScope loop_scope(function_state_);

  auto loop = factory()->NewWhileStatement(peek_position());
  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  SourceRange body_range;
  StatementT body = impl()->NullStatement();

  Consume(Token::kWhile);
  Expect(Token::kLeftParen);
  ExpressionT cond = ParseExpression();
  Expect(Token::kRightParen);
  {
    SourceRangeScope range_scope(scanner(), &body_range);
    body = ParseStatement(nullptr, nullptr);
  }

  loop->Initialize(cond, body);
  impl()->RecordIterationStatementSourceRange(loop, body_range);

  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseThrowStatement() {
  // ThrowStatement ::
  //   'throw' Expression ';'

  Consume(Token::kThrow);
  int pos = position();
  if (scanner()->HasLineTerminatorBeforeNext()) {
    ReportMessage(MessageTemplate::kNewlineAfterThrow);
    return impl()->NullStatement();
  }
  ExpressionT exception = ParseExpression();
  ExpectSemicolon();

  StatementT stmt = impl()->NewThrowStatement(exception, pos);
  impl()->RecordThrowSourceRange(stmt, end_position());

  return stmt;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseSwitchStatement(
    ZonePtrList<const AstRawString>* labels) {
  // SwitchStatement ::
  //   'switch' '(' Expression ')' '{' CaseClause* '}'
  // CaseClause ::
  //   'case' Expression ':' StatementList
  //   'default' ':' StatementList
  int switch_pos = peek_position();

  Consume(Token::kSwitch);
  Expect(Token::kLeftParen);
  ExpressionT tag = ParseExpression();
  Expect(Token::kRightParen);

  auto switch_statement = factory()->NewSwitchStatement(tag, switch_pos);

  {
    BlockState cases_block_state(zone(), &scope_);
    scope()->set_start_position(switch_pos);
    scope()->SetNonlinear();
    Target target(this, switch_statement, labels, nullptr,
                  Target::TARGET_FOR_ANONYMOUS);

    bool default_seen = false;
    Expect(Token::kLeftBrace);
    while (peek() != Token::kRightBrace) {
      // An empty label indicates the default case.
      ExpressionT label = impl()->NullExpression();
      StatementListT statements(pointer_buffer());
      SourceRange clause_range;
      {
        SourceRangeScope range_scope(scanner(), &clause_range);
        if (Check(Token::kCase)) {
          label = ParseExpression();
        } else {
          Expect(Token::kDefault);
          if (default_seen) {
            ReportMessage(MessageTemplate::kMultipleDefaultsInSwitch);
            return impl()->NullStatement();
          }
          default_seen = true;
        }
        Expect(Token::kColon);
        while (peek() != Token::kCase && peek() != Token::kDefault &&
               peek() != Token::kRightBrace) {
          StatementT stat = ParseStatementListItem();
          if (impl()->IsNull(stat)) return stat;
          if (stat->IsEmptyStatement()) continue;
          statements.Add(stat);
        }
      }
      auto clause = factory()->NewCaseClause(label, statements);
      impl()->RecordCaseClauseSourceRange(clause, clause_range);
      switch_statement->cases()->Add(clause, zone());
    }
    Expect(Token::kRightBrace);

    int end_pos = end_position();
    scope()->set_end_position(end_pos);
    impl()->RecordSwitchStatementSourceRange(switch_statement, end_pos);
    Scope* switch_scope = scope()->FinalizeBlockScope();
    function_state_ =
        AddOneSuspendPointIfBlockContainsAwaitUsing(scope(), function_state_);
    if (switch_scope != nullptr) {
      return impl()->RewriteSwitchStatement(switch_statement, switch_scope);
    }
    return switch_statement;
  }
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseTryStatement() {
  // TryStatement ::
  //   'try' Block Catch
  //   'try' Block Finally
  //   'try' Block Catch Finally
  //
  // Catch ::
  //   'catch' '(' Identifier ')' Block
  //
  // Finally ::
  //   'finally' Block

  Consume(Token::kTry);
  int pos = position();

  BlockT try_block = ParseBlock(nullptr);

  CatchInfo catch_info(this);

  if (peek() != Token::kCatch && peek() != Token::kFinally) {
    ReportMessage(MessageTemplate::kNoCatchOrFinally);
    return impl()->NullStatement();
  }

  SourceRange catch_range, finally_range;

  BlockT catch_block = impl()->NullBlock();
  {
    SourceRangeScope catch_range_scope(scanner(), &catch_range);
    if (Check(Token::kCatch)) {
      bool has_binding;
      has_binding = Check(Token::kLeftParen);

      if (has_binding) {
        catch_info.scope = NewScope(CATCH_SCOPE);
        catch_info.scope->set_start_position(position());

        {
          BlockState catch_block_state(&scope_, catch_info.scope);
          StatementListT catch_statements(pointer_buffer());

          // Create a block scope to hold any lexical declarations created
          // as part of destructuring the catch parameter.
          {
            BlockState catch_variable_block_state(zone(), &scope_);
            scope()->set_start_position(peek_position());

            if (peek_any_identifier()) {
              IdentifierT identifier = ParseNonRestrictedIdentifier();
              RETURN_IF_PARSE_ERROR;
              catch_info.variable = impl()->DeclareCatchVariableName(
                  catch_info.scope, identifier);
            } else {
              catch_info.variable = catch_info.scope->DeclareCatchVariableName(
                  ast_value_factory()->dot_catch_string());

              auto declaration_it = scope()->declarations()->end();

              VariableDeclarationParsingScope destructuring(
                  impl(), VariableMode::kLet, nullptr);
              catch_info.pattern = ParseBindingPattern();

              int initializer_position = end_position();
              auto declaration_end = scope()->declarations()->end();
              for (; declaration_it != declaration_end; ++declaration_it) {
                declaration_it->var()->set_initializer_position(
                    initializer_position);
              }

              RETURN_IF_PARSE_ERROR;
              catch_statements.Add(impl()->RewriteCatchPattern(&catch_info));
            }

            Expect(Token::kRightParen);

            BlockT inner_block = ParseBlock(nullptr);
            catch_statements.Add(inner_block);

            // Check for `catch(e) { let e; }` and similar errors.
            if (!impl()->HasCheckedSyntax()) {
              Scope* inner_scope = inner_block->scope();
              if (inner_scope != nullptr) {
                const AstRawString* conflict = nullptr;
                if (impl()->IsNull(catch_info.pattern)) {
                  const AstRawString* name = catch_info.variable->raw_name();
                  if (inner_scope->LookupLocal(name)) conflict = name;
                } else {
                  conflict = inner_scope->FindVariableDeclaredIn(
                      scope(), VariableMode::kVar);
                }
                if (conflict != nullptr) {
                  impl()->ReportVarRedeclarationIn(conflict, inner_scope);
                }
              }
            }

            scope()->set_end_position(end_position());
            catch_block = factory()->NewBlock(false, catch_statements);
            catch_block->set_scope(scope()->FinalizeBlockScope());
          }
        }

        catch_info.scope->set_end_position(end_position());
      } else {
        catch_block = ParseBlock(nullptr);
      }
    }
  }

  BlockT finally_block = impl()->NullBlock();
  DCHECK(has_error() || peek() == Token::kFinally ||
         !impl()->IsNull(catch_block));
  {
    SourceRangeScope range_scope(scanner(), &finally_range);
    if (Check(Token::kFinally)) {
      finally_block = ParseBlock(nullptr);
    }
  }

  RETURN_IF_PARSE_ERROR;
  return impl()->RewriteTryStatement(try_block, catch_block, catch_range,
                                     finally_block, finally_range, catch_info,
                                     pos);
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseForStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  // Either a standard for loop
  //   for (<init>; <cond>; <next>) { ... }
  // or a for-each loop
  //   for (<each> of|in <iterable>) { ... }
  //
  // We parse a declaration/expression after the 'for (' and then read the first
  // expression/declaration before we know if this is a for or a for-each.
  typename FunctionState::LoopScope loop_scope(function_state_);

  int stmt_pos = peek_position();
  ForInfo for_info(this);

  Consume(Token::kFor);
  Expect(Token::kLeftParen);

  bool starts_with_let = peek() == Token::kLet;
  bool starts_with_using_keyword = IfStartsWithUsingKeyword();
  if (peek() == Token::kConst || (starts_with_let && IsNextLetKeyword()) ||
      starts_with_using_keyword) {
    // The initializer contains lexical declarations,
    // so create an in-between scope.
    BlockState for_state(zone(), &scope_);
    scope()->set_start_position(position());

    // Also record whether inner functions or evals are found inside
    // this loop, as this information is used to simplify the desugaring
    // if none are found.
    typename FunctionState::FunctionOrEvalRecordingScope recording_scope(
        function_state_);

    // Create an inner block scope which will be the parent scope of scopes
    // possibly created by ParseVariableDeclarations.
    Scope* inner_block_scope = NewScope(BLOCK_SCOPE);
    inner_block_scope->set_start_position(end_position());
    {
      BlockState inner_state(&scope_, inner_block_scope);
      ParseVariableDeclarations(kForStatement, &for_info.parsing_result,
                                &for_info.bound_names);
    }
    DCHECK(IsLexicalVariableMode(for_info.parsing_result.descriptor.mode));
    for_info.position = position();

    if (CheckInOrOf(&for_info.mode)) {
      scope()->set_is_hidden();
      if (starts_with_using_keyword &&
          for_info.mode == ForEachStatement::ENUMERATE) {
        impl()->ReportMessageAt(scanner()->location(),
                                MessageTemplate::kInvalidUsingInForInLoop);
      }
      return ParseForEachStatementWithDeclarations(
          stmt_pos, &for_info, labels, own_labels, inner_block_scope);
    }

    Expect(Token::kSemicolon);

    // Parse the remaining code in the inner block scope since the declaration
    // above was parsed there. We'll finalize the unnecessary outer block scope
    // after parsing the rest of the loop.
    StatementT result = impl()->NullStatement();
    {
      BlockState inner_state(&scope_, inner_block_scope);
      StatementT init =
          impl()->BuildInitializationBlock(&for_info.parsing_result);

      result = ParseStandardForLoopWithLexicalDeclarations(
          stmt_pos, init, &for_info, labels, own_labels);
    }
    Scope* finalized = scope()->FinalizeBlockScope();
    DCHECK_NULL(finalized);
    USE(finalized);
    return result;
  }

  StatementT init = impl()->NullStatement();
  if (peek() == Token::kVar) {
    ParseVariableDeclarations(kForStatement, &for_info.parsing_result,
                              &for_info.bound_names);
    DCHECK_EQ(for_info.parsing_result.descriptor.mode, VariableMode::kVar);
    for_info.position = position();

    if (CheckInOrOf(&for_info.mode)) {
      return ParseForEachStatementWithDeclarations(stmt_pos, &for_info, labels,
                                                   own_labels, scope());
    }

    init = impl()->BuildInitializationBlock(&for_info.parsing_result);
  } else if (peek() != Token::kSemicolon) {
    // The initializer does not contain declarations.
    Scanner::Location next_loc = scanner()->peek_location();
    int lhs_beg_pos = next_loc.beg_pos;
    int lhs_end_pos;
    bool is_for_each;
    ExpressionT expression;

    {
      ExpressionParsingScope parsing_scope(impl());
      AcceptINScope scope(this, false);
      expression = ParseExpressionCoverGrammar();
      // `for (async of` is disallowed but `for (async.x of` is allowed, so
      // check if the token is kAsync after parsing the expression.
      bool expression_is_async = scanner()->current_token() == Token::kAsync &&
                                 !scanner()->literal_contains_escapes();
      // Initializer is reference followed by in/of.
      lhs_end_pos = end_position();
      is_for_each = CheckInOrOf(&for_info.mode);
      if (is_for_each) {
        if ((starts_with_let || expression_is_async) &&
            for_info.mode == ForEachStatement::ITERATE) {
          impl()->ReportMessageAt(next_loc, starts_with_let
                                                ? MessageTemplate::kForOfLet
                                                : MessageTemplate::kForOfAsync);
          return impl()->NullStatement();
        }
        if (expression->IsPattern()) {
          parsing_scope.ValidatePattern(expression, lhs_beg_pos, lhs_end_pos);
        } else {
          expression = parsing_scope.ValidateAndRewriteReference(
              expression, lhs_beg_pos, lhs_end_pos);
        }
      } else {
        parsing_scope.ValidateExpression();
      }
    }

    if (is_for_each) {
      return ParseForEachStatementWithoutDeclarations(
          stmt_pos, expression, lhs_beg_pos, lhs_end_pos, &for_info, labels,
          own_labels);
    }
    // Initializer is just an expression.
    init = factory()->NewExpressionStatement(expression, lhs_beg_pos);
  }

  Expect(Token::kSemicolon);

  // Standard 'for' loop, we have parsed the initializer at this point.
  ExpressionT cond = impl()->NullExpression();
  StatementT next = impl()->NullStatement();
  StatementT body = impl()->NullStatement();
  ForStatementT loop =
      ParseStandardForLoop(stmt_pos, labels, own_labels, &cond, &next, &body);
  RETURN_IF_PARSE_ERROR;
  loop->Initialize(init, cond, next, body);
  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseForEachStatementWithDeclarations(
    int stmt_pos, ForInfo* for_info, ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels, Scope* inner_block_scope) {
  // Just one declaration followed by in/of.
  if (for_info->parsing_result.declarations.size() != 1) {
    impl()->ReportMessageAt(for_info->parsing_result.bindings_loc,
                            MessageTemplate::kForInOfLoopMultiBindings,
                            ForEachStatement::VisitModeString(for_info->mode));
    return impl()->NullStatement();
  }
  if (for_info->parsing_result.first_initializer_loc.IsValid() &&
      (is_strict(language_mode()) ||
       for_info->mode == ForEachStatement::ITERATE ||
       IsLexicalVariableMode(for_info->parsing_result.descriptor.mode) ||
       !impl()->IsIdentifier(
           for_info->parsing_result.declarations[0].pattern))) {
    impl()->ReportMessageAt(for_info->parsing_result.first_initializer_loc,
                            MessageTemplate::kForInOfLoopInitializer,
                            ForEachStatement::VisitModeString(for_info->mode));
    return impl()->NullStatement();
  }

  BlockT init_block = impl()->RewriteForVarInLegacy(*for_info);

  auto loop = factory()->NewForEachStatement(for_info->mode, stmt_pos);
  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  ExpressionT enumerable = impl()->NullExpression();
  if (for_info->mode == ForEachStatement::ITERATE) {
    AcceptINScope scope(this, true);
    enumerable = ParseAssignmentExpression();
  } else {
    enumerable = ParseExpression();
  }

  Expect(Token::kRightParen);

  ExpressionT each_variable = impl()->NullExpression();
  BlockT body_block = impl()->NullBlock();
  {
    BlockState block_state(&scope_, inner_block_scope);

    SourceRange body_range;
    StatementT body = impl()->NullStatement();
    {
      SourceRangeScope range_scope(scanner(), &body_range);
      body = ParseStatement(nullptr, nullptr);
    }
    impl()->RecordIterationStatementSourceRange(loop, body_range);

    impl()->DesugarBindingInForEachStatement(for_info, &body_block,
                                             &each_variable);
    body_block->statements()->Add(body, zone());

    if (IsLexicalVariableMode(for_info->parsing_result.descriptor.mode)) {
      scope()->set_end_position(end_position());
      body_block->set_scope(scope()->FinalizeBlockScope());
    }
  }

  loop->Initialize(each_variable, enumerable, body_block);

  init_block = impl()->CreateForEachStatementTDZ(init_block, *for_info);

  // Parsed for-in loop w/ variable declarations.
  if (!impl()->IsNull(init_block)) {
    init_block->statements()->Add(loop, zone());
    if (IsLexicalVariableMode(for_info->parsing_result.descriptor.mode)) {
      scope()->set_end_position(end_position());
      init_block->set_scope(scope()->FinalizeBlockScope());
    }
    return init_block;
  }

  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseForEachStatementWithoutDeclarations(
    int stmt_pos, ExpressionT expression, int lhs_beg_pos, int lhs_end_pos,
    ForInfo* for_info, ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  auto loop = factory()->NewForEachStatement(for_info->mode, stmt_pos);
  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  ExpressionT enumerable = impl()->NullExpression();
  if (for_info->mode == ForEachStatement::ITERATE) {
    AcceptINScope scope(this, true);
    enumerable = ParseAssignmentExpression();
  } else {
    enumerable = ParseExpression();
  }

  Expect(Token::kRightParen);

  StatementT body = impl()->NullStatement();
  SourceRange body_range;
  {
    SourceRangeScope range_scope(scanner(), &body_range);
    body = ParseStatement(nullptr, nullptr);
  }
  impl()->RecordIterationStatementSourceRange(loop, body_range);
  RETURN_IF_PARSE_ERROR;
  loop->Initialize(expression, enumerable, body);
  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT
ParserBase<Impl>::ParseStandardForLoopWithLexicalDeclarations(
    int stmt_pos, StatementT init, ForInfo* for_info,
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  // The condition and the next statement of the for loop must be parsed
  // in a new scope.
  Scope* inner_scope = NewScope(BLOCK_SCOPE);
  ForStatementT loop = impl()->NullStatement();
  ExpressionT cond = impl()->NullExpression();
  StatementT next = impl()->NullStatement();
  StatementT body = impl()->NullStatement();
  {
    BlockState block_state(&scope_, inner_scope);
    scope()->set_start_position(scanner()->location().beg_pos);
    loop =
        ParseStandardForLoop(stmt_pos, labels, own_labels, &cond, &next, &body);
    RETURN_IF_PARSE_ERROR;
    scope()->set_end_position(end_position());
  }

  scope()->set_end_position(end_position());
  if (for_info->bound_names.length() > 0 &&
      function_state_->contains_function_or_eval()) {
    scope()->set_is_hidden();
    return impl()->DesugarLexicalBindingsInForStatement(
        loop, init, cond, next, body, inner_scope, *for_info);
  } else {
    inner_scope = inner_scope->FinalizeBlockScope();
    DCHECK_NULL(inner_scope);
    USE(inner_scope);
  }

  Scope* for_scope = scope()->FinalizeBlockScope();
  if (for_scope != nullptr) {
    // Rewrite a for statement of the form
    //   for (const x = i; c; n) b
    //
    // into
    //
    //   {
    //     const x = i;
    //     for (; c; n) b
    //   }
    //
    DCHECK(!impl()->IsNull(init));
    BlockT block = factory()->NewBlock(2, false);
    block->statements()->Add(init, zone());
    block->statements()->Add(loop, zone());
    block->set_scope(for_scope);
    loop->Initialize(impl()->NullStatement(), cond, next, body);
    return block;
  }

  loop->Initialize(init, cond, next, body);
  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::ForStatementT ParserBase<Impl>::ParseStandardForLoop(
    int stmt_pos, ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels, ExpressionT* cond,
    StatementT* next, StatementT* body) {
  CheckStackOverflow();
  ForStatementT loop = factory()->NewForStatement(stmt_pos);
  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  if (peek() != Token::kSemicolon) {
    *cond = ParseExpression();
  }
  Expect(Token::kSemicolon);

  if (peek() != Token::kRightParen) {
    ExpressionT exp = ParseExpression();
    *next = factory()->NewExpressionStatement(exp, exp->position());
  }
  Expect(Token::kRightParen);

  SourceRange body_range;
  {
    SourceRangeScope range_scope(scanner(), &body_range);
    *body = ParseStatement(nullptr, nullptr);
  }
  impl()->RecordIterationStatementSourceRange(loop, body_range);

  return loop;
}

template <typename Impl>
typename ParserBase<Impl>::StatementT ParserBase<Impl>::ParseForAwaitStatement(
    ZonePtrList<const AstRawString>* labels,
    ZonePtrList<const AstRawString>* own_labels) {
  // for await '(' ForDeclaration of AssignmentExpression ')'
  DCHECK(is_await_allowed());
  typename FunctionState::LoopScope loop_scope(function_state_);

  int stmt_pos = peek_position();

  ForInfo for_info(this);
  for_info.mode = ForEachStatement::ITERATE;

  // Create an in-between scope for let-bound iteration variables.
  BlockState for_state(zone(), &scope_);
  Expect(Token::kFor);
  Expect(Token::kAwait);
  Expect(Token::kLeftParen);
  scope()->set_start_position(position());
  scope()->set_is_hidden();

  auto loop = factory()->NewForOfStatement(stmt_pos, IteratorType::kAsync);
  // Two suspends: one for next() and one for return()
  function_state_->AddSuspend();
  function_state_->AddSuspend();

  Target target(this, loop, labels, own_labels, Target::TARGET_FOR_ANONYMOUS);

  ExpressionT each_variable = impl()->NullExpression();

  bool has_declarations = false;
  Scope* inner_block_scope = NewScope(BLOCK_SCOPE);
  inner_block_scope->set_start_position(peek_position());

  bool starts_with_let = peek() == Token::kLet;
  if (peek() == Token::kVar || peek() == Token::kConst ||
      (starts_with_let && IsNextLetKeyword()) || IfStartsWithUsingKeyword()) {
    // The initializer contains declarations
    // 'for' 'await' '(' ForDeclaration 'of' AssignmentExpression ')'
    //     Statement
    // 'for' 'await' '(' 'var' ForBinding 'of' AssignmentExpression ')'
    //     Statement
    has_declarations = true;

    {
      BlockState inner_state(&scope_, inner_block_scope);
      ParseVariableDeclarations(kForStatement, &for_info.parsing_result,
                                &for_info.bound_names);
    }
    for_info.position = position();

    // Only a single declaration is allowed in for-await-of loops
    if (for_info.parsing_result.declarations.size() != 1) {
      impl()->ReportMessageAt(for_info.parsing_result.bindings_loc,
                              MessageTemplate::kForInOfLoopMultiBindings,
                              "for-await-of");
      return impl()->NullStatement();
    }

    // for-await-of's declarations do not permit initializers.
    if (for_info.parsing_result.first_initializer_loc.IsValid()) {
      impl()->ReportMessageAt(for_info.parsing_result.first_initializer_loc,
                              MessageTemplate::kForInOfLoopInitializer,
                              "for-await-of");
      return impl()->NullStatement();
    }
  } else {
    // The initializer does not contain declarations.
    // 'for' 'await' '(' LeftHandSideExpression 'of' AssignmentExpression ')'
    //     Statement
    if (starts_with_let) {
      impl()->ReportMessageAt(scanner()->peek_location(),
                              MessageTemplate::kForOfLet);
      return impl()->NullStatement();
    }
    int lhs_beg_pos = peek_position();
    BlockState inner_state(&scope_, inner_block_scope);
    ExpressionParsingScope parsing_scope(impl());
    ExpressionT lhs = each_variable = ParseLeftHandSideExpression();
    int lhs_end_pos = end_position();

    if (lhs->IsPattern()) {
      parsing_scope.ValidatePattern(lhs, lhs_beg_pos, lhs_end_pos);
    } else {
      each_variable = parsing_scope.ValidateAndRewriteReference(
          lhs, lhs_beg_pos, lhs_end_pos);
    }
  }

  ExpectContextualKeyword(Token::kOf);

  const bool kAllowIn = true;
  ExpressionT iterable = impl()->NullExpression();

  {
    AcceptINScope scope(this, kAllowIn);
    iterable = ParseAssignmentExpression();
  }

  Expect(Token::kRightParen);

  StatementT body = impl()->NullStatement();
  {
    BlockState block_state(&scope_, inner_block_scope);

    SourceRange body_range;
    {
      SourceRangeScope range_scope(scanner(), &body_range);
      body = ParseStatement(nullptr, nullptr);
      scope()->set_end_position(end_position());
    }
    impl()->RecordIterationStatementSourceRange(loop, body_range);

    if (has_declarations) {
      BlockT body_block = impl()->NullBlock();
      impl()->DesugarBindingInForEachStatement(&for_info, &body_block,
                                               &each_variable);
      body_block->statements()->Add(body, zone());
      body_block->set_scope(scope()->FinalizeBlockScope());
      body = body_block;
    } else {
      Scope* block_scope = scope()->FinalizeBlockScope();
      DCHECK_NULL(block_scope);
      USE(block_scope);
    }
  }

  loop->Initialize(each_variable, iterable, body);

  if (!has_declarations) {
    Scope* for_scope = scope()->FinalizeBlockScope();
    DCHECK_NULL(for_scope);
    USE(for_scope);
    return loop;
  }

  BlockT init_block =
      impl()->CreateForEachStatementTDZ(impl()->NullBlock(), for_info);

  scope()->set_end_position(end_position());
  Scope* for_scope = scope()->FinalizeBlockScope();
  // Parsed for-in loop w/ variable declarations.
  if (!impl()->IsNull(init_block)) {
    init_block->statements()->Add(loop, zone());
    init_block->set_scope(for_scope);
    return init_block;
  }
  DCHECK_NULL(for_scope);
  return loop;
}

template <typename Impl>
void ParserBase<Impl>::CheckClassMethodName(IdentifierT name,
                                            ParsePropertyKind type,
                                            ParseFunctionFlags flags,
                                            bool is_static,
                                            bool* has_seen_constructor) {
  DCHECK(type == ParsePropertyKind::kMethod || IsAccessor(type));

  AstValueFactory* avf = ast_value_factory();

  if (impl()->IdentifierEquals(name, avf->private_constructor_string())) {
    ReportMessage(MessageTemplate::kConstructorIsPrivate);
    return;
  } else if (is_static) {
    if (impl()->IdentifierEquals(name, avf->prototype_string())) {
      ReportMessage(MessageTemplate::kStaticPrototype);
      return;
    }
  } else if (impl()->IdentifierEquals(name, avf->constructor_string())) {
    if (flags != ParseFunctionFlag::kIsNormal || IsAccessor(type)) {
      MessageTemplate msg = (flags & ParseFunctionFlag::kIsGenerator) != 0
                                ? MessageTemplate::kConstructorIsGenerator
                                : (flags & ParseFunctionFlag::kIsAsync) != 0
                                      ? MessageTemplate::kConstructorIsAsync
                                      : MessageTemplate::kConstructorIsAccessor;
      ReportMessage(msg);
      return;
    }
    if (*has_seen_constructor) {
      ReportMessage(MessageTemplate::kDuplicateConstructor);
      return;
    }
    *has_seen_constructor = true;
    return;
  }
}

template <typename Impl>
void ParserBase<Impl>::CheckClassFieldName(IdentifierT name, bool is_static) {
  AstValueFactory* avf = ast_value_factory();
  if (is_static && impl()->IdentifierEquals(name, avf->prototype_string())) {
    ReportMessage(MessageTemplate::kStaticPrototype);
    return;
  }

  if (impl()->IdentifierEquals(name, avf->constructor_string()) ||
      impl()->IdentifierEquals(name, avf->private_constructor_string())) {
    ReportMessage(MessageTemplate::kConstructorClassField);
    return;
  }
}

#undef RETURN_IF_PARSE_ERROR

}  // namespace v8::internal

#endif  // V8_PARSING_PARSER_BASE_H_
