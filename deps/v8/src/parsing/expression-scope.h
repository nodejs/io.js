// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_EXPRESSION_SCOPE_H_
#define V8_PARSING_EXPRESSION_SCOPE_H_

#include <utility>

#include "src/ast/scopes.h"
#include "src/common/message-template.h"
#include "src/objects/function-kind.h"
#include "src/parsing/scanner.h"
#include "src/zone/zone.h"  // For ScopedPtrList.

namespace v8 {
namespace internal {

template <typename Types>
class ExpressionParsingScope;
template <typename Types>
class AccumulationScope;
template <typename Types>
class ArrowHeadParsingScope;
template <typename Types>
class ParameterDeclarationParsingScope;
template <typename Types>
class VariableDeclarationParsingScope;
class VariableProxy;

// ExpressionScope is used in a stack fashion, and is used to specialize
// expression parsing for the task at hand. It allows the parser to reuse the
// same code to parse destructuring declarations, assignment patterns,
// expressions, and (async) arrow function heads.
//
// One of the specific subclasses needs to be instantiated to tell the parser
// the meaning of the expression it will parse next. The parser then calls
// Record* on the expression_scope() to indicate errors. The expression_scope
// will either discard those errors, immediately report those errors, or
// classify the errors for later validation.
// TODO(verwaest): Record is a slightly odd name since it will directly throw
// for unambiguous scopes.
template <typename Types>
class ExpressionScope {
 public:
  ExpressionScope(const ExpressionScope&) = delete;
  ExpressionScope& operator=(const ExpressionScope&) = delete;

  using ParserT = typename Types::Impl;
  using ExpressionT = typename Types::Expression;

  VariableProxy* NewVariable(const AstRawString* name,
                             int pos = kNoSourcePosition) {
    VariableProxy* result = parser_->NewRawVariable(name, pos);
    if (CanBeExpression()) {
      AsExpressionParsingScope()->TrackVariable(result);
    } else {
      Variable* var = Declare(name, pos);
      if (IsVarDeclaration()) {
        bool passed_through_with = false;
        for (Scope* scope = parser()->scope(); !scope->is_declaration_scope();
             scope = scope->outer_scope()) {
          if (scope->is_with_scope()) {
            passed_through_with = true;
          } else if (scope->is_catch_scope()) {
            Variable* masking_var = scope->LookupLocal(name);
            // If a variable is declared in a catch scope with a masking
            // catch-declared variable, the initializing assignment is an
            // assignment to the catch-declared variable instead.
            // https://tc39.es/ecma262/#sec-variablestatements-in-catch-blocks
            if (masking_var != nullptr) {
              result->set_is_assigned();
              if (passed_through_with) break;
              result->BindTo(masking_var);
              masking_var->SetMaybeAssigned();
              return result;
            }
          }
        }
        if (passed_through_with) {
          // If a variable is declared in a with scope, the initializing
          // assignment might target a with-declared variable instead.
          parser()->scope()->AddUnresolved(result);
          return result;
        }
      }
      DCHECK_NOT_NULL(var);
      result->BindTo(var);
    }
    return result;
  }

  void MergeVariableList(
      ScopedList<std::pair<VariableProxy*, int>>* variable_list) {
    if (!CanBeExpression()) return;
    // Merged variables come from a CanBeDeclaration expression scope, and
    // weren't added as unresolved references to the variable scope yet. Add
    // them to the variable scope on the boundary where it becomes clear they
    // aren't declarations. We explicitly delay declaring the variables up to
    // that point to avoid trying to add them to the unresolved list multiple
    // times, e.g., for (((a))).
    if (!CanBeDeclaration()) {
      for (auto& proxy_initializer_pair : *variable_list) {
        VariableProxy* proxy = proxy_initializer_pair.first;
        this->parser()->scope()->AddUnresolved(proxy);
      }
    }
    variable_list->MergeInto(AsExpressionParsingScope()->variable_list());
  }

  Variable* Declare(const AstRawString* name, int pos = kNoSourcePosition) {
    if (type_ == kParameterDeclaration) {
      return AsParameterDeclarationParsingScope()->Declare(name, pos);
    }
    return AsVariableDeclarationParsingScope()->Declare(name, pos);
  }

  void MarkIdentifierAsAssigned() {
    if (!CanBeExpression()) return;
    AsExpressionParsingScope()->MarkIdentifierAsAssigned();
  }

  void ValidateAsPattern(ExpressionT expression, int begin, int end) {
    if (!CanBeExpression()) return;
    AsExpressionParsingScope()->ValidatePattern(expression, begin, end);
    AsExpressionParsingScope()->ClearExpressionError();
  }

  void ValidateAsExpression() {
    if (!CanBeExpression()) return;
    AsExpressionParsingScope()->ValidateExpression();
    AsExpressionParsingScope()->ClearPatternError();
  }

  // Record async arrow parameters errors in all ambiguous async arrow scopes in
  // the chain up to the first unambiguous scope.
  void RecordAsyncArrowParametersError(const Scanner::Location& loc,
                                       MessageTemplate message) {
    // Only ambiguous scopes (ExpressionParsingScope, *ArrowHeadParsingScope)
    // need to propagate errors to a possible kAsyncArrowHeadParsingScope, so
    // immediately return if the current scope is not ambiguous.
    if (!CanBeExpression()) return;
    AsExpressionParsingScope()->RecordAsyncArrowParametersError(loc, message);
  }

  // Record initializer errors in all scopes that can turn into parameter scopes
  // (ArrowHeadParsingScopes) up to the first known unambiguous parameter scope.
  void RecordParameterInitializerError(const Scanner::Location& loc,
                                       MessageTemplate message) {
    ExpressionScope* scope = this;
    while (!scope->IsCertainlyParameterDeclaration()) {
      if (!has_possible_parameter_in_scope_chain_) return;
      if (scope->CanBeParameterDeclaration()) {
        scope->AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
      }
      scope = scope->parent();
      if (scope == nullptr) return;
    }
    Report(loc, message);
  }

  void RecordThisUse() {
    ExpressionScope* scope = this;
    do {
      if (scope->IsArrowHeadParsingScope()) {
        scope->AsArrowHeadParsingScope()->RecordThisUse();
      }
      scope = scope->parent();
    } while (scope != nullptr);
  }

  void RecordPatternError(const Scanner::Location& loc,
                          MessageTemplate message) {
    // TODO(verwaest): Non-assigning expression?
    if (IsCertainlyPattern()) {
      Report(loc, message);
    } else {
      AsExpressionParsingScope()->RecordPatternError(loc, message);
    }
  }

  void RecordStrictModeParameterError(const Scanner::Location& loc,
                                      MessageTemplate message) {
    DCHECK_IMPLIES(!has_error(), loc.IsValid());
    if (!CanBeParameterDeclaration()) return;
    if (IsCertainlyParameterDeclaration()) {
      if (is_strict(parser_->language_mode())) {
        Report(loc, message);
      } else {
        parser_->parameters_->set_strict_parameter_error(loc, message);
      }
    } else {
      parser_->next_arrow_function_info_.strict_parameter_error_location = loc;
      parser_->next_arrow_function_info_.strict_parameter_error_message =
          message;
    }
  }

  void RecordDeclarationError(const Scanner::Location& loc,
                              MessageTemplate message) {
    if (!CanBeDeclaration()) return;
    if (IsCertainlyDeclaration()) {
      Report(loc, message);
    } else {
      AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
    }
  }

  void RecordExpressionError(const Scanner::Location& loc,
                             MessageTemplate message) {
    if (!CanBeExpression()) return;
    // TODO(verwaest): Non-assigning expression?
    // if (IsCertainlyExpression()) Report(loc, message);
    AsExpressionParsingScope()->RecordExpressionError(loc, message);
  }

  void RecordNonSimpleParameter() {
    if (!IsArrowHeadParsingScope()) return;
    AsArrowHeadParsingScope()->RecordNonSimpleParameter();
  }

  bool IsCertainlyDeclaration() const {
    return base::IsInRange(type_, kParameterDeclaration, kLexicalDeclaration);
  }

  int SetInitializers(int variable_index, int peek_position) {
    if (CanBeExpression()) {
      return AsExpressionParsingScope()->SetInitializers(variable_index,
                                                         peek_position);
    }
    return variable_index;
  }

  bool has_possible_arrow_parameter_in_scope_chain() const {
    return has_possible_arrow_parameter_in_scope_chain_;
  }

 protected:
  enum ScopeType : uint8_t {
    // Expression or assignment target.
    kExpression,

    // Declaration or expression or assignment target.
    kMaybeArrowParameterDeclaration,
    kMaybeAsyncArrowParameterDeclaration,

    // Declarations.
    kParameterDeclaration,
    kVarDeclaration,
    kLexicalDeclaration,
  };

  ParserT* parser() const { return parser_; }
  ExpressionScope* parent() const { return parent_; }

  void Report(const Scanner::Location& loc, MessageTemplate message) const {
    parser_->ReportMessageAt(loc, message);
  }

  ExpressionScope(ParserT* parser, ScopeType type)
      : parser_(parser),
        parent_(parser->expression_scope_),
        type_(type),
        has_possible_parameter_in_scope_chain_(
            CanBeParameterDeclaration() ||
            (parent_ && parent_->has_possible_parameter_in_scope_chain_)),
        has_possible_arrow_parameter_in_scope_chain_(
            CanBeArrowParameterDeclaration() ||
            (parent_ &&
             parent_->has_possible_arrow_parameter_in_scope_chain_)) {
    parser->expression_scope_ = this;
  }

  ~ExpressionScope() {
    DCHECK(parser_->expression_scope_ == this ||
           parser_->expression_scope_ == parent_);
    parser_->expression_scope_ = parent_;
  }

  ExpressionParsingScope<Types>* AsExpressionParsingScope() {
    DCHECK(CanBeExpression());
    return static_cast<ExpressionParsingScope<Types>*>(this);
  }

#ifdef DEBUG
  bool has_error() const { return parser_->has_error(); }
#endif

  bool CanBeExpression() const {
    return base::IsInRange(type_, kExpression,
                           kMaybeAsyncArrowParameterDeclaration);
  }
  bool CanBeDeclaration() const {
    return base::IsInRange(type_, kMaybeArrowParameterDeclaration,
                           kLexicalDeclaration);
  }
  bool IsVariableDeclaration() const {
    return base::IsInRange(type_, kVarDeclaration, kLexicalDeclaration);
  }
  bool IsLexicalDeclaration() const { return type_ == kLexicalDeclaration; }
  bool IsAsyncArrowHeadParsingScope() const {
    return type_ == kMaybeAsyncArrowParameterDeclaration;
  }
  bool IsVarDeclaration() const { return type_ == kVarDeclaration; }

 private:
  friend class AccumulationScope<Types>;
  friend class ExpressionParsingScope<Types>;

  ArrowHeadParsingScope<Types>* AsArrowHeadParsingScope() {
    DCHECK(IsArrowHeadParsingScope());
    return static_cast<ArrowHeadParsingScope<Types>*>(this);
  }

  ParameterDeclarationParsingScope<Types>*
  AsParameterDeclarationParsingScope() {
    DCHECK(IsCertainlyParameterDeclaration());
    return static_cast<ParameterDeclarationParsingScope<Types>*>(this);
  }

  VariableDeclarationParsingScope<Types>* AsVariableDeclarationParsingScope() {
    DCHECK(IsVariableDeclaration());
    return static_cast<VariableDeclarationParsingScope<Types>*>(this);
  }

  bool IsArrowHeadParsingScope() const {
    return base::IsInRange(type_, kMaybeArrowParameterDeclaration,
                           kMaybeAsyncArrowParameterDeclaration);
  }
  bool IsCertainlyPattern() const { return IsCertainlyDeclaration(); }
  bool CanBeParameterDeclaration() const {
    return base::IsInRange(type_, kMaybeArrowParameterDeclaration,
                           kParameterDeclaration);
  }
  bool CanBeArrowParameterDeclaration() const {
    return base::IsInRange(type_, kMaybeArrowParameterDeclaration,
                           kMaybeAsyncArrowParameterDeclaration);
  }
  bool IsCertainlyParameterDeclaration() const {
    return type_ == kParameterDeclaration;
  }

  ParserT* parser_;
  ExpressionScope<Types>* parent_;
  ScopeType type_;
  bool has_possible_parameter_in_scope_chain_;
  bool has_possible_arrow_parameter_in_scope_chain_;
};

// Used to unambiguously parse var, let, const declarations.
template <typename Types>
class VariableDeclarationParsingScope : public ExpressionScope<Types> {
 public:
  using ParserT = typename Types::Impl;
  using ExpressionScopeT = ExpressionScope<Types>;
  using ScopeType = typename ExpressionScopeT::ScopeType;

  VariableDeclarationParsingScope(ParserT* parser, VariableMode mode,
                                  ZonePtrList<const AstRawString>* names)
      : ExpressionScopeT(parser, IsLexicalVariableMode(mode)
                                     ? ExpressionScopeT::kLexicalDeclaration
                                     : ExpressionScopeT::kVarDeclaration),
        mode_(mode),
        names_(names) {}

  VariableDeclarationParsingScope(const VariableDeclarationParsingScope&) =
      delete;
  VariableDeclarationParsingScope& operator=(
      const VariableDeclarationParsingScope&) = delete;

  Variable* Declare(const AstRawString* name, int pos) {
    VariableKind kind = NORMAL_VARIABLE;
    bool was_added;
    Variable* var = this->parser()->DeclareVariable(
        name, kind, mode_, Variable::DefaultInitializationFlag(mode_),
        this->parser()->scope(), &was_added, pos);
    if (was_added &&
        this->parser()->scope()->num_var() > kMaxNumFunctionLocals) {
      this->parser()->ReportMessage(MessageTemplate::kTooManyVariables);
    }
    if (names_) names_->Add(name, this->parser()->zone());
    if (this->IsLexicalDeclaration()) {
      if (this->parser()->IsLet(name)) {
        this->parser()->ReportMessageAt(
            Scanner::Location(pos, pos + name->length()),
            MessageTemplate::kLetInLexicalBinding);
      }
    } else {
      if (this->parser()->loop_nesting_depth() > 0) {
        // Due to hoisting, the value of a 'var'-declared variable may actually
        // change even if the code contains only the "initial" assignment,
        // namely when that assignment occurs inside a loop.  For example:
        //
        //   let i = 10;
        //   do { var x = i } while (i--):
        //
        // Note that non-lexical variables include temporaries, which may also
        // get assigned inside a loop due to the various rewritings that the
        // parser performs.
        //
        // Pessimistically mark all vars in loops as assigned. This
        // overapproximates the actual assigned vars due to unassigned var
        // without initializer, but that's unlikely anyway.
        //
        // This also handles marking of loop variables in for-in and for-of
        // loops, as determined by loop-nesting-depth.
        DCHECK_NOT_NULL(var);
        var->SetMaybeAssigned();
      }
    }
    return var;
  }

 private:
  // Limit the allowed number of local variables in a function. The hard limit
  // in Ignition is 2^31-1 due to the size of register operands. We limit it to
  // a more reasonable lower up-limit.
  static const int kMaxNumFunctionLocals = (1 << 23) - 1;

  VariableMode mode_;
  ZonePtrList<const AstRawString>* names_;
};

template <typename Types>
class ParameterDeclarationParsingScope : public ExpressionScope<Types> {
 public:
  using ParserT = typename Types::Impl;
  using ExpressionScopeT = ExpressionScope<Types>;
  using ScopeType = typename ExpressionScopeT::ScopeType;

  explicit ParameterDeclarationParsingScope(ParserT* parser)
      : ExpressionScopeT(parser, ExpressionScopeT::kParameterDeclaration) {}

  ParameterDeclarationParsingScope(const ParameterDeclarationParsingScope&) =
      delete;
  ParameterDeclarationParsingScope& operator=(
      const ParameterDeclarationParsingScope&) = delete;

  Variable* Declare(const AstRawString* name, int pos) {
    VariableKind kind = PARAMETER_VARIABLE;
    VariableMode mode = VariableMode::kVar;
    bool was_added;
    Variable* var = this->parser()->DeclareVariable(
        name, kind, mode, Variable::DefaultInitializationFlag(mode),
        this->parser()->scope(), &was_added, pos);
    if (!has_duplicate() && !was_added) {
      duplicate_loc_ = Scanner::Location(pos, pos + name->length());
    }
    return var;
  }

  bool has_duplicate() const { return duplicate_loc_.IsValid(); }

  const Scanner::Location& duplicate_location() const { return duplicate_loc_; }

 private:
  Scanner::Location duplicate_loc_ = Scanner::Location::invalid();
};

// Parsing expressions is always ambiguous between at least left-hand-side and
// right-hand-side of assignments. This class is used to keep track of errors
// relevant for either side until it is clear what was being parsed.
// The class also keeps track of all variable proxies that are created while the
// scope was active. If the scope is an expression, the variable proxies will be
// added to the unresolved list. Otherwise they are declarations and aren't
// added. The list is also used to mark the variables as assigned in case we are
// parsing an assignment expression.
template <typename Types>
class ExpressionParsingScope : public ExpressionScope<Types> {
 public:
  using ParserT = typename Types::Impl;
  using ExpressionT = typename Types::Expression;
  using ExpressionScopeT = ExpressionScope<Types>;
  using ScopeType = typename ExpressionScopeT::ScopeType;

  explicit ExpressionParsingScope(
      ParserT* parser, ScopeType type = ExpressionScopeT::kExpression)
      : ExpressionScopeT(parser, type),
        variable_list_(parser->variable_buffer()),
        has_async_arrow_in_scope_chain_(
            type == ExpressionScopeT::kMaybeAsyncArrowParameterDeclaration ||
            (this->parent() && this->parent()->CanBeExpression() &&
             this->parent()
                 ->AsExpressionParsingScope()
                 ->has_async_arrow_in_scope_chain_)) {
    DCHECK(this->CanBeExpression());
    clear(kExpressionIndex);
    clear(kPatternIndex);
  }

  ExpressionParsingScope(const ExpressionParsingScope&) = delete;
  ExpressionParsingScope& operator=(const ExpressionParsingScope&) = delete;

  void RecordAsyncArrowParametersError(const Scanner::Location& loc,
                                       MessageTemplate message) {
    for (ExpressionScopeT* scope = this; scope != nullptr;
         scope = scope->parent()) {
      if (!has_async_arrow_in_scope_chain_) break;
      if (scope->type_ ==
          ExpressionScopeT::kMaybeAsyncArrowParameterDeclaration) {
        scope->AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
      }
    }
  }

  ~ExpressionParsingScope() { DCHECK(this->has_error() || verified_); }

  ExpressionT ValidateAndRewriteReference(ExpressionT expression, int beg_pos,
                                          int end_pos) {
    if (V8_LIKELY(this->parser()->IsAssignableIdentifier(expression))) {
      MarkIdentifierAsAssigned();
      this->mark_verified();
      return expression;
    } else if (V8_LIKELY(expression->IsProperty())) {
      ValidateExpression();
      return expression;
    }
    this->mark_verified();
    const bool early_error = false;
    return this->parser()->RewriteInvalidReferenceExpression(
        expression, beg_pos, end_pos, MessageTemplate::kInvalidLhsInFor,
        early_error);
  }

  void RecordExpressionError(const Scanner::Location& loc,
                             MessageTemplate message) {
    Record(kExpressionIndex, loc, message);
  }

  void RecordPatternError(const Scanner::Location& loc,
                          MessageTemplate message) {
    Record(kPatternIndex, loc, message);
  }

  void ValidateExpression() { Validate(kExpressionIndex); }

  void ValidatePattern(ExpressionT expression, int begin, int end) {
    Validate(kPatternIndex);
    if (expression->is_parenthesized()) {
      ExpressionScopeT::Report(Scanner::Location(begin, end),
                               MessageTemplate::kInvalidDestructuringTarget);
    }
    for (auto& variable_initializer_pair : variable_list_) {
      variable_initializer_pair.first->set_is_assigned();
    }
  }

  void ClearExpressionError() {
    DCHECK(verified_);
#ifdef DEBUG
    verified_ = false;
#endif
    clear(kExpressionIndex);
  }

  void ClearPatternError() {
    DCHECK(verified_);
#ifdef DEBUG
    verified_ = false;
#endif
    clear(kPatternIndex);
  }

  void TrackVariable(VariableProxy* variable) {
    if (!this->CanBeDeclaration()) {
      this->parser()->scope()->AddUnresolved(variable);
    }
    variable_list_.Add({variable, kNoSourcePosition});
  }

  void MarkIdentifierAsAssigned() {
    // It's possible we're parsing a syntax error. In that case it's not
    // guaranteed that there's a variable in the list.
    if (variable_list_.length() == 0) return;
    variable_list_.at(variable_list_.length() - 1).first->set_is_assigned();
  }

  int SetInitializers(int first_variable_index, int position) {
    int len = variable_list_.length();
    if (len == 0) return 0;

    int end = len - 1;
    // Loop backwards and abort as soon as we see one that's already set to
    // avoid a loop on expressions like a,b,c,d,e,f,g (outside of an arrowhead).
    // TODO(delphick): Look into removing this loop.
    for (int i = end; i >= first_variable_index &&
                      variable_list_.at(i).second == kNoSourcePosition;
         --i) {
      variable_list_.at(i).second = position;
    }
    return end;
  }

  ScopedList<std::pair<VariableProxy*, int>>* variable_list() {
    return &variable_list_;
  }

 protected:
  bool is_verified() const {
#ifdef DEBUG
    return verified_;
#else
    return false;
#endif
  }

  void ValidatePattern() { Validate(kPatternIndex); }

 private:
  friend class AccumulationScope<Types>;

  enum ErrorNumber : uint8_t {
    kExpressionIndex = 0,
    kPatternIndex = 1,
    kNumberOfErrors = 2,
  };
  void clear(int index) {
    messages_[index] = MessageTemplate::kNone;
    locations_[index] = Scanner::Location::invalid();
  }
  bool is_valid(int index) const { return !locations_[index].IsValid(); }
  void Record(int index, const Scanner::Location& loc,
              MessageTemplate message) {
    DCHECK_IMPLIES(!this->has_error(), loc.IsValid());
    if (!is_valid(index)) return;
    messages_[index] = message;
    locations_[index] = loc;
  }
  void Validate(int index) {
    DCHECK(!this->is_verified());
    if (!is_valid(index)) Report(index);
    this->mark_verified();
  }
  void Report(int index) const {
    ExpressionScopeT::Report(locations_[index], messages_[index]);
  }

  // Debug verification to make sure every scope is validated exactly once.
  void mark_verified() {
#ifdef DEBUG
    verified_ = true;
#endif
  }
  void clear_verified() {
#ifdef DEBUG
    verified_ = false;
#endif
  }
#ifdef DEBUG
  bool verified_ = false;
#endif

  ScopedList<std::pair<VariableProxy*, int>> variable_list_;
  MessageTemplate messages_[kNumberOfErrors];
  Scanner::Location locations_[kNumberOfErrors];
  bool has_async_arrow_in_scope_chain_;
};

// This class is used to parse multiple ambiguous expressions and declarations
// in the same scope. E.g., in async(X,Y,Z) or [X,Y,Z], X and Y and Z will all
// be parsed in the respective outer ArrowHeadParsingScope and
// ExpressionParsingScope. It provides a clean error state in the underlying
// scope to parse the individual expressions, while keeping track of the
// expression and pattern errors since the start. The AccumulationScope is only
// used to keep track of the errors so far, and the underlying ExpressionScope
// keeps being used as the expression_scope(). If the expression_scope() isn't
// ambiguous, this class does not do anything.
template <typename Types>
class AccumulationScope {
 public:
  using ParserT = typename Types::Impl;

  static const int kNumberOfErrors =
      ExpressionParsingScope<Types>::kNumberOfErrors;
  explicit AccumulationScope(ExpressionScope<Types>* scope) : scope_(nullptr) {
    if (!scope->CanBeExpression()) return;
    scope_ = scope->AsExpressionParsingScope();
    for (int i = 0; i < kNumberOfErrors; i++) {
      copy(i);
      scope_->clear(i);
    }
  }

  AccumulationScope(const AccumulationScope&) = delete;
  AccumulationScope& operator=(const AccumulationScope&) = delete;

  // Merge errors from the underlying ExpressionParsingScope into this scope.
  // Only keeps the first error across all accumulate calls, and removes the
  // error from the underlying scope.
  void Accumulate() {
    if (scope_ == nullptr) return;
    DCHECK(!scope_->is_verified());
    for (int i = 0; i < kNumberOfErrors; i++) {
      if (!locations_[i].IsValid()) copy(i);
      scope_->clear(i);
    }
  }

  // This is called instead of Accumulate in case the parsed member is already
  // known to be an expression. In that case we don't need to accumulate the
  // expression but rather validate it immediately. We also ignore the pattern
  // error since the parsed member is known to not be a pattern. This is
  // necessary for "{x:1}.y" parsed as part of an assignment pattern. {x:1} will
  // record a pattern error, but "{x:1}.y" is actually a valid as part of an
  // assignment pattern since it's a property access.
  void ValidateExpression() {
    if (scope_ == nullptr) return;
    DCHECK(!scope_->is_verified());
    scope_->ValidateExpression();
    DCHECK(scope_->is_verified());
    scope_->clear(ExpressionParsingScope<Types>::kPatternIndex);
#ifdef DEBUG
    scope_->clear_verified();
#endif
  }

  ~AccumulationScope() {
    if (scope_ == nullptr) return;
    Accumulate();
    for (int i = 0; i < kNumberOfErrors; i++) copy_back(i);
  }

 private:
  void copy(int entry) {
    messages_[entry] = scope_->messages_[entry];
    locations_[entry] = scope_->locations_[entry];
  }

  void copy_back(int entry) {
    if (!locations_[entry].IsValid()) return;
    scope_->messages_[entry] = messages_[entry];
    scope_->locations_[entry] = locations_[entry];
  }

  ExpressionParsingScope<Types>* scope_;
  MessageTemplate messages_[2];
  Scanner::Location locations_[2];
};

// The head of an arrow function is ambiguous between expression, assignment
// pattern and declaration. This keeps track of the additional declaration
// error and allows the scope to be validated as a declaration rather than an
// expression or a pattern.
template <typename Types>
class ArrowHeadParsingScope : public ExpressionParsingScope<Types> {
 public:
  using ParserT = typename Types::Impl;
  using ScopeType = typename ExpressionScope<Types>::ScopeType;

  ArrowHeadParsingScope(ParserT* parser, FunctionKind kind,
                        int function_literal_id)
      : ExpressionParsingScope<Types>(
            parser,
            kind == FunctionKind::kArrowFunction
                ? ExpressionScope<Types>::kMaybeArrowParameterDeclaration
                : ExpressionScope<Types>::kMaybeAsyncArrowParameterDeclaration),
        function_literal_id_(function_literal_id) {
    DCHECK(kind == FunctionKind::kAsyncArrowFunction ||
           kind == FunctionKind::kArrowFunction);
    DCHECK(this->CanBeDeclaration());
    DCHECK(!this->IsCertainlyDeclaration());
    // clear last next_arrow_function_info tracked strict parameters error.
    parser->next_arrow_function_info_.ClearStrictParameterError();
  }

  ArrowHeadParsingScope(const ArrowHeadParsingScope&) = delete;
  ArrowHeadParsingScope& operator=(const ArrowHeadParsingScope&) = delete;

  void ValidateExpression() {
    // Turns out this is not an arrow head. Clear any possible tracked strict
    // parameter errors, and reinterpret tracked variables as unresolved
    // references.
    this->parser()->next_arrow_function_info_.ClearStrictParameterError();
    ExpressionParsingScope<Types>::ValidateExpression();
    this->parent()->MergeVariableList(this->variable_list());
  }

  DeclarationScope* ValidateAndCreateScope() {
    DCHECK(!this->is_verified());
    DeclarationScope* result = this->parser()->NewFunctionScope(kind());
    if (declaration_error_location.IsValid()) {
      ExpressionScope<Types>::Report(declaration_error_location,
                                     declaration_error_message);
      return result;
    }
    this->ValidatePattern();

    if (!has_simple_parameter_list_) result->SetHasNonSimpleParameters();
    VariableKind kind = PARAMETER_VARIABLE;
    VariableMode mode =
        has_simple_parameter_list_ ? VariableMode::kVar : VariableMode::kLet;
    for (auto& proxy_initializer_pair : *this->variable_list()) {
      VariableProxy* proxy = proxy_initializer_pair.first;
      int initializer_position = proxy_initializer_pair.second;
      // Default values for parameters will have been parsed as assignments so
      // clear the is_assigned bit as they are not actually assignments.
      proxy->clear_is_assigned();
      bool was_added;
      this->parser()->DeclareAndBindVariable(proxy, kind, mode, result,
                                             &was_added, initializer_position);
      if (!was_added) {
        ExpressionScope<Types>::Report(proxy->location(),
                                       MessageTemplate::kParamDupe);
      }
    }

#ifdef DEBUG
    if (!this->has_error()) {
      for (auto declaration : *result->declarations()) {
        DCHECK_NE(declaration->var()->initializer_position(),
                  kNoSourcePosition);
      }
    }
#endif  // DEBUG

    if (uses_this_) result->UsesThis();
    return result;
  }

  void RecordDeclarationError(const Scanner::Location& loc,
                              MessageTemplate message) {
    DCHECK_IMPLIES(!this->has_error(), loc.IsValid());
    declaration_error_location = loc;
    declaration_error_message = message;
  }

  void RecordNonSimpleParameter() { has_simple_parameter_list_ = false; }
  void RecordThisUse() { uses_this_ = true; }
  int function_literal_id() const { return function_literal_id_; }

 private:
  FunctionKind kind() const {
    return this->IsAsyncArrowHeadParsingScope()
               ? FunctionKind::kAsyncArrowFunction
               : FunctionKind::kArrowFunction;
  }

  Scanner::Location declaration_error_location = Scanner::Location::invalid();
  MessageTemplate declaration_error_message = MessageTemplate::kNone;
  int function_literal_id_;
  bool has_simple_parameter_list_ = true;
  bool uses_this_ = false;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_EXPRESSION_SCOPE_H_
