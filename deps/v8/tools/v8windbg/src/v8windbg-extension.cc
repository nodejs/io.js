// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/v8windbg/src/v8windbg-extension.h"

#include <iostream>

#include "tools/v8windbg/base/utilities.h"
#include "tools/v8windbg/src/cur-isolate.h"
#include "tools/v8windbg/src/js-stack.h"
#include "tools/v8windbg/src/local-variables.h"
#include "tools/v8windbg/src/object-inspection.h"

std::unique_ptr<Extension> Extension::current_extension_ = nullptr;
const wchar_t* pcur_isolate = L"curisolate";
const wchar_t* pjs_stack = L"jsstack";
const wchar_t* pv8_object = L"v8object";

HRESULT CreateExtension() {
  if (Extension::Current() != nullptr || sp_data_model_manager == nullptr ||
      sp_debug_host == nullptr) {
    return E_FAIL;
  } else {
    std::unique_ptr<Extension> new_extension(new (std::nothrow) Extension());
    if (new_extension == nullptr) return E_FAIL;
    RETURN_IF_FAIL(new_extension->Initialize());
    Extension::SetExtension(std::move(new_extension));
    return S_OK;
  }
}

void DestroyExtension() { Extension::SetExtension(nullptr); }

WRL::ComPtr<IDebugHostType> Extension::GetV8TaggedObjectType(
    WRL::ComPtr<IDebugHostContext>& sp_ctx) {
  return GetTypeFromV8Module(sp_ctx, kTaggedObjectU);
}

WRL::ComPtr<IDebugHostType> Extension::GetTypeFromV8Module(
    WRL::ComPtr<IDebugHostContext>& sp_ctx, const char16_t* type_name) {
  bool is_equal;
  if (sp_v8_module_ctx_ == nullptr ||
      !SUCCEEDED(sp_v8_module_ctx_->IsEqualTo(sp_ctx.Get(), &is_equal)) ||
      !is_equal) {
    // Context changed; clear the dictionary.
    cached_v8_module_types_.clear();
  }

  GetV8Module(sp_ctx);  // Will force the correct module to load
  if (sp_v8_module_ == nullptr) return nullptr;

  auto& dictionary_entry = cached_v8_module_types_[type_name];
  if (dictionary_entry == nullptr) {
    const std::wstring type_name_w(reinterpret_cast<const wchar_t*>(type_name));
    // The contract from debug_helper functions is to provide type names that
    // would be valid if used in C++ code within the v8::internal namespace.
    // They might be fully qualified but aren't required to be. Thus, we must
    // simluate an "unqualified name lookup" here, by searching for the type
    // starting in the innermost namespace and working outward.
    if (SUCCEEDED(sp_v8_module_->FindTypeByName(
            (L"v8::internal::" + type_name_w).c_str(), &dictionary_entry))) {
      return dictionary_entry;
    }
    if (SUCCEEDED(sp_v8_module_->FindTypeByName((L"v8::" + type_name_w).c_str(),
                                                &dictionary_entry))) {
      return dictionary_entry;
    }
    sp_v8_module_->FindTypeByName(reinterpret_cast<PCWSTR>(type_name),
                                  &dictionary_entry);
  }
  return dictionary_entry;
}

namespace {

// Returns whether the given module appears to have symbols for V8 code.
bool IsV8Module(IDebugHostModule* module) {
  WRL::ComPtr<IDebugHostSymbol> sp_isolate_sym;
  // The below symbol is specific to the main V8 module and is specified with
  // V8_NOINLINE, so it should always be present.
  if (FAILED(module->FindSymbolByName(
          L"v8::internal::Isolate::PushStackTraceAndDie", &sp_isolate_sym))) {
    return false;
  }
  return true;
}

}  // namespace

WRL::ComPtr<IDebugHostModule> Extension::GetV8Module(
    WRL::ComPtr<IDebugHostContext>& sp_ctx) {
  // Return the cached version if it exists and the context is the same

  // Note: Context will often have the CUSTOM flag set, which never compares
  // equal. So for now DON'T compare by context, but by proc_id. (An API is in
  // progress to compare by address space, which should be usable when shipped).
  /*
  if (sp_v8_module_ != nullptr) {
    bool is_equal;
    if (SUCCEEDED(sp_v8_module_ctx_->IsEqualTo(sp_ctx.Get(), &is_equal)) &&
  is_equal) { return sp_v8_module_; } else { sp_v8_module_ = nullptr;
      sp_v8_module_ctx_ = nullptr;
    }
  }
  */
  WRL::ComPtr<IDebugSystemObjects> sp_sys_objects;
  ULONG proc_id = 0;
  if (SUCCEEDED(sp_debug_control.As(&sp_sys_objects))) {
    if (SUCCEEDED(sp_sys_objects->GetCurrentProcessSystemId(&proc_id))) {
      if (proc_id == v8_module_proc_id_ && sp_v8_module_ != nullptr)
        return sp_v8_module_;
    }
  }

  // Search first for a few known module names, to avoid loading symbols for
  // unrelated modules if we can easily avoid it. Generally, failing to find a
  // module is fast but failing to find a symbol within a module is slow. Note
  // that "v8" is listed first because it's highly likely to be the correct
  // module if it exists. The others might include V8 symbols depending on the
  // build configuration.
  std::vector<const wchar_t*> known_names = {
      L"v8", L"v8_for_testing", L"cctest_exe", L"chrome",
      L"d8", L"msedge",         L"node",       L"v8_unittests_exe"};
  for (const wchar_t* name : known_names) {
    WRL::ComPtr<IDebugHostModule> sp_module;
    if (SUCCEEDED(sp_debug_host_symbols->FindModuleByName(sp_ctx.Get(), name,
                                                          &sp_module))) {
      if (IsV8Module(sp_module.Get())) {
        sp_v8_module_ = sp_module;
        sp_v8_module_ctx_ = sp_ctx;
        v8_module_proc_id_ = proc_id;
        return sp_v8_module_;
      }
    }
  }

  // Loop through all modules looking for the one that holds a known symbol.
  WRL::ComPtr<IDebugHostSymbolEnumerator> sp_enum;
  if (SUCCEEDED(
          sp_debug_host_symbols->EnumerateModules(sp_ctx.Get(), &sp_enum))) {
    HRESULT hr = S_OK;
    while (true) {
      WRL::ComPtr<IDebugHostSymbol> sp_mod_sym;
      hr = sp_enum->GetNext(&sp_mod_sym);
      // hr == E_BOUNDS : hit the end of the enumerator
      // hr == E_ABORT  : a user interrupt was requested
      if (FAILED(hr)) break;
      WRL::ComPtr<IDebugHostModule> sp_module;
      if (SUCCEEDED(sp_mod_sym.As(&sp_module))) /* should always succeed */
      {
        if (IsV8Module(sp_module.Get())) {
          sp_v8_module_ = sp_module;
          sp_v8_module_ctx_ = sp_ctx;
          v8_module_proc_id_ = proc_id;
          break;
        }
      }
    }
  }
  // This will be the located module, or still nullptr if above fails
  return sp_v8_module_;
}

Extension::Extension() = default;

HRESULT Extension::Initialize() {
  // Create an instance of the DataModel parent for Tagged<T> types.
  auto object_data_model{WRL::Make<V8ObjectDataModel>()};
  RETURN_IF_FAIL(sp_data_model_manager->CreateDataModelObject(
      object_data_model.Get(), &sp_object_data_model_));
  RETURN_IF_FAIL(sp_object_data_model_->SetConcept(
      __uuidof(IStringDisplayableConcept),
      static_cast<IStringDisplayableConcept*>(object_data_model.Get()),
      nullptr));
  RETURN_IF_FAIL(sp_object_data_model_->SetConcept(
      __uuidof(IDynamicKeyProviderConcept),
      static_cast<IDynamicKeyProviderConcept*>(object_data_model.Get()),
      nullptr));

  // Register that parent model for Tagged<T>.
  WRL::ComPtr<IDebugHostTypeSignature> sp_tagged_type_signature;
  RETURN_IF_FAIL(sp_debug_host_symbols->CreateTypeSignature(
      L"v8::internal::Tagged<*>", nullptr, &sp_tagged_type_signature));
  RETURN_IF_FAIL(sp_data_model_manager->RegisterModelForTypeSignature(
      sp_tagged_type_signature.Get(), sp_object_data_model_.Get()));
  registered_types_.push_back(
      {sp_tagged_type_signature.Get(), sp_object_data_model_.Get()});

  // Create an instance of the DataModel parent for custom iterable fields.
  auto indexed_field_model{WRL::Make<IndexedFieldParent>()};
  RETURN_IF_FAIL(sp_data_model_manager->CreateDataModelObject(
      indexed_field_model.Get(), &sp_indexed_field_model_));
  RETURN_IF_FAIL(sp_indexed_field_model_->SetConcept(
      __uuidof(IIndexableConcept),
      static_cast<IIndexableConcept*>(indexed_field_model.Get()), nullptr));
  RETURN_IF_FAIL(sp_indexed_field_model_->SetConcept(
      __uuidof(IIterableConcept),
      static_cast<IIterableConcept*>(indexed_field_model.Get()), nullptr));

  // Create an instance of the DataModel parent class for v8::Local<*> types.
  auto local_data_model{WRL::Make<V8LocalDataModel>()};
  RETURN_IF_FAIL(sp_data_model_manager->CreateDataModelObject(
      local_data_model.Get(), &sp_local_data_model_));

  // Register that parent model for all known types that act like v8::Local.
  std::vector<const wchar_t*> handle_class_names = {
      L"v8::Local<*>", L"v8::MaybeLocal<*>", L"v8::internal::Handle<*>",
      L"v8::internal::MaybeHandle<*>"};
  for (const wchar_t* name : handle_class_names) {
    WRL::ComPtr<IDebugHostTypeSignature> signature;
    RETURN_IF_FAIL(
        sp_debug_host_symbols->CreateTypeSignature(name, nullptr, &signature));
    RETURN_IF_FAIL(sp_data_model_manager->RegisterModelForTypeSignature(
        signature.Get(), sp_local_data_model_.Get()));
    registered_types_.push_back({signature.Get(), sp_local_data_model_.Get()});
  }

  // Add the 'Value' property to the parent model.
  auto local_value_property{WRL::Make<V8LocalValueProperty>()};
  WRL::ComPtr<IModelObject> sp_local_value_property_model;
  RETURN_IF_FAIL(CreateProperty(sp_data_model_manager.Get(),
                                local_value_property.Get(),
                                &sp_local_value_property_model));
  RETURN_IF_FAIL(sp_local_data_model_->SetKey(
      L"Value", sp_local_value_property_model.Get(), nullptr));

  // Register all function aliases.
  std::vector<std::pair<const wchar_t*, WRL::ComPtr<IModelMethod>>> functions =
      {{pcur_isolate, WRL::Make<CurrIsolateAlias>()},
       {pjs_stack, WRL::Make<JSStackAlias>()},
       {pv8_object, WRL::Make<InspectV8ObjectMethod>()}};
  for (const auto& function : functions) {
    WRL::ComPtr<IModelObject> method;
    RETURN_IF_FAIL(CreateMethod(sp_data_model_manager.Get(),
                                function.second.Get(), &method));
    RETURN_IF_FAIL(sp_debug_host_extensibility->CreateFunctionAlias(
        function.first, method.Get()));
  }

  // Register a handler for supplying stack frame locals. It has to override the
  // getter functions for "LocalVariables" and "Parameters".
  WRL::ComPtr<IModelObject> stack_frame;
  RETURN_IF_FAIL(sp_data_model_manager->AcquireNamedModel(
      L"Debugger.Models.StackFrame", &stack_frame));
  RETURN_IF_FAIL(OverrideLocalsGetter(stack_frame.Get(), L"LocalVariables",
                                      /*is_parameters=*/false));
  RETURN_IF_FAIL(OverrideLocalsGetter(stack_frame.Get(), L"Parameters",
                                      /*is_parameters=*/true));

  // Add node_id property for v8::internal::compiler::Node.
  RETURN_IF_FAIL(
      RegisterAndAddPropertyForClass<V8InternalCompilerNodeIdProperty>(
          L"v8::internal::compiler::Node", L"node_id",
          sp_compiler_node_data_model_));

  // Add bitset_name property for v8::internal::compiler::Type.
  RETURN_IF_FAIL(
      RegisterAndAddPropertyForClass<V8InternalCompilerBitsetNameProperty>(
          L"v8::internal::compiler::Type", L"bitset_name",
          sp_compiler_type_data_model_));

  return S_OK;
}

template <class PropertyClass>
HRESULT Extension::RegisterAndAddPropertyForClass(
    const wchar_t* class_name, const wchar_t* property_name,
    WRL::ComPtr<IModelObject> sp_data_model) {
  // Create an instance of the DataModel parent class.
  auto instance_data_model{WRL::Make<V8LocalDataModel>()};
  RETURN_IF_FAIL(sp_data_model_manager->CreateDataModelObject(
      instance_data_model.Get(), &sp_data_model));

  // Register that parent model.
  WRL::ComPtr<IDebugHostTypeSignature> class_signature;
  RETURN_IF_FAIL(sp_debug_host_symbols->CreateTypeSignature(class_name, nullptr,
                                                            &class_signature));
  RETURN_IF_FAIL(sp_data_model_manager->RegisterModelForTypeSignature(
      class_signature.Get(), sp_data_model.Get()));
  registered_types_.push_back({class_signature.Get(), sp_data_model.Get()});

  // Add the property to the parent model.
  auto property{WRL::Make<PropertyClass>()};
  WRL::ComPtr<IModelObject> sp_property_model;
  RETURN_IF_FAIL(CreateProperty(sp_data_model_manager.Get(), property.Get(),
                                &sp_property_model));
  RETURN_IF_FAIL(
      sp_data_model->SetKey(property_name, sp_property_model.Get(), nullptr));

  return S_OK;
}

HRESULT Extension::OverrideLocalsGetter(IModelObject* stack_frame,
                                        const wchar_t* key_name,
                                        bool is_parameters) {
  WRL::ComPtr<IModelObject> original_boxed_getter;
  WRL::ComPtr<IKeyStore> original_getter_metadata;
  RETURN_IF_FAIL(stack_frame->GetKey(key_name, &original_boxed_getter,
                                     &original_getter_metadata));
  WRL::ComPtr<IModelPropertyAccessor> original_getter;
  RETURN_IF_FAIL(UnboxProperty(original_boxed_getter.Get(), &original_getter));
  auto new_getter{WRL::Make<V8LocalVariables>(original_getter, is_parameters)};
  WRL::ComPtr<IModelObject> new_boxed_getter;
  RETURN_IF_FAIL(CreateProperty(sp_data_model_manager.Get(), new_getter.Get(),
                                &new_boxed_getter));
  RETURN_IF_FAIL(stack_frame->SetKey(key_name, new_boxed_getter.Get(),
                                     original_getter_metadata.Get()));
  overridden_properties_.push_back(
      {stack_frame, reinterpret_cast<const char16_t*>(key_name),
       original_boxed_getter.Get(), original_getter_metadata.Get()});
  return S_OK;
}

Extension::PropertyOverride::PropertyOverride() = default;
Extension::PropertyOverride::PropertyOverride(IModelObject* parent,
                                              std::u16string key_name,
                                              IModelObject* original_value,
                                              IKeyStore* original_metadata)
    : parent(parent),
      key_name(std::move(key_name)),
      original_value(original_value),
      original_metadata(original_metadata) {}
Extension::PropertyOverride::~PropertyOverride() = default;
Extension::PropertyOverride::PropertyOverride(const PropertyOverride&) =
    default;
Extension::PropertyOverride& Extension::PropertyOverride::operator=(
    const PropertyOverride&) = default;

Extension::RegistrationType::RegistrationType() = default;
Extension::RegistrationType::RegistrationType(
    IDebugHostTypeSignature* sp_signature, IModelObject* sp_data_model)
    : sp_signature(sp_signature), sp_data_model(sp_data_model) {}
Extension::RegistrationType::~RegistrationType() = default;
Extension::RegistrationType::RegistrationType(const RegistrationType&) =
    default;
Extension::RegistrationType& Extension::RegistrationType::operator=(
    const RegistrationType&) = default;

Extension::~Extension() {
  sp_debug_host_extensibility->DestroyFunctionAlias(pcur_isolate);
  sp_debug_host_extensibility->DestroyFunctionAlias(pjs_stack);
  sp_debug_host_extensibility->DestroyFunctionAlias(pv8_object);

  for (const auto& registered : registered_types_) {
    sp_data_model_manager->UnregisterModelForTypeSignature(
        registered.sp_data_model.Get(), registered.sp_signature.Get());
  }

  for (const auto& override : overridden_properties_) {
    override.parent->SetKey(
        reinterpret_cast<const wchar_t*>(override.key_name.c_str()),
        override.original_value.Get(), override.original_metadata.Get());
  }
}
