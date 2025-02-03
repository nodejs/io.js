// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/scopes.h"

#include <optional>
#include <set>

#include "src/ast/ast.h"
#include "src/base/logging.h"
#include "src/builtins/accessors.h"
#include "src/common/message-template.h"
#include "src/heap/local-factory-inl.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/scope-info.h"
#include "src/objects/string-inl.h"
#include "src/objects/string-set.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/preparse-data.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Implementation of LocalsMap
//
// Note: We are storing the handle locations as key values in the hash map.
//       When inserting a new variable via Declare(), we rely on the fact that
//       the handle location remains alive for the duration of that variable
//       use. Because a Variable holding a handle with the same location exists
//       this is ensured.

static_assert(sizeof(VariableMap) == (sizeof(void*) + 2 * sizeof(uint32_t) +
                                      sizeof(ZoneAllocationPolicy)),
              "Empty base optimization didn't kick in for VariableMap");

VariableMap::VariableMap(Zone* zone)
    : ZoneHashMap(8, ZoneAllocationPolicy(zone)) {}

VariableMap::VariableMap(const VariableMap& other, Zone* zone)
    : ZoneHashMap(other, ZoneAllocationPolicy(zone)) {}

Variable* VariableMap::Declare(Zone* zone, Scope* scope,
                               const AstRawString* name, VariableMode mode,
                               VariableKind kind,
                               InitializationFlag initialization_flag,
                               MaybeAssignedFlag maybe_assigned_flag,
                               IsStaticFlag is_static_flag, bool* was_added) {
  DCHECK_EQ(zone, allocator().zone());
  // AstRawStrings are unambiguous, i.e., the same string is always represented
  // by the same AstRawString*.
  // FIXME(marja): fix the type of Lookup.
  Entry* p = ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name),
                                         name->Hash());
  *was_added = p->value == nullptr;
  if (*was_added) {
    // The variable has not been declared yet -> insert it.
    DCHECK_EQ(name, p->key);
    Variable* variable =
        zone->New<Variable>(scope, name, mode, kind, initialization_flag,
                            maybe_assigned_flag, is_static_flag);
    p->value = variable;
  }
  return reinterpret_cast<Variable*>(p->value);
}

void VariableMap::Remove(Variable* var) {
  const AstRawString* name = var->raw_name();
  ZoneHashMap::Remove(const_cast<AstRawString*>(name), name->Hash());
}

void VariableMap::Add(Variable* var) {
  const AstRawString* name = var->raw_name();
  Entry* p = ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name),
                                         name->Hash());
  DCHECK_NULL(p->value);
  DCHECK_EQ(name, p->key);
  p->value = var;
}

Variable* VariableMap::Lookup(const AstRawString* name) {
  Entry* p = ZoneHashMap::Lookup(const_cast<AstRawString*>(name), name->Hash());
  if (p != nullptr) {
    DCHECK(reinterpret_cast<const AstRawString*>(p->key) == name);
    DCHECK_NOT_NULL(p->value);
    return reinterpret_cast<Variable*>(p->value);
  }
  return nullptr;
}

// ----------------------------------------------------------------------------
// Implementation of Scope

Scope::Scope(Zone* zone, ScopeType scope_type)
    : outer_scope_(nullptr), variables_(zone), scope_type_(scope_type) {
  DCHECK(is_script_scope());
  SetDefaults();
}

Scope::Scope(Zone* zone, Scope* outer_scope, ScopeType scope_type)
    : outer_scope_(outer_scope), variables_(zone), scope_type_(scope_type) {
  DCHECK(!is_script_scope());
  SetDefaults();
  set_language_mode(outer_scope->language_mode());
  private_name_lookup_skips_outer_class_ =
      outer_scope->is_class_scope() &&
      outer_scope->AsClassScope()->IsParsingHeritage();
  outer_scope_->AddInnerScope(this);
}

Variable* Scope::DeclareHomeObjectVariable(AstValueFactory* ast_value_factory) {
  bool was_added;
  Variable* home_object_variable = Declare(
      zone(), ast_value_factory->dot_home_object_string(), VariableMode::kConst,
      NORMAL_VARIABLE, InitializationFlag::kCreatedInitialized,
      MaybeAssignedFlag::kNotAssigned, &was_added);
  DCHECK(was_added);
  home_object_variable->set_is_used();
  home_object_variable->ForceContextAllocation();
  return home_object_variable;
}

Variable* Scope::DeclareStaticHomeObjectVariable(
    AstValueFactory* ast_value_factory) {
  bool was_added;
  Variable* static_home_object_variable =
      Declare(zone(), ast_value_factory->dot_static_home_object_string(),
              VariableMode::kConst, NORMAL_VARIABLE,
              InitializationFlag::kCreatedInitialized,
              MaybeAssignedFlag::kNotAssigned, &was_added);
  DCHECK(was_added);
  static_home_object_variable->set_is_used();
  static_home_object_variable->ForceContextAllocation();
  return static_home_object_variable;
}

DeclarationScope::DeclarationScope(Zone* zone,
                                   AstValueFactory* ast_value_factory,
                                   REPLMode repl_mode)
    : Scope(zone, repl_mode == REPLMode::kYes ? REPL_MODE_SCOPE : SCRIPT_SCOPE),
      function_kind_(repl_mode == REPLMode::kYes
                         ? FunctionKind::kAsyncFunction
                         : FunctionKind::kNormalFunction),
      params_(4, zone) {
  DCHECK(is_script_scope());
  SetDefaults();
  receiver_ = DeclareDynamicGlobal(ast_value_factory->this_string(),
                                   THIS_VARIABLE, this);
}

DeclarationScope::DeclarationScope(Zone* zone, Scope* outer_scope,
                                   ScopeType scope_type,
                                   FunctionKind function_kind)
    : Scope(zone, outer_scope, scope_type),
      function_kind_(function_kind),
      params_(4, zone) {
  DCHECK(!is_script_scope());
  SetDefaults();
}

ModuleScope::ModuleScope(DeclarationScope* script_scope,
                         AstValueFactory* avfactory)
    : DeclarationScope(avfactory->single_parse_zone(), script_scope,
                       MODULE_SCOPE, FunctionKind::kModule),
      module_descriptor_(
          avfactory->single_parse_zone()->New<SourceTextModuleDescriptor>(
              avfactory->single_parse_zone())) {
  set_language_mode(LanguageMode::kStrict);
  DeclareThis(avfactory);
}

ModuleScope::ModuleScope(Handle<ScopeInfo> scope_info,
                         AstValueFactory* avfactory)
    : DeclarationScope(avfactory->single_parse_zone(), MODULE_SCOPE, avfactory,
                       scope_info),
      module_descriptor_(nullptr) {
  set_language_mode(LanguageMode::kStrict);
}

ClassScope::ClassScope(Zone* zone, Scope* outer_scope, bool is_anonymous)
    : Scope(zone, outer_scope, CLASS_SCOPE),
      rare_data_and_is_parsing_heritage_(nullptr),
      is_anonymous_class_(is_anonymous) {
  set_language_mode(LanguageMode::kStrict);
}

template <typename IsolateT>
ClassScope::ClassScope(IsolateT* isolate, Zone* zone,
                       AstValueFactory* ast_value_factory,
                       Handle<ScopeInfo> scope_info)
    : Scope(zone, CLASS_SCOPE, ast_value_factory, scope_info),
      rare_data_and_is_parsing_heritage_(nullptr) {
  set_language_mode(LanguageMode::kStrict);
  if (scope_info->ClassScopeHasPrivateBrand()) {
    Variable* brand =
        LookupInScopeInfo(ast_value_factory->dot_brand_string(), this);
    DCHECK_NOT_NULL(brand);
    EnsureRareData()->brand = brand;
  }

  // If the class variable is context-allocated and its index is
  // saved for deserialization, deserialize it.
  if (scope_info->HasSavedClassVariable()) {
    Tagged<String> name;
    int index;
    std::tie(name, index) = scope_info->SavedClassVariable();
    DCHECK_EQ(scope_info->ContextLocalMode(index), VariableMode::kConst);
    DCHECK_EQ(scope_info->ContextLocalInitFlag(index),
              InitializationFlag::kNeedsInitialization);
    DCHECK_EQ(scope_info->ContextLocalMaybeAssignedFlag(index),
              MaybeAssignedFlag::kMaybeAssigned);
    Variable* var = DeclareClassVariable(
        ast_value_factory,
        ast_value_factory->GetString(name,
                                     SharedStringAccessGuardIfNeeded(isolate)),
        kNoSourcePosition);
    var->AllocateTo(VariableLocation::CONTEXT,
                    Context::MIN_CONTEXT_SLOTS + index);
  }

  DCHECK(scope_info->HasPositionInfo());
  set_start_position(scope_info->StartPosition());
  set_end_position(scope_info->EndPosition());
}
template ClassScope::ClassScope(Isolate* isolate, Zone* zone,
                                AstValueFactory* ast_value_factory,
                                Handle<ScopeInfo> scope_info);
template ClassScope::ClassScope(LocalIsolate* isolate, Zone* zone,
                                AstValueFactory* ast_value_factory,
                                Handle<ScopeInfo> scope_info);

Scope::Scope(Zone* zone, ScopeType scope_type,
             AstValueFactory* ast_value_factory, Handle<ScopeInfo> scope_info)
    : outer_scope_(nullptr),
      variables_(zone),
      scope_info_(scope_info),
      scope_type_(scope_type) {
  DCHECK(!scope_info.is_null());
  SetDefaults();
#ifdef DEBUG
  already_resolved_ = true;
#endif
  set_language_mode(scope_info->language_mode());
  DCHECK_EQ(ContextHeaderLength(), num_heap_slots_);
  private_name_lookup_skips_outer_class_ =
      scope_info->PrivateNameLookupSkipsOuterClass();
  // We don't really need to use the preparsed scope data; this is just to
  // shorten the recursion in SetMustUsePreparseData.
  must_use_preparsed_scope_data_ = true;

  if (scope_type == BLOCK_SCOPE) {
    // Set is_block_scope_for_object_literal_ based on the existence of the home
    // object variable (we don't store it explicitly).
    DCHECK_NOT_NULL(ast_value_factory);
    int home_object_index = scope_info->ContextSlotIndex(
        ast_value_factory->dot_home_object_string()->string());
    DCHECK_IMPLIES(home_object_index >= 0,
                   scope_type == CLASS_SCOPE || scope_type == BLOCK_SCOPE);
    if (home_object_index >= 0) {
      is_block_scope_for_object_literal_ = true;
    }
  }
}

DeclarationScope::DeclarationScope(Zone* zone, ScopeType scope_type,
                                   AstValueFactory* ast_value_factory,
                                   Handle<ScopeInfo> scope_info)
    : Scope(zone, scope_type, ast_value_factory, scope_info),
      function_kind_(scope_info->function_kind()),
      params_(0, zone) {
  DCHECK(!is_script_scope());
  SetDefaults();
  if (scope_info->SloppyEvalCanExtendVars()) {
    DCHECK(!is_eval_scope());
    sloppy_eval_can_extend_vars_ = true;
  }
  if (scope_info->ClassScopeHasPrivateBrand()) {
    DCHECK(IsClassConstructor(function_kind()));
    class_scope_has_private_brand_ = true;
  }
}

Scope::Scope(Zone* zone, const AstRawString* catch_variable_name,
             MaybeAssignedFlag maybe_assigned, Handle<ScopeInfo> scope_info)
    : outer_scope_(nullptr),
      variables_(zone),
      scope_info_(scope_info),
      scope_type_(CATCH_SCOPE) {
  SetDefaults();
#ifdef DEBUG
  already_resolved_ = true;
#endif
  // Cache the catch variable, even though it's also available via the
  // scope_info, as the parser expects that a catch scope always has the catch
  // variable as first and only variable.
  bool was_added;
  Variable* variable =
      Declare(zone, catch_variable_name, VariableMode::kVar, NORMAL_VARIABLE,
              kCreatedInitialized, maybe_assigned, &was_added);
  DCHECK(was_added);
  AllocateHeapSlot(variable);
}

void DeclarationScope::SetDefaults() {
  is_declaration_scope_ = true;
  has_simple_parameters_ = true;
#if V8_ENABLE_WEBASSEMBLY
  is_asm_module_ = false;
#endif  // V8_ENABLE_WEBASSEMBLY
  force_eager_compilation_ = false;
  has_arguments_parameter_ = false;
  uses_super_property_ = false;
  has_checked_syntax_ = false;
  has_this_reference_ = false;
  has_this_declaration_ =
      (is_function_scope() && !is_arrow_scope()) || is_module_scope();
  needs_private_name_context_chain_recalc_ = false;
  has_rest_ = false;
  receiver_ = nullptr;
  new_target_ = nullptr;
  function_ = nullptr;
  arguments_ = nullptr;
  rare_data_ = nullptr;
  should_eager_compile_ = false;
  was_lazily_parsed_ = false;
  is_skipped_function_ = false;
  preparse_data_builder_ = nullptr;
  class_scope_has_private_brand_ = false;
#ifdef DEBUG
  DeclarationScope* outer_declaration_scope =
      outer_scope_ ? outer_scope_->GetDeclarationScope() : nullptr;
  is_being_lazily_parsed_ =
      outer_declaration_scope ? outer_declaration_scope->is_being_lazily_parsed_
                              : false;
#endif
}

void Scope::SetDefaults() {
#ifdef DEBUG
  scope_name_ = nullptr;
  already_resolved_ = false;
  needs_migration_ = false;
#endif
  inner_scope_ = nullptr;
  sibling_ = nullptr;
  unresolved_list_.Clear();

  start_position_ = kNoSourcePosition;
  end_position_ = kNoSourcePosition;

  calls_eval_ = false;
  sloppy_eval_can_extend_vars_ = false;
  scope_nonlinear_ = false;
  is_hidden_ = false;
  is_debug_evaluate_scope_ = false;

  inner_scope_calls_eval_ = false;
  force_context_allocation_for_parameters_ = false;

  is_declaration_scope_ = false;

  private_name_lookup_skips_outer_class_ = false;

  must_use_preparsed_scope_data_ = false;

  deserialized_scope_uses_external_cache_ = false;

  needs_home_object_ = false;
  is_block_scope_for_object_literal_ = false;

  has_using_declaration_ = false;
  has_await_using_declaration_ = false;

  is_wrapped_function_ = false;

  num_stack_slots_ = 0;
  num_heap_slots_ = ContextHeaderLength();

  set_language_mode(LanguageMode::kSloppy);
}

bool Scope::HasSimpleParameters() {
  DeclarationScope* scope = GetClosureScope();
  return !scope->is_function_scope() || scope->has_simple_parameters();
}

void DeclarationScope::set_should_eager_compile() {
  should_eager_compile_ = !was_lazily_parsed_;
}

#if V8_ENABLE_WEBASSEMBLY
void DeclarationScope::set_is_asm_module() { is_asm_module_ = true; }

bool Scope::IsAsmModule() const {
  return is_function_scope() && AsDeclarationScope()->is_asm_module();
}

bool Scope::ContainsAsmModule() const {
  if (IsAsmModule()) return true;

  // Check inner scopes recursively
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    // Don't check inner functions which won't be eagerly compiled.
    if (!scope->is_function_scope() ||
        scope->AsDeclarationScope()->ShouldEagerCompile()) {
      if (scope->ContainsAsmModule()) return true;
    }
  }

  return false;
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename IsolateT>
Scope* Scope::DeserializeScopeChain(IsolateT* isolate, Zone* zone,
                                    Tagged<ScopeInfo> scope_info,
                                    DeclarationScope* script_scope,
                                    AstValueFactory* ast_value_factory,
                                    DeserializationMode deserialization_mode,
                                    ParseInfo* parse_info) {
  // Reconstruct the outer scope chain from a closure's context chain.
  Scope* current_scope = nullptr;
  Scope* innermost_scope = nullptr;
  Scope* outer_scope = nullptr;
  bool cache_scope_found = false;
  while (!scope_info.is_null()) {
    if (scope_info->scope_type() == WITH_SCOPE) {
      if (scope_info->IsDebugEvaluateScope()) {
        outer_scope =
            zone->New<DeclarationScope>(zone, FUNCTION_SCOPE, ast_value_factory,
                                        handle(scope_info, isolate));
        outer_scope->set_is_debug_evaluate_scope();
      } else {
        // For scope analysis, debug-evaluate is equivalent to a with scope.
        outer_scope = zone->New<Scope>(zone, WITH_SCOPE, ast_value_factory,
                                       handle(scope_info, isolate));
      }

    } else if (scope_info->is_script_scope()) {
      // If we reach a script scope, it's the outermost scope. Install the
      // scope info of this script context onto the existing script scope to
      // avoid nesting script scopes.
      if (deserialization_mode == DeserializationMode::kIncludingVariables) {
        script_scope->SetScriptScopeInfo(handle(scope_info, isolate));
      }
      DCHECK(!scope_info->HasOuterScopeInfo());
      break;
    } else if (scope_info->scope_type() == FUNCTION_SCOPE) {
      outer_scope = zone->New<DeclarationScope>(
          zone, FUNCTION_SCOPE, ast_value_factory, handle(scope_info, isolate));
#if V8_ENABLE_WEBASSEMBLY
      if (scope_info->IsAsmModule()) {
        outer_scope->AsDeclarationScope()->set_is_asm_module();
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    } else if (scope_info->scope_type() == EVAL_SCOPE) {
      outer_scope = zone->New<DeclarationScope>(
          zone, EVAL_SCOPE, ast_value_factory, handle(scope_info, isolate));
    } else if (scope_info->scope_type() == CLASS_SCOPE) {
      outer_scope = zone->New<ClassScope>(isolate, zone, ast_value_factory,
                                          handle(scope_info, isolate));
    } else if (scope_info->scope_type() == BLOCK_SCOPE) {
      if (scope_info->is_declaration_scope()) {
        outer_scope = zone->New<DeclarationScope>(
            zone, BLOCK_SCOPE, ast_value_factory, handle(scope_info, isolate));
      } else {
        outer_scope = zone->New<Scope>(zone, BLOCK_SCOPE, ast_value_factory,
                                       handle(scope_info, isolate));
      }
    } else if (scope_info->scope_type() == MODULE_SCOPE) {
      outer_scope = zone->New<ModuleScope>(handle(scope_info, isolate),
                                           ast_value_factory);
      if (parse_info) {
        parse_info->set_has_module_in_scope_chain();
      }
    } else {
      DCHECK_EQ(scope_info->scope_type(), CATCH_SCOPE);
      DCHECK_EQ(scope_info->ContextLocalCount(), 1);
      DCHECK_EQ(scope_info->ContextLocalMode(0), VariableMode::kVar);
      DCHECK_EQ(scope_info->ContextLocalInitFlag(0), kCreatedInitialized);
      DCHECK(scope_info->HasInlinedLocalNames());
      Tagged<String> name = scope_info->ContextInlinedLocalName(0);
      MaybeAssignedFlag maybe_assigned =
          scope_info->ContextLocalMaybeAssignedFlag(0);
      outer_scope =
          zone->New<Scope>(zone,
                           ast_value_factory->GetString(
                               name, SharedStringAccessGuardIfNeeded(isolate)),
                           maybe_assigned, handle(scope_info, isolate));
    }
    if (deserialization_mode == DeserializationMode::kScopesOnly) {
      outer_scope->scope_info_ = Handle<ScopeInfo>::null();
    }

    if (cache_scope_found) {
      outer_scope->set_deserialized_scope_uses_external_cache();
    } else {
      cache_scope_found =
          outer_scope->is_declaration_scope() && !outer_scope->is_eval_scope();
    }

    if (current_scope != nullptr) {
      outer_scope->AddInnerScope(current_scope);
    }
    current_scope = outer_scope;
    if (innermost_scope == nullptr) innermost_scope = current_scope;
    scope_info = scope_info->HasOuterScopeInfo() ? scope_info->OuterScopeInfo()
                                                 : Tagged<ScopeInfo>();
  }

  if (deserialization_mode == DeserializationMode::kIncludingVariables) {
    SetScriptScopeInfo(isolate, script_scope);
  }

  if (innermost_scope == nullptr) return script_scope;
  script_scope->AddInnerScope(current_scope);
  return innermost_scope;
}

template <typename IsolateT>
void Scope::SetScriptScopeInfo(IsolateT* isolate,
                               DeclarationScope* script_scope) {
  if (script_scope->scope_info_.is_null()) {
    script_scope->SetScriptScopeInfo(
        ReadOnlyRoots(isolate).global_this_binding_scope_info_handle());
  }
}

template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void Scope::SetScriptScopeInfo(Isolate* isolate,
                                                      DeclarationScope*
                                                          script_scope);
template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void Scope::SetScriptScopeInfo(LocalIsolate* isolate,
                                                      DeclarationScope*
                                                          script_scope);

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Scope* Scope::DeserializeScopeChain(
        Isolate* isolate, Zone* zone, Tagged<ScopeInfo> scope_info,
        DeclarationScope* script_scope, AstValueFactory* ast_value_factory,
        DeserializationMode deserialization_mode, ParseInfo* parse_info);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Scope* Scope::DeserializeScopeChain(
        LocalIsolate* isolate, Zone* zone, Tagged<ScopeInfo> scope_info,
        DeclarationScope* script_scope, AstValueFactory* ast_value_factory,
        DeserializationMode deserialization_mode, ParseInfo* parse_info);

DeclarationScope* Scope::AsDeclarationScope() {
  // Here and below: if an attacker corrupts the in-sandox SFI::unique_id or
  // fields of a Script object, we can get confused about which type of scope
  // we're operating on. These CHECKs defend against that.
  SBXCHECK(is_declaration_scope());
  return static_cast<DeclarationScope*>(this);
}

const DeclarationScope* Scope::AsDeclarationScope() const {
  SBXCHECK(is_declaration_scope());
  return static_cast<const DeclarationScope*>(this);
}

ModuleScope* Scope::AsModuleScope() {
  SBXCHECK(is_module_scope());
  return static_cast<ModuleScope*>(this);
}

const ModuleScope* Scope::AsModuleScope() const {
  SBXCHECK(is_module_scope());
  return static_cast<const ModuleScope*>(this);
}

ClassScope* Scope::AsClassScope() {
  SBXCHECK(is_class_scope());
  return static_cast<ClassScope*>(this);
}

const ClassScope* Scope::AsClassScope() const {
  SBXCHECK(is_class_scope());
  return static_cast<const ClassScope*>(this);
}

void DeclarationScope::DeclareSloppyBlockFunction(
    SloppyBlockFunctionStatement* sloppy_block_function) {
  sloppy_block_functions_.Add(sloppy_block_function);
}

void DeclarationScope::HoistSloppyBlockFunctions(AstNodeFactory* factory) {
  DCHECK(is_sloppy(language_mode()));
  DCHECK(is_function_scope() || is_eval_scope() || is_script_scope() ||
         (is_block_scope() && outer_scope()->is_function_scope()));
  DCHECK(HasSimpleParameters() || is_block_scope() || is_being_lazily_parsed_);
  DCHECK_EQ(factory == nullptr, is_being_lazily_parsed_);

  if (sloppy_block_functions_.is_empty()) return;

  // In case of complex parameters the current scope is the body scope and the
  // parameters are stored in the outer scope.
  Scope* parameter_scope = HasSimpleParameters() ? this : outer_scope_;
  DCHECK(parameter_scope->is_function_scope() || is_eval_scope() ||
         is_script_scope());

  DeclarationScope* decl_scope = GetNonEvalDeclarationScope();
  Scope* outer_scope = decl_scope->outer_scope();

  // For each variable which is used as a function declaration in a sloppy
  // block,
  for (SloppyBlockFunctionStatement* sloppy_block_function :
       sloppy_block_functions_) {
    const AstRawString* name = sloppy_block_function->name();

    // If the variable wouldn't conflict with a lexical declaration
    // or parameter,

    // Check if there's a conflict with a parameter.
    Variable* maybe_parameter = parameter_scope->LookupLocal(name);
    if (maybe_parameter != nullptr && maybe_parameter->is_parameter()) {
      continue;
    }

    // Check if there's a conflict with a lexical declaration
    Scope* query_scope = sloppy_block_function->scope()->outer_scope();
    bool should_hoist = true;

    // It is not sufficient to just do a Lookup on query_scope: for
    // example, that does not prevent hoisting of the function in
    // `{ let e; try {} catch (e) { function e(){} } }`
    //
    // Don't use a generic cache scope, as the cache scope would be the outer
    // scope and we terminate the iteration there anyway.
    do {
      Variable* var = query_scope->LookupInScopeOrScopeInfo(name, query_scope);
      if (var != nullptr && IsLexicalVariableMode(var->mode()) &&
          !var->is_sloppy_block_function()) {
        should_hoist = false;
        break;
      }
      query_scope = query_scope->outer_scope();
    } while (query_scope != outer_scope);

    if (!should_hoist) continue;

    if (factory) {
      DCHECK(!is_being_lazily_parsed_);
      int pos = sloppy_block_function->position();
      bool ok = true;
      bool was_added;
      auto declaration = factory->NewVariableDeclaration(pos);
      // Based on the preceding checks, it doesn't matter what we pass as
      // sloppy_mode_block_scope_function_redefinition.
      //
      // This synthesized var for Annex B functions-in-block (FiB) may be
      // declared multiple times for the same var scope, such as in the case of
      // shadowed functions-in-block like the following:
      //
      // {
      //    function f() {}
      //    { function f() {} }
      // }
      //
      // Redeclarations for vars do not create new bindings, but the
      // redeclarations' initializers are still run. That is, shadowed FiB will
      // result in multiple assignments to the same synthesized var.
      Variable* var = DeclareVariable(
          declaration, name, pos, VariableMode::kVar, NORMAL_VARIABLE,
          Variable::DefaultInitializationFlag(VariableMode::kVar), &was_added,
          nullptr, &ok);
      DCHECK(ok);
      VariableProxy* source =
          factory->NewVariableProxy(sloppy_block_function->var());
      VariableProxy* target = factory->NewVariableProxy(var);
      Assignment* assignment = factory->NewAssignment(
          sloppy_block_function->init(), target, source, pos);
      assignment->set_lookup_hoisting_mode(LookupHoistingMode::kLegacySloppy);
      Statement* statement = factory->NewExpressionStatement(assignment, pos);
      sloppy_block_function->set_statement(statement);
    } else {
      DCHECK(is_being_lazily_parsed_);
      bool was_added;
      Variable* var = DeclareVariableName(name, VariableMode::kVar, &was_added);
      if (sloppy_block_function->init() == Token::kAssign) {
        var->SetMaybeAssigned();
      }
    }
  }
}

void DeclarationScope::TakeUnresolvedReferencesFromParent() {
  DCHECK(outer_scope_->reparsing_for_class_initializer_);
  unresolved_list_.MoveTail(&outer_scope_->unresolved_list_,
                            outer_scope_->unresolved_list_.begin());
}

bool DeclarationScope::Analyze(ParseInfo* info) {
  RCS_SCOPE(info->runtime_call_stats(),
            RuntimeCallCounterId::kCompileScopeAnalysis,
            RuntimeCallStats::kThreadSpecific);
  DCHECK_NOT_NULL(info->literal());
  DeclarationScope* scope = info->literal()->scope();

  std::optional<AllowHandleDereference> allow_deref;
#ifdef DEBUG
  if (scope->outer_scope() && !scope->outer_scope()->scope_info_.is_null()) {
    allow_deref.emplace();
  }
#endif

  if (scope->is_eval_scope() && is_sloppy(scope->language_mode())) {
    AstNodeFactory factory(info->ast_value_factory(), info->zone());
    scope->HoistSloppyBlockFunctions(&factory);
  }

  // We are compiling one of four cases:
  // 1) top-level code,
  // 2) a function/eval/module on the top-level
  // 4) a class member initializer function scope
  // 3) 4 function/eval in a scope that was already resolved.
  DCHECK(scope->is_script_scope() || scope->outer_scope()->is_script_scope() ||
         scope->outer_scope()->already_resolved_);

  // The outer scope is never lazy.
  scope->set_should_eager_compile();

  if (scope->must_use_preparsed_scope_data_) {
    DCHECK_EQ(scope->scope_type_, ScopeType::FUNCTION_SCOPE);
    allow_deref.emplace();
    info->consumed_preparse_data()->RestoreScopeAllocationData(
        scope, info->ast_value_factory(), info->zone());
  }

  if (!scope->AllocateVariables(info)) return false;
  scope->GetScriptScope()->RewriteReplGlobalVariables();

#ifdef DEBUG
  if (v8_flags.print_scopes) {
    PrintF("Global scope:\n");
    scope->Print();
  }
  scope->CheckScopePositions();
  scope->CheckZones();
#endif

  return true;
}

void DeclarationScope::DeclareThis(AstValueFactory* ast_value_factory) {
  DCHECK(has_this_declaration());

  bool derived_constructor = IsDerivedConstructor(function_kind_);

  receiver_ = zone()->New<Variable>(
      this, ast_value_factory->this_string(),
      derived_constructor ? VariableMode::kConst : VariableMode::kVar,
      THIS_VARIABLE,
      derived_constructor ? kNeedsInitialization : kCreatedInitialized,
      kNotAssigned);
  // Derived constructors have hole checks when calling super. Mark the 'this'
  // variable as having hole initialization forced so that TDZ elision analysis
  // applies and numbers the variable.
  if (derived_constructor) {
    receiver_->ForceHoleInitialization(
        Variable::kHasHoleCheckUseInUnknownScope);
  }
  locals_.Add(receiver_);
}

void DeclarationScope::DeclareArguments(AstValueFactory* ast_value_factory) {
  DCHECK(is_function_scope());
  DCHECK(!is_arrow_scope());

  // Because when arguments_ is not nullptr, we already declared
  // "arguments exotic object" to add it into parameters before
  // impl()->InsertShadowingVarBindingInitializers, so here
  // only declare "arguments exotic object" when arguments_
  // is nullptr
  if (arguments_ != nullptr) {
    return;
  }

  // Declare 'arguments' variable which exists in all non arrow functions.  Note
  // that it might never be accessed, in which case it won't be allocated during
  // variable allocation.
  bool was_added = false;

  arguments_ =
      Declare(zone(), ast_value_factory->arguments_string(), VariableMode::kVar,
              NORMAL_VARIABLE, kCreatedInitialized, kNotAssigned, &was_added);
  // According to ES#sec-functiondeclarationinstantiation step 18
  // we should set argumentsObjectNeeded to false if has lexical
  // declared arguments only when hasParameterExpressions is false
  if (!was_added && IsLexicalVariableMode(arguments_->mode()) &&
      has_simple_parameters_) {
    // Check if there's lexically declared variable named arguments to avoid
    // redeclaration. See ES#sec-functiondeclarationinstantiation, step 20.
    arguments_ = nullptr;
  }
}

void DeclarationScope::DeclareDefaultFunctionVariables(
    AstValueFactory* ast_value_factory) {
  DCHECK(is_function_scope());
  DCHECK(!is_arrow_scope());

  DeclareThis(ast_value_factory);
  bool was_added;
  new_target_ = Declare(zone(), ast_value_factory->new_target_string(),
                        VariableMode::kConst, NORMAL_VARIABLE,
                        kCreatedInitialized, kNotAssigned, &was_added);
  DCHECK(was_added);

  if (IsConciseMethod(function_kind_) || IsClassConstructor(function_kind_) ||
      IsAccessorFunction(function_kind_)) {
    EnsureRareData()->this_function = Declare(
        zone(), ast_value_factory->this_function_string(), VariableMode::kConst,
        NORMAL_VARIABLE, kCreatedInitialized, kNotAssigned, &was_added);
    DCHECK(was_added);
  }
}

Variable* DeclarationScope::DeclareFunctionVar(const AstRawString* name,
                                               Scope* cache) {
  DCHECK(is_function_scope());
  DCHECK_NULL(function_);
  if (cache == nullptr) cache = this;
  DCHECK(this->IsOuterScopeOf(cache));
  DCHECK_NULL(cache->variables_.Lookup(name));
  VariableKind kind = is_sloppy(language_mode()) ? SLOPPY_FUNCTION_NAME_VARIABLE
                                                 : NORMAL_VARIABLE;
  function_ = zone()->New<Variable>(this, name, VariableMode::kConst, kind,
                                    kCreatedInitialized);
  if (sloppy_eval_can_extend_vars()) {
    cache->NonLocal(name, VariableMode::kDynamic);
  } else {
    cache->variables_.Add(function_);
  }
  return function_;
}

Variable* DeclarationScope::DeclareGeneratorObjectVar(
    const AstRawString* name) {
  DCHECK(is_function_scope() || is_module_scope() || is_repl_mode_scope());
  DCHECK_NULL(generator_object_var());

  Variable* result = EnsureRareData()->generator_object =
      NewTemporary(name, kNotAssigned);
  result->set_is_used();
  return result;
}

Scope* Scope::FinalizeBlockScope() {
  DCHECK(is_block_scope());
#ifdef DEBUG
  DCHECK_NE(sibling_, this);
#endif

  if (variables_.occupancy() > 0 ||
      (is_declaration_scope() &&
       AsDeclarationScope()->sloppy_eval_can_extend_vars())) {
    return this;
  }

  DCHECK(!is_class_scope());

  // Remove this scope from outer scope.
  outer_scope()->RemoveInnerScope(this);

  // Reparent inner scopes.
  if (inner_scope_ != nullptr) {
    Scope* scope = inner_scope_;
    scope->outer_scope_ = outer_scope();
    while (scope->sibling_ != nullptr) {
      scope = scope->sibling_;
      scope->outer_scope_ = outer_scope();
    }
    scope->sibling_ = outer_scope()->inner_scope_;
    outer_scope()->inner_scope_ = inner_scope_;
    inner_scope_ = nullptr;
  }

  // Move unresolved variables
  if (!unresolved_list_.is_empty()) {
    outer_scope()->unresolved_list_.Prepend(std::move(unresolved_list_));
    unresolved_list_.Clear();
  }

  if (inner_scope_calls_eval_) outer_scope()->inner_scope_calls_eval_ = true;

  // No need to propagate sloppy_eval_can_extend_vars_, since if it was relevant
  // to this scope we would have had to bail out at the top.
  DCHECK(!is_declaration_scope() ||
         !AsDeclarationScope()->sloppy_eval_can_extend_vars());

  // This block does not need a context.
  num_heap_slots_ = 0;

  // Mark scope as removed by making it its own sibling.
#ifdef DEBUG
  sibling_ = this;
#endif

  return nullptr;
}

void DeclarationScope::AddLocal(Variable* var) {
  DCHECK(!already_resolved_);
  // Temporaries are only placed in ClosureScopes.
  DCHECK_EQ(GetClosureScope(), this);
  locals_.Add(var);
}

void Scope::Snapshot::Reparent(DeclarationScope* new_parent) {
  DCHECK_EQ(new_parent, outer_scope_->inner_scope_);
  DCHECK_EQ(new_parent->outer_scope_, outer_scope_);
  DCHECK_EQ(new_parent, new_parent->GetClosureScope());
  DCHECK_NULL(new_parent->inner_scope_);
  DCHECK(new_parent->unresolved_list_.is_empty());
  Scope* inner_scope = new_parent->sibling_;
  if (inner_scope != top_inner_scope_) {
    for (; inner_scope->sibling() != top_inner_scope_;
         inner_scope = inner_scope->sibling()) {
      inner_scope->outer_scope_ = new_parent;
      if (inner_scope->inner_scope_calls_eval_) {
        new_parent->inner_scope_calls_eval_ = true;
      }
      DCHECK_NE(inner_scope, new_parent);
    }
    inner_scope->outer_scope_ = new_parent;
    if (inner_scope->inner_scope_calls_eval_) {
      new_parent->inner_scope_calls_eval_ = true;
    }
    new_parent->inner_scope_ = new_parent->sibling_;
    inner_scope->sibling_ = nullptr;
    // Reset the sibling rather than the inner_scope_ since we
    // want to keep new_parent there.
    new_parent->sibling_ = top_inner_scope_;
  }

  new_parent->unresolved_list_.MoveTail(&outer_scope_->unresolved_list_,
                                        top_unresolved_);

  // Move temporaries allocated for complex parameter initializers.
  DeclarationScope* outer_closure = outer_scope_->GetClosureScope();
  for (auto it = top_local_; it != outer_closure->locals()->end(); ++it) {
    Variable* local = *it;
    DCHECK_EQ(VariableMode::kTemporary, local->mode());
    DCHECK_EQ(local->scope(), local->scope()->GetClosureScope());
    DCHECK_NE(local->scope(), new_parent);
    local->set_scope(new_parent);
  }
  new_parent->locals_.MoveTail(outer_closure->locals(), top_local_);
  outer_closure->locals_.Rewind(top_local_);

  // Move eval calls since Snapshot's creation into new_parent.
  if (outer_scope_->calls_eval_) {
    new_parent->RecordEvalCall();
    outer_scope_->calls_eval_ = false;
    declaration_scope_->sloppy_eval_can_extend_vars_ = false;
  }
}

Variable* Scope::LookupInScopeInfo(const AstRawString* name, Scope* cache) {
  DCHECK(!scope_info_.is_null());
  DCHECK(this->IsOuterScopeOf(cache));
  DCHECK(!cache->deserialized_scope_uses_external_cache());
  // The case where where the cache can be another scope is when the cache scope
  // is the last scope that doesn't use an external cache.
  //
  // The one exception to this is when looking up the home object, which may
  // skip multiple scopes that don't use an external cache (e.g., several arrow
  // functions).
  DCHECK_IMPLIES(
      cache != this,
      cache->outer_scope()->deserialized_scope_uses_external_cache() ||
          cache->GetHomeObjectScope() == this);
  DCHECK_NULL(cache->variables_.Lookup(name));
  DisallowGarbageCollection no_gc;

  Tagged<String> name_handle = *name->string();
  Tagged<ScopeInfo> scope_info = *scope_info_;
  // The Scope is backed up by ScopeInfo. This means it cannot operate in a
  // heap-independent mode, and all strings must be internalized immediately. So
  // it's ok to get the Handle<String> here.
  bool found = false;

  VariableLocation location;
  int index;
  VariableLookupResult lookup_result;

  {
    location = VariableLocation::CONTEXT;
    index = scope_info->ContextSlotIndex(name->string(), &lookup_result);
    found = index >= 0;
  }

  if (!found && is_module_scope()) {
    location = VariableLocation::MODULE;
    index = scope_info->ModuleIndex(name_handle, &lookup_result.mode,
                                    &lookup_result.init_flag,
                                    &lookup_result.maybe_assigned_flag);
    found = index != 0;
  }

  if (!found) {
    index = scope_info->FunctionContextSlotIndex(name_handle);
    if (index < 0) return nullptr;  // Nowhere found.
    Variable* var = AsDeclarationScope()->DeclareFunctionVar(name, cache);
    DCHECK_EQ(VariableMode::kConst, var->mode());
    var->AllocateTo(VariableLocation::CONTEXT, index);
    return cache->variables_.Lookup(name);
  }

  if (!is_module_scope()) {
    DCHECK_NE(index, scope_info->ReceiverContextSlotIndex());
  }

  bool was_added;
  Variable* var = cache->variables_.Declare(
      zone(), this, name, lookup_result.mode, NORMAL_VARIABLE,
      lookup_result.init_flag, lookup_result.maybe_assigned_flag,
      IsStaticFlag::kNotStatic, &was_added);
  DCHECK(was_added);
  var->AllocateTo(location, index);
  return var;
}

Variable* DeclarationScope::DeclareParameter(const AstRawString* name,
                                             VariableMode mode,
                                             bool is_optional, bool is_rest,
                                             AstValueFactory* ast_value_factory,
                                             int position) {
  DCHECK(!already_resolved_);
  DCHECK(is_function_scope() || is_module_scope());
  DCHECK(!has_rest_);
  DCHECK(!is_optional || !is_rest);
  DCHECK(!is_being_lazily_parsed_);
  DCHECK(!was_lazily_parsed_);
  Variable* var;
  if (mode == VariableMode::kTemporary) {
    var = NewTemporary(name);
  } else {
    var = LookupLocal(name);
    DCHECK_EQ(mode, VariableMode::kVar);
    DCHECK(var->is_parameter());
  }
  has_rest_ = is_rest;
  var->set_initializer_position(position);
  params_.Add(var, zone());
  if (!is_rest) ++num_parameters_;
  if (name == ast_value_factory->arguments_string()) {
    has_arguments_parameter_ = true;
  }
  // Params are automatically marked as used to make sure that the debugger and
  // function.arguments sees them.
  // TODO(verwaest): Reevaluate whether we always need to do this, since
  // strict-mode function.arguments does not make the arguments available.
  var->set_is_used();
  return var;
}

void DeclarationScope::RecordParameter(bool is_rest) {
  DCHECK(!already_resolved_);
  DCHECK(is_function_scope() || is_module_scope());
  DCHECK(is_being_lazily_parsed_);
  DCHECK(!has_rest_);
  has_rest_ = is_rest;
  if (!is_rest) ++num_parameters_;
}

Variable* Scope::DeclareLocal(const AstRawString* name, VariableMode mode,
                              VariableKind kind, bool* was_added,
                              InitializationFlag init_flag) {
  DCHECK(!already_resolved_);
  // Private methods should be declared with ClassScope::DeclarePrivateName()
  DCHECK(!IsPrivateMethodOrAccessorVariableMode(mode));
  // This function handles VariableMode::kVar, VariableMode::kLet,
  // VariableMode::kConst, VariableMode::kUsing, and VariableMode::kAwaitUsing
  // modes. VariableMode::kDynamic variables are introduced during variable
  // allocation, and VariableMode::kTemporary variables are allocated via
  // NewTemporary().
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK_IMPLIES(GetDeclarationScope()->is_being_lazily_parsed(),
                 mode == VariableMode::kVar || mode == VariableMode::kLet ||
                     mode == VariableMode::kConst ||
                     mode == VariableMode::kUsing ||
                     mode == VariableMode::kAwaitUsing);
  DCHECK(!GetDeclarationScope()->was_lazily_parsed());
  Variable* var =
      Declare(zone(), name, mode, kind, init_flag, kNotAssigned, was_added);

  // Pessimistically assume that top-level variables will be assigned and used.
  //
  // Top-level variables in a script can be accessed by other scripts or even
  // become global properties. While this does not apply to top-level variables
  // in a module (assuming they are not exported), we must still mark these as
  // assigned because they might be accessed by a lazily parsed top-level
  // function, which, for efficiency, we preparse without variable tracking.
  if (is_script_scope() || is_module_scope()) {
    if (mode != VariableMode::kConst) var->SetMaybeAssigned();
    var->set_is_used();
  }

  return var;
}

Variable* Scope::DeclareVariable(
    Declaration* declaration, const AstRawString* name, int pos,
    VariableMode mode, VariableKind kind, InitializationFlag init,
    bool* was_added, bool* sloppy_mode_block_scope_function_redefinition,
    bool* ok) {
  // Private methods should be declared with ClassScope::DeclarePrivateName()
  DCHECK(!IsPrivateMethodOrAccessorVariableMode(mode));
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK(!already_resolved_);
  DCHECK(!GetDeclarationScope()->is_being_lazily_parsed());
  DCHECK(!GetDeclarationScope()->was_lazily_parsed());

  if (mode == VariableMode::kVar && !is_declaration_scope()) {
    return GetDeclarationScope()->DeclareVariable(
        declaration, name, pos, mode, kind, init, was_added,
        sloppy_mode_block_scope_function_redefinition, ok);
  }
  DCHECK(!is_catch_scope());
  DCHECK(!is_with_scope());
  DCHECK(is_declaration_scope() ||
         (IsLexicalVariableMode(mode) && is_block_scope()));

  DCHECK_NOT_NULL(name);

  Variable* var = LookupLocal(name);
  // Declare the variable in the declaration scope.
  *was_added = var == nullptr;
  if (V8_LIKELY(*was_added)) {
    if (V8_UNLIKELY(is_eval_scope() && is_sloppy(language_mode()) &&
                    mode == VariableMode::kVar)) {
      // In a var binding in a sloppy direct eval, pollute the enclosing scope
      // with this new binding by doing the following:
      // The proxy is bound to a lookup variable to force a dynamic declaration
      // using the DeclareEvalVar or DeclareEvalFunction runtime functions.
      DCHECK_EQ(NORMAL_VARIABLE, kind);
      var = NonLocal(name, VariableMode::kDynamic);
      // Mark the var as used in case anyone outside the eval wants to use it.
      var->set_is_used();
    } else {
      // Declare the name.
      var = DeclareLocal(name, mode, kind, was_added, init);
      DCHECK(*was_added);
    }
  } else {
    var->SetMaybeAssigned();
    if (V8_UNLIKELY(IsLexicalVariableMode(mode) ||
                    IsLexicalVariableMode(var->mode()))) {
      // The name was declared in this scope before; check for conflicting
      // re-declarations. We have a conflict if either of the declarations is
      // not a var (in script scope, we also have to ignore legacy const for
      // compatibility). There is similar code in runtime.cc in the Declare
      // functions. The function CheckConflictingVarDeclarations checks for
      // var and let bindings from different scopes whereas this is a check
      // for conflicting declarations within the same scope. This check also
      // covers the special case
      //
      // function () { let x; { var x; } }
      //
      // because the var declaration is hoisted to the function scope where
      // 'x' is already bound.
      //
      // In harmony we treat re-declarations as early errors. See ES5 16 for a
      // definition of early errors.
      //
      // Allow duplicate function decls for web compat, see bug 4693.
      *ok = var->is_sloppy_block_function() &&
            kind == SLOPPY_BLOCK_FUNCTION_VARIABLE;
      *sloppy_mode_block_scope_function_redefinition = *ok;
    }
  }
  DCHECK_NOT_NULL(var);

  // We add a declaration node for every declaration. The compiler
  // will only generate code if necessary. In particular, declarations
  // for inner local variables that do not represent functions won't
  // result in any generated code.
  //
  // This will lead to multiple declaration nodes for the
  // same variable if it is declared several times. This is not a
  // semantic issue, but it may be a performance issue since it may
  // lead to repeated DeclareEvalVar or DeclareEvalFunction calls.
  decls_.Add(declaration);
  declaration->set_var(var);
  return var;
}

Variable* Scope::DeclareVariableName(const AstRawString* name,
                                     VariableMode mode, bool* was_added,
                                     VariableKind kind) {
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK(!already_resolved_);
  DCHECK(GetDeclarationScope()->is_being_lazily_parsed());
  // Private methods should be declared with ClassScope::DeclarePrivateName()
  DCHECK(!IsPrivateMethodOrAccessorVariableMode(mode));
  if (mode == VariableMode::kVar && !is_declaration_scope()) {
    return GetDeclarationScope()->DeclareVariableName(name, mode, was_added,
                                                      kind);
  }
  DCHECK(!is_with_scope());
  DCHECK(!is_eval_scope());
  DCHECK(is_declaration_scope() || IsLexicalVariableMode(mode));
  DCHECK(scope_info_.is_null());

  // Declare the variable in the declaration scope.
  Variable* var = DeclareLocal(name, mode, kind, was_added);
  if (!*was_added) {
    if (IsLexicalVariableMode(mode) || IsLexicalVariableMode(var->mode())) {
      if (!var->is_sloppy_block_function() ||
          kind != SLOPPY_BLOCK_FUNCTION_VARIABLE) {
        // Duplicate functions are allowed in the sloppy mode, but if this is
        // not a function declaration, it's an error. This is an error PreParser
        // hasn't previously detected.
        return nullptr;
      }
      // Sloppy block function redefinition.
    }
    var->SetMaybeAssigned();
  }
  var->set_is_used();
  return var;
}

Variable* Scope::DeclareCatchVariableName(const AstRawString* name) {
  DCHECK(!already_resolved_);
  DCHECK(is_catch_scope());
  DCHECK(scope_info_.is_null());

  bool was_added;
  Variable* result = Declare(zone(), name, VariableMode::kVar, NORMAL_VARIABLE,
                             kCreatedInitialized, kNotAssigned, &was_added);
  DCHECK(was_added);
  return result;
}

void Scope::AddUnresolved(VariableProxy* proxy) {
  // The scope is only allowed to already be resolved if we're reparsing a class
  // initializer. Class initializers will manually resolve these references
  // separate from regular variable resolution.
  DCHECK_IMPLIES(already_resolved_, reparsing_for_class_initializer_);
  DCHECK(!proxy->is_resolved());
  unresolved_list_.Add(proxy);
}

Variable* DeclarationScope::DeclareDynamicGlobal(const AstRawString* name,
                                                 VariableKind kind,
                                                 Scope* cache) {
  DCHECK(is_script_scope());
  bool was_added;
  return cache->variables_.Declare(
      zone(), this, name, VariableMode::kDynamicGlobal, kind,
      kCreatedInitialized, kNotAssigned, IsStaticFlag::kNotStatic, &was_added);
  // TODO(neis): Mark variable as maybe-assigned?
}

void Scope::DeleteUnresolved(VariableProxy* var) {
  DCHECK(unresolved_list_.Contains(var));
  var->mark_removed_from_unresolved();
}

Variable* Scope::NewTemporary(const AstRawString* name) {
  return NewTemporary(name, kMaybeAssigned);
}

Variable* Scope::NewTemporary(const AstRawString* name,
                              MaybeAssignedFlag maybe_assigned) {
  DeclarationScope* scope = GetClosureScope();
  Variable* var = zone()->New<Variable>(scope, name, VariableMode::kTemporary,
                                        NORMAL_VARIABLE, kCreatedInitialized);
  scope->AddLocal(var);
  if (maybe_assigned == kMaybeAssigned) var->SetMaybeAssigned();
  return var;
}

Declaration* DeclarationScope::CheckConflictingVarDeclarations(
    bool* allowed_catch_binding_var_redeclaration) {
  if (has_checked_syntax_) return nullptr;
  for (Declaration* decl : decls_) {
    // Lexical vs lexical conflicts within the same scope have already been
    // captured in Parser::Declare. The only conflicts we still need to check
    // are lexical vs nested var.
    if (decl->IsVariableDeclaration() &&
        decl->AsVariableDeclaration()->AsNested() != nullptr) {
      Scope* current = decl->AsVariableDeclaration()->AsNested()->scope();
      if (decl->var()->mode() != VariableMode::kVar &&
          decl->var()->mode() != VariableMode::kDynamic)
        continue;
      // Iterate through all scopes until the declaration scope.
      do {
        // There is a conflict if there exists a non-VAR binding.
        Variable* other_var = current->LookupLocal(decl->var()->raw_name());
        if (current->is_catch_scope()) {
          *allowed_catch_binding_var_redeclaration |= other_var != nullptr;
          current = current->outer_scope();
          continue;
        }
        if (other_var != nullptr) {
          DCHECK(IsLexicalVariableMode(other_var->mode()));
          return decl;
        }
        current = current->outer_scope();
      } while (current != this);
    }
  }

  if (V8_LIKELY(!is_eval_scope())) return nullptr;
  if (!is_sloppy(language_mode())) return nullptr;

  // Var declarations in sloppy eval are hoisted to the first non-eval
  // declaration scope. Check for conflicts between the eval scope that
  // declaration scope.
  Scope* end = outer_scope()->GetNonEvalDeclarationScope()->outer_scope();

  for (Declaration* decl : decls_) {
    if (IsLexicalVariableMode(decl->var()->mode())) continue;
    Scope* current = outer_scope_;
    // Iterate through all scopes until and including the declaration scope.
    do {
      // There is a conflict if there exists a non-VAR binding up to the
      // declaration scope in which this sloppy-eval runs.
      //
      // Use the current scope as the cache, since the general cache would be
      // the end scope.
      Variable* other_var =
          current->LookupInScopeOrScopeInfo(decl->var()->raw_name(), current);
      if (other_var != nullptr && !current->is_catch_scope()) {
        // If this is a VAR, then we know that it doesn't conflict with
        // anything, so we can't conflict with anything either. The one
        // exception is the binding variable in catch scopes, which is handled
        // by the if above.
        if (!IsLexicalVariableMode(other_var->mode())) break;
        return decl;
      }
      current = current->outer_scope();
    } while (current != end);
  }
  return nullptr;
}

const AstRawString* Scope::FindVariableDeclaredIn(Scope* scope,
                                                  VariableMode mode_limit) {
  const VariableMap& variables = scope->variables_;
  for (ZoneHashMap::Entry* p = variables.Start(); p != nullptr;
       p = variables.Next(p)) {
    const AstRawString* name = static_cast<const AstRawString*>(p->key);
    Variable* var = LookupLocal(name);
    if (var != nullptr && var->mode() <= mode_limit) return name;
  }
  return nullptr;
}

void DeclarationScope::DeserializeReceiver(AstValueFactory* ast_value_factory) {
  if (is_script_scope()) {
    DCHECK_NOT_NULL(receiver_);
    return;
  }
  DCHECK(has_this_declaration());
  DeclareThis(ast_value_factory);
  if (is_debug_evaluate_scope()) {
    receiver_->AllocateTo(VariableLocation::LOOKUP, -1);
  } else {
    receiver_->AllocateTo(VariableLocation::CONTEXT,
                          scope_info_->ReceiverContextSlotIndex());
  }
}

bool DeclarationScope::AllocateVariables(ParseInfo* info) {
  // Module variables must be allocated before variable resolution
  // to ensure that UpdateNeedsHoleCheck() can detect import variables.
  if (is_module_scope()) AsModuleScope()->AllocateModuleVariables();

  PrivateNameScopeIterator private_name_scope_iter(this);
  if (!private_name_scope_iter.Done() &&
      !private_name_scope_iter.GetScope()->ResolvePrivateNames(info)) {
    DCHECK(info->pending_error_handler()->has_pending_error());
    return false;
  }

  if (!ResolveVariablesRecursively(info->scope())) {
    DCHECK(info->pending_error_handler()->has_pending_error());
    return false;
  }

  // Don't allocate variables of preparsed scopes.
  if (!was_lazily_parsed()) AllocateVariablesRecursively();

  return true;
}

bool Scope::HasThisReference() const {
  if (is_declaration_scope() && AsDeclarationScope()->has_this_reference()) {
    return true;
  }

  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    if (!scope->is_declaration_scope() ||
        !scope->AsDeclarationScope()->has_this_declaration()) {
      if (scope->HasThisReference()) return true;
    }
  }

  return false;
}

bool Scope::AllowsLazyParsingWithoutUnresolvedVariables(
    const Scope* outer) const {
  // If none of the outer scopes need to decide whether to context allocate
  // specific variables, we can preparse inner functions without unresolved
  // variables. Otherwise we need to find unresolved variables to force context
  // allocation of the matching declarations. We can stop at the outer scope for
  // the parse, since context allocation of those variables is already
  // guaranteed to be correct.
  for (const Scope* s = this; s != outer; s = s->outer_scope_) {
    // Eval forces context allocation on all outer scopes, so we don't need to
    // look at those scopes. Sloppy eval makes top-level non-lexical variables
    // dynamic, whereas strict-mode requires context allocation.
    if (s->is_eval_scope()) return is_sloppy(s->language_mode());
    // Catch scopes force context allocation of all variables.
    if (s->is_catch_scope()) continue;
    // With scopes do not introduce variables that need allocation.
    if (s->is_with_scope()) continue;
    DCHECK(s->is_module_scope() || s->is_block_scope() ||
           s->is_function_scope());
    return false;
  }
  return true;
}

bool DeclarationScope::AllowsLazyCompilation() const {
  // Functions which force eager compilation and class member initializer
  // functions are not lazily compilable.
  return !force_eager_compilation_ &&
         !IsClassMembersInitializerFunction(function_kind());
}

int Scope::ContextChainLength(Scope* scope) const {
  int n = 0;
  for (const Scope* s = this; s != scope; s = s->outer_scope_) {
    DCHECK_NOT_NULL(s);  // scope must be in the scope chain
    if (s->NeedsContext()) n++;
  }
  return n;
}

int Scope::ContextChainLengthUntilOutermostSloppyEval() const {
  int result = 0;
  int length = 0;

  for (const Scope* s = this; s != nullptr; s = s->outer_scope()) {
    if (!s->NeedsContext()) continue;
    length++;
    if (s->is_declaration_scope() &&
        s->AsDeclarationScope()->sloppy_eval_can_extend_vars()) {
      result = length;
    }
  }

  return result;
}

DeclarationScope* Scope::GetDeclarationScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

DeclarationScope* Scope::GetNonEvalDeclarationScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope() || scope->is_eval_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

const DeclarationScope* Scope::GetClosureScope() const {
  const Scope* scope = this;
  while (!scope->is_declaration_scope() || scope->is_block_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

DeclarationScope* Scope::GetClosureScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope() || scope->is_block_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

bool Scope::NeedsScopeInfo() const {
  DCHECK(!already_resolved_);
  DCHECK(GetClosureScope()->ShouldEagerCompile());
  // The debugger expects all functions to have scope infos.
  // TODO(yangguo): Remove this requirement.
  if (is_function_scope()) return true;
  return NeedsContext();
}

bool Scope::ShouldBanArguments() {
  return GetReceiverScope()->should_ban_arguments();
}

DeclarationScope* Scope::GetReceiverScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope() ||
         (!scope->is_script_scope() &&
          !scope->AsDeclarationScope()->has_this_declaration())) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

DeclarationScope* Scope::GetConstructorScope() {
  Scope* scope = this;
  while (scope != nullptr && !scope->IsConstructorScope()) {
    scope = scope->outer_scope();
  }
  if (scope == nullptr) {
    return nullptr;
  }
  DCHECK(scope->IsConstructorScope());
  return scope->AsDeclarationScope();
}

Scope* Scope::GetHomeObjectScope() {
  Scope* scope = GetReceiverScope();
  DCHECK(scope->is_function_scope());
  FunctionKind kind = scope->AsDeclarationScope()->function_kind();
  // "super" in arrow functions binds outside the arrow function. Arrow
  // functions are also never receiver scopes since they close over the
  // receiver.
  DCHECK(!IsArrowFunction(kind));
  // If we find a function which doesn't bind "super" (is not a method etc.), we
  // know "super" here doesn't bind anywhere and we can return nullptr.
  if (!BindsSuper(kind)) return nullptr;
  // Functions that bind "super" can only syntactically occur nested inside home
  // object scopes (i.e. class scopes and object literal scopes), so directly
  // return the outer scope.
  Scope* outer_scope = scope->outer_scope();
  CHECK(outer_scope->is_home_object_scope());
  return outer_scope;
}

DeclarationScope* Scope::GetScriptScope() {
  Scope* scope = this;
  while (!scope->is_script_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

Scope* Scope::GetOuterScopeWithContext() {
  Scope* scope = outer_scope_;
  while (scope && !scope->NeedsContext()) {
    scope = scope->outer_scope();
  }
  return scope;
}

namespace {
bool WasLazilyParsed(Scope* scope) {
  return scope->is_declaration_scope() &&
         scope->AsDeclarationScope()->was_lazily_parsed();
}

}  // namespace

template <typename FunctionType>
void Scope::ForEach(FunctionType callback) {
  Scope* scope = this;
  while (true) {
    Iteration iteration = callback(scope);
    // Try to descend into inner scopes first.
    if ((iteration == Iteration::kDescend) && scope->inner_scope_ != nullptr) {
      scope = scope->inner_scope_;
    } else {
      // Find the next outer scope with a sibling.
      while (scope->sibling_ == nullptr) {
        if (scope == this) return;
        scope = scope->outer_scope_;
      }
      if (scope == this) return;
      scope = scope->sibling_;
    }
  }
}

bool Scope::IsConstructorScope() const {
  return is_declaration_scope() &&
         IsClassConstructor(AsDeclarationScope()->function_kind());
}

bool Scope::IsOuterScopeOf(Scope* other) const {
  Scope* scope = other;
  while (scope) {
    if (scope == this) return true;
    scope = scope->outer_scope();
  }
  return false;
}

void Scope::AnalyzePartially(DeclarationScope* max_outer_scope,
                             AstNodeFactory* ast_node_factory,
                             UnresolvedList* new_unresolved_list,
                             bool maybe_in_arrowhead) {
  this->ForEach([max_outer_scope, ast_node_factory, new_unresolved_list,
                 maybe_in_arrowhead](Scope* scope) {
    // Skip already lazily parsed scopes. This can only happen to functions
    // inside arrowheads.
    if (WasLazilyParsed(scope)) {
      DCHECK(max_outer_scope->is_arrow_scope());
      return Iteration::kContinue;
    }

    for (VariableProxy* proxy = scope->unresolved_list_.first();
         proxy != nullptr; proxy = proxy->next_unresolved()) {
      if (proxy->is_removed_from_unresolved()) continue;
      DCHECK(!proxy->is_resolved());
      Variable* var =
          Lookup<kParsedScope>(proxy, scope, max_outer_scope->outer_scope());
      if (var == nullptr) {
        // Don't copy unresolved references to the script scope, unless it's a
        // reference to a private name or method. In that case keep it so we
        // can fail later.
        if (!max_outer_scope->outer_scope()->is_script_scope() ||
            maybe_in_arrowhead) {
          VariableProxy* copy = ast_node_factory->CopyVariableProxy(proxy);
          new_unresolved_list->Add(copy);
        }
      } else {
        var->set_is_used();
        if (proxy->is_assigned()) var->SetMaybeAssigned();
      }
    }

    // Clear unresolved_list_ as it's in an inconsistent state.
    scope->unresolved_list_.Clear();
    return Iteration::kDescend;
  });
}

void DeclarationScope::ResetAfterPreparsing(AstValueFactory* ast_value_factory,
                                            bool aborted) {
  DCHECK(is_function_scope());

  // Reset all non-trivial members.
  params_.DropAndClear();
  decls_.Clear();
  locals_.Clear();
  inner_scope_ = nullptr;
  unresolved_list_.Clear();
  sloppy_block_functions_.Clear();
  rare_data_ = nullptr;
  has_rest_ = false;
  function_ = nullptr;

  DCHECK_NE(zone(), ast_value_factory->single_parse_zone());
  // Make sure this scope and zone aren't used for allocation anymore.
  {
    // Get the zone, while variables_ is still valid
    Zone* zone = this->zone();
    variables_.Invalidate();
    zone->Reset();
  }

  if (aborted) {
    // Prepare scope for use in the outer zone.
    variables_ = VariableMap(ast_value_factory->single_parse_zone());
    if (!IsArrowFunction(function_kind_)) {
      has_simple_parameters_ = true;
      DeclareDefaultFunctionVariables(ast_value_factory);
    }
  }

#ifdef DEBUG
  needs_migration_ = false;
  is_being_lazily_parsed_ = false;
#endif

  was_lazily_parsed_ = !aborted;
}

bool Scope::IsSkippableFunctionScope() {
  // Lazy non-arrow function scopes are skippable. Lazy functions are exactly
  // those Scopes which have their own PreparseDataBuilder object. This
  // logic ensures that the scope allocation data is consistent with the
  // skippable function data (both agree on where the lazy function boundaries
  // are).
  if (!is_function_scope()) return false;
  DeclarationScope* declaration_scope = AsDeclarationScope();
  return !declaration_scope->is_arrow_scope() &&
         declaration_scope->preparse_data_builder() != nullptr;
}

void Scope::SavePreparseData(Parser* parser) {
  this->ForEach([parser](Scope* scope) {
    // Save preparse data for every skippable scope, unless it was already
    // previously saved (this can happen with functions inside arrowheads).
    if (scope->IsSkippableFunctionScope() &&
        !scope->AsDeclarationScope()->was_lazily_parsed()) {
      scope->AsDeclarationScope()->SavePreparseDataForDeclarationScope(parser);
    }
    return Iteration::kDescend;
  });
}

void DeclarationScope::SavePreparseDataForDeclarationScope(Parser* parser) {
  if (preparse_data_builder_ == nullptr) return;
  preparse_data_builder_->SaveScopeAllocationData(this, parser);
}

void DeclarationScope::AnalyzePartially(Parser* parser,
                                        AstNodeFactory* ast_node_factory,
                                        bool maybe_in_arrowhead) {
  DCHECK(!force_eager_compilation_);
  UnresolvedList new_unresolved_list;

  // We don't need to do partial analysis for top level functions, since they
  // can only access values in the global scope, and we can't track assignments
  // for these since they're accessible across scripts.
  //
  // If the top level function has inner functions though, we do still want to
  // analyze those to save their preparse data.
  //
  // Additionally, functions in potential arrowheads _need_ to be analyzed, in
  // case they do end up being in an arrowhead and the arrow function needs to
  // know about context acceses. For example, in
  //
  //     (a, b=function foo(){ a = 1 }) => { b(); return a}
  //
  // `function foo(){ a = 1 }` is "maybe_in_arrowhead" but still top-level when
  // parsed, and is re-scoped to the arrow function when that one is parsed. The
  // arrow function needs to know that `a` needs to be context allocated, so
  // that the call to `b()` correctly updates the `a` parameter.
  const bool is_top_level_function = outer_scope_->is_script_scope();
  const bool has_inner_functions = preparse_data_builder_ != nullptr &&
                                   preparse_data_builder_->HasInnerFunctions();
  if (maybe_in_arrowhead || !is_top_level_function || has_inner_functions) {
    // Try to resolve unresolved variables for this Scope and migrate those
    // which cannot be resolved inside. It doesn't make sense to try to resolve
    // them in the outer Scopes here, because they are incomplete.
    Scope::AnalyzePartially(this, ast_node_factory, &new_unresolved_list,
                            maybe_in_arrowhead);

    // Migrate function_ to the right Zone.
    if (function_ != nullptr) {
      function_ = ast_node_factory->CopyVariable(function_);
    }

    SavePreparseData(parser);
  }

#ifdef DEBUG
  if (v8_flags.print_scopes) {
    PrintF("Inner function scope:\n");
    Print();
  }
#endif

  ResetAfterPreparsing(ast_node_factory->ast_value_factory(), false);

  unresolved_list_ = std::move(new_unresolved_list);
}

void DeclarationScope::RewriteReplGlobalVariables() {
  DCHECK(is_script_scope());
  if (!is_repl_mode_scope()) return;

  for (VariableMap::Entry* p = variables_.Start(); p != nullptr;
       p = variables_.Next(p)) {
    Variable* var = reinterpret_cast<Variable*>(p->value);
    var->RewriteLocationForRepl();
  }
}

#ifdef DEBUG
namespace {

const char* Header(ScopeType scope_type, FunctionKind function_kind,
                   bool is_declaration_scope) {
  switch (scope_type) {
    case EVAL_SCOPE: return "eval";
    case FUNCTION_SCOPE:
      if (IsGeneratorFunction(function_kind)) return "function*";
      if (IsAsyncFunction(function_kind)) return "async function";
      if (IsArrowFunction(function_kind)) return "arrow";
      return "function";
    case MODULE_SCOPE: return "module";
    case REPL_MODE_SCOPE:
      return "repl";
    case SCRIPT_SCOPE: return "global";
    case CATCH_SCOPE: return "catch";
    case BLOCK_SCOPE: return is_declaration_scope ? "varblock" : "block";
    case CLASS_SCOPE:
      return "class";
    case WITH_SCOPE: return "with";
    case SHADOW_REALM_SCOPE:
      return "shadowrealm";
  }
  UNREACHABLE();
}

void Indent(int n, const char* str) { PrintF("%*s%s", n, "", str); }

void PrintName(const AstRawString* name) {
  PrintF("%.*s", name->length(), name->raw_data());
}

void PrintLocation(Variable* var) {
  switch (var->location()) {
    case VariableLocation::UNALLOCATED:
      break;
    case VariableLocation::PARAMETER:
      PrintF("parameter[%d]", var->index());
      break;
    case VariableLocation::LOCAL:
      PrintF("local[%d]", var->index());
      break;
    case VariableLocation::CONTEXT:
      PrintF("context[%d]", var->index());
      break;
    case VariableLocation::LOOKUP:
      PrintF("lookup");
      break;
    case VariableLocation::MODULE:
      PrintF("module");
      break;
    case VariableLocation::REPL_GLOBAL:
      PrintF("repl global[%d]", var->index());
      break;
  }
}

void PrintVar(int indent, Variable* var) {
  Indent(indent, VariableMode2String(var->mode()));
  PrintF(" ");
  if (var->raw_name()->IsEmpty())
    PrintF(".%p", reinterpret_cast<void*>(var));
  else
    PrintName(var->raw_name());
  PrintF(";  // (%p) ", reinterpret_cast<void*>(var));
  PrintLocation(var);
  bool comma = !var->IsUnallocated();
  if (var->has_forced_context_allocation()) {
    if (comma) PrintF(", ");
    PrintF("forced context allocation");
    comma = true;
  }
  if (var->maybe_assigned() == kNotAssigned) {
    if (comma) PrintF(", ");
    PrintF("never assigned");
    comma = true;
  }
  if (var->initialization_flag() == kNeedsInitialization &&
      !var->binding_needs_init()) {
    if (comma) PrintF(", ");
    PrintF("hole initialization elided");
  }
  PrintF("\n");
}

void PrintMap(int indent, const char* label, VariableMap* map, bool locals,
              Variable* function_var) {
  bool printed_label = false;
  for (VariableMap::Entry* p = map->Start(); p != nullptr; p = map->Next(p)) {
    Variable* var = reinterpret_cast<Variable*>(p->value);
    if (var == function_var) continue;
    bool local = !IsDynamicVariableMode(var->mode());
    if ((locals ? local : !local) &&
        (var->is_used() || !var->IsUnallocated())) {
      if (!printed_label) {
        Indent(indent, label);
        printed_label = true;
      }
      PrintVar(indent, var);
    }
  }
}

}  // anonymous namespace

void DeclarationScope::PrintParameters() {
  PrintF(" (");
  for (int i = 0; i < params_.length(); i++) {
    if (i > 0) PrintF(", ");
    const AstRawString* name = params_[i]->raw_name();
    if (name->IsEmpty()) {
      PrintF(".%p", reinterpret_cast<void*>(params_[i]));
    } else {
      PrintName(name);
    }
  }
  PrintF(")");
}

void Scope::Print(int n) {
  int n0 = (n > 0 ? n : 0);
  int n1 = n0 + 2;  // indentation

  // Print header.
  FunctionKind function_kind = is_function_scope()
                                   ? AsDeclarationScope()->function_kind()
                                   : FunctionKind::kNormalFunction;
  Indent(n0, Header(scope_type_, function_kind, is_declaration_scope()));
  if (scope_name_ != nullptr && !scope_name_->IsEmpty()) {
    PrintF(" ");
    PrintName(scope_name_);
  }

  // Print parameters, if any.
  Variable* function = nullptr;
  if (is_function_scope()) {
    AsDeclarationScope()->PrintParameters();
    function = AsDeclarationScope()->function_var();
  }

  PrintF(" { // (%p) (%d, %d)\n", reinterpret_cast<void*>(this),
         start_position(), end_position());
  if (is_hidden()) {
    Indent(n1, "// is hidden\n");
  }

  // Function name, if any (named function literals, only).
  if (function != nullptr) {
    Indent(n1, "// (local) function name: ");
    PrintName(function->raw_name());
    PrintF("\n");
  }

  // Scope info.
  if (is_strict(language_mode())) {
    Indent(n1, "// strict mode scope\n");
  }
#if V8_ENABLE_WEBASSEMBLY
  if (IsAsmModule()) Indent(n1, "// scope is an asm module\n");
#endif  // V8_ENABLE_WEBASSEMBLY
  if (is_declaration_scope() &&
      AsDeclarationScope()->sloppy_eval_can_extend_vars()) {
    Indent(n1, "// scope calls sloppy 'eval'\n");
  }
  if (private_name_lookup_skips_outer_class()) {
    Indent(n1, "// scope skips outer class for #-names\n");
  }
  if (inner_scope_calls_eval_) Indent(n1, "// inner scope calls 'eval'\n");
  if (is_declaration_scope()) {
    DeclarationScope* scope = AsDeclarationScope();
    if (scope->was_lazily_parsed()) Indent(n1, "// lazily parsed\n");
    if (scope->ShouldEagerCompile()) Indent(n1, "// will be compiled\n");
    if (scope->needs_private_name_context_chain_recalc()) {
      Indent(n1, "// needs #-name context chain recalc\n");
    }
    Indent(n1, "// ");
    PrintF("%s\n", FunctionKind2String(scope->function_kind()));
    if (scope->class_scope_has_private_brand()) {
      Indent(n1, "// class scope has private brand\n");
    }
  }
  if (num_stack_slots_ > 0) {
    Indent(n1, "// ");
    PrintF("%d stack slots\n", num_stack_slots_);
  }
  if (num_heap_slots_ > 0) {
    Indent(n1, "// ");
    PrintF("%d heap slots\n", num_heap_slots_);
  }

  // Print locals.
  if (function != nullptr) {
    Indent(n1, "// function var:\n");
    PrintVar(n1, function);
  }

  // Print temporaries.
  {
    bool printed_header = false;
    for (Variable* local : locals_) {
      if (local->mode() != VariableMode::kTemporary) continue;
      if (!printed_header) {
        printed_header = true;
        Indent(n1, "// temporary vars:\n");
      }
      PrintVar(n1, local);
    }
  }

  if (variables_.occupancy() > 0) {
    PrintMap(n1, "// local vars:\n", &variables_, true, function);
    PrintMap(n1, "// dynamic vars:\n", &variables_, false, function);
  }

  if (is_class_scope()) {
    ClassScope* class_scope = AsClassScope();
    if (class_scope->GetRareData() != nullptr) {
      PrintMap(n1, "// private name vars:\n",
               &(class_scope->GetRareData()->private_name_map), true, function);
      Variable* brand = class_scope->brand();
      if (brand != nullptr) {
        Indent(n1, "// brand var:\n");
        PrintVar(n1, brand);
      }
    }
    if (class_scope->class_variable() != nullptr) {
      Indent(n1, "// class var");
      PrintF("%s%s:\n",
             class_scope->class_variable()->is_used() ? ", used" : ", unused",
             class_scope->should_save_class_variable_index()
                 ? ", index saved"
                 : ", index not saved");
      PrintVar(n1, class_scope->class_variable());
    }
  }

  // Print inner scopes (disable by providing negative n).
  if (n >= 0) {
    for (Scope* scope = inner_scope_; scope != nullptr;
         scope = scope->sibling_) {
      PrintF("\n");
      scope->Print(n1);
    }
  }

  Indent(n0, "}\n");
}

void Scope::CheckScopePositions() {
  this->ForEach([](Scope* scope) {
    // Visible leaf scopes must have real positions.
    if (!scope->is_hidden() && scope->inner_scope_ == nullptr) {
      DCHECK_NE(kNoSourcePosition, scope->start_position());
      DCHECK_NE(kNoSourcePosition, scope->end_position());
    }
    return Iteration::kDescend;
  });
}

void Scope::CheckZones() {
  DCHECK(!needs_migration_);
  this->ForEach([](Scope* scope) {
    if (WasLazilyParsed(scope)) {
      DCHECK_NULL(scope->zone());
      DCHECK_NULL(scope->inner_scope_);
      return Iteration::kContinue;
    }
    return Iteration::kDescend;
  });
}
#endif  // DEBUG

Variable* Scope::NonLocal(const AstRawString* name, VariableMode mode) {
  // Declare a new non-local.
  DCHECK(IsDynamicVariableMode(mode));
  bool was_added;
  Variable* var = variables_.Declare(zone(), this, name, mode, NORMAL_VARIABLE,
                                     kCreatedInitialized, kNotAssigned,
                                     IsStaticFlag::kNotStatic, &was_added);
  // Allocate it by giving it a dynamic lookup.
  var->AllocateTo(VariableLocation::LOOKUP, -1);
  return var;
}

void Scope::ForceDynamicLookup(VariableProxy* proxy) {
  // At the moment this is only used for looking up private names dynamically
  // in debug-evaluate from top-level scope.
  DCHECK(proxy->IsPrivateName());
  DCHECK(is_script_scope() || is_module_scope() || is_eval_scope());
  Variable* dynamic = NonLocal(proxy->raw_name(), VariableMode::kDynamic);
  proxy->BindTo(dynamic);
}

// static
template <Scope::ScopeLookupMode mode>
Variable* Scope::Lookup(VariableProxy* proxy, Scope* scope,
                        Scope* outer_scope_end, Scope* cache_scope,
                        bool force_context_allocation) {
  // If we have already passed the cache scope in earlier recursions, we should
  // first quickly check if the current scope uses the cache scope before
  // continuing.
  if (mode == kDeserializedScope &&
      scope->deserialized_scope_uses_external_cache()) {
    Variable* var = cache_scope->variables_.Lookup(proxy->raw_name());
    if (var != nullptr) return var;
  }

  while (true) {
    DCHECK_IMPLIES(mode == kParsedScope, !scope->is_debug_evaluate_scope_);
    // Short-cut: whenever we find a debug-evaluate scope, just look everything
    // up dynamically. Debug-evaluate doesn't properly create scope info for the
    // lookups it does. It may not have a valid 'this' declaration, and anything
    // accessed through debug-evaluate might invalidly resolve to
    // stack-allocated variables.
    // TODO(yangguo): Remove once debug-evaluate creates proper ScopeInfo for
    // the scopes in which it's evaluating.
    if (mode == kDeserializedScope &&
        V8_UNLIKELY(scope->is_debug_evaluate_scope_)) {
      DCHECK(scope->deserialized_scope_uses_external_cache() ||
             scope == cache_scope);
      return cache_scope->NonLocal(proxy->raw_name(), VariableMode::kDynamic);
    }

    // Try to find the variable in this scope.
    Variable* var;
    if (mode == kParsedScope) {
      var = scope->LookupLocal(proxy->raw_name());
    } else {
      DCHECK_EQ(mode, kDeserializedScope);
      bool external_cache = scope->deserialized_scope_uses_external_cache();
      if (!external_cache) {
        // Check the cache on each deserialized scope, up to the main cache
        // scope when we get to it (we may still have deserialized scopes
        // in-between the initial and cache scopes so we can't just check the
        // cache before the loop).
        var = scope->variables_.Lookup(proxy->raw_name());
        if (var != nullptr) return var;
      }
      var = scope->LookupInScopeInfo(proxy->raw_name(),
                                     external_cache ? cache_scope : scope);
    }

    // We found a variable and we are done. (Even if there is an 'eval' in this
    // scope which introduces the same variable again, the resulting variable
    // remains the same.)
    //
    // For sloppy eval though, we skip dynamic variable to avoid resolving to a
    // variable when the variable and proxy are in the same eval execution. The
    // variable is not available on subsequent lazy executions of functions in
    // the eval, so this avoids inner functions from looking up different
    // variables during eager and lazy compilation.
    //
    // TODO(leszeks): Maybe we want to restrict this to e.g. lookups of a proxy
    // living in a different scope to the current one, or some other
    // optimisation.
    if (var != nullptr &&
        !(scope->is_eval_scope() && var->mode() == VariableMode::kDynamic)) {
      if (mode == kParsedScope && force_context_allocation &&
          !var->is_dynamic()) {
        var->ForceContextAllocation();
      }
      return var;
    }

    if (scope->outer_scope_ == outer_scope_end) break;

    DCHECK(!scope->is_script_scope());
    if (V8_UNLIKELY(scope->is_with_scope())) {
      return LookupWith(proxy, scope, outer_scope_end, cache_scope,
                        force_context_allocation);
    }
    if (V8_UNLIKELY(
            scope->is_declaration_scope() &&
            scope->AsDeclarationScope()->sloppy_eval_can_extend_vars())) {
      return LookupSloppyEval(proxy, scope, outer_scope_end, cache_scope,
                              force_context_allocation);
    }

    force_context_allocation |= scope->is_function_scope();
    scope = scope->outer_scope_;

    // TODO(verwaest): Separate through AnalyzePartially.
    if (mode == kParsedScope && !scope->scope_info_.is_null()) {
      DCHECK_NULL(cache_scope);
      cache_scope = scope->GetNonEvalDeclarationScope();
      return Lookup<kDeserializedScope>(proxy, scope, outer_scope_end,
                                        cache_scope);
    }
  }

  // We may just be trying to find all free variables. In that case, don't
  // declare them in the outer scope.
  // TODO(marja): Separate Lookup for preparsed scopes better.
  if (mode == kParsedScope && !scope->is_script_scope()) {
    return nullptr;
  }

  // No binding has been found. Declare a variable on the global object.
  return scope->AsDeclarationScope()->DeclareDynamicGlobal(
      proxy->raw_name(), NORMAL_VARIABLE,
      mode == kDeserializedScope ? cache_scope : scope);
}

template Variable* Scope::Lookup<Scope::kParsedScope>(
    VariableProxy* proxy, Scope* scope, Scope* outer_scope_end,
    Scope* cache_scope, bool force_context_allocation);
template Variable* Scope::Lookup<Scope::kDeserializedScope>(
    VariableProxy* proxy, Scope* scope, Scope* outer_scope_end,
    Scope* cache_scope, bool force_context_allocation);

Variable* Scope::LookupWith(VariableProxy* proxy, Scope* scope,
                            Scope* outer_scope_end, Scope* cache_scope,
                            bool force_context_allocation) {
  DCHECK(scope->is_with_scope());

  Variable* var =
      scope->outer_scope_->scope_info_.is_null()
          ? Lookup<kParsedScope>(proxy, scope->outer_scope_, outer_scope_end,
                                 nullptr, force_context_allocation)
          : Lookup<kDeserializedScope>(proxy, scope->outer_scope_,
                                       outer_scope_end, cache_scope);

  if (var == nullptr) return var;

  // The current scope is a with scope, so the variable binding can not be
  // statically resolved. However, note that it was necessary to do a lookup
  // in the outer scope anyway, because if a binding exists in an outer
  // scope, the associated variable has to be marked as potentially being
  // accessed from inside of an inner with scope (the property may not be in
  // the 'with' object).
  if (!var->is_dynamic() && var->IsUnallocated()) {
    DCHECK(!scope->already_resolved_);
    var->set_is_used();
    var->ForceContextAllocation();
    if (proxy->is_assigned()) var->SetMaybeAssigned();
  }
  Scope* target_scope;
  if (scope->deserialized_scope_uses_external_cache()) {
    DCHECK_NOT_NULL(cache_scope);
    cache_scope->variables_.Remove(var);
    target_scope = cache_scope;
  } else {
    target_scope = scope;
  }
  Variable* dynamic =
      target_scope->NonLocal(proxy->raw_name(), VariableMode::kDynamic);
  dynamic->set_local_if_not_shadowed(var);
  return dynamic;
}

Variable* Scope::LookupSloppyEval(VariableProxy* proxy, Scope* scope,
                                  Scope* outer_scope_end, Scope* cache_scope,
                                  bool force_context_allocation) {
  DCHECK(scope->is_declaration_scope() &&
         scope->AsDeclarationScope()->sloppy_eval_can_extend_vars());

  // If we're compiling eval, it's possible that the outer scope is the first
  // ScopeInfo-backed scope. We use the next declaration scope as the cache for
  // this case, to avoid complexity around sloppy block function hoisting and
  // conflict detection through catch scopes in the eval.
  Scope* entry_cache = cache_scope == nullptr
                           ? scope->outer_scope()->GetNonEvalDeclarationScope()
                           : cache_scope;
  Variable* var =
      scope->outer_scope_->scope_info_.is_null()
          ? Lookup<kParsedScope>(proxy, scope->outer_scope_, outer_scope_end,
                                 nullptr, force_context_allocation)
          : Lookup<kDeserializedScope>(proxy, scope->outer_scope_,
                                       outer_scope_end, entry_cache);
  if (var == nullptr) return var;

  // We may not want to use the cache scope, change it back to the given scope
  // if necessary.
  if (!scope->deserialized_scope_uses_external_cache()) {
    // For a deserialized scope, we'll be replacing the cache_scope.
    DCHECK_IMPLIES(!scope->scope_info_.is_null(), cache_scope != nullptr);
    cache_scope = scope;
  }

  // A variable binding may have been found in an outer scope, but the current
  // scope makes a sloppy 'eval' call, so the found variable may not be the
  // correct one (the 'eval' may introduce a binding with the same name). In
  // that case, change the lookup result to reflect this situation. Only
  // scopes that can host var bindings (declaration scopes) need be considered
  // here (this excludes block and catch scopes), and variable lookups at
  // script scope are always dynamic.
  if (var->IsGlobalObjectProperty()) {
    Scope* target = cache_scope == nullptr ? scope : cache_scope;
    var = target->NonLocal(proxy->raw_name(), VariableMode::kDynamicGlobal);
  }

  if (var->is_dynamic()) return var;

  Variable* invalidated = var;
  if (cache_scope != nullptr) cache_scope->variables_.Remove(invalidated);

  Scope* target = cache_scope == nullptr ? scope : cache_scope;
  var = target->NonLocal(proxy->raw_name(), VariableMode::kDynamicLocal);
  var->set_local_if_not_shadowed(invalidated);

  return var;
}

void Scope::ResolveVariable(VariableProxy* proxy) {
  DCHECK(!proxy->is_resolved());
  Variable* var;
  if (V8_UNLIKELY(proxy->is_home_object())) {
    // VariableProxies of the home object cannot be resolved like a normal
    // variable. Consider the case of a super.property usage in heritage
    // position:
    //
    //   class C extends super.foo { m() { super.bar(); } }
    //
    // The super.foo property access is logically nested under C's class scope,
    // which also has a home object due to its own method m's usage of
    // super.bar(). However, super.foo must resolve super in C's outer scope.
    //
    // Because of the above, start resolving home objects directly at the home
    // object scope instead of the current scope.
    Scope* scope = GetHomeObjectScope();
    DCHECK_NOT_NULL(scope);
    if (scope->scope_info_.is_null()) {
      var = Lookup<kParsedScope>(proxy, scope, nullptr);
    } else {
      Scope* entry_cache = scope->deserialized_scope_uses_external_cache()
                               ? GetNonEvalDeclarationScope()
                               : scope;
      var = Lookup<kDeserializedScope>(proxy, scope, nullptr, entry_cache);
    }
  } else {
    var = Lookup<kParsedScope>(proxy, this, nullptr);
  }
  DCHECK_NOT_NULL(var);
  ResolveTo(proxy, var);
}

namespace {

void SetNeedsHoleCheck(Variable* var, VariableProxy* proxy,
                       Variable::ForceHoleInitializationFlag flag) {
  proxy->set_needs_hole_check();
  var->ForceHoleInitialization(flag);
}

void UpdateNeedsHoleCheck(Variable* var, VariableProxy* proxy, Scope* scope) {
  if (var->mode() == VariableMode::kDynamicLocal) {
    // Dynamically introduced variables never need a hole check (since they're
    // VariableMode::kVar bindings, either from var or function declarations),
    // but the variable they shadow might need a hole check, which we want to do
    // if we decide that no shadowing variable was dynamically introduced.
    DCHECK_EQ(kCreatedInitialized, var->initialization_flag());
    return UpdateNeedsHoleCheck(var->local_if_not_shadowed(), proxy, scope);
  }

  if (var->initialization_flag() == kCreatedInitialized) return;

  // It's impossible to eliminate module import hole checks here, because it's
  // unknown at compilation time whether the binding referred to in the
  // exporting module itself requires hole checks.
  if (var->location() == VariableLocation::MODULE && !var->IsExport()) {
    SetNeedsHoleCheck(var, proxy, Variable::kHasHoleCheckUseInUnknownScope);
    return;
  }

  // Check if the binding really needs an initialization check. The check
  // can be skipped in the following situation: we have a VariableMode::kLet or
  // VariableMode::kConst binding, both the Variable and the VariableProxy have
  // the same declaration scope (i.e. they are both in global code, in the same
  // function or in the same eval code), the VariableProxy is in the source
  // physically located after the initializer of the variable, and that the
  // initializer cannot be skipped due to a nonlinear scope.
  //
  // The condition on the closure scopes is a conservative check for
  // nested functions that access a binding and are called before the
  // binding is initialized:
  //   function() { f(); let x = 1; function f() { x = 2; } }
  //
  // The check cannot be skipped on non-linear scopes, namely switch
  // scopes, to ensure tests are done in cases like the following:
  //   switch (1) { case 0: let x = 2; case 1: f(x); }
  // The scope of the variable needs to be checked, in case the use is
  // in a sub-block which may be linear.
  if (var->scope()->GetClosureScope() != scope->GetClosureScope()) {
    SetNeedsHoleCheck(var, proxy,
                      Variable::kHasHoleCheckUseInDifferentClosureScope);
    return;
  }

  // We should always have valid source positions.
  DCHECK_NE(var->initializer_position(), kNoSourcePosition);
  DCHECK_NE(proxy->position(), kNoSourcePosition);

  if (var->scope()->is_nonlinear() ||
      var->initializer_position() >= proxy->position()) {
    SetNeedsHoleCheck(var, proxy, Variable::kHasHoleCheckUseInSameClosureScope);
    return;
  }
}

}  // anonymous namespace

void Scope::ResolveTo(VariableProxy* proxy, Variable* var) {
  DCHECK_NOT_NULL(var);
  UpdateNeedsHoleCheck(var, proxy, this);
  proxy->BindTo(var);
}

void Scope::ResolvePreparsedVariable(VariableProxy* proxy, Scope* scope,
                                     Scope* end) {
  // Resolve the variable in all parsed scopes to force context allocation.
  for (; scope != end; scope = scope->outer_scope_) {
    Variable* var = scope->LookupLocal(proxy->raw_name());
    if (var != nullptr) {
      var->set_is_used();
      if (!var->is_dynamic()) {
        var->ForceContextAllocation();
        if (proxy->is_assigned()) var->SetMaybeAssigned();
        return;
      }
    }
  }
}

bool Scope::ResolveVariablesRecursively(Scope* end) {
  // Lazy parsed declaration scopes are already partially analyzed. If there are
  // unresolved references remaining, they just need to be resolved in outer
  // scopes.
  if (WasLazilyParsed(this)) {
    DCHECK_EQ(variables_.occupancy(), 0);
    // Resolve in all parsed scopes except for the script scope.
    if (!end->is_script_scope()) end = end->outer_scope();

    for (VariableProxy* proxy : unresolved_list_) {
      ResolvePreparsedVariable(proxy, outer_scope(), end);
    }
  } else {
    // Resolve unresolved variables for this scope.
    for (VariableProxy* proxy : unresolved_list_) {
      ResolveVariable(proxy);
    }

    // Resolve unresolved variables for inner scopes.
    for (Scope* scope = inner_scope_; scope != nullptr;
         scope = scope->sibling_) {
      if (!scope->ResolveVariablesRecursively(end)) return false;
    }
  }
  return true;
}

bool Scope::MustAllocate(Variable* var) {
  DCHECK(var->location() != VariableLocation::MODULE);
  // Give var a read/write use if there is a chance it might be accessed
  // via an eval() call.  This is only possible if the variable has a
  // visible name.
  if (!var->raw_name()->IsEmpty() &&
      (inner_scope_calls_eval_ || is_catch_scope() || is_script_scope())) {
    var->set_is_used();
    if (inner_scope_calls_eval_ && !var->is_this()) var->SetMaybeAssigned();
  }
  CHECK(!var->has_forced_context_allocation() || var->is_used());
  // Global variables do not need to be allocated.
  return !var->IsGlobalObjectProperty() && var->is_used();
}


bool Scope::MustAllocateInContext(Variable* var) {
  // If var is accessed from an inner scope, or if there is a possibility
  // that it might be accessed from the current or an inner scope (through
  // an eval() call or a runtime with lookup), it must be allocated in the
  // context.
  //
  // Temporary variables are always stack-allocated.  Catch-bound variables are
  // always context-allocated.
  VariableMode mode = var->mode();
  if (mode == VariableMode::kTemporary) return false;
  if (is_catch_scope()) return true;
  if (is_script_scope() || is_eval_scope()) {
    if (IsLexicalVariableMode(mode)) {
      return true;
    }
  }
  return var->has_forced_context_allocation() || inner_scope_calls_eval_;
}

void Scope::AllocateStackSlot(Variable* var) {
  if (is_block_scope()) {
    outer_scope()->GetDeclarationScope()->AllocateStackSlot(var);
  } else {
    var->AllocateTo(VariableLocation::LOCAL, num_stack_slots_++);
  }
}


void Scope::AllocateHeapSlot(Variable* var) {
  var->AllocateTo(VariableLocation::CONTEXT, num_heap_slots_++);
}

void DeclarationScope::AllocateParameterLocals() {
  DCHECK(is_function_scope());

  bool has_mapped_arguments = false;
  if (arguments_ != nullptr) {
    DCHECK(!is_arrow_scope());
    if (MustAllocate(arguments_) && !has_arguments_parameter_) {
      // 'arguments' is used and does not refer to a function
      // parameter of the same name. If the arguments object
      // aliases formal parameters, we conservatively allocate
      // them specially in the loop below.
      has_mapped_arguments =
          GetArgumentsType() == CreateArgumentsType::kMappedArguments;
    } else {
      // 'arguments' is unused. Tell the code generator that it does not need to
      // allocate the arguments object by nulling out arguments_.
      arguments_ = nullptr;
    }
  }

  // The same parameter may occur multiple times in the parameters_ list.
  // If it does, and if it is not copied into the context object, it must
  // receive the highest parameter index for that parameter; thus iteration
  // order is relevant!
  for (int i = num_parameters() - 1; i >= 0; --i) {
    Variable* var = params_[i];
    DCHECK_NOT_NULL(var);
    DCHECK(!has_rest_ || var != rest_parameter());
    DCHECK_EQ(this, var->scope());
    if (has_mapped_arguments) {
      var->set_is_used();
      var->SetMaybeAssigned();
      var->ForceContextAllocation();
    }
    AllocateParameter(var, i);
  }
}

void DeclarationScope::AllocateParameter(Variable* var, int index) {
  if (!MustAllocate(var)) return;
  if (has_forced_context_allocation_for_parameters() ||
      MustAllocateInContext(var)) {
    DCHECK(var->IsUnallocated() || var->IsContextSlot());
    if (var->IsUnallocated()) AllocateHeapSlot(var);
  } else {
    DCHECK(var->IsUnallocated() || var->IsParameter());
    if (var->IsUnallocated()) {
      var->AllocateTo(VariableLocation::PARAMETER, index);
    }
  }
}

void DeclarationScope::AllocateReceiver() {
  if (!has_this_declaration()) return;
  DCHECK_NOT_NULL(receiver());
  DCHECK_EQ(receiver()->scope(), this);
  AllocateParameter(receiver(), -1);
}

void Scope::AllocateNonParameterLocal(Variable* var) {
  DCHECK_EQ(var->scope(), this);
  if (var->IsUnallocated() && MustAllocate(var)) {
    if (MustAllocateInContext(var)) {
      AllocateHeapSlot(var);
      DCHECK_IMPLIES(is_catch_scope(),
                     var->index() == Context::THROWN_OBJECT_INDEX);
    } else {
      AllocateStackSlot(var);
    }
  }
}

void Scope::AllocateNonParameterLocalsAndDeclaredGlobals() {
  if (is_declaration_scope() && AsDeclarationScope()->is_arrow_scope()) {
    // In arrow functions, allocate non-temporaries first and then all the
    // temporaries to make the local variable ordering stable when reparsing to
    // collect source positions.
    for (Variable* local : locals_) {
      if (local->mode() != VariableMode::kTemporary)
        AllocateNonParameterLocal(local);
    }

    for (Variable* local : locals_) {
      if (local->mode() == VariableMode::kTemporary)
        AllocateNonParameterLocal(local);
    }
  } else {
    for (Variable* local : locals_) {
      AllocateNonParameterLocal(local);
    }
  }

  if (is_declaration_scope()) {
    AsDeclarationScope()->AllocateLocals();
  }
}

void DeclarationScope::AllocateLocals() {
  // For now, function_ must be allocated at the very end.  If it gets
  // allocated in the context, it must be the last slot in the context,
  // because of the current ScopeInfo implementation (see
  // ScopeInfo::ScopeInfo(FunctionScope* scope) constructor).
  if (function_ != nullptr && MustAllocate(function_)) {
    AllocateNonParameterLocal(function_);
  } else {
    function_ = nullptr;
  }

  DCHECK(!has_rest_ || !MustAllocate(rest_parameter()) ||
         !rest_parameter()->IsUnallocated());

  if (new_target_ != nullptr && !MustAllocate(new_target_)) {
    new_target_ = nullptr;
  }

  NullifyRareVariableIf(RareVariable::kThisFunction, [=, this](Variable* var) {
    return !MustAllocate(var);
  });
}

void ModuleScope::AllocateModuleVariables() {
  for (const auto& it : module()->regular_imports()) {
    Variable* var = LookupLocal(it.first);
    var->AllocateTo(VariableLocation::MODULE, it.second->cell_index);
    DCHECK(!var->IsExport());
  }

  for (const auto& it : module()->regular_exports()) {
    Variable* var = LookupLocal(it.first);
    var->AllocateTo(VariableLocation::MODULE, it.second->cell_index);
    DCHECK(var->IsExport());
  }
}

// Needs to be kept in sync with ScopeInfo::UniqueIdInScript and
// SharedFunctionInfo::UniqueIdInScript.
int Scope::UniqueIdInScript() const {
  DCHECK(!is_hidden_catch_scope());
  // Script scopes start "before" the script to avoid clashing with a scope that
  // starts on character 0.
  if (is_script_scope() || scope_type() == EVAL_SCOPE ||
      scope_type() == MODULE_SCOPE) {
    return -2;
  }
  // Wrapped functions start before the function body, but after the script
  // start, to avoid clashing with a scope starting on character 0.
  if (is_wrapped_function()) {
    return -1;
  }
  if (is_declaration_scope()) {
    // Default constructors have the same start position as their parent class
    // scope. Use the next char position to distinguish this scope.
    return start_position() +
           IsDefaultConstructor(AsDeclarationScope()->function_kind());
  }
  return start_position();
}

void Scope::AllocateVariablesRecursively() {
  this->ForEach([](Scope* scope) -> Iteration {
    DCHECK(!scope->already_resolved_);
    if (WasLazilyParsed(scope)) return Iteration::kContinue;
    if (scope->sloppy_eval_can_extend_vars_) {
      scope->num_heap_slots_ = Context::MIN_CONTEXT_EXTENDED_SLOTS;
    }
    DCHECK_EQ(scope->ContextHeaderLength(), scope->num_heap_slots_);

    // Allocate variables for this scope.
    // Parameters must be allocated first, if any.
    if (scope->is_declaration_scope()) {
      scope->AsDeclarationScope()->AllocateReceiver();
      if (scope->is_function_scope()) {
        scope->AsDeclarationScope()->AllocateParameterLocals();
      }
    }
    scope->AllocateNonParameterLocalsAndDeclaredGlobals();

    // Force allocation of a context for this scope if necessary. For a 'with'
    // scope and for a function scope that makes an 'eval' call we need a
    // context, even if no local variables were statically allocated in the
    // scope. Likewise for modules and function scopes representing asm.js
    // modules. Also force a context, if the scope is stricter than the outer
    // scope.
    bool must_have_context =
        scope->is_with_scope() || scope->is_module_scope() ||
#if V8_ENABLE_WEBASSEMBLY
        scope->IsAsmModule() ||
#endif  // V8_ENABLE_WEBASSEMBLY
        scope->ForceContextForLanguageMode() ||
        (scope->is_function_scope() &&
         scope->AsDeclarationScope()->sloppy_eval_can_extend_vars()) ||
        (scope->is_block_scope() && scope->is_declaration_scope() &&
         scope->AsDeclarationScope()->sloppy_eval_can_extend_vars());

    // If we didn't allocate any locals in the local context, then we only
    // need the minimal number of slots if we must have a context.
    if (scope->num_heap_slots_ == scope->ContextHeaderLength() &&
        !must_have_context) {
      scope->num_heap_slots_ = 0;
    }

    // Allocation done.
    DCHECK(scope->num_heap_slots_ == 0 ||
           scope->num_heap_slots_ >= scope->ContextHeaderLength());
    return Iteration::kDescend;
  });
}

template <typename IsolateT>
void Scope::AllocateScopeInfosRecursively(
    IsolateT* isolate, MaybeHandle<ScopeInfo> outer_scope,
    std::unordered_map<int, Handle<ScopeInfo>>& scope_infos_to_reuse) {
  DCHECK(scope_info_.is_null());
  MaybeHandle<ScopeInfo> next_outer_scope = outer_scope;

  auto it = is_hidden_catch_scope()
                ? scope_infos_to_reuse.end()
                : scope_infos_to_reuse.find(UniqueIdInScript());
  if (it != scope_infos_to_reuse.end()) {
    scope_info_ = it->second;
    CHECK(NeedsContext());
    // The ScopeInfo chain mirrors the context chain, so we only link to the
    // next outer scope that needs a context.
    next_outer_scope = scope_info_;
    DCHECK(!scope_info_.is_null());
    DCHECK(!is_hidden_catch_scope());
    CHECK_EQ(scope_info_->scope_type(), scope_type_);
    CHECK_EQ(scope_info_->ContextLength(), num_heap_slots_);
#ifdef DEBUG
    // Consume the scope info.
    it->second = {};
#endif
  } else if (NeedsScopeInfo()) {
    scope_info_ = ScopeInfo::Create(isolate, zone(), this, outer_scope);
#ifdef DEBUG
    // Mark this ID as being used. Skip hidden scopes because they are
    // synthetic, unreusable, but hard to make unique.
    if (v8_flags.reuse_scope_infos && !is_hidden_catch_scope()) {
      scope_infos_to_reuse[UniqueIdInScript()] = {};
      DCHECK_EQ(UniqueIdInScript(), scope_info_->UniqueIdInScript());
    }
#endif
    // The ScopeInfo chain mirrors the context chain, so we only link to the
    // next outer scope that needs a context.
    if (NeedsContext()) next_outer_scope = scope_info_;
  }

  // Allocate ScopeInfos for inner scopes.
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
#ifdef DEBUG
    if (!scope->is_hidden_catch_scope()) {
      DCHECK_GT(scope->UniqueIdInScript(), UniqueIdInScript());
      DCHECK_IMPLIES(
          scope->sibling_ && !scope->sibling_->is_hidden_catch_scope(),
          scope->sibling_->UniqueIdInScript() != scope->UniqueIdInScript());
    }
#endif
    if (!scope->is_function_scope() ||
        scope->AsDeclarationScope()->ShouldEagerCompile()) {
      scope->AllocateScopeInfosRecursively(isolate, next_outer_scope,
                                           scope_infos_to_reuse);
    }
  }
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Scope::
    AllocateScopeInfosRecursively<Isolate>(
        Isolate* isolate, MaybeHandle<ScopeInfo> outer_scope,
        std::unordered_map<int, Handle<ScopeInfo>>& scope_infos_to_reuse);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Scope::
    AllocateScopeInfosRecursively<LocalIsolate>(
        LocalIsolate* isolate, MaybeHandle<ScopeInfo> outer_scope,
        std::unordered_map<int, Handle<ScopeInfo>>& scope_infos_to_reuse);

void DeclarationScope::RecalcPrivateNameContextChain() {
  // The outermost scope in a class heritage expression is marked to skip the
  // class scope during private name resolution. It is possible, however, that
  // either the class scope won't require a Context and ScopeInfo, or the
  // outermost scope in the heritage position won't. Simply copying the bit from
  // full parse into the ScopeInfo will break lazy compilation. In the former
  // case the scope that is marked to skip its outer scope will incorrectly skip
  // a different class scope than the one we intended to skip. In the latter
  // case variables resolved through an inner scope will incorrectly check the
  // class scope since we lost the skip bit from the outermost heritage scope.
  //
  // This method fixes both cases by, in outermost to innermost order, copying
  // the value of the skip bit from outer scopes that don't require a Context.
  DCHECK(needs_private_name_context_chain_recalc_);
  this->ForEach([](Scope* scope) {
    Scope* outer = scope->outer_scope();
    if (!outer) return Iteration::kDescend;
    if (!outer->NeedsContext()) {
      scope->private_name_lookup_skips_outer_class_ =
          outer->private_name_lookup_skips_outer_class();
    }
    if (!scope->is_function_scope() ||
        scope->AsDeclarationScope()->ShouldEagerCompile()) {
      return Iteration::kDescend;
    }
    return Iteration::kContinue;
  });
}

void DeclarationScope::RecordNeedsPrivateNameContextChainRecalc() {
  DCHECK_EQ(GetClosureScope(), this);
  DeclarationScope* scope;
  for (scope = this; scope != nullptr;
       scope = scope->outer_scope() != nullptr
                   ? scope->outer_scope()->GetClosureScope()
                   : nullptr) {
    if (scope->needs_private_name_context_chain_recalc_) return;
    scope->needs_private_name_context_chain_recalc_ = true;
  }
}

// static
template <typename IsolateT>
void DeclarationScope::AllocateScopeInfos(ParseInfo* info,
                                          DirectHandle<Script> script,
                                          IsolateT* isolate) {
  DeclarationScope* scope = info->literal()->scope();

  // No one else should have allocated a scope info for this scope yet.
  DCHECK(scope->scope_info_.is_null());

  MaybeHandle<ScopeInfo> outer_scope;
  if (scope->outer_scope_ != nullptr) {
    DCHECK((std::is_same<Isolate, v8::internal::Isolate>::value));
    outer_scope = scope->outer_scope_->scope_info_;
  }

  if (scope->needs_private_name_context_chain_recalc()) {
    scope->RecalcPrivateNameContextChain();
  }

  Tagged<WeakFixedArray> infos = script->infos();
  std::unordered_map<int, Handle<ScopeInfo>> scope_infos_to_reuse;
  if (v8_flags.reuse_scope_infos && infos->length() != 0) {
    Tagged<SharedFunctionInfo> sfi = *info->literal()->shared_function_info();
    Tagged<ScopeInfo> outer = sfi->HasOuterScopeInfo()
                                  ? sfi->GetOuterScopeInfo()
                                  : Tagged<ScopeInfo>();
    // Look at all inner functions whether they have scope infos that we should
    // reuse. Also look at the compiled function itself, and reuse its function
    // scope info if it exists.
    for (int i = info->literal()->function_literal_id();
         i < info->max_info_id() + 1; ++i) {
      Tagged<MaybeObject> maybe_info = infos->get(i);
      if (maybe_info.IsWeak()) {
        Tagged<Object> info = maybe_info.GetHeapObjectAssumeWeak();
        Tagged<ScopeInfo> scope_info;
        if (Is<SharedFunctionInfo>(info)) {
          Tagged<SharedFunctionInfo> sfi = Cast<SharedFunctionInfo>(info);
          if (!sfi->scope_info()->IsEmpty() &&
              sfi->scope_info()->HasContext()) {
            scope_info = sfi->scope_info();
          } else if (sfi->HasOuterScopeInfo()) {
            scope_info = sfi->GetOuterScopeInfo();
          } else {
            continue;
          }
        } else {
          scope_info = Cast<ScopeInfo>(info);
        }
        while (true) {
          if (scope_info == outer) break;
          int id = scope_info->UniqueIdInScript();
          auto it = scope_infos_to_reuse.find(id);
          if (it != scope_infos_to_reuse.end()) {
            CHECK_EQ(*it->second, scope_info);
            break;
          }
          scope_infos_to_reuse[id] = handle(scope_info, isolate);
          if (!scope_info->HasOuterScopeInfo()) break;
          scope_info = scope_info->OuterScopeInfo();
        }
      }
    }
  }

  scope->AllocateScopeInfosRecursively(isolate, outer_scope,
                                       scope_infos_to_reuse);

  // The debugger expects all shared function infos to contain a scope info.
  // Since the top-most scope will end up in a shared function info, make sure
  // it has one, even if it doesn't need a scope info.
  // TODO(yangguo): Remove this requirement.
  if (scope->scope_info_.is_null()) {
    scope->scope_info_ =
        ScopeInfo::Create(isolate, scope->zone(), scope, outer_scope);
  }

  // Ensuring that the outer script scope has a scope info avoids having
  // special case for native contexts vs other contexts.
  if (info->script_scope() && info->script_scope()->scope_info_.is_null()) {
    info->script_scope()->scope_info_ = isolate->factory()->empty_scope_info();
  }
}

template V8_EXPORT_PRIVATE void DeclarationScope::AllocateScopeInfos(
    ParseInfo* info, DirectHandle<Script> script, Isolate* isolate);
template V8_EXPORT_PRIVATE void DeclarationScope::AllocateScopeInfos(
    ParseInfo* info, DirectHandle<Script> script, LocalIsolate* isolate);

int Scope::ContextLocalCount() const {
  if (num_heap_slots() == 0) return 0;
  Variable* function =
      is_function_scope() ? AsDeclarationScope()->function_var() : nullptr;
  bool is_function_var_in_context =
      function != nullptr && function->IsContextSlot();
  return num_heap_slots() - ContextHeaderLength() -
         (is_function_var_in_context ? 1 : 0);
}

bool IsComplementaryAccessorPair(VariableMode a, VariableMode b) {
  switch (a) {
    case VariableMode::kPrivateGetterOnly:
      return b == VariableMode::kPrivateSetterOnly;
    case VariableMode::kPrivateSetterOnly:
      return b == VariableMode::kPrivateGetterOnly;
    default:
      return false;
  }
}

Variable* ClassScope::DeclarePrivateName(const AstRawString* name,
                                         VariableMode mode,
                                         IsStaticFlag is_static_flag,
                                         bool* was_added) {
  Variable* result = EnsureRareData()->private_name_map.Declare(
      zone(), this, name, mode, NORMAL_VARIABLE,
      InitializationFlag::kNeedsInitialization, MaybeAssignedFlag::kNotAssigned,
      is_static_flag, was_added);
  if (*was_added) {
    locals_.Add(result);
    has_static_private_methods_ |=
        (result->is_static() &&
         IsPrivateMethodOrAccessorVariableMode(result->mode()));
  } else if (IsComplementaryAccessorPair(result->mode(), mode) &&
             result->is_static_flag() == is_static_flag) {
    *was_added = true;
    result->set_mode(VariableMode::kPrivateGetterAndSetter);
  }
  result->ForceContextAllocation();
  return result;
}

Variable* ClassScope::LookupLocalPrivateName(const AstRawString* name) {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr) {
    return nullptr;
  }
  return rare_data->private_name_map.Lookup(name);
}

UnresolvedList::Iterator ClassScope::GetUnresolvedPrivateNameTail() {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr) {
    return UnresolvedList::Iterator();
  }
  return rare_data->unresolved_private_names.end();
}

void ClassScope::ResetUnresolvedPrivateNameTail(UnresolvedList::Iterator tail) {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr ||
      rare_data->unresolved_private_names.end() == tail) {
    return;
  }

  bool tail_is_empty = tail == UnresolvedList::Iterator();
  if (tail_is_empty) {
    // If the saved tail is empty, the list used to be empty, so clear it.
    rare_data->unresolved_private_names.Clear();
  } else {
    rare_data->unresolved_private_names.Rewind(tail);
  }
}

void ClassScope::MigrateUnresolvedPrivateNameTail(
    AstNodeFactory* ast_node_factory, UnresolvedList::Iterator tail) {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr ||
      rare_data->unresolved_private_names.end() == tail) {
    return;
  }
  UnresolvedList migrated_names;

  // If the saved tail is empty, the list used to be empty, so we should
  // migrate everything after the head.
  bool tail_is_empty = tail == UnresolvedList::Iterator();
  UnresolvedList::Iterator it =
      tail_is_empty ? rare_data->unresolved_private_names.begin() : tail;

  for (; it != rare_data->unresolved_private_names.end(); ++it) {
    VariableProxy* proxy = *it;
    VariableProxy* copy = ast_node_factory->CopyVariableProxy(proxy);
    migrated_names.Add(copy);
  }

  // Replace with the migrated copies.
  if (tail_is_empty) {
    rare_data->unresolved_private_names.Clear();
  } else {
    rare_data->unresolved_private_names.Rewind(tail);
  }
  rare_data->unresolved_private_names.Append(std::move(migrated_names));
}

Variable* ClassScope::LookupPrivateNameInScopeInfo(const AstRawString* name) {
  DCHECK(!scope_info_.is_null());
  DCHECK_NULL(LookupLocalPrivateName(name));
  DisallowGarbageCollection no_gc;

  VariableLookupResult lookup_result;
  int index = scope_info_->ContextSlotIndex(name->string(), &lookup_result);
  if (index < 0) {
    return nullptr;
  }

  DCHECK(IsImmutableLexicalOrPrivateVariableMode(lookup_result.mode));
  DCHECK_EQ(lookup_result.init_flag, InitializationFlag::kNeedsInitialization);
  DCHECK_EQ(lookup_result.maybe_assigned_flag, MaybeAssignedFlag::kNotAssigned);

  // Add the found private name to the map to speed up subsequent
  // lookups for the same name.
  bool was_added;
  Variable* var = DeclarePrivateName(name, lookup_result.mode,
                                     lookup_result.is_static_flag, &was_added);
  DCHECK(was_added);
  var->AllocateTo(VariableLocation::CONTEXT, index);
  return var;
}

Variable* ClassScope::LookupPrivateName(VariableProxy* proxy) {
  DCHECK(!proxy->is_resolved());

  for (PrivateNameScopeIterator scope_iter(this); !scope_iter.Done();
       scope_iter.Next()) {
    ClassScope* scope = scope_iter.GetScope();
    // Try finding it in the private name map first, if it can't be found,
    // try the deserialized scope info.
    Variable* var = scope->LookupLocalPrivateName(proxy->raw_name());
    if (var == nullptr && !scope->scope_info_.is_null()) {
      var = scope->LookupPrivateNameInScopeInfo(proxy->raw_name());
    }
    if (var != nullptr) {
      return var;
    }
  }
  return nullptr;
}

bool ClassScope::ResolvePrivateNames(ParseInfo* info) {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr || rare_data->unresolved_private_names.is_empty()) {
    return true;
  }

  UnresolvedList& list = rare_data->unresolved_private_names;
  for (VariableProxy* proxy : list) {
    Variable* var = LookupPrivateName(proxy);
    if (var == nullptr) {
      // It's only possible to fail to resolve private names here if
      // this is at the top level or the private name is accessed through eval.
      DCHECK(info->flags().is_eval() || outer_scope_->is_script_scope());
      Scanner::Location loc = proxy->location();
      info->pending_error_handler()->ReportMessageAt(
          loc.beg_pos, loc.end_pos,
          MessageTemplate::kInvalidPrivateFieldResolution, proxy->raw_name());
      return false;
    } else {
      proxy->BindTo(var);
    }
  }

  // By now all unresolved private names should be resolved so
  // clear the list.
  list.Clear();
  return true;
}

VariableProxy* ClassScope::ResolvePrivateNamesPartially() {
  RareData* rare_data = GetRareData();
  if (rare_data == nullptr || rare_data->unresolved_private_names.is_empty()) {
    return nullptr;
  }

  PrivateNameScopeIterator private_name_scope_iter(this);
  private_name_scope_iter.Next();
  UnresolvedList& unresolved = rare_data->unresolved_private_names;
  bool has_private_names = rare_data->private_name_map.capacity() > 0;

  // If the class itself does not have private names, nor does it have
  // an outer private name scope, then we are certain any private name access
  // inside cannot be resolved.
  if (!has_private_names && private_name_scope_iter.Done() &&
      !unresolved.is_empty()) {
    return unresolved.first();
  }

  for (VariableProxy* proxy = unresolved.first(); proxy != nullptr;) {
    DCHECK(proxy->IsPrivateName());
    VariableProxy* next = proxy->next_unresolved();
    unresolved.Remove(proxy);
    Variable* var = nullptr;

    // If we can find private name in the current class scope, we can bind
    // them immediately because it's going to shadow any outer private names.
    if (has_private_names) {
      var = LookupLocalPrivateName(proxy->raw_name());
      if (var != nullptr) {
        var->set_is_used();
        proxy->BindTo(var);
        // If the variable being accessed is a static private method, we need to
        // save the class variable in the context to check that the receiver is
        // the class during runtime.
        has_explicit_static_private_methods_access_ |=
            (var->is_static() &&
             IsPrivateMethodOrAccessorVariableMode(var->mode()));
      }
    }

    // If the current scope does not have declared private names,
    // try looking from the outer class scope later.
    if (var == nullptr) {
      // There's no outer private name scope so we are certain that the variable
      // cannot be resolved later.
      if (private_name_scope_iter.Done()) {
        return proxy;
      }

      // The private name may be found later in the outer private name scope, so
      // push it to the outer scope.
      private_name_scope_iter.AddUnresolvedPrivateName(proxy);
    }

    proxy = next;
  }

  DCHECK(unresolved.is_empty());
  return nullptr;
}

Variable* ClassScope::DeclareBrandVariable(AstValueFactory* ast_value_factory,
                                           IsStaticFlag is_static_flag,
                                           int class_token_pos) {
  DCHECK_IMPLIES(GetRareData() != nullptr, GetRareData()->brand == nullptr);
  bool was_added;
  Variable* brand = Declare(zone(), ast_value_factory->dot_brand_string(),
                            VariableMode::kConst, NORMAL_VARIABLE,
                            InitializationFlag::kNeedsInitialization,
                            MaybeAssignedFlag::kNotAssigned, &was_added);
  DCHECK(was_added);
  brand->set_is_static_flag(is_static_flag);
  brand->ForceContextAllocation();
  brand->set_is_used();
  EnsureRareData()->brand = brand;
  brand->set_initializer_position(class_token_pos);
  return brand;
}

Variable* ClassScope::DeclareClassVariable(AstValueFactory* ast_value_factory,
                                           const AstRawString* name,
                                           int class_token_pos) {
  DCHECK_NULL(class_variable_);
  DCHECK_NOT_NULL(name);
  bool was_added;
  class_variable_ =
      Declare(zone(), name->IsEmpty() ? ast_value_factory->dot_string() : name,
              VariableMode::kConst, NORMAL_VARIABLE,
              InitializationFlag::kNeedsInitialization,
              MaybeAssignedFlag::kMaybeAssigned, &was_added);
  DCHECK(was_added);
  class_variable_->set_initializer_position(class_token_pos);
  return class_variable_;
}

PrivateNameScopeIterator::PrivateNameScopeIterator(Scope* start)
    : start_scope_(start), current_scope_(start) {
  if (!start->is_class_scope() || start->AsClassScope()->IsParsingHeritage()) {
    Next();
  }
}

void PrivateNameScopeIterator::Next() {
  DCHECK(!Done());
  Scope* inner = current_scope_;
  Scope* scope = inner->outer_scope();
  while (scope != nullptr) {
    if (scope->is_class_scope()) {
      if (!inner->private_name_lookup_skips_outer_class()) {
        current_scope_ = scope;
        return;
      }
      skipped_any_scopes_ = true;
    }
    inner = scope;
    scope = scope->outer_scope();
  }
  current_scope_ = nullptr;
}

void PrivateNameScopeIterator::AddUnresolvedPrivateName(VariableProxy* proxy) {
  // During a reparse, current_scope_->already_resolved_ may be true here,
  // because the class scope is deserialized while the function scope inside may
  // be new.
  DCHECK(!proxy->is_resolved());
  DCHECK(proxy->IsPrivateName());

  // Use dynamic lookup for top-level scopes in debug-evaluate.
  if (Done()) {
    start_scope_->ForceDynamicLookup(proxy);
    return;
  }

  GetScope()->EnsureRareData()->unresolved_private_names.Add(proxy);
  // Any closure scope that contain uses of private names that skips over a
  // class scope due to heritage expressions need private name context chain
  // recalculation, since not all scopes require a Context or ScopeInfo. See
  // comment in DeclarationScope::RecalcPrivateNameContextChain.
  if (V8_UNLIKELY(skipped_any_scopes_)) {
    start_scope_->GetClosureScope()->RecordNeedsPrivateNameContextChainRecalc();
  }
}

}  // namespace internal
}  // namespace v8
