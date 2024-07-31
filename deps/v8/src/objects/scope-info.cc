// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/scope-info.h"

#include <stdlib.h>

#include "src/ast/scopes.h"
#include "src/ast/variables.h"
#include "src/init/bootstrapper.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/scope-info-inl.h"
#include "src/objects/string-set-inl.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#ifdef DEBUG
bool ScopeInfo::Equals(Tagged<ScopeInfo> other,
                       bool is_live_edit_compare) const {
  if (length() != other->length()) return false;
  for (int index = 0; index < length(); ++index) {
    if (is_live_edit_compare && HasPositionInfo() &&
        index >= PositionInfoIndex() && index <= PositionInfoIndex() + 1) {
      continue;
    }
    Tagged<Object> entry = get(index);
    Tagged<Object> other_entry = other->get(index);
    if (IsSmi(entry)) {
      if (entry != other_entry) return false;
    } else {
      if (HeapObject::cast(entry)->map()->instance_type() !=
          HeapObject::cast(other_entry)->map()->instance_type()) {
        return false;
      }
      if (IsString(entry)) {
        if (!String::cast(entry)->Equals(String::cast(other_entry))) {
          return false;
        }
      } else if (IsScopeInfo(entry)) {
        if (!is_live_edit_compare && !ScopeInfo::cast(entry)->Equals(
                                         ScopeInfo::cast(other_entry), false)) {
          return false;
        }
      } else if (IsSourceTextModuleInfo(entry)) {
        if (!is_live_edit_compare &&
            !SourceTextModuleInfo::cast(entry)->Equals(
                SourceTextModuleInfo::cast(other_entry))) {
          return false;
        }
      } else if (IsOddball(entry)) {
        if (Oddball::cast(entry)->kind() !=
            Oddball::cast(other_entry)->kind()) {
          return false;
        }
      } else {
        UNREACHABLE();
      }
    }
  }
  return true;
}
#endif

// static
template <typename IsolateT>
Handle<ScopeInfo> ScopeInfo::Create(IsolateT* isolate, Zone* zone, Scope* scope,
                                    MaybeHandle<ScopeInfo> outer_scope) {
  // Collect variables.
  int context_local_count = 0;
  int module_vars_count = 0;
  // Stack allocated block scope variables are allocated in the parent
  // declaration scope, but are recorded in the block scope's scope info. First
  // slot index indicates at which offset a particular scope starts in the
  // parent declaration scope.
  for (Variable* var : *scope->locals()) {
    switch (var->location()) {
      case VariableLocation::CONTEXT:
      case VariableLocation::REPL_GLOBAL:
        context_local_count++;
        break;
      case VariableLocation::MODULE:
        module_vars_count++;
        break;
      default:
        break;
    }
  }
  // Determine use and location of the "this" binding if it is present.
  VariableAllocationInfo receiver_info;
  if (scope->is_declaration_scope() &&
      scope->AsDeclarationScope()->has_this_declaration()) {
    Variable* var = scope->AsDeclarationScope()->receiver();
    if (!var->is_used()) {
      receiver_info = VariableAllocationInfo::UNUSED;
    } else if (var->IsContextSlot()) {
      receiver_info = VariableAllocationInfo::CONTEXT;
    } else {
      DCHECK(var->IsParameter());
      receiver_info = VariableAllocationInfo::STACK;
    }
  } else {
    receiver_info = VariableAllocationInfo::NONE;
  }

  DCHECK(module_vars_count == 0 || scope->is_module_scope());

  // Make sure we allocate the correct amount.
  DCHECK_EQ(scope->ContextLocalCount(), context_local_count);

  // If the number of locals is small, we inline directly
  // in the scope info object.
  bool has_inlined_local_names =
      context_local_count < kScopeInfoMaxInlinedLocalNamesSize;

  const bool has_new_target =
      scope->is_declaration_scope() &&
      scope->AsDeclarationScope()->new_target_var() != nullptr;
  // TODO(cbruni): Don't always waste a field for the inferred name.
  const bool has_inferred_function_name = scope->is_function_scope();

  // Determine use and location of the function variable if it is present.
  VariableAllocationInfo function_name_info;
  if (scope->is_function_scope()) {
    if (scope->AsDeclarationScope()->function_var() != nullptr) {
      Variable* var = scope->AsDeclarationScope()->function_var();
      if (!var->is_used()) {
        function_name_info = VariableAllocationInfo::UNUSED;
      } else if (var->IsContextSlot()) {
        function_name_info = VariableAllocationInfo::CONTEXT;
      } else {
        DCHECK(var->IsStackLocal());
        function_name_info = VariableAllocationInfo::STACK;
      }
    } else {
      // Always reserve space for the debug name in the scope info.
      function_name_info = VariableAllocationInfo::UNUSED;
    }
  } else if (scope->is_module_scope() || scope->is_script_scope() ||
             scope->is_eval_scope()) {
    // Always reserve space for the debug name in the scope info.
    function_name_info = VariableAllocationInfo::UNUSED;
  } else {
    function_name_info = VariableAllocationInfo::NONE;
  }

  const bool has_brand =
      scope->is_class_scope()
          ? scope->AsClassScope()->brand() != nullptr
          : scope->IsConstructorScope() &&
                scope->AsDeclarationScope()->class_scope_has_private_brand();
  const bool should_save_class_variable_index =
      scope->is_class_scope()
          ? scope->AsClassScope()->should_save_class_variable_index()
          : false;
  const bool has_function_name =
      function_name_info != VariableAllocationInfo::NONE;
  const bool has_position_info = NeedsPositionInfo(scope->scope_type());
  const int parameter_count =
      scope->is_declaration_scope()
          ? scope->AsDeclarationScope()->num_parameters()
          : 0;
  const bool has_outer_scope_info = !outer_scope.is_null();

  Handle<SourceTextModuleInfo> module_info;
  if (scope->is_module_scope()) {
    module_info = SourceTextModuleInfo::New(isolate, zone,
                                            scope->AsModuleScope()->module());
  }

// Make sure the Fields enum agrees with Torque-generated offsets.
#define ASSERT_MATCHED_FIELD(name) \
  static_assert(OffsetOfElementAt(k##name) == k##name##Offset);
  FOR_EACH_SCOPE_INFO_NUMERIC_FIELD(ASSERT_MATCHED_FIELD)
#undef ASSERT_MATCHED_FIELD

  const int local_names_container_size =
      has_inlined_local_names ? context_local_count : 1;

  const int length = kVariablePartIndex + local_names_container_size +
                     context_local_count +
                     (should_save_class_variable_index ? 1 : 0) +
                     (has_function_name ? kFunctionNameEntries : 0) +
                     (has_inferred_function_name ? 1 : 0) +
                     (has_position_info ? kPositionInfoEntries : 0) +
                     (has_outer_scope_info ? 1 : 0) +
                     (scope->is_module_scope()
                          ? 2 + kModuleVariableEntryLength * module_vars_count
                          : 0);

  // Create hash table if local names are not inlined.
  Handle<NameToIndexHashTable> local_names_hashtable;
  if (!has_inlined_local_names) {
    local_names_hashtable = NameToIndexHashTable::New(
        isolate, context_local_count, AllocationType::kOld);
  }

  Handle<ScopeInfo> scope_info_handle =
      isolate->factory()->NewScopeInfo(length);
  int index = kVariablePartIndex;
  {
    DisallowGarbageCollection no_gc;
    Tagged<ScopeInfo> scope_info = *scope_info_handle;
    WriteBarrierMode mode = scope_info->GetWriteBarrierMode(no_gc);

    bool has_simple_parameters = false;
    bool is_asm_module = false;
    bool sloppy_eval_can_extend_vars = false;
    if (scope->is_function_scope()) {
      DeclarationScope* function_scope = scope->AsDeclarationScope();
      has_simple_parameters = function_scope->has_simple_parameters();
#if V8_ENABLE_WEBASSEMBLY
      is_asm_module = function_scope->is_asm_module();
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    FunctionKind function_kind = FunctionKind::kNormalFunction;
    if (scope->is_declaration_scope()) {
      function_kind = scope->AsDeclarationScope()->function_kind();
      sloppy_eval_can_extend_vars =
          scope->AsDeclarationScope()->sloppy_eval_can_extend_vars();
    }

    // Encode the flags.
    int flags =
        ScopeTypeBits::encode(scope->scope_type()) |
        SloppyEvalCanExtendVarsBit::encode(sloppy_eval_can_extend_vars) |
        LanguageModeBit::encode(scope->language_mode()) |
        DeclarationScopeBit::encode(scope->is_declaration_scope()) |
        ReceiverVariableBits::encode(receiver_info) |
        ClassScopeHasPrivateBrandBit::encode(has_brand) |
        HasSavedClassVariableBit::encode(should_save_class_variable_index) |
        HasNewTargetBit::encode(has_new_target) |
        FunctionVariableBits::encode(function_name_info) |
        HasInferredFunctionNameBit::encode(has_inferred_function_name) |
        IsAsmModuleBit::encode(is_asm_module) |
        HasSimpleParametersBit::encode(has_simple_parameters) |
        FunctionKindBits::encode(function_kind) |
        HasOuterScopeInfoBit::encode(has_outer_scope_info) |
        IsDebugEvaluateScopeBit::encode(scope->is_debug_evaluate_scope()) |
        ForceContextAllocationBit::encode(
            scope->ForceContextForLanguageMode()) |
        PrivateNameLookupSkipsOuterClassBit::encode(
            scope->private_name_lookup_skips_outer_class()) |
        HasContextExtensionSlotBit::encode(scope->HasContextExtensionSlot()) |
        IsReplModeScopeBit::encode(scope->is_repl_mode_scope()) |
        HasLocalsBlockListBit::encode(false);
    scope_info->set_flags(flags);

    scope_info->set_parameter_count(parameter_count);
    scope_info->set_context_local_count(context_local_count);

    if (scope->is_module_scope()) {
      scope_info->set_module_variable_count(module_vars_count);
      ++index;
    }
    if (!has_inlined_local_names) {
      scope_info->set_context_local_names_hashtable(*local_names_hashtable);
    }

    // Add context locals' names and info, module variables' names and info.
    // Context locals are added using their index.
    int context_local_base = index;
    int context_local_info_base =
        context_local_base + local_names_container_size;
    int module_var_entry = scope_info->ModuleVariablesIndex();

    for (Variable* var : *scope->locals()) {
      switch (var->location()) {
        case VariableLocation::CONTEXT:
        case VariableLocation::REPL_GLOBAL: {
          // Due to duplicate parameters, context locals aren't guaranteed to
          // come in order.
          int local_index = var->index() - scope->ContextHeaderLength();
          DCHECK_LE(0, local_index);
          DCHECK_LT(local_index, context_local_count);
          uint32_t info =
              VariableModeBits::encode(var->mode()) |
              InitFlagBit::encode(var->initialization_flag()) |
              MaybeAssignedFlagBit::encode(var->maybe_assigned()) |
              ParameterNumberBits::encode(ParameterNumberBits::kMax) |
              IsStaticFlagBit::encode(var->is_static_flag());
          if (has_inlined_local_names) {
            scope_info->set(context_local_base + local_index, *var->name(),
                            mode);
          } else {
            Handle<NameToIndexHashTable> new_table = NameToIndexHashTable::Add(
                isolate, local_names_hashtable, var->name(), local_index);
            DCHECK_EQ(*new_table, *local_names_hashtable);
            USE(new_table);
          }
          scope_info->set(context_local_info_base + local_index,
                          Smi::FromInt(info));
          break;
        }
        case VariableLocation::MODULE: {
          scope_info->set(
              module_var_entry +
                  TorqueGeneratedModuleVariableOffsets::kNameOffset /
                      kTaggedSize,
              *var->name(), mode);
          scope_info->set(
              module_var_entry +
                  TorqueGeneratedModuleVariableOffsets::kIndexOffset /
                      kTaggedSize,
              Smi::FromInt(var->index()));
          uint32_t properties =
              VariableModeBits::encode(var->mode()) |
              InitFlagBit::encode(var->initialization_flag()) |
              MaybeAssignedFlagBit::encode(var->maybe_assigned()) |
              ParameterNumberBits::encode(ParameterNumberBits::kMax) |
              IsStaticFlagBit::encode(var->is_static_flag());
          scope_info->set(
              module_var_entry +
                  TorqueGeneratedModuleVariableOffsets::kPropertiesOffset /
                      kTaggedSize,
              Smi::FromInt(properties));
          module_var_entry += kModuleVariableEntryLength;
          break;
        }
        default:
          break;
      }
    }

    if (scope->is_declaration_scope()) {
      // Mark contexts slots with the parameter number they represent. We walk
      // the list of parameters. That can include duplicate entries if a
      // parameter name is repeated. By walking upwards, we'll automatically
      // mark the context slot with the highest parameter number that uses this
      // variable. That will be the parameter number that is represented by the
      // context slot. All lower parameters will only be available on the stack
      // through the arguments object.
      for (int i = 0; i < parameter_count; i++) {
        Variable* parameter = scope->AsDeclarationScope()->parameter(i);
        if (parameter->location() != VariableLocation::CONTEXT) continue;
        int param_index = parameter->index() - scope->ContextHeaderLength();
        int info_index = context_local_info_base + param_index;
        int info = Smi::ToInt(scope_info->get(info_index));
        info = ParameterNumberBits::update(info, i);
        scope_info->set(info_index, Smi::FromInt(info));
      }
    }

    // Advance past local names and local names info.
    index += local_names_container_size + context_local_count;

    DCHECK_EQ(index, scope_info->SavedClassVariableInfoIndex());
    // If the scope is a class scope and has used static private methods, save
    // the context slot index of the class variable.
    // Store the class variable index.
    if (should_save_class_variable_index) {
      Variable* class_variable = scope->AsClassScope()->class_variable();
      DCHECK_EQ(class_variable->location(), VariableLocation::CONTEXT);
      int local_index;
      if (has_inlined_local_names) {
        local_index = class_variable->index();
      } else {
        Handle<Name> name = class_variable->name();
        InternalIndex entry = local_names_hashtable->FindEntry(isolate, name);
        local_index = entry.as_int();
      }
      scope_info->set(index++, Smi::FromInt(local_index));
    }

    // If present, add the function variable name and its index.
    DCHECK_EQ(index, scope_info->FunctionVariableInfoIndex());
    if (has_function_name) {
      Variable* var = scope->AsDeclarationScope()->function_var();
      int var_index = -1;
      Tagged<Object> name = Smi::zero();
      if (var != nullptr) {
        var_index = var->index();
        name = *var->name();
      }
      scope_info->set(index++, name, mode);
      scope_info->set(index++, Smi::FromInt(var_index));
      DCHECK(function_name_info != VariableAllocationInfo::CONTEXT ||
             var_index == scope_info->ContextLength() - 1);
    }

    DCHECK_EQ(index, scope_info->InferredFunctionNameIndex());
    if (has_inferred_function_name) {
      // The inferred function name is taken from the SFI.
      index++;
    }

    DCHECK_EQ(index, scope_info->PositionInfoIndex());
    if (has_position_info) {
      scope_info->set(index++, Smi::FromInt(scope->start_position()));
      scope_info->set(index++, Smi::FromInt(scope->end_position()));
    }

    // If present, add the outer scope info.
    DCHECK(index == scope_info->OuterScopeInfoIndex());
    if (has_outer_scope_info) {
      scope_info->set(index++, *outer_scope.ToHandleChecked(), mode);
    }

    // Module-specific information (only for module scopes).
    if (scope->is_module_scope()) {
      DCHECK_EQ(index, scope_info->ModuleInfoIndex());
      scope_info->set(index++, *module_info);
      DCHECK_EQ(index, scope_info->ModuleVariablesIndex());
      // The variable entries themselves have already been written above.
      index += kModuleVariableEntryLength * module_vars_count;
    }
  }

  DCHECK_EQ(index, scope_info_handle->length());
  DCHECK_EQ(parameter_count, scope_info_handle->ParameterCount());
  DCHECK_EQ(scope->num_heap_slots(), scope_info_handle->ContextLength());

  return scope_info_handle;
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<ScopeInfo> ScopeInfo::Create(Isolate* isolate, Zone* zone,
                                        Scope* scope,
                                        MaybeHandle<ScopeInfo> outer_scope);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<ScopeInfo> ScopeInfo::Create(LocalIsolate* isolate, Zone* zone,
                                        Scope* scope,
                                        MaybeHandle<ScopeInfo> outer_scope);

// static
Handle<ScopeInfo> ScopeInfo::CreateForWithScope(
    Isolate* isolate, MaybeHandle<ScopeInfo> outer_scope) {
  const bool has_outer_scope_info = !outer_scope.is_null();
  const int length = kVariablePartIndex + (has_outer_scope_info ? 1 : 0);

  Factory* factory = isolate->factory();
  Handle<ScopeInfo> scope_info = factory->NewScopeInfo(length);

  // Encode the flags.
  int flags =
      ScopeTypeBits::encode(WITH_SCOPE) |
      SloppyEvalCanExtendVarsBit::encode(false) |
      LanguageModeBit::encode(LanguageMode::kSloppy) |
      DeclarationScopeBit::encode(false) |
      ReceiverVariableBits::encode(VariableAllocationInfo::NONE) |
      ClassScopeHasPrivateBrandBit::encode(false) |
      HasSavedClassVariableBit::encode(false) | HasNewTargetBit::encode(false) |
      FunctionVariableBits::encode(VariableAllocationInfo::NONE) |
      IsAsmModuleBit::encode(false) | HasSimpleParametersBit::encode(true) |
      FunctionKindBits::encode(FunctionKind::kNormalFunction) |
      HasOuterScopeInfoBit::encode(has_outer_scope_info) |
      IsDebugEvaluateScopeBit::encode(false) |
      ForceContextAllocationBit::encode(false) |
      PrivateNameLookupSkipsOuterClassBit::encode(false) |
      HasContextExtensionSlotBit::encode(true) |
      IsReplModeScopeBit::encode(false) | HasLocalsBlockListBit::encode(false);
  scope_info->set_flags(flags);

  scope_info->set_parameter_count(0);
  scope_info->set_context_local_count(0);

  int index = kVariablePartIndex;
  DCHECK_EQ(index, scope_info->FunctionVariableInfoIndex());
  DCHECK_EQ(index, scope_info->InferredFunctionNameIndex());
  DCHECK_EQ(index, scope_info->PositionInfoIndex());
  DCHECK(index == scope_info->OuterScopeInfoIndex());
  if (has_outer_scope_info) {
    scope_info->set(index++, *outer_scope.ToHandleChecked());
  }
  DCHECK_EQ(index, scope_info->length());
  DCHECK_EQ(0, scope_info->ParameterCount());
  DCHECK_EQ(scope_info->ContextHeaderLength(), scope_info->ContextLength());
  return scope_info;
}

// static
Handle<ScopeInfo> ScopeInfo::CreateGlobalThisBinding(Isolate* isolate) {
  return CreateForBootstrapping(isolate, BootstrappingType::kScript);
}

// static
Handle<ScopeInfo> ScopeInfo::CreateForEmptyFunction(Isolate* isolate) {
  return CreateForBootstrapping(isolate, BootstrappingType::kFunction);
}

// static
Handle<ScopeInfo> ScopeInfo::CreateForNativeContext(Isolate* isolate) {
  return CreateForBootstrapping(isolate, BootstrappingType::kNative);
}

// static
Handle<ScopeInfo> ScopeInfo::CreateForShadowRealmNativeContext(
    Isolate* isolate) {
  return CreateForBootstrapping(isolate, BootstrappingType::kShadowRealm);
}

// static
Handle<ScopeInfo> ScopeInfo::CreateForBootstrapping(Isolate* isolate,
                                                    BootstrappingType type) {
  const int parameter_count = 0;
  const bool is_empty_function = type == BootstrappingType::kFunction;
  const bool is_native_context = (type == BootstrappingType::kNative) ||
                                 (type == BootstrappingType::kShadowRealm);
  const bool is_script = type == BootstrappingType::kScript;
  const bool is_shadow_realm = type == BootstrappingType::kShadowRealm;
  const bool has_const_tracking_let_side_data = is_script;
  const int context_local_count =
      is_empty_function || is_native_context ? 0 : 1;
  const bool has_inferred_function_name = is_empty_function;
  const bool has_position_info = true;
  // NOTE: Local names are always inlined here, since context_local_count < 2.
  DCHECK_LT(context_local_count, kScopeInfoMaxInlinedLocalNamesSize);
  const int length = kVariablePartIndex + 2 * context_local_count +
                     (is_empty_function ? kFunctionNameEntries : 0) +
                     (has_inferred_function_name ? 1 : 0) +
                     (has_position_info ? kPositionInfoEntries : 0);

  Factory* factory = isolate->factory();
  Handle<ScopeInfo> scope_info =
      factory->NewScopeInfo(length, AllocationType::kReadOnly);
  DisallowGarbageCollection _nogc;
  // Encode the flags.
  DCHECK_IMPLIES(is_shadow_realm || is_script, !is_empty_function);
  int flags =
      ScopeTypeBits::encode(
          is_empty_function
              ? FUNCTION_SCOPE
              : (is_shadow_realm ? SHADOW_REALM_SCOPE : SCRIPT_SCOPE)) |
      SloppyEvalCanExtendVarsBit::encode(false) |
      LanguageModeBit::encode(LanguageMode::kSloppy) |
      DeclarationScopeBit::encode(true) |
      ReceiverVariableBits::encode(is_script ? VariableAllocationInfo::CONTEXT
                                             : VariableAllocationInfo::UNUSED) |
      ClassScopeHasPrivateBrandBit::encode(false) |
      HasSavedClassVariableBit::encode(false) | HasNewTargetBit::encode(false) |
      FunctionVariableBits::encode(is_empty_function
                                       ? VariableAllocationInfo::UNUSED
                                       : VariableAllocationInfo::NONE) |
      HasInferredFunctionNameBit::encode(has_inferred_function_name) |
      IsAsmModuleBit::encode(false) | HasSimpleParametersBit::encode(true) |
      FunctionKindBits::encode(FunctionKind::kNormalFunction) |
      HasOuterScopeInfoBit::encode(false) |
      IsDebugEvaluateScopeBit::encode(false) |
      ForceContextAllocationBit::encode(false) |
      PrivateNameLookupSkipsOuterClassBit::encode(false) |
      HasContextExtensionSlotBit::encode(is_native_context ||
                                         has_const_tracking_let_side_data) |
      IsReplModeScopeBit::encode(false) | HasLocalsBlockListBit::encode(false);
  Tagged<ScopeInfo> raw_scope_info = *scope_info;
  raw_scope_info->set_flags(flags);
  raw_scope_info->set_parameter_count(parameter_count);
  raw_scope_info->set_context_local_count(context_local_count);

  int index = kVariablePartIndex;

  // Here we add info for context-allocated "this".
  DCHECK_EQ(index, raw_scope_info->ContextLocalNamesIndex());
  ReadOnlyRoots roots(isolate);
  if (context_local_count) {
    raw_scope_info->set(index++, roots.this_string());
  }
  DCHECK_EQ(index, raw_scope_info->ContextLocalInfosIndex());
  if (context_local_count > 0) {
    const uint32_t value =
        VariableModeBits::encode(VariableMode::kConst) |
        InitFlagBit::encode(kCreatedInitialized) |
        MaybeAssignedFlagBit::encode(kNotAssigned) |
        ParameterNumberBits::encode(ParameterNumberBits::kMax) |
        IsStaticFlagBit::encode(IsStaticFlag::kNotStatic);
    raw_scope_info->set(index++, Smi::FromInt(value));
  }

  DCHECK_EQ(index, raw_scope_info->FunctionVariableInfoIndex());
  if (is_empty_function) {
    raw_scope_info->set(index++, roots.empty_string());
    raw_scope_info->set(index++, Smi::zero());
  }
  DCHECK_EQ(index, raw_scope_info->InferredFunctionNameIndex());
  if (has_inferred_function_name) {
    raw_scope_info->set(index++, roots.empty_string());
  }
  DCHECK_EQ(index, raw_scope_info->PositionInfoIndex());
  // Store dummy position to be in sync with the {scope_type}.
  raw_scope_info->set(index++, Smi::zero());
  raw_scope_info->set(index++, Smi::zero());
  DCHECK_EQ(index, raw_scope_info->OuterScopeInfoIndex());
  DCHECK_EQ(index, raw_scope_info->length());
  DCHECK_EQ(raw_scope_info->ParameterCount(), parameter_count);
  if (is_empty_function || is_native_context) {
    DCHECK_EQ(raw_scope_info->ContextLength(), 0);
  } else {
    DCHECK_EQ(raw_scope_info->ContextLength(),
              raw_scope_info->ContextHeaderLength() + 1);
  }

  return scope_info;
}

Tagged<Object> ScopeInfo::get(int index) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return get(cage_base, index);
}

Tagged<Object> ScopeInfo::get(PtrComprCageBase cage_base, int index) const {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  return TaggedField<Object>::Relaxed_Load(cage_base, *this,
                                           OffsetOfElementAt(index));
}

void ScopeInfo::set(int index, Tagged<Smi> value) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  DCHECK(IsSmi(Tagged<Object>(value)));
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(*this, offset, value);
}

void ScopeInfo::set(int index, Tagged<Object> value, WriteBarrierMode mode) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

void ScopeInfo::CopyElements(Isolate* isolate, int dst_index,
                             Tagged<ScopeInfo> src, int src_index, int len,
                             WriteBarrierMode mode) {
  if (len == 0) return;
  DCHECK_LE(src_index + len, src->length());
  DisallowGarbageCollection no_gc;

  ObjectSlot dst_slot(RawFieldOfElementAt(dst_index));
  ObjectSlot src_slot(src->RawFieldOfElementAt(src_index));
  isolate->heap()->CopyRange(*this, dst_slot, src_slot, len, mode);
}

ObjectSlot ScopeInfo::RawFieldOfElementAt(int index) {
  return RawField(OffsetOfElementAt(index));
}

int ScopeInfo::length() const {
  // AllocatedSize() is generated by Torque and represents the size in bytes of
  // the object, as computed from flags, context_local_count, and possibly
  // module_variable_count. Convert that size into a number of slots.
  return (AllocatedSize() - HeapObject::kHeaderSize) / kTaggedSize;
}

// static
Handle<ScopeInfo> ScopeInfo::RecreateWithBlockList(
    Isolate* isolate, Handle<ScopeInfo> original, Handle<StringSet> blocklist) {
  DCHECK(!original.is_null());
  if (original->HasLocalsBlockList()) return original;

  int length = original->length() + 1;
  Handle<ScopeInfo> scope_info = isolate->factory()->NewScopeInfo(length);

  // Copy the static part first and update the flags to include the
  // blocklist field, so {LocalsBlockListIndex} returns the correct value.
  scope_info->CopyElements(isolate, 0, *original, 0, kVariablePartIndex,
                           WriteBarrierMode::UPDATE_WRITE_BARRIER);
  scope_info->set_flags(
      HasLocalsBlockListBit::update(scope_info->Flags(), true));

  // Copy the dynamic part including the provided blocklist:
  //   1) copy all the fields up to the blocklist index
  //   2) add the blocklist
  //   3) copy the remaining fields
  scope_info->CopyElements(
      isolate, kVariablePartIndex, *original, kVariablePartIndex,
      scope_info->LocalsBlockListIndex() - kVariablePartIndex,
      WriteBarrierMode::UPDATE_WRITE_BARRIER);
  scope_info->set_locals_block_list(*blocklist);
  scope_info->CopyElements(isolate, scope_info->LocalsBlockListIndex() + 1,
                           *original, scope_info->LocalsBlockListIndex(),
                           length - scope_info->LocalsBlockListIndex() - 1,
                           WriteBarrierMode::UPDATE_WRITE_BARRIER);
  return scope_info;
}

Tagged<ScopeInfo> ScopeInfo::Empty(Isolate* isolate) {
  return ReadOnlyRoots(isolate).empty_scope_info();
}

bool ScopeInfo::IsEmpty() const { return IsEmptyBit::decode(Flags()); }

ScopeType ScopeInfo::scope_type() const {
  DCHECK(!this->IsEmpty());
  return ScopeTypeBits::decode(Flags());
}

bool ScopeInfo::is_script_scope() const {
  return !this->IsEmpty() && scope_type() == SCRIPT_SCOPE;
}

bool ScopeInfo::SloppyEvalCanExtendVars() const {
  bool sloppy_eval_can_extend_vars =
      SloppyEvalCanExtendVarsBit::decode(Flags());
  DCHECK_IMPLIES(sloppy_eval_can_extend_vars, is_sloppy(language_mode()));
  DCHECK_IMPLIES(sloppy_eval_can_extend_vars, is_declaration_scope());
  return sloppy_eval_can_extend_vars;
}

LanguageMode ScopeInfo::language_mode() const {
  return LanguageModeBit::decode(Flags());
}

bool ScopeInfo::is_declaration_scope() const {
  return DeclarationScopeBit::decode(Flags());
}

int ScopeInfo::ContextLength() const {
  if (this->IsEmpty()) return 0;
  int context_locals = ContextLocalCount();
  bool function_name_context_slot = HasContextAllocatedFunctionName();
  bool force_context = ForceContextAllocationBit::decode(Flags());
  bool has_context =
      context_locals > 0 || force_context || function_name_context_slot ||
      scope_type() == WITH_SCOPE || scope_type() == CLASS_SCOPE ||
      (scope_type() == BLOCK_SCOPE && SloppyEvalCanExtendVars() &&
       is_declaration_scope()) ||
      (scope_type() == FUNCTION_SCOPE && SloppyEvalCanExtendVars()) ||
      (scope_type() == FUNCTION_SCOPE && IsAsmModule()) ||
      scope_type() == MODULE_SCOPE;

  if (!has_context) return 0;
  return ContextHeaderLength() + context_locals +
         (function_name_context_slot ? 1 : 0);
}

bool ScopeInfo::HasContextExtensionSlot() const {
  return HasContextExtensionSlotBit::decode(Flags());
}

int ScopeInfo::ContextHeaderLength() const {
  return HasContextExtensionSlot() ? Context::MIN_CONTEXT_EXTENDED_SLOTS
                                   : Context::MIN_CONTEXT_SLOTS;
}

bool ScopeInfo::HasReceiver() const {
  return VariableAllocationInfo::NONE != ReceiverVariableBits::decode(Flags());
}

bool ScopeInfo::HasAllocatedReceiver() const {
  VariableAllocationInfo allocation = ReceiverVariableBits::decode(Flags());
  return allocation == VariableAllocationInfo::STACK ||
         allocation == VariableAllocationInfo::CONTEXT;
}

bool ScopeInfo::ClassScopeHasPrivateBrand() const {
  return ClassScopeHasPrivateBrandBit::decode(Flags());
}

bool ScopeInfo::HasSavedClassVariable() const {
  return HasSavedClassVariableBit::decode(Flags());
}

bool ScopeInfo::HasNewTarget() const {
  return HasNewTargetBit::decode(Flags());
}

bool ScopeInfo::HasFunctionName() const {
  return VariableAllocationInfo::NONE != FunctionVariableBits::decode(Flags());
}

bool ScopeInfo::HasContextAllocatedFunctionName() const {
  return VariableAllocationInfo::CONTEXT ==
         FunctionVariableBits::decode(Flags());
}

bool ScopeInfo::HasInferredFunctionName() const {
  return HasInferredFunctionNameBit::decode(Flags());
}

bool ScopeInfo::HasPositionInfo() const {
  if (this->IsEmpty()) return false;
  return NeedsPositionInfo(scope_type());
}

// static
bool ScopeInfo::NeedsPositionInfo(ScopeType type) {
  return type == FUNCTION_SCOPE || type == SCRIPT_SCOPE || type == EVAL_SCOPE ||
         type == MODULE_SCOPE || type == CLASS_SCOPE;
}

bool ScopeInfo::HasSharedFunctionName() const {
  return FunctionName() != SharedFunctionInfo::kNoSharedNameSentinel;
}

void ScopeInfo::SetFunctionName(Tagged<Object> name) {
  DCHECK(HasFunctionName());
  DCHECK(IsString(name) || name == SharedFunctionInfo::kNoSharedNameSentinel);
  DCHECK_IMPLIES(HasContextAllocatedFunctionName(), IsInternalizedString(name));
  set_function_variable_info_name(name);
}

void ScopeInfo::SetInferredFunctionName(Tagged<String> name) {
  DCHECK(HasInferredFunctionName());
  set_inferred_function_name(name);
}

bool ScopeInfo::HasOuterScopeInfo() const {
  return HasOuterScopeInfoBit::decode(Flags());
}

bool ScopeInfo::IsDebugEvaluateScope() const {
  return IsDebugEvaluateScopeBit::decode(Flags());
}

void ScopeInfo::SetIsDebugEvaluateScope() {
  CHECK(!this->IsEmpty());
  DCHECK_EQ(scope_type(), WITH_SCOPE);
  set_flags(Flags() | IsDebugEvaluateScopeBit::encode(true));
}

bool ScopeInfo::PrivateNameLookupSkipsOuterClass() const {
  return PrivateNameLookupSkipsOuterClassBit::decode(Flags());
}

bool ScopeInfo::IsReplModeScope() const {
  return IsReplModeScopeBit::decode(Flags());
}

bool ScopeInfo::HasLocalsBlockList() const {
  return HasLocalsBlockListBit::decode(Flags());
}

Tagged<StringSet> ScopeInfo::LocalsBlockList() const {
  DCHECK(HasLocalsBlockList());
  return StringSet::cast(locals_block_list());
}

bool ScopeInfo::HasContext() const { return ContextLength() > 0; }

Tagged<Object> ScopeInfo::FunctionName() const {
  DCHECK(HasFunctionName());
  return function_variable_info_name();
}

Tagged<Object> ScopeInfo::InferredFunctionName() const {
  DCHECK(HasInferredFunctionName());
  return inferred_function_name();
}

Tagged<String> ScopeInfo::FunctionDebugName() const {
  if (!HasFunctionName()) return GetReadOnlyRoots().empty_string();
  Tagged<Object> name = FunctionName();
  if (IsString(name) && String::cast(name)->length() > 0) {
    return String::cast(name);
  }
  if (HasInferredFunctionName()) {
    name = InferredFunctionName();
    if (IsString(name)) return String::cast(name);
  }
  return GetReadOnlyRoots().empty_string();
}

int ScopeInfo::StartPosition() const {
  DCHECK(HasPositionInfo());
  return position_info_start();
}

int ScopeInfo::EndPosition() const {
  DCHECK(HasPositionInfo());
  return position_info_end();
}

void ScopeInfo::SetPositionInfo(int start, int end) {
  DCHECK(HasPositionInfo());
  DCHECK_LE(start, end);
  set_position_info_start(start);
  set_position_info_end(end);
}

Tagged<ScopeInfo> ScopeInfo::OuterScopeInfo() const {
  DCHECK(HasOuterScopeInfo());
  return ScopeInfo::cast(outer_scope_info());
}

Tagged<SourceTextModuleInfo> ScopeInfo::ModuleDescriptorInfo() const {
  DCHECK(scope_type() == MODULE_SCOPE);
  return SourceTextModuleInfo::cast(module_info());
}

Tagged<String> ScopeInfo::ContextInlinedLocalName(int var) const {
  DCHECK(HasInlinedLocalNames());
  return context_local_names(var);
}

Tagged<String> ScopeInfo::ContextInlinedLocalName(PtrComprCageBase cage_base,
                                                  int var) const {
  DCHECK(HasInlinedLocalNames());
  return context_local_names(cage_base, var);
}

VariableMode ScopeInfo::ContextLocalMode(int var) const {
  int value = context_local_infos(var);
  return VariableModeBits::decode(value);
}

IsStaticFlag ScopeInfo::ContextLocalIsStaticFlag(int var) const {
  int value = context_local_infos(var);
  return IsStaticFlagBit::decode(value);
}

InitializationFlag ScopeInfo::ContextLocalInitFlag(int var) const {
  int value = context_local_infos(var);
  return InitFlagBit::decode(value);
}

bool ScopeInfo::ContextLocalIsParameter(int var) const {
  int value = context_local_infos(var);
  return ParameterNumberBits::decode(value) != ParameterNumberBits::kMax;
}

uint32_t ScopeInfo::ContextLocalParameterNumber(int var) const {
  DCHECK(ContextLocalIsParameter(var));
  int value = context_local_infos(var);
  return ParameterNumberBits::decode(value);
}

MaybeAssignedFlag ScopeInfo::ContextLocalMaybeAssignedFlag(int var) const {
  int value = context_local_infos(var);
  return MaybeAssignedFlagBit::decode(value);
}

// static
bool ScopeInfo::VariableIsSynthetic(Tagged<String> name) {
  // There's currently no flag stored on the ScopeInfo to indicate that a
  // variable is a compiler-introduced temporary. However, to avoid conflict
  // with user declarations, the current temporaries like .generator_object and
  // .result start with a dot, so we can use that as a flag. It's a hack!
  return name->length() == 0 || name->Get(0) == '.' || name->Get(0) == '#' ||
         name->Equals(name->GetReadOnlyRoots().this_string());
}

int ScopeInfo::ModuleVariableCount() const {
  DCHECK_EQ(scope_type(), MODULE_SCOPE);
  return module_variable_count();
}

int ScopeInfo::ModuleIndex(Tagged<String> name, VariableMode* mode,
                           InitializationFlag* init_flag,
                           MaybeAssignedFlag* maybe_assigned_flag) {
  DisallowGarbageCollection no_gc;
  DCHECK(IsInternalizedString(name));
  DCHECK_EQ(scope_type(), MODULE_SCOPE);
  DCHECK_NOT_NULL(mode);
  DCHECK_NOT_NULL(init_flag);
  DCHECK_NOT_NULL(maybe_assigned_flag);

  int module_vars_count = module_variable_count();
  for (int i = 0; i < module_vars_count; ++i) {
    Tagged<String> var_name = module_variables_name(i);
    if (name->Equals(var_name)) {
      int index;
      ModuleVariable(i, nullptr, &index, mode, init_flag, maybe_assigned_flag);
      return index;
    }
  }

  return 0;
}

int ScopeInfo::InlinedLocalNamesLookup(Tagged<String> name) {
  DisallowGarbageCollection no_gc;
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  int local_count = context_local_count();
  for (int i = 0; i < local_count; ++i) {
    if (name == ContextInlinedLocalName(cage_base, i)) {
      return i;
    }
  }
  return -1;
}

int ScopeInfo::ContextSlotIndex(Handle<String> name,
                                VariableLookupResult* lookup_result) {
  DisallowGarbageCollection no_gc;
  DCHECK(IsInternalizedString(*name));
  DCHECK_NOT_NULL(lookup_result);

  if (this->IsEmpty()) return -1;

  int index = HasInlinedLocalNames()
                  ? InlinedLocalNamesLookup(*name)
                  : context_local_names_hashtable()->Lookup(name);

  if (index != -1) {
    lookup_result->mode = ContextLocalMode(index);
    lookup_result->is_static_flag = ContextLocalIsStaticFlag(index);
    lookup_result->init_flag = ContextLocalInitFlag(index);
    lookup_result->maybe_assigned_flag = ContextLocalMaybeAssignedFlag(index);
    lookup_result->is_repl_mode = IsReplModeScope();
    int context_slot = ContextHeaderLength() + index;
    DCHECK_LT(context_slot, ContextLength());
    return context_slot;
  }

  return -1;
}

int ScopeInfo::ContextSlotIndex(Handle<String> name) {
  VariableLookupResult lookup_result;
  return ContextSlotIndex(name, &lookup_result);
}

std::pair<Tagged<String>, int> ScopeInfo::SavedClassVariable() const {
  DCHECK(HasSavedClassVariableBit::decode(Flags()));
  if (HasInlinedLocalNames()) {
    // The saved class variable info corresponds to the context slot index.
    int index = saved_class_variable_info() - Context::MIN_CONTEXT_SLOTS;
    DCHECK_GE(index, 0);
    DCHECK_LT(index, ContextLocalCount());
    Tagged<String> name = ContextInlinedLocalName(index);
    return std::make_pair(name, index);
  } else {
    // The saved class variable info corresponds to the offset in the hash
    // table storage.
    InternalIndex entry(saved_class_variable_info());
    Tagged<NameToIndexHashTable> table = context_local_names_hashtable();
    Tagged<Object> name = table->KeyAt(entry);
    DCHECK(IsString(name));
    return std::make_pair(String::cast(name), table->IndexAt(entry));
  }
}

int ScopeInfo::ReceiverContextSlotIndex() const {
  if (ReceiverVariableBits::decode(Flags()) ==
      VariableAllocationInfo::CONTEXT) {
    return ContextHeaderLength();
  }
  return -1;
}

int ScopeInfo::ParametersStartIndex() const {
  if (ReceiverVariableBits::decode(Flags()) ==
      VariableAllocationInfo::CONTEXT) {
    return ContextHeaderLength() + 1;
  }
  return ContextHeaderLength();
}

int ScopeInfo::FunctionContextSlotIndex(Tagged<String> name) const {
  DCHECK(IsInternalizedString(name));
  if (HasContextAllocatedFunctionName()) {
    DCHECK_IMPLIES(HasFunctionName(), IsInternalizedString(FunctionName()));
    if (FunctionName() == name) {
      return function_variable_info_context_or_stack_slot_index();
    }
  }
  return -1;
}

FunctionKind ScopeInfo::function_kind() const {
  return FunctionKindBits::decode(Flags());
}

int ScopeInfo::ContextLocalNamesIndex() const {
  return ConvertOffsetToIndex(ContextLocalNamesOffset());
}

int ScopeInfo::ContextLocalInfosIndex() const {
  return ConvertOffsetToIndex(ContextLocalInfosOffset());
}

int ScopeInfo::SavedClassVariableInfoIndex() const {
  return ConvertOffsetToIndex(SavedClassVariableInfoOffset());
}

int ScopeInfo::FunctionVariableInfoIndex() const {
  return ConvertOffsetToIndex(FunctionVariableInfoOffset());
}

int ScopeInfo::InferredFunctionNameIndex() const {
  return ConvertOffsetToIndex(InferredFunctionNameOffset());
}

int ScopeInfo::PositionInfoIndex() const {
  return ConvertOffsetToIndex(PositionInfoOffset());
}

int ScopeInfo::OuterScopeInfoIndex() const {
  return ConvertOffsetToIndex(OuterScopeInfoOffset());
}

int ScopeInfo::LocalsBlockListIndex() const {
  return ConvertOffsetToIndex(LocalsBlockListOffset());
}

int ScopeInfo::ModuleInfoIndex() const {
  return ConvertOffsetToIndex(ModuleInfoOffset());
}

int ScopeInfo::ModuleVariableCountIndex() const {
  return ConvertOffsetToIndex(kModuleVariableCountOffset);
}

int ScopeInfo::ModuleVariablesIndex() const {
  return ConvertOffsetToIndex(ModuleVariablesOffset());
}

void ScopeInfo::ModuleVariable(int i, Tagged<String>* name, int* index,
                               VariableMode* mode,
                               InitializationFlag* init_flag,
                               MaybeAssignedFlag* maybe_assigned_flag) {
  int properties = module_variables_properties(i);

  if (name != nullptr) {
    *name = module_variables_name(i);
  }
  if (index != nullptr) {
    *index = module_variables_index(i);
    DCHECK_NE(*index, 0);
  }
  if (mode != nullptr) {
    *mode = VariableModeBits::decode(properties);
  }
  if (init_flag != nullptr) {
    *init_flag = InitFlagBit::decode(properties);
  }
  if (maybe_assigned_flag != nullptr) {
    *maybe_assigned_flag = MaybeAssignedFlagBit::decode(properties);
  }
}

uint32_t ScopeInfo::Hash() {
  // Hash ScopeInfo based on its start and end position.
  // Note: Ideally we'd also have the script ID. But since we only use the
  // hash in a debug-evaluate cache, we don't worry too much about collisions.
  if (HasPositionInfo()) {
    return static_cast<uint32_t>(
        base::hash_combine(flags(), StartPosition(), EndPosition()));
  }

  return static_cast<uint32_t>(
      base::hash_combine(flags(), context_local_count()));
}

std::ostream& operator<<(std::ostream& os, VariableAllocationInfo var_info) {
  switch (var_info) {
    case VariableAllocationInfo::NONE:
      return os << "NONE";
    case VariableAllocationInfo::STACK:
      return os << "STACK";
    case VariableAllocationInfo::CONTEXT:
      return os << "CONTEXT";
    case VariableAllocationInfo::UNUSED:
      return os << "UNUSED";
  }
  UNREACHABLE();
}

template <typename IsolateT>
Handle<ModuleRequest> ModuleRequest::New(IsolateT* isolate,
                                         Handle<String> specifier,
                                         Handle<FixedArray> import_attributes,
                                         int position) {
  Handle<ModuleRequest> result = Handle<ModuleRequest>::cast(
      isolate->factory()->NewStruct(MODULE_REQUEST_TYPE, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  Tagged<ModuleRequest> raw = *result;
  raw->set_specifier(*specifier);
  raw->set_import_attributes(*import_attributes);
  raw->set_position(position);
  return result;
}

template Handle<ModuleRequest> ModuleRequest::New(
    Isolate* isolate, Handle<String> specifier,
    Handle<FixedArray> import_attributes, int position);
template Handle<ModuleRequest> ModuleRequest::New(
    LocalIsolate* isolate, Handle<String> specifier,
    Handle<FixedArray> import_attributes, int position);

template <typename IsolateT>
Handle<SourceTextModuleInfoEntry> SourceTextModuleInfoEntry::New(
    IsolateT* isolate, Handle<PrimitiveHeapObject> export_name,
    Handle<PrimitiveHeapObject> local_name,
    Handle<PrimitiveHeapObject> import_name, int module_request, int cell_index,
    int beg_pos, int end_pos) {
  Handle<SourceTextModuleInfoEntry> result =
      Handle<SourceTextModuleInfoEntry>::cast(isolate->factory()->NewStruct(
          SOURCE_TEXT_MODULE_INFO_ENTRY_TYPE, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  Tagged<SourceTextModuleInfoEntry> raw = *result;
  raw->set_export_name(*export_name);
  raw->set_local_name(*local_name);
  raw->set_import_name(*import_name);
  raw->set_module_request(module_request);
  raw->set_cell_index(cell_index);
  raw->set_beg_pos(beg_pos);
  raw->set_end_pos(end_pos);
  return result;
}

template Handle<SourceTextModuleInfoEntry> SourceTextModuleInfoEntry::New(
    Isolate* isolate, Handle<PrimitiveHeapObject> export_name,
    Handle<PrimitiveHeapObject> local_name,
    Handle<PrimitiveHeapObject> import_name, int module_request, int cell_index,
    int beg_pos, int end_pos);
template Handle<SourceTextModuleInfoEntry> SourceTextModuleInfoEntry::New(
    LocalIsolate* isolate, Handle<PrimitiveHeapObject> export_name,
    Handle<PrimitiveHeapObject> local_name,
    Handle<PrimitiveHeapObject> import_name, int module_request, int cell_index,
    int beg_pos, int end_pos);

template <typename IsolateT>
Handle<SourceTextModuleInfo> SourceTextModuleInfo::New(
    IsolateT* isolate, Zone* zone, SourceTextModuleDescriptor* descr) {
  // Serialize module requests.
  int size = static_cast<int>(descr->module_requests().size());
  Handle<FixedArray> module_requests =
      isolate->factory()->NewFixedArray(size, AllocationType::kOld);
  for (const auto& elem : descr->module_requests()) {
    Handle<ModuleRequest> serialized_module_request = elem->Serialize(isolate);
    module_requests->set(elem->index(), *serialized_module_request);
  }

  // Serialize special exports.
  Handle<FixedArray> special_exports = isolate->factory()->NewFixedArray(
      static_cast<int>(descr->special_exports().size()), AllocationType::kOld);
  {
    int i = 0;
    for (auto entry : descr->special_exports()) {
      Handle<SourceTextModuleInfoEntry> serialized_entry =
          entry->Serialize(isolate);
      special_exports->set(i++, *serialized_entry);
    }
  }

  // Serialize namespace imports.
  Handle<FixedArray> namespace_imports = isolate->factory()->NewFixedArray(
      static_cast<int>(descr->namespace_imports().size()),
      AllocationType::kOld);
  {
    int i = 0;
    for (auto entry : descr->namespace_imports()) {
      Handle<SourceTextModuleInfoEntry> serialized_entry =
          entry->Serialize(isolate);
      namespace_imports->set(i++, *serialized_entry);
    }
  }

  // Serialize regular exports.
  Handle<FixedArray> regular_exports =
      descr->SerializeRegularExports(isolate, zone);

  // Serialize regular imports.
  Handle<FixedArray> regular_imports = isolate->factory()->NewFixedArray(
      static_cast<int>(descr->regular_imports().size()), AllocationType::kOld);
  {
    int i = 0;
    for (const auto& elem : descr->regular_imports()) {
      Handle<SourceTextModuleInfoEntry> serialized_entry =
          elem.second->Serialize(isolate);
      regular_imports->set(i++, *serialized_entry);
    }
  }

  Handle<SourceTextModuleInfo> result =
      isolate->factory()->NewSourceTextModuleInfo();
  result->set(kModuleRequestsIndex, *module_requests);
  result->set(kSpecialExportsIndex, *special_exports);
  result->set(kRegularExportsIndex, *regular_exports);
  result->set(kNamespaceImportsIndex, *namespace_imports);
  result->set(kRegularImportsIndex, *regular_imports);
  return result;
}
template Handle<SourceTextModuleInfo> SourceTextModuleInfo::New(
    Isolate* isolate, Zone* zone, SourceTextModuleDescriptor* descr);
template Handle<SourceTextModuleInfo> SourceTextModuleInfo::New(
    LocalIsolate* isolate, Zone* zone, SourceTextModuleDescriptor* descr);

int SourceTextModuleInfo::RegularExportCount() const {
  DCHECK_EQ(regular_exports()->length() % kRegularExportLength, 0);
  return regular_exports()->length() / kRegularExportLength;
}

Tagged<String> SourceTextModuleInfo::RegularExportLocalName(int i) const {
  return String::cast(regular_exports()->get(i * kRegularExportLength +
                                             kRegularExportLocalNameOffset));
}

int SourceTextModuleInfo::RegularExportCellIndex(int i) const {
  return Smi::ToInt(regular_exports()->get(i * kRegularExportLength +
                                           kRegularExportCellIndexOffset));
}

Tagged<FixedArray> SourceTextModuleInfo::RegularExportExportNames(int i) const {
  return FixedArray::cast(regular_exports()->get(
      i * kRegularExportLength + kRegularExportExportNamesOffset));
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"
