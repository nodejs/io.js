// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_SCOPES_H_
#define V8_AST_SCOPES_H_

#include <numeric>

#include "src/ast/ast.h"
#include "src/base/compiler-specific.h"
#include "src/base/hashmap.h"
#include "src/base/pointer-with-payload.h"
#include "src/base/threaded-list.h"
#include "src/common/globals.h"
#include "src/objects/function-kind.h"
#include "src/zone/zone-hashmap.h"
#include "src/zone/zone.h"

namespace v8 {

namespace internal {
class Scope;
}  // namespace internal

namespace base {
template <>
struct PointerWithPayloadTraits<v8::internal::Scope> {
  static constexpr int kAvailableBits = 1;
};
}  // namespace base

namespace internal {

class AstNodeFactory;
class AstValueFactory;
class AstRawString;
class Declaration;
class ParseInfo;
class Parser;
class PreparseDataBuilder;
class SloppyBlockFunctionStatement;
class Statement;
class StringSet;
class VariableProxy;

using UnresolvedList =
    base::ThreadedList<VariableProxy, VariableProxy::UnresolvedNext>;

// A hash map to support fast variable declaration and lookup.
class VariableMap : public ZoneHashMap {
 public:
  explicit VariableMap(Zone* zone);
  VariableMap(const VariableMap& other, Zone* zone);

  VariableMap(VariableMap&& other) V8_NOEXCEPT : ZoneHashMap(std::move(other)) {
  }

  VariableMap& operator=(VariableMap&& other) V8_NOEXCEPT {
    static_cast<ZoneHashMap&>(*this) = std::move(other);
    return *this;
  }

  Variable* Declare(Zone* zone, Scope* scope, const AstRawString* name,
                    VariableMode mode, VariableKind kind,
                    InitializationFlag initialization_flag,
                    MaybeAssignedFlag maybe_assigned_flag,
                    IsStaticFlag is_static_flag, bool* was_added);

  V8_EXPORT_PRIVATE Variable* Lookup(const AstRawString* name);
  void Remove(Variable* var);
  void Add(Variable* var);

  Zone* zone() const { return allocator().zone(); }
};

// Global invariants after AST construction: Each reference (i.e. identifier)
// to a JavaScript variable (including global properties) is represented by a
// VariableProxy node. Immediately after AST construction and before variable
// allocation, most VariableProxy nodes are "unresolved", i.e. not bound to a
// corresponding variable (though some are bound during parse time). Variable
// allocation binds each unresolved VariableProxy to one Variable and assigns
// a location. Note that many VariableProxy nodes may refer to the same Java-
// Script variable.

// JS environments are represented in the parser using Scope, DeclarationScope
// and ModuleScope. DeclarationScope is used for any scope that hosts 'var'
// declarations. This includes script, module, eval, varblock, and function
// scope. ModuleScope further specializes DeclarationScope.
class V8_EXPORT_PRIVATE Scope : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  // ---------------------------------------------------------------------------
  // Construction

  Scope(Zone* zone, Scope* outer_scope, ScopeType scope_type);

#ifdef DEBUG
  // The scope name is only used for printing/debugging.
  void SetScopeName(const AstRawString* scope_name) {
    scope_name_ = scope_name;
  }
#endif

  // An ID that uniquely identifies this scope within the script. Inner scopes
  // have a higher ID than their outer scopes. ScopeInfo created from a scope
  // has the same ID as the scope.
  int UniqueIdInScript() const;

  DeclarationScope* AsDeclarationScope();
  const DeclarationScope* AsDeclarationScope() const;
  ModuleScope* AsModuleScope();
  const ModuleScope* AsModuleScope() const;
  ClassScope* AsClassScope();
  const ClassScope* AsClassScope() const;

  bool is_reparsed() const { return !scope_info_.is_null(); }

  // Re-writes the {VariableLocation} of top-level 'let' bindings from CONTEXT
  // to REPL_GLOBAL. Should only be called on REPL scripts.
  void RewriteReplGlobalVariables();

  class Snapshot final {
   public:
    inline explicit Snapshot(Scope* scope);

    // Disallow copy and move.
    Snapshot(const Snapshot&) = delete;
    Snapshot(Snapshot&&) = delete;

    ~Snapshot() {
      // Restore eval flags from before the scope was active.
      if (sloppy_eval_can_extend_vars_) {
        declaration_scope_->sloppy_eval_can_extend_vars_ = true;
      }
      if (calls_eval_) {
        outer_scope_->calls_eval_ = true;
      }
    }

    void Reparent(DeclarationScope* new_parent);

   private:
    Scope* outer_scope_;
    Scope* declaration_scope_;
    Scope* top_inner_scope_;
    UnresolvedList::Iterator top_unresolved_;
    base::ThreadedList<Variable>::Iterator top_local_;
    // While the scope is active, the scope caches the flag values for
    // outer_scope_ / declaration_scope_ they can be used to know what happened
    // while parsing the arrow head. If this turns out to be an arrow head, new
    // values on the respective scopes will be cleared and moved to the inner
    // scope. Otherwise the cached flags will be merged with the flags from the
    // arrow head.
    bool calls_eval_;
    bool sloppy_eval_can_extend_vars_;
  };

  enum class DeserializationMode { kIncludingVariables, kScopesOnly };

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  static Scope* DeserializeScopeChain(IsolateT* isolate, Zone* zone,
                                      Tagged<ScopeInfo> scope_info,
                                      DeclarationScope* script_scope,
                                      AstValueFactory* ast_value_factory,
                                      DeserializationMode deserialization_mode,
                                      ParseInfo* info = nullptr);

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  static void SetScriptScopeInfo(IsolateT* isolate,
                                 DeclarationScope* script_scope);

  // Checks if the block scope is redundant, i.e. it does not contain any
  // block scoped declarations. In that case it is removed from the scope
  // tree and its children are reparented.
  Scope* FinalizeBlockScope();

  Zone* zone() const { return variables_.zone(); }

  void SetMustUsePreparseData() {
    if (must_use_preparsed_scope_data_) {
      return;
    }
    must_use_preparsed_scope_data_ = true;
    if (outer_scope_) {
      outer_scope_->SetMustUsePreparseData();
    }
  }

  bool must_use_preparsed_scope_data() const {
    return must_use_preparsed_scope_data_;
  }

  // ---------------------------------------------------------------------------
  // Declarations

  // Lookup a variable in this scope. Returns the variable or nullptr if not
  // found.
  Variable* LookupLocal(const AstRawString* name) {
    DCHECK(scope_info_.is_null());
    return variables_.Lookup(name);
  }

  Variable* LookupInScopeInfo(const AstRawString* name, Scope* cache);

  // Declare a local variable in this scope. If the variable has been
  // declared before, the previously declared variable is returned.
  Variable* DeclareLocal(const AstRawString* name, VariableMode mode,
                         VariableKind kind, bool* was_added,
                         InitializationFlag init_flag = kCreatedInitialized);

  Variable* DeclareVariable(Declaration* declaration, const AstRawString* name,
                            int pos, VariableMode mode, VariableKind kind,
                            InitializationFlag init, bool* was_added,
                            bool* sloppy_mode_block_scope_function_redefinition,
                            bool* ok);

  // Returns nullptr if there was a declaration conflict.
  Variable* DeclareVariableName(const AstRawString* name, VariableMode mode,
                                bool* was_added,
                                VariableKind kind = NORMAL_VARIABLE);
  Variable* DeclareCatchVariableName(const AstRawString* name);

  Variable* DeclareHomeObjectVariable(AstValueFactory* ast_value_factory);
  Variable* DeclareStaticHomeObjectVariable(AstValueFactory* ast_value_factory);

  // Declarations list.
  base::ThreadedList<Declaration>* declarations() { return &decls_; }

  base::ThreadedList<Variable>* locals() { return &locals_; }

  // Create a new unresolved variable.
  VariableProxy* NewUnresolved(AstNodeFactory* factory,
                               const AstRawString* name, int start_pos,
                               VariableKind kind = NORMAL_VARIABLE) {
    DCHECK_IMPLIES(already_resolved_, reparsing_for_class_initializer_);
    DCHECK_EQ(factory->zone(), zone());
    VariableProxy* proxy = factory->NewVariableProxy(name, kind, start_pos);
    AddUnresolved(proxy);
    return proxy;
  }

  void AddUnresolved(VariableProxy* proxy);

  // Deletes an unresolved variable. The variable proxy cannot be reused for
  // another list later. During parsing, an unresolved variable may have been
  // added optimistically, but then only the variable name was used (typically
  // for labels and arrow function parameters). If the variable was not
  // declared, the addition introduced a new unresolved variable which may end
  // up being allocated globally as a "ghost" variable. DeleteUnresolved removes
  // such a variable again if it was added; otherwise this is a no-op.
  void DeleteUnresolved(VariableProxy* var);

  // Creates a new temporary variable in this scope's TemporaryScope.  The
  // name is only used for printing and cannot be used to find the variable.
  // In particular, the only way to get hold of the temporary is by keeping the
  // Variable* around.  The name should not clash with a legitimate variable
  // names.
  // TODO(verwaest): Move to DeclarationScope?
  Variable* NewTemporary(const AstRawString* name);

  // Find variable with (variable->mode() <= |mode_limit|) that was declared in
  // |scope|. This is used to catch patterns like `try{}catch(e){let e;}` and
  // function([e]) { let e }, which are errors even though the two 'e's are each
  // time declared in different scopes. Returns the first duplicate variable
  // name if there is one, nullptr otherwise.
  const AstRawString* FindVariableDeclaredIn(Scope* scope,
                                             VariableMode mode_limit);

  // ---------------------------------------------------------------------------
  // Scope-specific info.

  // Inform the scope and outer scopes that the corresponding code contains an
  // eval call.
  inline void RecordEvalCall();

  void RecordInnerScopeEvalCall() {
    inner_scope_calls_eval_ = true;
    for (Scope* scope = outer_scope(); scope != nullptr;
         scope = scope->outer_scope()) {
      if (scope->inner_scope_calls_eval_) return;
      scope->inner_scope_calls_eval_ = true;
    }
  }

  // Set the language mode flag (unless disabled by a global flag).
  void SetLanguageMode(LanguageMode language_mode) {
    DCHECK(!is_module_scope() || is_strict(language_mode));
    set_language_mode(language_mode);
  }

  // Inform the scope that the scope may execute declarations nonlinearly.
  // Currently, the only nonlinear scope is a switch statement. The name is
  // more general in case something else comes up with similar control flow,
  // for example the ability to break out of something which does not have
  // its own lexical scope.
  // The bit does not need to be stored on the ScopeInfo because none of
  // the three compilers will perform hole check elimination on a variable
  // located in VariableLocation::CONTEXT. So, direct eval and closures
  // will not expose holes.
  void SetNonlinear() { scope_nonlinear_ = true; }

  // Position in the source where this scope begins and ends.
  //
  // * For the scope of a with statement
  //     with (obj) stmt
  //   start position: start position of first token of 'stmt'
  //   end position: end position of last token of 'stmt'
  // * For the scope of a block
  //     { stmts }
  //   start position: start position of '{'
  //   end position: end position of '}'
  // * For the scope of a function literal or decalaration
  //     function fun(a,b) { stmts }
  //   start position: start position of '('
  //   end position: end position of '}'
  // * For the scope of a catch block
  //     try { stms } catch(e) { stmts }
  //   start position: start position of '('
  //   end position: end position of ')'
  // * For the scope of a for-statement
  //     for (let x ...) stmt
  //   start position: start position of '('
  //   end position: end position of last token of 'stmt'
  // * For the scope of a switch statement
  //     switch (tag) { cases }
  //   start position: start position of '{'
  //   end position: end position of '}'
  // * For the scope of a class literal or declaration
  //     class A extends B { body }
  //   start position: start position of 'class'
  //   end position: end position of '}'
  // * For the scope of a class member initializer functions:
  //     class A extends B { body }
  //   start position: start position of '{'
  //   end position: end position of '}'
  int start_position() const { return start_position_; }
  void set_start_position(int statement_pos) {
    start_position_ = statement_pos;
  }
  int end_position() const { return end_position_; }
  void set_end_position(int statement_pos) { end_position_ = statement_pos; }

  // Scopes created for desugaring are hidden. I.e. not visible to the debugger.
  bool is_hidden() const { return is_hidden_; }
  void set_is_hidden() { is_hidden_ = true; }

  void ForceContextAllocationForParameters() {
    DCHECK(!already_resolved_);
    force_context_allocation_for_parameters_ = true;
  }
  bool has_forced_context_allocation_for_parameters() const {
    return force_context_allocation_for_parameters_;
  }

  // ---------------------------------------------------------------------------
  // Predicates.

  // Specific scope types.
  bool is_eval_scope() const { return scope_type_ == EVAL_SCOPE; }
  bool is_function_scope() const { return scope_type_ == FUNCTION_SCOPE; }
  bool is_module_scope() const { return scope_type_ == MODULE_SCOPE; }
  bool is_script_scope() const {
    return scope_type_ == SCRIPT_SCOPE || scope_type_ == REPL_MODE_SCOPE;
  }
  bool is_catch_scope() const { return scope_type_ == CATCH_SCOPE; }
  bool is_block_scope() const {
    return scope_type_ == BLOCK_SCOPE || scope_type_ == CLASS_SCOPE;
  }
  bool is_with_scope() const { return scope_type_ == WITH_SCOPE; }
  bool is_declaration_scope() const { return is_declaration_scope_; }
  bool is_class_scope() const { return scope_type_ == CLASS_SCOPE; }
  bool is_home_object_scope() const {
    return is_class_scope() ||
           (is_block_scope() && is_block_scope_for_object_literal_);
  }
  bool is_block_scope_for_object_literal() const {
    DCHECK_IMPLIES(is_block_scope_for_object_literal_, is_block_scope());
    return is_block_scope_for_object_literal_;
  }
  void set_is_block_scope_for_object_literal() {
    DCHECK(is_block_scope());
    is_block_scope_for_object_literal_ = true;
  }

  bool inner_scope_calls_eval() const { return inner_scope_calls_eval_; }
  bool private_name_lookup_skips_outer_class() const {
    return private_name_lookup_skips_outer_class_;
  }

  bool has_using_declaration() const { return has_using_declaration_; }
  bool has_await_using_declaration() const {
    return has_await_using_declaration_;
  }

  bool is_wrapped_function() const {
    DCHECK_IMPLIES(is_wrapped_function_, is_function_scope());
    return is_wrapped_function_;
  }
  void set_is_wrapped_function() {
    DCHECK(is_function_scope());
    is_wrapped_function_ = true;
  }

#if V8_ENABLE_WEBASSEMBLY
  bool IsAsmModule() const;
  // Returns true if this scope or any inner scopes that might be eagerly
  // compiled are asm modules.
  bool ContainsAsmModule() const;
#endif  // V8_ENABLE_WEBASSEMBLY

  // Does this scope have the potential to execute declarations non-linearly?
  bool is_nonlinear() const { return scope_nonlinear_; }
  // Returns if we need to force a context because the current scope is stricter
  // than the outerscope. We need this to properly track the language mode using
  // the context. This is required in ICs where we lookup the language mode
  // from the context.
  bool ForceContextForLanguageMode() const {
    // For function scopes we need not force a context since the language mode
    // can be obtained from the closure. Script scopes always have a context.
    if (scope_type_ == FUNCTION_SCOPE || is_script_scope()) {
      return false;
    }
    DCHECK_NOT_NULL(outer_scope_);
    return (language_mode() > outer_scope_->language_mode());
  }

  // Whether this needs to be represented by a runtime context.
  bool NeedsContext() const {
    // Catch scopes always have heap slots.
    DCHECK_IMPLIES(is_catch_scope(), num_heap_slots() > 0);
    DCHECK_IMPLIES(is_with_scope(), num_heap_slots() > 0);
    DCHECK_IMPLIES(ForceContextForLanguageMode(), num_heap_slots() > 0);
    return num_heap_slots() > 0;
  }

  // Use Scope::ForEach for depth first traversal of scopes.
  // Before:
  // void Scope::VisitRecursively() {
  //   DoSomething();
  //   for (Scope* s = inner_scope_; s != nullptr; s = s->sibling_) {
  //     if (s->ShouldContinue()) continue;
  //     s->VisitRecursively();
  //   }
  // }
  //
  // After:
  // void Scope::VisitIteratively() {
  //   this->ForEach([](Scope* s) {
  //      s->DoSomething();
  //      return s->ShouldContinue() ? kContinue : kDescend;
  //   });
  // }
  template <typename FunctionType>
  V8_INLINE void ForEach(FunctionType callback);
  enum Iteration {
    // Continue the iteration on the same level, do not recurse/descent into
    // inner scopes.
    kContinue,
    // Recurse/descend into inner scopes.
    kDescend
  };

  bool IsConstructorScope() const;

  // Check is this scope is an outer scope of the given scope.
  bool IsOuterScopeOf(Scope* other) const;

  // ---------------------------------------------------------------------------
  // Accessors.

  // The type of this scope.
  ScopeType scope_type() const { return scope_type_; }

  // The language mode of this scope.
  LanguageMode language_mode() const {
    return is_strict_ ? LanguageMode::kStrict : LanguageMode::kSloppy;
  }

  // inner_scope() and sibling() together implement the inner scope list of a
  // scope. Inner scope points to the an inner scope of the function, and
  // "sibling" points to a next inner scope of the outer scope of this scope.
  Scope* inner_scope() const { return inner_scope_; }
  Scope* sibling() const { return sibling_; }

  // The scope immediately surrounding this scope, or nullptr.
  Scope* outer_scope() const { return outer_scope_; }

  Variable* catch_variable() const {
    DCHECK(is_catch_scope());
    DCHECK_EQ(1, num_var());
    return static_cast<Variable*>(variables_.Start()->value);
  }

  bool ShouldBanArguments();

  // ---------------------------------------------------------------------------
  // Variable allocation.

  // Result of variable allocation.
  int num_stack_slots() const { return num_stack_slots_; }
  int num_heap_slots() const { return num_heap_slots_; }

  bool HasContextExtensionSlot() const {
    switch (scope_type_) {
      case MODULE_SCOPE:
      case WITH_SCOPE:  // DebugEvaluateContext as well
      case SCRIPT_SCOPE:  // Side data for const tracking let.
      case REPL_MODE_SCOPE:
        return true;
      default:
        DCHECK_IMPLIES(sloppy_eval_can_extend_vars_,
                       scope_type_ == FUNCTION_SCOPE ||
                           scope_type_ == EVAL_SCOPE ||
                           scope_type_ == BLOCK_SCOPE);
        DCHECK_IMPLIES(sloppy_eval_can_extend_vars_, is_declaration_scope());
        return sloppy_eval_can_extend_vars_;
    }
    UNREACHABLE();
  }
  int ContextHeaderLength() const {
    return HasContextExtensionSlot() ? Context::MIN_CONTEXT_EXTENDED_SLOTS
                                     : Context::MIN_CONTEXT_SLOTS;
  }

  int ContextLocalCount() const;

  // Determine if we can parse a function literal in this scope lazily without
  // caring about the unresolved variables within.
  bool AllowsLazyParsingWithoutUnresolvedVariables(const Scope* outer) const;

  // The number of contexts between this and scope; zero if this == scope.
  int ContextChainLength(Scope* scope) const;

  // The number of contexts between this and the outermost context that has a
  // sloppy eval call. One if this->sloppy_eval_can_extend_vars().
  int ContextChainLengthUntilOutermostSloppyEval() const;

  // Find the first function, script, eval or (declaration) block scope. This is
  // the scope where var declarations will be hoisted to in the implementation.
  DeclarationScope* GetDeclarationScope();

  // Find the first function, script, or (declaration) block scope.
  // This is the scope where var declarations will be hoisted to in the
  // implementation, including vars in direct sloppy eval calls.
  //
  // TODO(leszeks): Check how often we skip eval scopes in GetDeclarationScope,
  // and possibly merge this with GetDeclarationScope.
  DeclarationScope* GetNonEvalDeclarationScope();

  // Find the first non-block declaration scope. This should be either a script,
  // function, or eval scope. Same as DeclarationScope(), but skips declaration
  // "block" scopes. Used for differentiating associated function objects (i.e.,
  // the scope for which a function prologue allocates a context) or declaring
  // temporaries.
  DeclarationScope* GetClosureScope();
  const DeclarationScope* GetClosureScope() const;

  // Find the first (non-arrow) function or script scope.  This is where
  // 'this' is bound, and what determines the function kind.
  DeclarationScope* GetReceiverScope();

  // Find the first constructor scope. Its outer scope is where the instance
  // members that should be initialized right after super() is called
  // are declared.
  DeclarationScope* GetConstructorScope();

  // Find the first class scope or object literal block scope. This is where
  // 'super' is bound.
  Scope* GetHomeObjectScope();

  DeclarationScope* GetScriptScope();

  // Find the innermost outer scope that needs a context.
  Scope* GetOuterScopeWithContext();

  bool HasReceiverToDeserialize() const;
  bool HasThisReference() const;
  // Analyze() must have been called once to create the ScopeInfo.
  Handle<ScopeInfo> scope_info() const {
    DCHECK(!scope_info_.is_null());
    return scope_info_;
  }

  int num_var() const { return variables_.occupancy(); }

  // ---------------------------------------------------------------------------
  // Debugging.

#ifdef DEBUG
  void Print(int n = 0);  // n = indentation; n < 0 => don't print recursively

  // Check that the scope has positions assigned.
  void CheckScopePositions();

  // Check that all Scopes in the scope tree use the same Zone.
  void CheckZones();

  void MarkReparsingForClassInitializer() {
    reparsing_for_class_initializer_ = true;
  }
#endif

  // Retrieve `IsSimpleParameterList` of current or outer function.
  bool HasSimpleParameters();
  void set_is_debug_evaluate_scope() { is_debug_evaluate_scope_ = true; }
  bool is_debug_evaluate_scope() const { return is_debug_evaluate_scope_; }
  bool IsSkippableFunctionScope();
  bool is_repl_mode_scope() const { return scope_type_ == REPL_MODE_SCOPE; }

  bool needs_home_object() const {
    DCHECK(is_home_object_scope());
    return needs_home_object_;
  }

  void set_needs_home_object() {
    DCHECK(is_home_object_scope());
    needs_home_object_ = true;
  }

  bool RemoveInnerScope(Scope* inner_scope) {
    DCHECK_NOT_NULL(inner_scope);
    if (inner_scope == inner_scope_) {
      inner_scope_ = inner_scope_->sibling_;
      return true;
    }
    for (Scope* scope = inner_scope_; scope != nullptr;
         scope = scope->sibling_) {
      if (scope->sibling_ == inner_scope) {
        scope->sibling_ = scope->sibling_->sibling_;
        return true;
      }
    }
    return false;
  }

  Variable* LookupInScopeOrScopeInfo(const AstRawString* name, Scope* cache) {
    Variable* var = variables_.Lookup(name);
    if (var != nullptr || scope_info_.is_null()) return var;
    return LookupInScopeInfo(name, cache);
  }

  Variable* LookupForTesting(const AstRawString* name) {
    for (Scope* scope = this; scope != nullptr; scope = scope->outer_scope()) {
      Variable* var = scope->LookupInScopeOrScopeInfo(name, scope);
      if (var != nullptr) return var;
    }
    return nullptr;
  }

  void ForceDynamicLookup(VariableProxy* proxy);

 protected:
  Scope(Zone* zone, ScopeType scope_type);

  void set_language_mode(LanguageMode language_mode) {
    is_strict_ = is_strict(language_mode);
  }

 private:
  Variable* Declare(Zone* zone, const AstRawString* name, VariableMode mode,
                    VariableKind kind, InitializationFlag initialization_flag,
                    MaybeAssignedFlag maybe_assigned_flag, bool* was_added) {
    // Static variables can only be declared using ClassScope methods.
    Variable* result = variables_.Declare(
        zone, this, name, mode, kind, initialization_flag, maybe_assigned_flag,
        IsStaticFlag::kNotStatic, was_added);
    if (mode == VariableMode::kUsing) has_using_declaration_ = true;
    if (mode == VariableMode::kAwaitUsing) has_await_using_declaration_ = true;
    if (*was_added) locals_.Add(result);
    return result;
  }

  // This method should only be invoked on scopes created during parsing (i.e.,
  // not deserialized from a context). Also, since NeedsContext() is only
  // returning a valid result after variables are resolved, NeedsScopeInfo()
  // should also be invoked after resolution.
  bool NeedsScopeInfo() const;

  Variable* NewTemporary(const AstRawString* name,
                         MaybeAssignedFlag maybe_assigned);

  // Walk the scope chain to find DeclarationScopes; call
  // SavePreparseDataForDeclarationScope for each.
  void SavePreparseData(Parser* parser);

  // Create a non-local variable with a given name.
  // These variables are looked up dynamically at runtime.
  Variable* NonLocal(const AstRawString* name, VariableMode mode);

  enum ScopeLookupMode {
    kParsedScope,
    kDeserializedScope,
  };

  // Variable resolution.
  // Lookup a variable reference given by name starting with this scope, and
  // stopping when reaching the outer_scope_end scope. If the code is executed
  // because of a call to 'eval', the context parameter should be set to the
  // calling context of 'eval'.
  template <ScopeLookupMode mode>
  static Variable* Lookup(VariableProxy* proxy, Scope* scope,
                          Scope* outer_scope_end, Scope* cache_scope = nullptr,
                          bool force_context_allocation = false);
  static Variable* LookupWith(VariableProxy* proxy, Scope* scope,
                              Scope* outer_scope_end, Scope* cache_scope,
                              bool force_context_allocation);
  static Variable* LookupSloppyEval(VariableProxy* proxy, Scope* scope,
                                    Scope* outer_scope_end, Scope* cache_scope,
                                    bool force_context_allocation);
  static void ResolvePreparsedVariable(VariableProxy* proxy, Scope* scope,
                                       Scope* end);
  void ResolveTo(VariableProxy* proxy, Variable* var);
  void ResolveVariable(VariableProxy* proxy);
  V8_WARN_UNUSED_RESULT bool ResolveVariablesRecursively(Scope* end);

  // Finds free variables of this scope. This mutates the unresolved variables
  // list along the way, so full resolution cannot be done afterwards.
  void AnalyzePartially(DeclarationScope* max_outer_scope,
                        AstNodeFactory* ast_node_factory,
                        UnresolvedList* new_unresolved_list,
                        bool maybe_in_arrowhead);

  // Predicates.
  bool MustAllocate(Variable* var);
  bool MustAllocateInContext(Variable* var);

  // Variable allocation.
  void AllocateStackSlot(Variable* var);
  V8_INLINE void AllocateHeapSlot(Variable* var);
  void AllocateNonParameterLocal(Variable* var);
  void AllocateDeclaredGlobal(Variable* var);
  V8_INLINE void AllocateNonParameterLocalsAndDeclaredGlobals();
  void AllocateVariablesRecursively();

  template <typename IsolateT>
  void AllocateScopeInfosRecursively(
      IsolateT* isolate, MaybeHandle<ScopeInfo> outer_scope,
      std::unordered_map<int, IndirectHandle<ScopeInfo>>& scope_infos_to_reuse);

  // Construct a scope based on the scope info.
  Scope(Zone* zone, ScopeType type, AstValueFactory* ast_value_factory,
        Handle<ScopeInfo> scope_info);

  // Construct a catch scope with a binding for the name.
  Scope(Zone* zone, const AstRawString* catch_variable_name,
        MaybeAssignedFlag maybe_assigned, Handle<ScopeInfo> scope_info);

  void AddInnerScope(Scope* inner_scope) {
    inner_scope->sibling_ = inner_scope_;
    inner_scope_ = inner_scope;
    inner_scope->outer_scope_ = this;
  }

  void SetDefaults();

  friend class DeclarationScope;
  friend class ClassScope;
  friend class ScopeTestHelper;
  friend Zone;

  // Scope tree.
  Scope* outer_scope_;  // the immediately enclosing outer scope, or nullptr
  Scope* inner_scope_;  // an inner scope of this scope
  Scope* sibling_;  // a sibling inner scope of the outer scope of this scope.

  // The variables declared in this scope:
  //
  // All user-declared variables (incl. parameters).  For script scopes
  // variables may be implicitly 'declared' by being used (possibly in
  // an inner scope) with no intervening with statements or eval calls.
  VariableMap variables_;
  // In case of non-scopeinfo-backed scopes, this contains the variables of the
  // map above in order of addition.
  base::ThreadedList<Variable> locals_;
  // Unresolved variables referred to from this scope. The proxies themselves
  // form a linked list of all unresolved proxies.
  UnresolvedList unresolved_list_;
  // Declarations.
  base::ThreadedList<Declaration> decls_;

  // Serialized scope info support.
  IndirectHandle<ScopeInfo> scope_info_;
// Debugging support.
#ifdef DEBUG
  const AstRawString* scope_name_;

  // True if it doesn't need scope resolution (e.g., if the scope was
  // constructed based on a serialized scope info or a catch context).
  bool already_resolved_;
  bool reparsing_for_class_initializer_;
  // True if this scope may contain objects from a temp zone that needs to be
  // fixed up.
  bool needs_migration_;
#endif

  // Source positions.
  int start_position_;
  int end_position_;

  // Computed via AllocateVariables.
  int num_stack_slots_;
  int num_heap_slots_;

  // The scope type.
  const ScopeType scope_type_;

  // Scope-specific information computed during parsing.
  //
  // The language mode of this scope.
  static_assert(LanguageModeSize == 2);
  bool is_strict_ : 1;
  // This scope contains an 'eval' call.
  bool calls_eval_ : 1;
  // The context associated with this scope can be extended by a sloppy eval
  // called inside of it.
  bool sloppy_eval_can_extend_vars_ : 1;
  // This scope's declarations might not be executed in order (e.g., switch).
  bool scope_nonlinear_ : 1;
  bool is_hidden_ : 1;
  // Temporary workaround that allows masking of 'this' in debug-evaluate
  // scopes.
  bool is_debug_evaluate_scope_ : 1;

  // True if one of the inner scopes or the scope itself calls eval.
  bool inner_scope_calls_eval_ : 1;
  bool force_context_allocation_for_parameters_ : 1;

  // True if it holds 'var' declarations.
  bool is_declaration_scope_ : 1;

  // True if the outer scope is a class scope and should be skipped when
  // resolving private names, i.e. if the scope is in a class heritage
  // expression.
  bool private_name_lookup_skips_outer_class_ : 1;

  bool must_use_preparsed_scope_data_ : 1;

  bool needs_home_object_ : 1;
  bool is_block_scope_for_object_literal_ : 1;

  // If declarations include any `using` or `await using` declarations.
  bool has_using_declaration_ : 1;
  bool has_await_using_declaration_ : 1;

  // If the scope was generated for wrapped function syntax, which will affect
  // its UniqueIdInScript.
  bool is_wrapped_function_ : 1;
};

class V8_EXPORT_PRIVATE DeclarationScope : public Scope {
 public:
  DeclarationScope(Zone* zone, Scope* outer_scope, ScopeType scope_type,
                   FunctionKind function_kind = FunctionKind::kNormalFunction);
  DeclarationScope(Zone* zone, ScopeType scope_type,
                   AstValueFactory* ast_value_factory,
                   Handle<ScopeInfo> scope_info);
  // Creates a script scope.
  DeclarationScope(Zone* zone, AstValueFactory* ast_value_factory,
                   REPLMode repl_mode = REPLMode::kNo);

  FunctionKind function_kind() const { return function_kind_; }

  // Inform the scope that the corresponding code uses "super".
  void RecordSuperPropertyUsage() {
    DCHECK(IsConciseMethod(function_kind()) ||
           IsAccessorFunction(function_kind()) ||
           IsClassConstructor(function_kind()));
    uses_super_property_ = true;
    Scope* home_object_scope = GetHomeObjectScope();
    DCHECK_NOT_NULL(home_object_scope);
    home_object_scope->set_needs_home_object();
  }

  bool uses_super_property() const { return uses_super_property_; }

  void TakeUnresolvedReferencesFromParent();

  bool is_arrow_scope() const {
    return is_function_scope() && IsArrowFunction(function_kind_);
  }

  // Inform the scope and outer scopes that the corresponding code contains an
  // eval call.
  void RecordDeclarationScopeEvalCall() {
    calls_eval_ = true;

    // The caller already checked whether we're in sloppy mode.
    CHECK(is_sloppy(language_mode()));

    // Sloppy eval in script scopes can only introduce global variables anyway,
    // so we don't care that it calls sloppy eval.
    if (is_script_scope()) return;

    // Sloppy eval in an eval scope can only introduce variables into the outer
    // (non-eval) declaration scope, not into this eval scope.
    if (is_eval_scope()) {
#ifdef DEBUG
      // One of three things must be true:
      //   1. The outer non-eval declaration scope should already be marked as
      //      being extendable by sloppy eval, by the current sloppy eval rather
      //      than the inner one,
      //   2. The outer non-eval declaration scope is a script scope and thus
      //      isn't extendable anyway, or
      //   3. This is a debug evaluate and all bets are off.
      DeclarationScope* outer_decl_scope = outer_scope()->GetDeclarationScope();
      while (outer_decl_scope->is_eval_scope()) {
        outer_decl_scope = outer_decl_scope->GetDeclarationScope();
      }
      if (outer_decl_scope->is_debug_evaluate_scope()) {
        // Don't check anything.
        // TODO(9662): Figure out where variables declared by an eval inside a
        // debug-evaluate actually go.
      } else if (!outer_decl_scope->is_script_scope()) {
        DCHECK(outer_decl_scope->sloppy_eval_can_extend_vars_);
      }
#endif

      return;
    }

    sloppy_eval_can_extend_vars_ = true;
  }

  bool sloppy_eval_can_extend_vars() const {
    return sloppy_eval_can_extend_vars_;
  }

  bool was_lazily_parsed() const { return was_lazily_parsed_; }

  Variable* LookupInModule(const AstRawString* name) {
    DCHECK(is_module_scope());
    Variable* var = variables_.Lookup(name);
    DCHECK_NOT_NULL(var);
    return var;
  }

  void DeserializeReceiver(AstValueFactory* ast_value_factory);

#ifdef DEBUG
  void set_is_being_lazily_parsed(bool is_being_lazily_parsed) {
    is_being_lazily_parsed_ = is_being_lazily_parsed;
  }
  bool is_being_lazily_parsed() const { return is_being_lazily_parsed_; }
#endif

  void set_zone(Zone* zone) {
#ifdef DEBUG
    needs_migration_ = true;
#endif
    // Migrate variables_' backing store to new zone.
    variables_ = VariableMap(variables_, zone);
  }

  // ---------------------------------------------------------------------------
  // Illegal redeclaration support.

  // Check if the scope has conflicting var
  // declarations, i.e. a var declaration that has been hoisted from a nested
  // scope over a let binding of the same name.
  Declaration* CheckConflictingVarDeclarations(
      bool* allowed_catch_binding_var_redeclaration);

  void set_has_checked_syntax(bool has_checked_syntax) {
    has_checked_syntax_ = has_checked_syntax;
  }
  bool has_checked_syntax() const { return has_checked_syntax_; }

  bool ShouldEagerCompile() const {
    return force_eager_compilation_ || should_eager_compile_;
  }

  void set_should_eager_compile();

  void SetScriptScopeInfo(Handle<ScopeInfo> scope_info) {
    DCHECK(is_script_scope());
    DCHECK(scope_info_.is_null());
    scope_info_ = scope_info;
  }

#if V8_ENABLE_WEBASSEMBLY
  bool is_asm_module() const { return is_asm_module_; }
  void set_is_asm_module();
#endif  // V8_ENABLE_WEBASSEMBLY

  bool should_ban_arguments() const {
    return IsClassMembersInitializerFunction(function_kind());
  }

  void set_module_has_toplevel_await() {
    DCHECK(IsModule(function_kind_));
    function_kind_ = FunctionKind::kModuleWithTopLevelAwait;
  }

  void DeclareThis(AstValueFactory* ast_value_factory);
  void DeclareArguments(AstValueFactory* ast_value_factory);
  void DeclareDefaultFunctionVariables(AstValueFactory* ast_value_factory);

  // Declare the function variable for a function literal. This variable
  // is in an intermediate scope between this function scope and the the
  // outer scope. Only possible for function scopes; at most one variable.
  //
  // This function needs to be called after all other variables have been
  // declared in the scope. It will add a variable for {name} to {variables_};
  // either the function variable itself, or a non-local in case the function
  // calls sloppy eval.
  Variable* DeclareFunctionVar(const AstRawString* name,
                               Scope* cache = nullptr);

  // Declare some special internal variables which must be accessible to
  // Ignition without ScopeInfo.
  Variable* DeclareGeneratorObjectVar(const AstRawString* name);

  // Declare a parameter in this scope.  When there are duplicated
  // parameters the rightmost one 'wins'.  However, the implementation
  // expects all parameters to be declared and from left to right.
  Variable* DeclareParameter(const AstRawString* name, VariableMode mode,
                             bool is_optional, bool is_rest,
                             AstValueFactory* ast_value_factory, int position);

  // Makes sure that num_parameters_ and has_rest is correct for the preparser.
  void RecordParameter(bool is_rest);

  // Declare an implicit global variable in this scope which must be a
  // script scope.  The variable was introduced (possibly from an inner
  // scope) by a reference to an unresolved variable with no intervening
  // with statements or eval calls.
  Variable* DeclareDynamicGlobal(const AstRawString* name,
                                 VariableKind variable_kind, Scope* cache);

  // The variable corresponding to the 'this' value.
  Variable* receiver() {
    DCHECK(has_this_declaration() || is_script_scope());
    DCHECK_NOT_NULL(receiver_);
    return receiver_;
  }

  bool has_this_declaration() const { return has_this_declaration_; }

  // The variable corresponding to the 'new.target' value.
  Variable* new_target_var() { return new_target_; }

  // The variable holding the function literal for named function
  // literals, or nullptr.  Only valid for function scopes.
  Variable* function_var() const { return function_; }

  // The variable holding the JSGeneratorObject for generator, async
  // and async generator functions, and modules. Only valid for
  // function, module and REPL mode script scopes.
  Variable* generator_object_var() const {
    DCHECK(is_function_scope() || is_module_scope() || is_repl_mode_scope());
    return GetRareVariable(RareVariable::kGeneratorObject);
  }

  // Parameters. The left-most parameter has index 0.
  // Only valid for function and module scopes.
  Variable* parameter(int index) const {
    DCHECK(is_function_scope() || is_module_scope());
    DCHECK(!is_being_lazily_parsed_);
    return params_[index];
  }

  // Returns the number of formal parameters, excluding a possible rest
  // parameter.  Examples:
  //   function foo(a, b) {}         ==> 2
  //   function foo(a, b, ...c) {}   ==> 2
  //   function foo(a, b, c = 1) {}  ==> 3
  int num_parameters() const { return num_parameters_; }

  // The function's rest parameter (nullptr if there is none).
  Variable* rest_parameter() const {
    return has_rest_ ? params_[params_.length() - 1] : nullptr;
  }

  bool has_simple_parameters() const { return has_simple_parameters_; }

  // TODO(caitp): manage this state in a better way. PreParser must be able to
  // communicate that the scope is non-simple, without allocating any parameters
  // as the Parser does. This is necessary to ensure that TC39's proposed early
  // error can be reported consistently regardless of whether lazily parsed or
  // not.
  void SetHasNonSimpleParameters() {
    DCHECK(is_function_scope());
    has_simple_parameters_ = false;
  }

  void MakeParametersNonSimple() {
    SetHasNonSimpleParameters();
    for (ZoneHashMap::Entry* p = variables_.Start(); p != nullptr;
         p = variables_.Next(p)) {
      Variable* var = reinterpret_cast<Variable*>(p->value);
      if (var->is_parameter()) var->MakeParameterNonSimple();
    }
  }

  // Returns whether the arguments object aliases formal parameters.
  CreateArgumentsType GetArgumentsType() const {
    DCHECK(is_function_scope());
    DCHECK(!is_arrow_scope());
    DCHECK_NOT_NULL(arguments_);
    return is_sloppy(language_mode()) && has_simple_parameters()
               ? CreateArgumentsType::kMappedArguments
               : CreateArgumentsType::kUnmappedArguments;
  }

  // The local variable 'arguments' if we need to allocate it; nullptr
  // otherwise.
  Variable* arguments() const {
    DCHECK_IMPLIES(is_arrow_scope(), arguments_ == nullptr);
    return arguments_;
  }

  Variable* this_function_var() const {
    Variable* this_function = GetRareVariable(RareVariable::kThisFunction);

    // This is only used in derived constructors atm.
    DCHECK(this_function == nullptr ||
           (is_function_scope() && (IsClassConstructor(function_kind()) ||
                                    IsConciseMethod(function_kind()) ||
                                    IsAccessorFunction(function_kind()))));
    return this_function;
  }

  // Adds a local variable in this scope's locals list. This is for adjusting
  // the scope of temporaries and do-expression vars when desugaring parameter
  // initializers.
  void AddLocal(Variable* var);

  void DeclareSloppyBlockFunction(
      SloppyBlockFunctionStatement* sloppy_block_function);

  // Go through sloppy_block_functions_ and hoist those (into this scope)
  // which should be hoisted.
  void HoistSloppyBlockFunctions(AstNodeFactory* factory);

  // Compute top scope and allocate variables. For lazy compilation the top
  // scope only contains the single lazily compiled function, so this
  // doesn't re-allocate variables repeatedly.
  //
  // Returns false if private names can not be resolved and
  // ParseInfo's pending_error_handler will be populated with an
  // error. Otherwise, returns true.
  V8_WARN_UNUSED_RESULT
  static bool Analyze(ParseInfo* info);

  // To be called during parsing. Do just enough scope analysis that we can
  // discard the Scope contents for lazily compiled functions. In particular,
  // this records variables which cannot be resolved inside the Scope (we don't
  // yet know what they will resolve to since the outer Scopes are incomplete)
  // and recreates them with the correct Zone with ast_node_factory.
  void AnalyzePartially(Parser* parser, AstNodeFactory* ast_node_factory,
                        bool maybe_in_arrowhead);

  // Allocate ScopeInfos for top scope and any inner scopes that need them.
  // Does nothing if ScopeInfo is already allocated.
  template <typename IsolateT>
  V8_EXPORT_PRIVATE static void AllocateScopeInfos(ParseInfo* info,
                                                   DirectHandle<Script> script,
                                                   IsolateT* isolate);

  // Determine if we can use lazy compilation for this scope.
  bool AllowsLazyCompilation() const;

  // Make sure this closure and all outer closures are eagerly compiled.
  void ForceEagerCompilation() {
    DCHECK_EQ(this, GetClosureScope());
    DeclarationScope* s;
    for (s = this; !s->is_script_scope();
         s = s->outer_scope()->GetClosureScope()) {
      s->force_eager_compilation_ = true;
    }
    s->force_eager_compilation_ = true;
  }

#ifdef DEBUG
  void PrintParameters();
#endif

  V8_INLINE void AllocateLocals();
  V8_INLINE void AllocateParameterLocals();
  V8_INLINE void AllocateReceiver();

  void ResetAfterPreparsing(AstValueFactory* ast_value_factory, bool aborted);

  bool is_skipped_function() const { return is_skipped_function_; }
  void set_is_skipped_function(bool is_skipped_function) {
    is_skipped_function_ = is_skipped_function;
  }

  bool has_inferred_function_name() const {
    return has_inferred_function_name_;
  }
  void set_has_inferred_function_name(bool value) {
    DCHECK(is_function_scope());
    has_inferred_function_name_ = value;
  }

  // Save data describing the context allocation of the variables in this scope
  // and its subscopes (except scopes at the laziness boundary). The data is
  // saved in produced_preparse_data_.
  void SavePreparseDataForDeclarationScope(Parser* parser);

  void set_preparse_data_builder(PreparseDataBuilder* preparse_data_builder) {
    preparse_data_builder_ = preparse_data_builder;
  }

  PreparseDataBuilder* preparse_data_builder() const {
    return preparse_data_builder_;
  }

  void set_has_this_reference() { has_this_reference_ = true; }
  bool has_this_reference() const { return has_this_reference_; }
  void UsesThis() {
    set_has_this_reference();
    GetReceiverScope()->receiver()->ForceContextAllocation();
  }

  bool needs_private_name_context_chain_recalc() const {
    return needs_private_name_context_chain_recalc_;
  }
  void RecordNeedsPrivateNameContextChainRecalc();

  void set_class_scope_has_private_brand(bool value) {
    class_scope_has_private_brand_ = value;
  }
  bool class_scope_has_private_brand() const {
    return class_scope_has_private_brand_;
  }

 private:
  V8_INLINE void AllocateParameter(Variable* var, int index);

  // Resolve and fill in the allocation information for all variables
  // in this scopes. Must be called *after* all scopes have been
  // processed (parsed) to ensure that unresolved variables can be
  // resolved properly.
  //
  // In the case of code compiled and run using 'eval', the context
  // parameter is the context in which eval was called.  In all other
  // cases the context parameter is an empty handle.
  //
  // Returns false if private names can not be resolved.
  bool AllocateVariables(ParseInfo* info);

  void SetDefaults();

  // Recalculate the private name context chain from the existing skip bit in
  // preparation for AllocateScopeInfos. Because the private name scope is
  // implemented with a skip bit for scopes in heritage position, that bit may
  // need to be recomputed due scopes that do not need contexts.
  void RecalcPrivateNameContextChain();

  bool has_simple_parameters_ : 1;
#if V8_ENABLE_WEBASSEMBLY
  // This scope contains an "use asm" annotation.
  bool is_asm_module_ : 1;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool force_eager_compilation_ : 1;
  // This function scope has a rest parameter.
  bool has_rest_ : 1;
  // This scope has a parameter called "arguments".
  bool has_arguments_parameter_ : 1;
  // This scope uses "super" property ('super.foo').
  bool uses_super_property_ : 1;
  bool should_eager_compile_ : 1;
  // Set to true after we have finished lazy parsing the scope.
  bool was_lazily_parsed_ : 1;
#if DEBUG
  bool is_being_lazily_parsed_ : 1;
#endif
  bool is_skipped_function_ : 1;
  bool has_inferred_function_name_ : 1;
  bool has_checked_syntax_ : 1;
  bool has_this_reference_ : 1;
  bool has_this_declaration_ : 1;
  bool needs_private_name_context_chain_recalc_ : 1;
  bool class_scope_has_private_brand_ : 1;
  // If the scope is a function scope, this is the function kind.
  FunctionKind function_kind_;

  int num_parameters_ = 0;

  // Parameter list in source order.
  ZonePtrList<Variable> params_;
  // Map of function names to lists of functions defined in sloppy blocks
  base::ThreadedList<SloppyBlockFunctionStatement> sloppy_block_functions_;
  // Convenience variable.
  Variable* receiver_;
  // Function variable, if any; function scopes only.
  Variable* function_;
  // new.target variable, function scopes only.
  Variable* new_target_;
  // Convenience variable; function scopes only.
  Variable* arguments_;

  // For producing the scope allocation data during preparsing.
  PreparseDataBuilder* preparse_data_builder_;

  struct RareData : public ZoneObject {
    // Convenience variable; Subclass constructor only
    Variable* this_function = nullptr;

    // Generator object, if any; generator function scopes and module scopes
    // only.
    Variable* generator_object = nullptr;
  };

  enum class RareVariable {
    kThisFunction = offsetof(RareData, this_function),
    kGeneratorObject = offsetof(RareData, generator_object),
  };

  V8_INLINE RareData* EnsureRareData() {
    if (rare_data_ == nullptr) {
      rare_data_ = zone()->New<RareData>();
    }
    return rare_data_;
  }

  V8_INLINE Variable* GetRareVariable(RareVariable id) const {
    if (rare_data_ == nullptr) return nullptr;
    return *reinterpret_cast<Variable**>(
        reinterpret_cast<uint8_t*>(rare_data_) + static_cast<ptrdiff_t>(id));
  }

  // Set `var` to null if it's non-null and Predicate (Variable*) -> bool
  // returns true.
  template <typename Predicate>
  V8_INLINE void NullifyRareVariableIf(RareVariable id, Predicate predicate) {
    if (V8_LIKELY(rare_data_ == nullptr)) return;
    Variable** var = reinterpret_cast<Variable**>(
        reinterpret_cast<uint8_t*>(rare_data_) + static_cast<ptrdiff_t>(id));
    if (*var && predicate(*var)) *var = nullptr;
  }

  RareData* rare_data_ = nullptr;
};

void Scope::RecordEvalCall() {
  calls_eval_ = true;
  if (is_sloppy(language_mode())) {
    GetDeclarationScope()->RecordDeclarationScopeEvalCall();
  }
  RecordInnerScopeEvalCall();
  // The eval contents might access "super" (if it's inside a function that
  // binds super).
  DeclarationScope* receiver_scope = GetReceiverScope();
  DCHECK(!receiver_scope->is_arrow_scope());
  FunctionKind function_kind = receiver_scope->function_kind();
  if (BindsSuper(function_kind)) {
    receiver_scope->RecordSuperPropertyUsage();
  }
}

Scope::Snapshot::Snapshot(Scope* scope)
    : outer_scope_(scope),
      declaration_scope_(scope->GetDeclarationScope()),
      top_inner_scope_(scope->inner_scope_),
      top_unresolved_(scope->unresolved_list_.end()),
      top_local_(scope->GetClosureScope()->locals_.end()),
      calls_eval_(outer_scope_->calls_eval_),
      sloppy_eval_can_extend_vars_(
          declaration_scope_->sloppy_eval_can_extend_vars_) {
  // Reset in order to record (sloppy) eval calls during this Snapshot's
  // lifetime.
  outer_scope_->calls_eval_ = false;
  declaration_scope_->sloppy_eval_can_extend_vars_ = false;
}

class ModuleScope final : public DeclarationScope {
 public:
  ModuleScope(DeclarationScope* script_scope, AstValueFactory* avfactory);

  // Deserialization. Does not restore the module descriptor.
  ModuleScope(Handle<ScopeInfo> scope_info, AstValueFactory* avfactory);

  // Returns nullptr in a deserialized scope.
  SourceTextModuleDescriptor* module() const { return module_descriptor_; }

  // Set MODULE as VariableLocation for all variables that will live in a
  // module's export table.
  void AllocateModuleVariables();

 private:
  SourceTextModuleDescriptor* const module_descriptor_;
};

class V8_EXPORT_PRIVATE ClassScope : public Scope {
 public:
  ClassScope(Zone* zone, Scope* outer_scope, bool is_anonymous);
  // Deserialization.
  template <typename IsolateT>
  ClassScope(IsolateT* isolate, Zone* zone, AstValueFactory* ast_value_factory,
             Handle<ScopeInfo> scope_info);

  struct HeritageParsingScope {
    explicit HeritageParsingScope(ClassScope* class_scope)
        : class_scope_(class_scope) {
      class_scope_->SetIsParsingHeritage(true);
    }
    ~HeritageParsingScope() { class_scope_->SetIsParsingHeritage(false); }

   private:
    ClassScope* class_scope_;
  };

  // Declare a private name in the private name map and add it to the
  // local variables of this scope.
  Variable* DeclarePrivateName(const AstRawString* name, VariableMode mode,
                               IsStaticFlag is_static_flag, bool* was_added);
  Variable* RedeclareSyntheticContextVariable(const AstRawString* name);

  // Try resolving all unresolved private names found in the current scope.
  // Called from DeclarationScope::AllocateVariables() when reparsing a
  // method to generate code or when eval() is called to access private names.
  // If there are any private names that cannot be resolved, returns false.
  V8_WARN_UNUSED_RESULT bool ResolvePrivateNames(ParseInfo* info);

  // Called after the entire class literal is parsed.
  // - If we are certain a private name cannot be resolve, return that
  //   variable proxy.
  // - If we find the private name in the scope chain, return nullptr.
  //   If the name is found in the current class scope, resolve it
  //   immediately.
  // - If we are not sure if the private name can be resolved or not yet,
  //   return nullptr.
  VariableProxy* ResolvePrivateNamesPartially();

  // Get the current tail of unresolved private names to be used to
  // reset the tail.
  UnresolvedList::Iterator GetUnresolvedPrivateNameTail();

  // Reset the tail of unresolved private names, discard everything
  // between the tail passed into this method and the current tail.
  void ResetUnresolvedPrivateNameTail(UnresolvedList::Iterator tail);

  // Migrate private names added between the tail passed into this method
  // and the current tail.
  void MigrateUnresolvedPrivateNameTail(AstNodeFactory* ast_node_factory,
                                        UnresolvedList::Iterator tail);
  Variable* DeclareBrandVariable(AstValueFactory* ast_value_factory,
                                 IsStaticFlag is_static_flag,
                                 int class_token_pos);

  Variable* DeclareClassVariable(AstValueFactory* ast_value_factory,
                                 const AstRawString* name, int class_token_pos);

  Variable* brand() {
    return GetRareData() == nullptr ? nullptr : GetRareData()->brand;
  }

  Variable* class_variable() { return class_variable_; }

  V8_INLINE bool IsParsingHeritage() {
    return rare_data_and_is_parsing_heritage_.GetPayload();
  }

  // Only maintained when the scope is parsed, not when the scope is
  // deserialized.
  bool has_static_private_methods() const {
    return has_static_private_methods_;
  }

  // Returns whether the index of class variable of this class scope should be
  // recorded in the ScopeInfo.
  // If any inner scope accesses static private names directly, the class
  // variable will be forced to be context-allocated.
  // The inner scope may also calls eval which may results in access to
  // static private names.
  // Only maintained when the scope is parsed.
  bool should_save_class_variable_index() const {
    return should_save_class_variable_index_ ||
           has_explicit_static_private_methods_access_ ||
           (has_static_private_methods_ && inner_scope_calls_eval_);
  }

  // Only maintained when the scope is parsed.
  bool is_anonymous_class() const { return is_anonymous_class_; }

  // Overriden during reparsing
  void set_should_save_class_variable_index() {
    should_save_class_variable_index_ = true;
  }

 private:
  friend class Scope;
  friend class PrivateNameScopeIterator;

  // Find the private name declared in the private name map first,
  // if it cannot be found there, try scope info if there is any.
  // Returns nullptr if it cannot be found.
  Variable* LookupPrivateName(VariableProxy* proxy);
  // Lookup a private name from the local private name map of the current
  // scope.
  Variable* LookupLocalPrivateName(const AstRawString* name);
  // Lookup a private name from the scope info of the current scope.
  Variable* LookupPrivateNameInScopeInfo(const AstRawString* name);

  struct RareData : public ZoneObject {
    explicit RareData(Zone* zone) : private_name_map(zone) {}
    UnresolvedList unresolved_private_names;
    VariableMap private_name_map;
    Variable* brand = nullptr;
  };

  V8_INLINE RareData* GetRareData() {
    return rare_data_and_is_parsing_heritage_.GetPointer();
  }
  V8_INLINE RareData* EnsureRareData() {
    if (GetRareData() == nullptr) {
      rare_data_and_is_parsing_heritage_.SetPointer(
          zone()->New<RareData>(zone()));
    }
    return GetRareData();
  }
  V8_INLINE void SetIsParsingHeritage(bool v) {
    rare_data_and_is_parsing_heritage_.SetPayload(v);
  }

  base::PointerWithPayload<RareData, bool, 1>
      rare_data_and_is_parsing_heritage_;
  Variable* class_variable_ = nullptr;
  // These are only maintained when the scope is parsed, not when the
  // scope is deserialized.
  bool has_static_private_methods_ : 1 = false;
  bool has_explicit_static_private_methods_access_ : 1 = false;
  bool is_anonymous_class_ : 1 = false;
  // This is only maintained during reparsing, restored from the
  // preparsed data.
  bool should_save_class_variable_index_ : 1 = false;
};

// Iterate over the private name scope chain. The iteration proceeds from the
// innermost private name scope outwards.
class PrivateNameScopeIterator {
 public:
  explicit PrivateNameScopeIterator(Scope* start);

  bool Done() const { return current_scope_ == nullptr; }
  void Next();

  // Add an unresolved private name to the current scope.
  void AddUnresolvedPrivateName(VariableProxy* proxy);

  ClassScope* GetScope() const {
    DCHECK(!Done());
    return current_scope_->AsClassScope();
  }

 private:
  bool skipped_any_scopes_ = false;
  Scope* start_scope_;
  Scope* current_scope_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_SCOPES_H_
