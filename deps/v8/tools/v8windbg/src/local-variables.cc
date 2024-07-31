// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/v8windbg/src/local-variables.h"

#include <vector>

#include "src/base/logging.h"
#include "tools/v8windbg/base/utilities.h"
#include "tools/v8windbg/src/object-inspection.h"
#include "tools/v8windbg/src/v8-debug-helper-interop.h"
#include "tools/v8windbg/src/v8windbg-extension.h"

V8LocalVariables::V8LocalVariables(WRL::ComPtr<IModelPropertyAccessor> original,
                                   bool is_parameters)
    : original_(original), is_parameters_(is_parameters) {}
V8LocalVariables::~V8LocalVariables() = default;

IFACEMETHODIMP V8LocalVariables::GetValue(PCWSTR key, IModelObject* context,
                                          IModelObject** value) noexcept {
  // Try to find out about the instruction pointer. If it is within the V8
  // module, or points to unknown space outside a module (generated code), then
  // we're interested. Otherwise, we have nothing useful to do.
  WRL::ComPtr<IModelObject> attributes;
  RETURN_IF_FAIL(context->GetKeyValue(L"Attributes", &attributes, nullptr));
  WRL::ComPtr<IModelObject> boxed_instruction_offset;
  RETURN_IF_FAIL(attributes->GetKeyValue(L"InstructionOffset",
                                         &boxed_instruction_offset, nullptr));
  ULONG64 instruction_offset{};
  RETURN_IF_FAIL(
      UnboxULong64(boxed_instruction_offset.Get(), &instruction_offset));
  WRL::ComPtr<IDebugHostSymbols> symbols;
  RETURN_IF_FAIL(sp_debug_host.As(&symbols));
  WRL::ComPtr<IDebugHostContext> host_context;
  RETURN_IF_FAIL(sp_debug_host->GetCurrentContext(&host_context));
  WRL::ComPtr<IDebugHostModule> module;
  if (SUCCEEDED(symbols->FindModuleByLocation(host_context.Get(),
                                              instruction_offset, &module))) {
    Location module_base;
    RETURN_IF_FAIL(module->GetBaseLocation(&module_base));
    WRL::ComPtr<IDebugHostModule> v8_module =
        Extension::Current()->GetV8Module(host_context);
    if (v8_module == nullptr) {
      // Anything in a module must not be in the V8 module if the V8 module
      // doesn't exist.
      return original_->GetValue(key, context, value);
    }
    Location v8_base;
    RETURN_IF_FAIL(v8_module->GetBaseLocation(&v8_base));
    if (module_base != v8_base) {
      // It's in a module, but not the one that contains V8.
      return original_->GetValue(key, context, value);
    }
    // Next, determine whether the instruction pointer refers to a builtin.
    DCHECK_GE(instruction_offset, v8_base.GetOffset());
    ULONG64 rva = instruction_offset - v8_base.GetOffset();
    WRL::ComPtr<IDebugHostSymbol> symbol;
    _bstr_t symbol_name;
    WRL::ComPtr<IDebugHostModule2> module2;
    RETURN_IF_FAIL(module.As(&module2));
    ULONG64 offset_within_symbol{};
    if (FAILED(module2->FindContainingSymbolByRVA(rva, &symbol,
                                                  &offset_within_symbol)) ||
        FAILED(symbol->GetName(symbol_name.GetAddress())) ||
        strncmp("Builtins_", static_cast<const char*>(symbol_name),
                strlen("Builtins_"))) {
      return original_->GetValue(key, context, value);
    }
  }

  // Initialize an empty result object.
  WRL::ComPtr<IModelObject> result;
  RETURN_IF_FAIL(sp_data_model_manager->CreateSyntheticObject(
      host_context.Get(), &result));
  WRL::ComPtr<IModelObject> parent_model;
  RETURN_IF_FAIL(sp_data_model_manager->AcquireNamedModel(
      is_parameters_ ? L"Debugger.Models.Parameters"
                     : L"Debugger.Models.LocalVariables",
      &parent_model));
  RETURN_IF_FAIL(result->AddParentModel(parent_model.Get(), /*context=*/nullptr,
                                        /*override=*/false));

  if (is_parameters_) {
    // We're not actually adding any parameters data yet; we just need it to not
    // fail so that the locals pane displays the LocalVariables. The locals pane
    // displays nothing if getting either LocalVariables or Parameters fails.
    *value = result.Detach();
    return S_OK;
  }

  // Get the stack and frame pointers for the current frame.
  WRL::ComPtr<IModelObject> boxed_stack_offset;
  RETURN_IF_FAIL(
      attributes->GetKeyValue(L"StackOffset", &boxed_stack_offset, nullptr));
  ULONG64 stack_offset{};
  RETURN_IF_FAIL(UnboxULong64(boxed_stack_offset.Get(), &stack_offset));
  WRL::ComPtr<IModelObject> boxed_frame_offset;
  RETURN_IF_FAIL(
      attributes->GetKeyValue(L"FrameOffset", &boxed_frame_offset, nullptr));
  ULONG64 frame_offset{};
  RETURN_IF_FAIL(UnboxULong64(boxed_frame_offset.Get(), &frame_offset));

  // Eventually v8_debug_helper will provide some help here, but for now, just
  // provide the option to view the whole stack frame as tagged data. It can
  // be somewhat useful.
  WRL::ComPtr<IDebugHostType> object_type =
      Extension::Current()->GetV8TaggedObjectType(host_context);
  if (object_type == nullptr) {
    // There's nothing useful to do if we can't find the symbol for
    // v8::internal::Tagged<v8::internal::Object>.
    return original_->GetValue(key, context, value);
  }
  ULONG64 object_size{};
  RETURN_IF_FAIL(object_type->GetSize(&object_size));
  ULONG64 num_objects = (frame_offset - stack_offset) / object_size;
  ArrayDimension dimensions[] = {
      {/*start=*/0, /*length=*/num_objects, /*stride=*/object_size}};
  WRL::ComPtr<IDebugHostType> object_array_type;
  RETURN_IF_FAIL(object_type->CreateArrayOf(/*dimensions=*/1, dimensions,
                                            &object_array_type));
  WRL::ComPtr<IModelObject> array;
  RETURN_IF_FAIL(sp_data_model_manager->CreateTypedObject(
      host_context.Get(), stack_offset, object_array_type.Get(), &array));
  RETURN_IF_FAIL(
      result->SetKey(L"memory interpreted as Objects", array.Get(), nullptr));

  std::vector<Property> properties = GetStackFrame(host_context, frame_offset);
  for (const auto& prop : properties) {
    WRL::ComPtr<IModelObject> property;
    RETURN_IF_FAIL(GetModelForProperty(prop, host_context, &property));
    result->SetKey(reinterpret_cast<const wchar_t*>(prop.name.c_str()),
                   property.Get(), nullptr);
  }

  *value = result.Detach();
  return S_OK;
}

IFACEMETHODIMP V8LocalVariables::SetValue(PCWSTR key, IModelObject* context,
                                          IModelObject* value) noexcept {
  return E_NOTIMPL;
}
