// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_OS_LINUX
#include <sys/mman.h>
#include <sys/stat.h>
// `sys/mman.h defines `MAP_TYPE`, but `MAP_TYPE` also gets defined within V8.
// Since we don't need `sys/mman.h`'s `MAP_TYPE`, we undefine it immediately
// after the `#include`.
#undef MAP_TYPE
#endif  // V8_TARGET_OS_LINUX

#include "src/wasm/wasm-objects.h"

#include <optional>

#include "src/base/iterator.h"
#include "src/base/vector.h"
#include "src/builtins/builtins-inl.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/logging/counters.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/shared-function-info.h"
#include "src/roots/roots-inl.h"
#include "src/utils/utils.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/stacks.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-code-pointer-table-inl.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#endif  // V8_ENABLE_DRUMBRAKE

// Needs to be last so macros do not get undefined.
#include "src/objects/object-macros.h"

#define TRACE_IFT(...)              \
  do {                              \
    if (false) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {

namespace {

// Utilities for handling "uses" lists. We reserve one slot for the
// used length, then store pairs of (instance, table_index).
static constexpr int kReservedSlotOffset = 1;
void SetUsedLength(Tagged<ProtectedWeakFixedArray> uses, int length) {
  // {set} includes a DCHECK for sufficient capacity.
  uses->set(0, Smi::FromInt(length));
}
int GetUsedLength(Tagged<ProtectedWeakFixedArray> uses) {
  if (uses->length() == 0) return 0;
  return Cast<Smi>(uses->get(0)).value();
}
void SetEntry(Tagged<ProtectedWeakFixedArray> uses, int slot_index,
              Tagged<WasmTrustedInstanceData> user, int table_index) {
  DCHECK(slot_index & 1);
  uses->set(slot_index, MakeWeak(user));
  uses->set(slot_index + 1, Smi::FromInt(table_index));
}
// These are two separate functions because GCMole produces bogus warnings
// when we return a std::pair<A, B> and call it as `auto [a, b] = ...`.
Tagged<WasmTrustedInstanceData> GetInstance(
    Tagged<ProtectedWeakFixedArray> uses, int slot_index) {
  DCHECK(slot_index & 1);
  return Cast<WasmTrustedInstanceData>(
      uses->get(slot_index).GetHeapObjectAssumeWeak());
}
int GetTableIndex(Tagged<ProtectedWeakFixedArray> uses, int slot_index) {
  DCHECK(slot_index & 1);
  return Cast<Smi>(uses->get(slot_index + 1)).value();
}
void CopyEntry(Tagged<ProtectedWeakFixedArray> dst, int dst_index,
               Tagged<ProtectedWeakFixedArray> src, int src_index) {
  DCHECK(dst_index & 1);
  DCHECK(src_index & 1);
  // There shouldn't be a reason to copy cleared entries.
  DCHECK(
      IsWasmTrustedInstanceData(src->get(src_index).GetHeapObjectAssumeWeak()));
  DCHECK(IsSmi(src->get(src_index + 1)));
  dst->set(dst_index, src->get(src_index));
  dst->set(dst_index + 1, src->get(src_index + 1));
}

}  // namespace

// Import a few often used types from the wasm namespace.
using WasmFunction = wasm::WasmFunction;
using WasmModule = wasm::WasmModule;

// static
Handle<WasmModuleObject> WasmModuleObject::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    DirectHandle<Script> script) {
  DirectHandle<Managed<wasm::NativeModule>> managed_native_module;
  if (script->type() == Script::Type::kWasm) {
    managed_native_module = direct_handle(
        Cast<Managed<wasm::NativeModule>>(script->wasm_managed_native_module()),
        isolate);
  } else {
    const WasmModule* module = native_module->module();
    size_t memory_estimate =
        native_module->committed_code_space() +
        wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
    managed_native_module = Managed<wasm::NativeModule>::From(
        isolate, memory_estimate, std::move(native_module));
  }
  Handle<WasmModuleObject> module_object = Cast<WasmModuleObject>(
      isolate->factory()->NewJSObject(isolate->wasm_module_constructor()));
  module_object->set_managed_native_module(*managed_native_module);
  module_object->set_script(*script);
  return module_object;
}

Handle<String> WasmModuleObject::ExtractUtf8StringFromModuleBytes(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object,
    wasm::WireBytesRef ref, InternalizeString internalize) {
  base::Vector<const uint8_t> wire_bytes =
      module_object->native_module()->wire_bytes();
  return ExtractUtf8StringFromModuleBytes(isolate, wire_bytes, ref,
                                          internalize);
}

Handle<String> WasmModuleObject::ExtractUtf8StringFromModuleBytes(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    wasm::WireBytesRef ref, InternalizeString internalize) {
  base::Vector<const uint8_t> name_vec =
      wire_bytes.SubVector(ref.offset(), ref.end_offset());
  // UTF8 validation happens at decode time.
  DCHECK(unibrow::Utf8::ValidateEncoding(name_vec.begin(), name_vec.length()));
  auto* factory = isolate->factory();
  return internalize
             ? factory->InternalizeUtf8String(
                   base::Vector<const char>::cast(name_vec))
             : factory
                   ->NewStringFromUtf8(base::Vector<const char>::cast(name_vec))
                   .ToHandleChecked();
}

MaybeDirectHandle<String> WasmModuleObject::GetModuleNameOrNull(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object) {
  const WasmModule* module = module_object->module();
  if (!module->name.is_set()) return {};
  return ExtractUtf8StringFromModuleBytes(isolate, module_object, module->name,
                                          kNoInternalize);
}

MaybeHandle<String> WasmModuleObject::GetFunctionNameOrNull(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object,
    uint32_t func_index) {
  DCHECK_LT(func_index, module_object->module()->functions.size());
  wasm::WireBytesRef name =
      module_object->module()->lazily_generated_names.LookupFunctionName(
          wasm::ModuleWireBytes(module_object->native_module()->wire_bytes()),
          func_index);
  if (!name.is_set()) return {};
  return ExtractUtf8StringFromModuleBytes(isolate, module_object, name,
                                          kNoInternalize);
}

base::Vector<const uint8_t> WasmModuleObject::GetRawFunctionName(
    int func_index) {
  if (func_index == wasm::kAnonymousFuncIndex) {
    return base::Vector<const uint8_t>({nullptr, 0});
  }
  DCHECK_GT(module()->functions.size(), func_index);
  wasm::ModuleWireBytes wire_bytes(native_module()->wire_bytes());
  wasm::WireBytesRef name_ref =
      module()->lazily_generated_names.LookupFunctionName(wire_bytes,
                                                          func_index);
  wasm::WasmName name = wire_bytes.GetNameOrNull(name_ref);
  return base::Vector<const uint8_t>::cast(name);
}

Handle<WasmTableObject> WasmTableObject::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
    wasm::ValueType type, wasm::CanonicalValueType canonical_type,
    uint32_t initial, bool has_maximum, uint64_t maximum,
    DirectHandle<Object> initial_value, wasm::AddressType address_type) {
  CHECK(type.is_object_reference());

  DCHECK_LE(initial, wasm::max_table_size());
  DirectHandle<FixedArray> entries = isolate->factory()->NewFixedArray(initial);
  for (int i = 0; i < static_cast<int>(initial); ++i) {
    entries->set(i, *initial_value);
  }
  bool is_function_table = canonical_type.IsFunctionType();
  DirectHandle<WasmDispatchTable> dispatch_table =
      is_function_table
          ? isolate->factory()->NewWasmDispatchTable(initial, canonical_type)
          : DirectHandle<WasmDispatchTable>{};

  DirectHandle<UnionOf<Undefined, Number, BigInt>> max =
      isolate->factory()->undefined_value();
  if (has_maximum) {
    if (address_type == wasm::AddressType::kI32) {
      DCHECK_GE(kMaxUInt32, maximum);
      max = isolate->factory()->NewNumber(maximum);
    } else {
      max = BigInt::FromUint64(isolate, maximum);
    }
  }

  DirectHandle<JSFunction> table_ctor(
      isolate->native_context()->wasm_table_constructor(), isolate);
  auto table_obj =
      Cast<WasmTableObject>(isolate->factory()->NewJSObject(table_ctor));
  DisallowGarbageCollection no_gc;

  if (!trusted_data.is_null()) {
    table_obj->set_trusted_data(*trusted_data);
  } else {
    table_obj->clear_trusted_data();
  }
  table_obj->set_entries(*entries);
  table_obj->set_current_length(initial);
  table_obj->set_maximum_length(*max);
  table_obj->set_raw_type(static_cast<int>(type.raw_bit_field()));
  table_obj->set_address_type(address_type);
  table_obj->set_padding_for_address_type_0(0);
  table_obj->set_padding_for_address_type_1(0);
#if TAGGED_SIZE_8_BYTES
  table_obj->set_padding_for_address_type_2(0);
#endif

  if (is_function_table) {
    DCHECK_EQ(table_obj->current_length(), dispatch_table->length());
    table_obj->set_trusted_dispatch_table(*dispatch_table);
  } else {
    table_obj->clear_trusted_dispatch_table();
  }
  return table_obj;
}

int WasmTableObject::Grow(Isolate* isolate, DirectHandle<WasmTableObject> table,
                          uint32_t count, DirectHandle<Object> init_value) {
  uint32_t old_size = table->current_length();
  if (count == 0) return old_size;  // Degenerate case: nothing to do.

  // Check if growing by {count} is valid.
  static_assert(wasm::kV8MaxWasmTableSize <= kMaxUInt32);
  uint64_t static_max_size = wasm::max_table_size();
  uint32_t max_size = static_cast<uint32_t>(std::min(
      static_max_size, table->maximum_length_u64().value_or(static_max_size)));
  DCHECK_LE(old_size, max_size);
  if (count > max_size - old_size) return -1;

  uint32_t new_size = old_size + count;
  // Even with 2x over-allocation, there should not be an integer overflow.
  static_assert(wasm::kV8MaxWasmTableSize <= kMaxInt / 2);
  DCHECK_GE(kMaxInt, new_size);
  int old_capacity = table->entries()->length();
  if (new_size > static_cast<uint32_t>(old_capacity)) {
    int grow = static_cast<int>(new_size) - old_capacity;
    // Grow at least by the old capacity, to implement exponential growing.
    grow = std::max(grow, old_capacity);
    // Never grow larger than the max size.
    grow = std::min(grow, static_cast<int>(max_size - old_capacity));
    auto new_store = isolate->factory()->CopyFixedArrayAndGrow(
        direct_handle(table->entries(), isolate), grow);
    table->set_entries(*new_store, WriteBarrierMode::UPDATE_WRITE_BARRIER);
  }

  if (table->has_trusted_dispatch_table()) {
    Handle<WasmDispatchTable> dispatch_table(
        table->trusted_dispatch_table(isolate), isolate);
    DCHECK_EQ(old_size, dispatch_table->length());
    Handle<WasmDispatchTable> new_dispatch_table =
        WasmDispatchTable::Grow(isolate, dispatch_table, new_size);
    if (!dispatch_table.is_identical_to(new_dispatch_table)) {
      table->set_trusted_dispatch_table(*new_dispatch_table);
    }
    DCHECK_EQ(new_size, table->trusted_dispatch_table(isolate)->length());

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_jitless) {
      Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
      int used_length = GetUsedLength(uses);
      for (int i = kReservedSlotOffset; i < used_length; i += 2) {
        if (uses->get(i).IsCleared()) continue;
        Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
        if (instance->has_interpreter_object()) {
          int table_index = GetTableIndex(uses, i);
          wasm::WasmInterpreterRuntime::UpdateIndirectCallTable(
              isolate, handle(instance->instance_object(), isolate),
              table_index);
        }
      }
    }
#endif  // V8_ENABLE_DRUMBRAKE
  }

  // Only update the current length after all allocations of sub-objects
  // (in particular: the new dispatch table) are done, so that heap verification
  // can assert that the dispatch table's length matches the table's length.
  table->set_current_length(new_size);

  for (uint32_t entry = old_size; entry < new_size; ++entry) {
    WasmTableObject::Set(isolate, table, entry, init_value);
  }
  return old_size;
}

MaybeHandle<Object> WasmTableObject::JSToWasmElement(
    Isolate* isolate, DirectHandle<WasmTableObject> table, Handle<Object> entry,
    const char** error_message) {
  const WasmModule* module = !table->has_trusted_data()
                                 ? nullptr
                                 : table->trusted_data(isolate)->module();
  return wasm::JSToWasmObject(isolate, module, entry, table->type(module),
                              error_message);
}

void WasmTableObject::SetFunctionTableEntry(Isolate* isolate,
                                            DirectHandle<WasmTableObject> table,
                                            int entry_index,
                                            DirectHandle<Object> entry) {
  if (IsWasmNull(*entry, isolate)) {
    table->ClearDispatchTable(entry_index);  // Degenerate case.
    table->entries()->set(entry_index, ReadOnlyRoots(isolate).wasm_null());
    return;
  }
  DCHECK(IsWasmFuncRef(*entry));
  DirectHandle<Object> external = WasmInternalFunction::GetOrCreateExternal(
      direct_handle(Cast<WasmFuncRef>(*entry)->internal(isolate), isolate));

  if (WasmExportedFunction::IsWasmExportedFunction(*external)) {
    auto exported_function = Cast<WasmExportedFunction>(external);
    auto func_data = exported_function->shared()->wasm_exported_function_data();
    DirectHandle<WasmTrustedInstanceData> target_instance_data(
        func_data->instance_data(), isolate);
    int func_index = func_data->function_index();
    const WasmModule* module = target_instance_data->module();
    SBXCHECK_BOUNDS(func_index, module->functions.size());
    auto* wasm_function = module->functions.data() + func_index;
    UpdateDispatchTable(isolate, table, entry_index, wasm_function,
                        target_instance_data
#if V8_ENABLE_DRUMBRAKE
                        ,
                        func_index
#endif  // V8_ENABLE_DRUMBRAKE
    );
  } else if (WasmJSFunction::IsWasmJSFunction(*external)) {
    UpdateDispatchTable(isolate, table, entry_index,
                        Cast<WasmJSFunction>(external));
  } else {
    DCHECK(WasmCapiFunction::IsWasmCapiFunction(*external));
    UpdateDispatchTable(isolate, table, entry_index,
                        Cast<WasmCapiFunction>(external));
  }
  table->entries()->set(entry_index, *entry);
}

// Note: This needs to be handlified because it can call {NewWasmImportData}.
void WasmTableObject::Set(Isolate* isolate, DirectHandle<WasmTableObject> table,
                          uint32_t index, DirectHandle<Object> entry) {
  // Callers need to perform bounds checks, type check, and error handling.
  DCHECK(table->is_in_bounds(index));

  DirectHandle<FixedArray> entries(table->entries(), isolate);
  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  switch (table->unsafe_type().heap_representation_non_shared()) {
    case wasm::HeapType::kExtern:
    case wasm::HeapType::kString:
    case wasm::HeapType::kStringViewWtf8:
    case wasm::HeapType::kStringViewWtf16:
    case wasm::HeapType::kStringViewIter:
    case wasm::HeapType::kEq:
    case wasm::HeapType::kStruct:
    case wasm::HeapType::kArray:
    case wasm::HeapType::kAny:
    case wasm::HeapType::kI31:
    case wasm::HeapType::kNone:
    case wasm::HeapType::kNoFunc:
    case wasm::HeapType::kNoExtern:
    case wasm::HeapType::kExn:
    case wasm::HeapType::kNoExn:
      entries->set(entry_index, *entry);
      return;
    case wasm::HeapType::kFunc:
      SetFunctionTableEntry(isolate, table, entry_index, entry);
      return;
    case wasm::HeapType::kBottom:
    case wasm::HeapType::kTop:
      UNREACHABLE();
    default:
      DCHECK(table->has_trusted_data());
      const wasm::WasmModule* module = table->trusted_data(isolate)->module();
      if (module->has_signature(table->type(module).ref_index())) {
        SetFunctionTableEntry(isolate, table, entry_index, entry);
        return;
      }
      entries->set(entry_index, *entry);
      return;
  }
}

Handle<Object> WasmTableObject::Get(Isolate* isolate,
                                    DirectHandle<WasmTableObject> table,
                                    uint32_t index) {
  DirectHandle<FixedArray> entries(table->entries(), isolate);
  // Callers need to perform bounds checks and error handling.
  DCHECK(table->is_in_bounds(index));

  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  Handle<Object> entry(entries->get(entry_index), isolate);

  if (IsWasmNull(*entry, isolate)) return entry;
  if (IsWasmFuncRef(*entry)) return entry;

  switch (table->unsafe_type().heap_representation_non_shared()) {
    case wasm::HeapType::kStringViewWtf8:
    case wasm::HeapType::kStringViewWtf16:
    case wasm::HeapType::kStringViewIter:
    case wasm::HeapType::kExtern:
    case wasm::HeapType::kString:
    case wasm::HeapType::kEq:
    case wasm::HeapType::kI31:
    case wasm::HeapType::kStruct:
    case wasm::HeapType::kArray:
    case wasm::HeapType::kAny:
    case wasm::HeapType::kNone:
    case wasm::HeapType::kNoFunc:
    case wasm::HeapType::kNoExtern:
    case wasm::HeapType::kExn:
    case wasm::HeapType::kNoExn:
      return entry;
    case wasm::HeapType::kFunc:
      // Placeholder; handled below.
      break;
    case wasm::HeapType::kBottom:
    case wasm::HeapType::kTop:
      UNREACHABLE();
    default:
      DCHECK(table->has_trusted_data());
      const WasmModule* module = table->trusted_data(isolate)->module();
      wasm::ModuleTypeIndex element_type = table->type(module).ref_index();
      if (module->has_array(element_type) || module->has_struct(element_type)) {
        return entry;
      }
      DCHECK(module->has_signature(element_type));
      break;
  }

  // {entry} is not a valid entry in the table. It has to be a placeholder
  // for lazy initialization.
  DirectHandle<Tuple2> tuple = Cast<Tuple2>(entry);
  auto trusted_instance_data =
      handle(Cast<WasmInstanceObject>(tuple->value1())->trusted_data(isolate),
             isolate);
  int function_index = Cast<Smi>(tuple->value2()).value();

  // Create a WasmInternalFunction and WasmFuncRef for the function if it does
  // not exist yet, and store it in the table.
  Handle<WasmFuncRef> func_ref = WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate, trusted_instance_data, function_index);
  entries->set(entry_index, *func_ref);
  return func_ref;
}

void WasmTableObject::Fill(Isolate* isolate,
                           DirectHandle<WasmTableObject> table, uint32_t start,
                           DirectHandle<Object> entry, uint32_t count) {
  // Bounds checks must be done by the caller.
  DCHECK_LE(start, table->current_length());
  DCHECK_LE(count, table->current_length());
  DCHECK_LE(start + count, table->current_length());

  for (uint32_t i = 0; i < count; i++) {
    WasmTableObject::Set(isolate, table, start + i, entry);
  }
}

#if V8_ENABLE_SANDBOX || DEBUG
bool FunctionSigMatchesTable(wasm::CanonicalTypeIndex sig_id,
                             wasm::CanonicalValueType table_type) {
  DCHECK(table_type.is_object_reference());
  // When in-sandbox data is corrupted, we can't trust the statically
  // checked types; to prevent sandbox escapes, we have to verify actual
  // types before installing the dispatch table entry. There are three
  // alternative success conditions:
  // (1) Generic "funcref" tables can hold any function entry.
  if (table_type.heap_representation_non_shared() == wasm::HeapType::kFunc) {
    return true;
  }
  // (2) Most function types are expected to be final, so they can be compared
  //     cheaply by canonicalized index equality.
  wasm::CanonicalTypeIndex canonical_table_type = table_type.ref_index();
  if (V8_LIKELY(sig_id == canonical_table_type)) return true;
  // (3) In the remaining cases, perform the full subtype check.
  return wasm::GetWasmEngine()->type_canonicalizer()->IsCanonicalSubtype(
      sig_id, canonical_table_type);
}
#endif  // V8_ENABLE_SANDBOX || DEBUG

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    const wasm::WasmFunction* func,
    DirectHandle<WasmTrustedInstanceData> target_instance_data
#if V8_ENABLE_DRUMBRAKE
    ,
    int target_func_index
#endif  // V8_ENABLE_DRUMBRAKE
) {
  DirectHandle<TrustedObject> implicit_arg =
      func->imported
          // The function in the target instance was imported. Use its imports
          // table to look up the ref.
          ? direct_handle(Cast<TrustedObject>(
                              target_instance_data->dispatch_table_for_imports()
                                  ->implicit_arg(func->func_index)),
                          isolate)
          // For wasm functions, just pass the target instance data.
          : target_instance_data;
  WasmCodePointer call_target =
      target_instance_data->GetCallTarget(func->func_index);

#if V8_ENABLE_DRUMBRAKE
  if (target_func_index <
      static_cast<int>(
          target_instance_data->module()->num_imported_functions)) {
    target_func_index = target_instance_data->imported_function_indices()->get(
        target_func_index);
  }
#endif  // V8_ENABLE_DRUMBRAKE

  const WasmModule* target_module = target_instance_data->module();
  wasm::CanonicalTypeIndex sig_id =
      target_module->canonical_sig_id(func->sig_index);
  DirectHandle<WasmDispatchTable> dispatch_table(
      table->trusted_dispatch_table(isolate), isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_id, dispatch_table->table_type()));

  if (v8_flags.wasm_generic_wrapper && IsWasmImportData(*implicit_arg)) {
    auto import_data = Cast<WasmImportData>(implicit_arg);
    DirectHandle<WasmImportData> new_import_data =
        isolate->factory()->NewWasmImportData(import_data);
    new_import_data->set_call_origin(*dispatch_table);
    new_import_data->set_table_slot(entry_index);
    implicit_arg = new_import_data;
  }
  if (target_instance_data->dispatch_table_for_imports()->IsAWrapper(
          func->func_index)) {
    wasm::WasmCodeRefScope code_ref_scope;
    uint64_t signature_hash = wasm::GetTypeCanonicalizer()
                                  ->LookupFunctionSignature(sig_id)
                                  ->signature_hash();
    dispatch_table->SetForWrapper(
        entry_index, *implicit_arg,
        wasm::GetProcessWideWasmCodePointerTable()->GetEntrypoint(
            call_target, signature_hash),
        sig_id, signature_hash,
#if V8_ENABLE_DRUMBRAKE
        target_func_index,
#endif
        wasm::GetWasmImportWrapperCache()->FindWrapper(call_target),
        WasmDispatchTable::kExistingEntry);
  } else {
    dispatch_table->SetForNonWrapper(entry_index, *implicit_arg, call_target,
                                     sig_id,
#if V8_ENABLE_DRUMBRAKE
                                     target_func_index,
#endif
                                     WasmDispatchTable::kExistingEntry);
  }

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
    int used_length = GetUsedLength(uses);
    for (int i = kReservedSlotOffset; i < used_length; i += 2) {
      if (uses->get(i).IsCleared()) continue;
      Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
      if (instance->has_interpreter_object()) {
        int table_index = GetTableIndex(uses, i);
        wasm::WasmInterpreterRuntime::UpdateIndirectCallTable(
            isolate, handle(instance->instance_object(), isolate), table_index);
      }
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmJSFunction> function) {
  Tagged<WasmJSFunctionData> function_data =
      function->shared()->wasm_js_function_data();
  wasm::CanonicalTypeIndex sig_id = function_data->sig_index();

  wasm::WasmCodeRefScope code_ref_scope;

  DirectHandle<WasmDispatchTable> dispatch_table(
      table->trusted_dispatch_table(isolate), isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_id, dispatch_table->table_type()));

  DirectHandle<WasmImportData> import_data(
      Cast<WasmImportData>(function_data->internal()->implicit_arg()), isolate);
  WasmCodePointer code_pointer = function_data->internal()->call_target();
  wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
  wasm::WasmCode* wasm_code = cache->FindWrapper(code_pointer);
  Address call_target = wasm::GetProcessWideWasmCodePointerTable()
                            ->GetEntrypointWithoutSignatureCheck(code_pointer);
  uint64_t signature_hash;
  if (wasm_code) {
    DCHECK_EQ(wasm_code->instruction_start(), call_target);
    signature_hash = wasm_code->signature_hash();
  } else {
    // The function's code_pointer is not a compiled wrapper.
    // Opportunistically check if a matching wrapper has already been
    // compiled, but otherwise don't eagerly compile it now.
    Handle<JSReceiver> callable(function_data->GetCallable(), isolate);
    const wasm::CanonicalSig* sig =
        wasm::GetWasmEngine()->type_canonicalizer()->LookupFunctionSignature(
            sig_id);
    signature_hash = sig->signature_hash();
    wasm::ResolvedWasmImport resolved({}, -1, callable, sig, sig_id,
                                      wasm::WellKnownImport::kUninstantiated);
    wasm::ImportCallKind kind = resolved.kind();
    callable = resolved.callable();  // Update to ultimate target.
    // Note: it is possible that kind == kLinkError here; e.g. that happens when
    // the WasmJSFunction wraps another function that doesn't have the same
    // signature. We don't need to check for that case: it's safe to do the
    // cache lookup below, it just won't find a code object.
    int expected_arity = static_cast<int>(sig->parameter_count());
    if (kind == wasm::ImportCallKind::kJSFunctionArityMismatch) {
      expected_arity = Cast<JSFunction>(callable)
                           ->shared()
                           ->internal_formal_parameter_count_without_receiver();
    }
    wasm::Suspend suspend = function_data->GetSuspend();
    wasm_code = cache->MaybeGet(kind, sig_id, expected_arity, suspend);
    if (wasm_code) {
      call_target = wasm_code->instruction_start();
      DCHECK_EQ(sig->signature_hash(), wasm_code->signature_hash());
    } else {
      // We still don't have a compiled wrapper. Allocate a new import_data
      // so we can store the proper call_origin for later wrapper tier-up.
      DCHECK(call_target ==
                 Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate) ||
             call_target == Builtins::EntryOf(
                                Builtin::kWasmToJsWrapperInvalidSig, isolate));
      import_data = isolate->factory()->NewWasmImportData(
          callable, suspend, MaybeDirectHandle<WasmTrustedInstanceData>{}, sig);
      import_data->SetIndexInTableAsCallOrigin(*dispatch_table, entry_index);
    }
  }

  DCHECK(wasm_code ||
         call_target ==
             Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate) ||
         call_target ==
             Builtins::EntryOf(Builtin::kWasmToJsWrapperInvalidSig, isolate));
  dispatch_table->SetForWrapper(entry_index, *import_data, call_target, sig_id,
                                signature_hash,
#if V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                wasm_code, WasmDispatchTable::kExistingEntry);
}

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmCapiFunction> capi_function) {
  DirectHandle<WasmCapiFunctionData> func_data(
      capi_function->shared()->wasm_capi_function_data(), isolate);
  const wasm::CanonicalSig* sig = func_data->sig();
  DCHECK(wasm::GetTypeCanonicalizer()->Contains(sig));
  wasm::CanonicalTypeIndex sig_index = func_data->sig_index();

  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
  auto kind = wasm::ImportCallKind::kWasmToCapi;
  int param_count = static_cast<int>(sig->parameter_count());
  wasm::WasmCode* wasm_code =
      cache->MaybeGet(kind, sig_index, param_count, wasm::kNoSuspend);
  if (wasm_code == nullptr) {
    wasm::WasmCompilationResult result =
        compiler::CompileWasmCapiCallWrapper(sig);
    {
      wasm::WasmImportWrapperCache::ModificationScope cache_scope(cache);
      wasm::WasmImportWrapperCache::CacheKey key(kind, sig_index, param_count,
                                                 wasm::kNoSuspend);
      wasm_code = cache_scope.AddWrapper(
          key, std::move(result), wasm::WasmCode::Kind::kWasmToCapiWrapper,
          sig->signature_hash());
    }
    // To avoid lock order inversion, code printing must happen after the
    // end of the {cache_scope}.
    wasm_code->MaybePrint();
    isolate->counters()->wasm_generated_code_size()->Increment(
        wasm_code->instructions().length());
    isolate->counters()->wasm_reloc_size()->Increment(
        wasm_code->reloc_info().length());
  }
  Tagged<HeapObject> implicit_arg = func_data->internal()->implicit_arg();
  Address call_target = wasm_code->instruction_start();
  Tagged<WasmDispatchTable> dispatch_table =
      table->trusted_dispatch_table(isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_index, dispatch_table->table_type()));
  dispatch_table->SetForWrapper(entry_index, implicit_arg, call_target,
                                sig_index, wasm_code->signature_hash(),
#if V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                wasm_code, WasmDispatchTable::kExistingEntry);
}

void WasmTableObject::ClearDispatchTable(int index) {
  DisallowGarbageCollection no_gc;
  Isolate* isolate = GetIsolate();
  Tagged<WasmDispatchTable> dispatch_table = trusted_dispatch_table(isolate);
  dispatch_table->Clear(index, WasmDispatchTable::kExistingEntry);
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
    int used_length = GetUsedLength(uses);
    for (int i = kReservedSlotOffset; i < used_length; i += 2) {
      if (uses->get(i).IsCleared()) continue;
      Tagged<WasmTrustedInstanceData> non_shared_instance_data =
          GetInstance(uses, i);
      if (non_shared_instance_data->has_interpreter_object()) {
        int table_index = GetTableIndex(uses, i);
        Handle<WasmInstanceObject> instance_handle =
            handle(non_shared_instance_data->instance_object(), isolate);
        wasm::WasmInterpreterRuntime::ClearIndirectCallCacheEntry(
            isolate, instance_handle, table_index, index);
      }
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

// static
void WasmTableObject::SetFunctionTablePlaceholder(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int func_index) {
  // Put (instance, func_index) as a Tuple2 into the entry_index.
  // The {WasmExportedFunction} will be created lazily.
  // Allocate directly in old space as the tuples are typically long-lived, and
  // we create many of them, which would result in lots of GC when initializing
  // large tables.
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(trusted_instance_data->has_instance_object());
  DirectHandle<Tuple2> tuple = isolate->factory()->NewTuple2(
      direct_handle(trusted_instance_data->instance_object(), isolate),
      direct_handle(Smi::FromInt(func_index), isolate), AllocationType::kOld);
  table->entries()->set(entry_index, *tuple);
}

// static
void WasmTableObject::GetFunctionTableEntry(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    bool* is_valid, bool* is_null,
    MaybeHandle<WasmTrustedInstanceData>* instance_data, int* function_index,
    MaybeDirectHandle<WasmJSFunction>* maybe_js_function) {
#if DEBUG
  if (table->has_trusted_data()) {
    const wasm::WasmModule* module = table->trusted_data(isolate)->module();
    DCHECK(wasm::IsSubtypeOf(table->type(module), wasm::kWasmFuncRef, module));
  } else {
    // A function table defined outside a module may only have type exactly
    // {funcref}.
    DCHECK(table->unsafe_type() == wasm::kWasmFuncRef);
  }
  DCHECK_LT(entry_index, table->current_length());
#endif
  // We initialize {is_valid} with {true}. We may change it later.
  *is_valid = true;
  DirectHandle<Object> element(table->entries()->get(entry_index), isolate);

  *is_null = IsWasmNull(*element, isolate);
  if (*is_null) return;

  if (IsWasmFuncRef(*element)) {
    DirectHandle<WasmInternalFunction> internal{
        Cast<WasmFuncRef>(*element)->internal(isolate), isolate};
    element = WasmInternalFunction::GetOrCreateExternal(internal);
  }
  if (WasmExportedFunction::IsWasmExportedFunction(*element)) {
    auto target_func = Cast<WasmExportedFunction>(element);
    auto func_data = Cast<WasmExportedFunctionData>(
        target_func->shared()->wasm_exported_function_data());
    *instance_data = handle(func_data->instance_data(), isolate);
    *function_index = func_data->function_index();
    *maybe_js_function = MaybeDirectHandle<WasmJSFunction>();
    return;
  }
  if (WasmJSFunction::IsWasmJSFunction(*element)) {
    *instance_data = MaybeHandle<WasmTrustedInstanceData>();
    *maybe_js_function = Cast<WasmJSFunction>(element);
    return;
  }
  if (IsTuple2(*element)) {
    auto tuple = Cast<Tuple2>(element);
    *instance_data =
        handle(Cast<WasmInstanceObject>(tuple->value1())->trusted_data(isolate),
               isolate);
    *function_index = Cast<Smi>(tuple->value2()).value();
    *maybe_js_function = MaybeDirectHandle<WasmJSFunction>();
    return;
  }
  *is_valid = false;
}

DirectHandle<WasmSuspendingObject> WasmSuspendingObject::New(
    Isolate* isolate, DirectHandle<JSReceiver> callable) {
  DirectHandle<JSFunction> suspending_ctor(
      isolate->native_context()->wasm_suspending_constructor(), isolate);
  auto suspending_obj = Cast<WasmSuspendingObject>(
      isolate->factory()->NewJSObject(suspending_ctor));
  suspending_obj->set_callable(*callable);
  return suspending_obj;
}

namespace {

void SetInstanceMemory(Tagged<WasmTrustedInstanceData> trusted_instance_data,
                       Tagged<JSArrayBuffer> buffer, int memory_index) {
  DisallowHeapAllocation no_gc;
  const WasmModule* module = trusted_instance_data->module();
  const wasm::WasmMemory& memory = module->memories[memory_index];

  bool is_wasm_module = module->origin == wasm::kWasmOrigin;
  bool use_trap_handler = memory.bounds_checks == wasm::kTrapHandler;
  // Asm.js does not use trap handling.
  CHECK_IMPLIES(use_trap_handler, is_wasm_module);
  // ArrayBuffers allocated for Wasm do always have a BackingStore.
  std::shared_ptr<BackingStore> backing_store = buffer->GetBackingStore();
  CHECK_IMPLIES(is_wasm_module, backing_store);
  CHECK_IMPLIES(is_wasm_module, backing_store->is_wasm_memory());
  // Wasm modules compiled to use the trap handler don't have bounds checks,
  // so they must have a memory that has guard regions.
  // Note: This CHECK can fail when in-sandbox corruption modified a
  // WasmMemoryObject. We currently believe that this would at worst
  // corrupt the contents of other Wasm memories or ArrayBuffers, but having
  // this CHECK in release mode is nice as an additional layer of defense.
  CHECK_IMPLIES(use_trap_handler, backing_store->has_guard_regions());
  // We checked this before, but a malicious worker thread with an in-sandbox
  // corruption primitive could have modified it since then.
  size_t byte_length = buffer->byte_length();
  SBXCHECK_GE(byte_length, memory.min_memory_size);

  trusted_instance_data->SetRawMemory(
      memory_index, reinterpret_cast<uint8_t*>(buffer->backing_store()),
      byte_length);

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless &&
      trusted_instance_data->has_interpreter_object()) {
    AllowHeapAllocation allow_heap;
    Isolate* isolate = trusted_instance_data->instance_object()->GetIsolate();
    HandleScope scope(isolate);
    wasm::WasmInterpreterRuntime::UpdateMemoryAddress(
        handle(trusted_instance_data->instance_object(), isolate));
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

}  // namespace

Handle<WasmMemoryObject> WasmMemoryObject::New(
    Isolate* isolate, DirectHandle<JSArrayBuffer> buffer, int maximum,
    wasm::AddressType address_type) {
  DirectHandle<JSFunction> memory_ctor(
      isolate->native_context()->wasm_memory_constructor(), isolate);

  auto memory_object = Cast<WasmMemoryObject>(
      isolate->factory()->NewJSObject(memory_ctor, AllocationType::kOld));
  memory_object->set_array_buffer(*buffer);
  memory_object->set_maximum_pages(maximum);
  memory_object->set_address_type(address_type);
  memory_object->set_padding_for_address_type_0(0);
  memory_object->set_padding_for_address_type_1(0);
#if TAGGED_SIZE_8_BYTES
  memory_object->set_padding_for_address_type_2(0);
#endif
  memory_object->set_instances(ReadOnlyRoots{isolate}.empty_weak_array_list());

  std::shared_ptr<BackingStore> backing_store = buffer->GetBackingStore();
  if (buffer->is_shared()) {
    // Only Wasm memory can be shared (in contrast to asm.js memory).
    CHECK(backing_store && backing_store->is_wasm_memory());
    backing_store->AttachSharedWasmMemoryObject(isolate, memory_object);
  } else if (backing_store) {
    CHECK(!backing_store->is_shared());
  }

  // For debugging purposes we memorize a link from the JSArrayBuffer
  // to its owning WasmMemoryObject instance.
  DirectHandle<Symbol> symbol =
      isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, buffer, symbol, memory_object).Check();

  return memory_object;
}

MaybeHandle<WasmMemoryObject> WasmMemoryObject::New(
    Isolate* isolate, int initial, int maximum, SharedFlag shared,
    wasm::AddressType address_type) {
  bool has_maximum = maximum != kNoMaximum;

  int engine_maximum = address_type == wasm::AddressType::kI64
                           ? static_cast<int>(wasm::max_mem64_pages())
                           : static_cast<int>(wasm::max_mem32_pages());

  if (initial > engine_maximum) return {};

#ifdef V8_TARGET_ARCH_32_BIT
  // On 32-bit platforms we need an heuristic here to balance overall memory
  // and address space consumption.
  constexpr int kGBPages = 1024 * 1024 * 1024 / wasm::kWasmPageSize;
  // We allocate the smallest of the following sizes, but at least the initial
  // size:
  // 1) the module-defined maximum;
  // 2) 1GB;
  // 3) the engine maximum;
  int allocation_maximum = std::min(kGBPages, engine_maximum);
  int heuristic_maximum;
  if (initial > kGBPages) {
    // We always allocate at least the initial size.
    heuristic_maximum = initial;
  } else if (has_maximum) {
    // We try to reserve the maximum, but at most the allocation_maximum to
    // avoid OOMs.
    heuristic_maximum = std::min(maximum, allocation_maximum);
  } else if (shared == SharedFlag::kShared) {
    // If shared memory has no maximum, we use the allocation_maximum as an
    // implicit maximum.
    heuristic_maximum = allocation_maximum;
  } else {
    // If non-shared memory has no maximum, we only allocate the initial size
    // and then grow with realloc.
    heuristic_maximum = initial;
  }
#else
  int heuristic_maximum =
      has_maximum ? std::min(engine_maximum, maximum) : engine_maximum;
#endif

  std::unique_ptr<BackingStore> backing_store =
      BackingStore::AllocateWasmMemory(isolate, initial, heuristic_maximum,
                                       address_type == wasm::AddressType::kI32
                                           ? WasmMemoryFlag::kWasmMemory32
                                           : WasmMemoryFlag::kWasmMemory64,
                                       shared);

  if (!backing_store) return {};

  DirectHandle<JSArrayBuffer> buffer =
      shared == SharedFlag::kShared
          ? isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store))
          : isolate->factory()->NewJSArrayBuffer(std::move(backing_store));

  return New(isolate, buffer, maximum, address_type);
}

void WasmMemoryObject::UseInInstance(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    DirectHandle<WasmTrustedInstanceData> shared_trusted_instance_data,
    int memory_index_in_instance) {
  SetInstanceMemory(*trusted_instance_data, memory->array_buffer(),
                    memory_index_in_instance);
  if (!shared_trusted_instance_data.is_null()) {
    SetInstanceMemory(*shared_trusted_instance_data, memory->array_buffer(),
                      memory_index_in_instance);
  }
  Handle<WeakArrayList> instances{memory->instances(), isolate};
  auto weak_instance_object = MaybeObjectDirectHandle::Weak(
      trusted_instance_data->instance_object(), isolate);
  instances = WeakArrayList::Append(isolate, instances, weak_instance_object);
  memory->set_instances(*instances);
}

void WasmMemoryObject::SetNewBuffer(Tagged<JSArrayBuffer> new_buffer) {
  DisallowGarbageCollection no_gc;
  set_array_buffer(new_buffer);
  Tagged<WeakArrayList> instances = this->instances();
  Isolate* isolate = GetIsolate();
  for (int i = 0, len = instances->length(); i < len; ++i) {
    Tagged<MaybeObject> elem = instances->Get(i);
    if (elem.IsCleared()) continue;
    Tagged<WasmInstanceObject> instance_object =
        Cast<WasmInstanceObject>(elem.GetHeapObjectAssumeWeak());
    Tagged<WasmTrustedInstanceData> trusted_data =
        instance_object->trusted_data(isolate);
    // TODO(clemens): Avoid the iteration by also remembering the memory index
    // if we ever see larger numbers of memories.
    Tagged<FixedArray> memory_objects = trusted_data->memory_objects();
    int num_memories = memory_objects->length();
    for (int mem_idx = 0; mem_idx < num_memories; ++mem_idx) {
      if (memory_objects->get(mem_idx) == *this) {
        SetInstanceMemory(trusted_data, new_buffer, mem_idx);
      }
    }
  }
}

// static
int32_t WasmMemoryObject::Grow(Isolate* isolate,
                               DirectHandle<WasmMemoryObject> memory_object,
                               uint32_t pages) {
  TRACE_EVENT0("v8.wasm", "wasm.GrowMemory");
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);

  std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
  // Only Wasm memory can grow, and Wasm memory always has a backing store.
  DCHECK_NOT_NULL(backing_store);

  // Check for maximum memory size.
  // Note: The {wasm::max_mem_pages()} limit is already checked in
  // {BackingStore::CopyWasmMemory}, and is irrelevant for
  // {GrowWasmMemoryInPlace} because memory is never allocated with more
  // capacity than that limit.
  size_t old_size = old_buffer->byte_length();
  DCHECK_EQ(0, old_size % wasm::kWasmPageSize);
  size_t old_pages = old_size / wasm::kWasmPageSize;
  size_t max_pages = memory_object->is_memory64() ? wasm::max_mem64_pages()
                                                  : wasm::max_mem32_pages();
  if (memory_object->has_maximum_pages()) {
    max_pages = std::min(max_pages,
                         static_cast<size_t>(memory_object->maximum_pages()));
  }
  DCHECK_GE(max_pages, old_pages);
  if (pages > max_pages - old_pages) return -1;

  const bool must_grow_in_place =
      old_buffer->is_shared() || backing_store->has_guard_regions();
  const bool try_grow_in_place =
      must_grow_in_place || !v8_flags.stress_wasm_memory_moving;

  std::optional<size_t> result_inplace =
      try_grow_in_place
          ? backing_store->GrowWasmMemoryInPlace(isolate, pages, max_pages)
          : std::nullopt;
  if (must_grow_in_place && !result_inplace.has_value()) {
    // There are different limits per platform, thus crash if the correctness
    // fuzzer is running.
    if (v8_flags.correctness_fuzzer_suppressions) {
      FATAL("could not grow wasm memory");
    }
    return -1;
  }

  // Handle shared memory first.
  if (old_buffer->is_shared()) {
    DCHECK(result_inplace.has_value());
    backing_store->BroadcastSharedWasmMemoryGrow(isolate);
    // Broadcasting the update should update this memory object too.
    CHECK_NE(*old_buffer, memory_object->array_buffer());
    size_t new_pages = result_inplace.value() + pages;
    // If the allocation succeeded, then this can't possibly overflow:
    size_t new_byte_length = new_pages * wasm::kWasmPageSize;
    // This is a less than check, as it is not guaranteed that the SAB
    // length here will be equal to the stashed length above as calls to
    // grow the same memory object can come in from different workers.
    // It is also possible that a call to Grow was in progress when
    // handling this call.
    CHECK_LE(new_byte_length, memory_object->array_buffer()->byte_length());
    // As {old_pages} was read racefully, we return here the synchronized
    // value provided by {GrowWasmMemoryInPlace}, to provide the atomic
    // read-modify-write behavior required by the spec.
    return static_cast<int32_t>(result_inplace.value());  // success
  }

  // Check if the non-shared memory could grow in-place.
  if (result_inplace.has_value()) {
    // Detach old and create a new one with the grown backing store.
    JSArrayBuffer::Detach(old_buffer, true).Check();
    DirectHandle<JSArrayBuffer> new_buffer =
        isolate->factory()->NewJSArrayBuffer(std::move(backing_store));
    memory_object->SetNewBuffer(*new_buffer);
    // For debugging purposes we memorize a link from the JSArrayBuffer
    // to its owning WasmMemoryObject instance.
    DirectHandle<Symbol> symbol =
        isolate->factory()->array_buffer_wasm_memory_symbol();
    Object::SetProperty(isolate, new_buffer, symbol, memory_object).Check();
    DCHECK_EQ(result_inplace.value(), old_pages);
    return static_cast<int32_t>(result_inplace.value());  // success
  }

  size_t new_pages = old_pages + pages;
  // Check for overflow (should be excluded via {max_pages} above).
  DCHECK_LE(old_pages, new_pages);
  // Trying to grow in-place without actually growing must always succeed.
  DCHECK_IMPLIES(try_grow_in_place, old_pages < new_pages);

  // Try allocating a new backing store and copying.
  // To avoid overall quadratic complexity of many small grow operations, we
  // grow by at least 0.5 MB + 12.5% of the existing memory size.
  // These numbers are kept small because we must be careful about address
  // space consumption on 32-bit platforms.
  size_t min_growth = old_pages + 8 + (old_pages >> 3);
  // First apply {min_growth}, then {max_pages}. The order is important, because
  // {min_growth} can be bigger than {max_pages}, and in that case we want to
  // cap to {max_pages}.
  size_t new_capacity = std::min(max_pages, std::max(new_pages, min_growth));
  DCHECK_LE(new_pages, new_capacity);
  std::unique_ptr<BackingStore> new_backing_store =
      backing_store->CopyWasmMemory(isolate, new_pages, new_capacity,
                                    memory_object->is_memory64()
                                        ? WasmMemoryFlag::kWasmMemory64
                                        : WasmMemoryFlag::kWasmMemory32);
  if (!new_backing_store) {
    // Crash on out-of-memory if the correctness fuzzer is running.
    if (v8_flags.correctness_fuzzer_suppressions) {
      FATAL("could not grow wasm memory");
    }
    // Set the non-determinism flag in the WasmEngine.
    wasm::WasmEngine::set_had_nondeterminism();
    return -1;
  }

  // Detach old and create a new one with the new backing store.
  JSArrayBuffer::Detach(old_buffer, true).Check();
  DirectHandle<JSArrayBuffer> new_buffer =
      isolate->factory()->NewJSArrayBuffer(std::move(new_backing_store));
  memory_object->SetNewBuffer(*new_buffer);
  // For debugging purposes we memorize a link from the JSArrayBuffer
  // to its owning WasmMemoryObject instance.
  DirectHandle<Symbol> symbol =
      isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, new_buffer, symbol, memory_object).Check();
  return static_cast<int32_t>(old_pages);  // success
}

MaybeDirectHandle<WasmMemoryMapDescriptor>
WasmMemoryMapDescriptor::NewFromAnonymous(Isolate* isolate, size_t length) {
#if V8_TARGET_OS_LINUX
  CHECK(v8_flags.experimental_wasm_memory_control);
  DirectHandle<JSFunction> descriptor_ctor(
      isolate->native_context()->wasm_memory_map_descriptor_constructor(),
      isolate);

  auto descriptor_object = Cast<WasmMemoryMapDescriptor>(
      isolate->factory()->NewJSObject(descriptor_ctor, AllocationType::kOld));

  int file_descriptor = memfd_create("wasm_memory_map_descriptor", MFD_CLOEXEC);
  printf("Create file descriptor: %d\n", file_descriptor);
  if (file_descriptor == -1) {
    return {};
  }
  int ret_val = ftruncate(file_descriptor, length);
  if (ret_val == -1) {
    return {};
  }
  v8::WasmMemoryMapDescriptor::WasmFileDescriptor wasm_fd =
      static_cast<v8::WasmMemoryMapDescriptor::WasmFileDescriptor>(
          file_descriptor);
  descriptor_object->set_file_descriptor(wasm_fd);

  return descriptor_object;
#else   // V8_TARGET_OS_LINUX
  return {};
#endif  // V8_TARGET_OS_LINUX
}

DirectHandle<WasmMemoryMapDescriptor>
WasmMemoryMapDescriptor::NewFromFileDescriptor(Isolate* isolate,
                                               int file_descriptor) {
  CHECK(v8_flags.experimental_wasm_memory_control);
  DirectHandle<JSFunction> descriptor_ctor(
      isolate->native_context()->wasm_memory_map_descriptor_constructor(),
      isolate);

  auto descriptor_object = Cast<WasmMemoryMapDescriptor>(
      isolate->factory()->NewJSObject(descriptor_ctor, AllocationType::kOld));

  descriptor_object->set_file_descriptor(file_descriptor);

  return descriptor_object;
}

size_t WasmMemoryObject::MapDescriptor(
    DirectHandle<WasmMemoryMapDescriptor> descriptor, size_t offset) {
#if V8_TARGET_OS_LINUX
  CHECK(v8_flags.experimental_wasm_memory_control);
  if (this->array_buffer()->is_shared()) {
    // TODO(ahaas): Handle concurrent calls to `MapDescriptor`. To prevent
    // concurrency issues, we disable `MapDescriptor` for shared wasm memories
    // so far.
    return 0;
  }

  uint8_t* target =
      reinterpret_cast<uint8_t*>(this->array_buffer()->backing_store()) +
      offset;

  struct stat stat_for_size;
  if (fstat(descriptor->file_descriptor(), &stat_for_size) == -1) {
    // Could not determine file size.
    return 0;
  }
  size_t size = RoundUp(stat_for_size.st_size,
                        GetArrayBufferPageAllocator()->AllocatePageSize());

  if (size + offset < size) {
    // Overflow
    return 0;
  }
  if (size + offset > this->array_buffer()->byte_length()) {
    return 0;
  }

  void* ret_val =
      mmap(target, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
           descriptor->file_descriptor(), 0);
  if (ret_val == MAP_FAILED) {
    printf("%s\n", strerror(errno));
    return 0;
  }
  DCHECK_EQ(ret_val, target);
  return size;
#else
  return 0;
#endif
}

// static
MaybeHandle<WasmGlobalObject> WasmGlobalObject::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
    MaybeDirectHandle<JSArrayBuffer> maybe_untagged_buffer,
    MaybeDirectHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
    int32_t offset, bool is_mutable) {
  DirectHandle<JSFunction> global_ctor(
      isolate->native_context()->wasm_global_constructor(), isolate);
  auto global_obj =
      Cast<WasmGlobalObject>(isolate->factory()->NewJSObject(global_ctor));
  {
    // Disallow GC until all fields have acceptable types.
    DisallowGarbageCollection no_gc;
    if (!trusted_data.is_null()) {
      global_obj->set_trusted_data(*trusted_data);
    } else {
      global_obj->clear_trusted_data();
    }
    global_obj->set_type(type);
    global_obj->set_offset(offset);
    global_obj->set_is_mutable(is_mutable);
  }

  if (type.is_reference()) {
    DCHECK(maybe_untagged_buffer.is_null());
    DirectHandle<FixedArray> tagged_buffer;
    if (!maybe_tagged_buffer.ToHandle(&tagged_buffer)) {
      // If no buffer was provided, create one.
      tagged_buffer =
          isolate->factory()->NewFixedArray(1, AllocationType::kOld);
      CHECK_EQ(offset, 0);
    }
    global_obj->set_tagged_buffer(*tagged_buffer);
  } else {
    DCHECK(maybe_tagged_buffer.is_null());
    uint32_t type_size = type.value_kind_size();

    DirectHandle<JSArrayBuffer> untagged_buffer;
    if (!maybe_untagged_buffer.ToHandle(&untagged_buffer)) {
      MaybeDirectHandle<JSArrayBuffer> result =
          isolate->factory()->NewJSArrayBufferAndBackingStore(
              offset + type_size, InitializedFlag::kZeroInitialized);

      if (!result.ToHandle(&untagged_buffer)) return {};
    }

    // Check that the offset is in bounds.
    CHECK_LE(offset + type_size, untagged_buffer->byte_length());

    global_obj->set_untagged_buffer(*untagged_buffer);
  }

  return global_obj;
}

FunctionTargetAndImplicitArg::FunctionTargetAndImplicitArg(
    Isolate* isolate, Handle<WasmTrustedInstanceData> target_instance_data,
    int target_func_index) {
  implicit_arg_ = target_instance_data;
  if (target_func_index <
      static_cast<int>(
          target_instance_data->module()->num_imported_functions)) {
    // The function in the target instance was imported. Load the ref from the
    // dispatch table for imports.
    implicit_arg_ = handle(
        Cast<TrustedObject>(
            target_instance_data->dispatch_table_for_imports()->implicit_arg(
                target_func_index)),
        isolate);
#if V8_ENABLE_DRUMBRAKE
    target_func_index_ = target_instance_data->imported_function_indices()->get(
        target_func_index);
#endif  // V8_ENABLE_DRUMBRAKE
  } else {
    // The function in the target instance was not imported.
#if V8_ENABLE_DRUMBRAKE
    target_func_index_ = target_func_index;
#endif  // V8_ENABLE_DRUMBRAKE
  }
  call_target_ = target_instance_data->GetCallTarget(target_func_index);
}

void ImportedFunctionEntry::SetGenericWasmToJs(
    Isolate* isolate, DirectHandle<JSReceiver> callable, wasm::Suspend suspend,
    const wasm::CanonicalSig* sig, wasm::CanonicalTypeIndex sig_id) {
  Address wrapper_entry;
  if (wasm::IsJSCompatibleSignature(sig)) {
    DCHECK(
        UseGenericWasmToJSWrapper(wasm::kDefaultImportCallKind, sig, suspend));
    wrapper_entry = Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate);
  } else {
    wrapper_entry =
        Builtins::EntryOf(Builtin::kWasmToJsWrapperInvalidSig, isolate);
  }
  TRACE_IFT("Import callable 0x%" PRIxPTR "[%d] = {callable=0x%" PRIxPTR
            ", target=0x%" PRIxPTR "}\n",
            instance_data_->ptr(), index_, callable->ptr(), wrapper_entry);
  DirectHandle<WasmImportData> import_data =
      isolate->factory()->NewWasmImportData(callable, suspend, instance_data_,
                                            sig);
  import_data->SetIndexInTableAsCallOrigin(
      instance_data_->dispatch_table_for_imports(), index_);
  DisallowGarbageCollection no_gc;

  instance_data_->dispatch_table_for_imports()->SetForWrapper(
      index_, *import_data, wrapper_entry, sig_id, sig->signature_hash(),
#if V8_ENABLE_DRUMBRAKE
      WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
      nullptr, WasmDispatchTable::kNewEntry);
#if V8_ENABLE_DRUMBRAKE
  instance_data_->imported_function_indices()->set(index_, -1);
#endif  // V8_ENABLE_DRUMBRAKE
}

void ImportedFunctionEntry::SetCompiledWasmToJs(
    Isolate* isolate, DirectHandle<JSReceiver> callable,
    wasm::WasmCode* wasm_to_js_wrapper, wasm::Suspend suspend,
    const wasm::CanonicalSig* sig, wasm::CanonicalTypeIndex sig_id) {
  TRACE_IFT("Import callable 0x%" PRIxPTR "[%d] = {callable=0x%" PRIxPTR
            ", target=%p}\n",
            instance_data_->ptr(), index_, callable->ptr(),
            wasm_to_js_wrapper ? nullptr
                               : wasm_to_js_wrapper->instructions().begin());
  DCHECK(v8_flags.wasm_jitless ||
         wasm_to_js_wrapper->kind() == wasm::WasmCode::kWasmToJsWrapper ||
         wasm_to_js_wrapper->kind() == wasm::WasmCode::kWasmToCapiWrapper);
  DirectHandle<WasmImportData> import_data =
      isolate->factory()->NewWasmImportData(callable, suspend, instance_data_,
                                            sig);
  DisallowGarbageCollection no_gc;
  Tagged<WasmDispatchTable> dispatch_table =
      instance_data_->dispatch_table_for_imports();
  DCHECK_EQ(v8_flags.wasm_jitless, wasm_to_js_wrapper == nullptr);
  DCHECK_IMPLIES(wasm_to_js_wrapper != nullptr,
                 wasm_to_js_wrapper->signature_hash() == sig->signature_hash());

  dispatch_table->SetForWrapper(
      index_, *import_data,
      v8_flags.wasm_jitless ? Address{}
                            : wasm_to_js_wrapper->instruction_start(),
      sig_id, sig->signature_hash(),
#if V8_ENABLE_DRUMBRAKE
      WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
      wasm_to_js_wrapper, WasmDispatchTable::kNewEntry);

#if V8_ENABLE_DRUMBRAKE
  instance_data_->imported_function_indices()->set(index_, -1);
#endif  // V8_ENABLE_DRUMBRAKE
}

void ImportedFunctionEntry::SetWasmToWasm(
    Tagged<WasmTrustedInstanceData> target_instance_data,
    WasmCodePointer call_target, wasm::CanonicalTypeIndex sig_id
#if V8_ENABLE_DRUMBRAKE
    ,
    int exported_function_index
#endif  // V8_ENABLE_DRUMBRAKE
) {
  TRACE_IFT("Import Wasm 0x%" PRIxPTR "[%d] = {instance_data=0x%" PRIxPTR
            ", target=0x%" PRIxPTR "}\n",
            instance_data_->ptr(), index_, target_instance_data.ptr(),
            wasm::GetProcessWideWasmCodePointerTable()
                ->GetEntrypointWithoutSignatureCheck(call_target));
  DisallowGarbageCollection no_gc;
  Tagged<WasmDispatchTable> dispatch_table =
      instance_data_->dispatch_table_for_imports();
  dispatch_table->SetForNonWrapper(index_, target_instance_data, call_target,
                                   sig_id,
#if V8_ENABLE_DRUMBRAKE
                                   WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                   WasmDispatchTable::kExistingEntry);

#if V8_ENABLE_DRUMBRAKE
  instance_data_->imported_function_indices()->set(index_,
                                                   exported_function_index);
#endif  // V8_ENABLE_DRUMBRAKE
}

// Returns an empty Tagged<Object>() if no callable is available, a JSReceiver
// otherwise.
Tagged<Object> ImportedFunctionEntry::maybe_callable() {
  Tagged<Object> data = implicit_arg();
  if (!IsWasmImportData(data)) return Tagged<Object>();
  return Cast<JSReceiver>(Cast<WasmImportData>(data)->callable());
}

Tagged<JSReceiver> ImportedFunctionEntry::callable() {
  return Cast<JSReceiver>(Cast<WasmImportData>(implicit_arg())->callable());
}

Tagged<Object> ImportedFunctionEntry::implicit_arg() {
  return instance_data_->dispatch_table_for_imports()->implicit_arg(index_);
}

WasmCodePointer ImportedFunctionEntry::target() {
  return instance_data_->dispatch_table_for_imports()->target(index_);
}

#if V8_ENABLE_DRUMBRAKE
int ImportedFunctionEntry::function_index_in_called_module() {
  return instance_data_->imported_function_indices()->get(index_);
}
#endif  // V8_ENABLE_DRUMBRAKE

// static
constexpr std::array<uint16_t, WasmTrustedInstanceData::kTaggedFieldsCount>
    WasmTrustedInstanceData::kTaggedFieldOffsets;
// static
constexpr std::array<const char*, WasmTrustedInstanceData::kTaggedFieldsCount>
    WasmTrustedInstanceData::kTaggedFieldNames;
// static
constexpr std::array<uint16_t, 6>
    WasmTrustedInstanceData::kProtectedFieldOffsets;
// static
constexpr std::array<const char*, 6>
    WasmTrustedInstanceData::kProtectedFieldNames;

void WasmTrustedInstanceData::SetRawMemory(int memory_index, uint8_t* mem_start,
                                           size_t mem_size) {
  CHECK_LT(memory_index, module()->memories.size());

  CHECK_LE(mem_size, module()->memories[memory_index].is_memory64()
                         ? wasm::max_mem64_bytes()
                         : wasm::max_mem32_bytes());
  // All memory bases and sizes are stored in a TrustedFixedAddressArray.
  Tagged<TrustedFixedAddressArray> bases_and_sizes = memory_bases_and_sizes();
  bases_and_sizes->set(memory_index * 2, reinterpret_cast<Address>(mem_start));
  bases_and_sizes->set(memory_index * 2 + 1, mem_size);
  // Memory 0 has fast-access fields.
  if (memory_index == 0) {
    set_memory0_start(mem_start);
    set_memory0_size(mem_size);
  }
}

#if V8_ENABLE_DRUMBRAKE
Handle<Tuple2> WasmTrustedInstanceData::GetOrCreateInterpreterObject(
    Handle<WasmInstanceObject> instance) {
  DCHECK(v8_flags.wasm_jitless);
  Isolate* isolate = instance->GetIsolate();
  Handle<WasmTrustedInstanceData> trusted_data =
      handle(instance->trusted_data(isolate), isolate);
  if (trusted_data->has_interpreter_object()) {
    return handle(trusted_data->interpreter_object(), isolate);
  }
  Handle<Tuple2> new_interpreter = WasmInterpreterObject::New(instance);
  DCHECK(trusted_data->has_interpreter_object());
  return new_interpreter;
}

Handle<Tuple2> WasmTrustedInstanceData::GetInterpreterObject(
    Handle<WasmInstanceObject> instance) {
  DCHECK(v8_flags.wasm_jitless);
  Isolate* isolate = instance->GetIsolate();
  Handle<WasmTrustedInstanceData> trusted_data =
      handle(instance->trusted_data(isolate), isolate);
  CHECK(trusted_data->has_interpreter_object());
  return handle(trusted_data->interpreter_object(), isolate);
}
#endif  // V8_ENABLE_DRUMBRAKE

Handle<WasmTrustedInstanceData> WasmTrustedInstanceData::New(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object,
    bool shared) {
  // Read the link to the {std::shared_ptr<NativeModule>} once from the
  // `module_object` and use it to initialize the fields of the
  // `WasmTrustedInstanceData`. It will then be stored in a `TrustedManaged` in
  // the `WasmTrustedInstanceData` where it is safe from manipulation.
  std::shared_ptr<wasm::NativeModule> native_module =
      module_object->shared_native_module();

  // Do first allocate all objects that will be stored in instance fields,
  // because otherwise we would have to allocate when the instance is not fully
  // initialized yet, which can lead to heap verification errors.
  const WasmModule* module = native_module->module();

  int num_imported_functions = module->num_imported_functions;
  DirectHandle<WasmDispatchTable> dispatch_table_for_imports =
      isolate->factory()->NewWasmDispatchTable(num_imported_functions,
                                               wasm::kCanonicalFuncRef);
  DirectHandle<FixedArray> well_known_imports =
      isolate->factory()->NewFixedArray(num_imported_functions);

  DirectHandle<FixedArray> func_refs =
      isolate->factory()->NewFixedArrayWithZeroes(
          static_cast<int>(module->functions.size()));

  int num_imported_mutable_globals = module->num_imported_mutable_globals;
  // The imported_mutable_globals is essentially a FixedAddressArray (storing
  // sandboxed pointers), but some entries (the indices for reference-type
  // globals) are accessed as 32-bit integers which is more convenient with a
  // raw ByteArray.
  DirectHandle<FixedAddressArray> imported_mutable_globals =
      FixedAddressArray::New(isolate, num_imported_mutable_globals);

  int num_data_segments = module->num_declared_data_segments;
  DirectHandle<FixedAddressArray> data_segment_starts =
      FixedAddressArray::New(isolate, num_data_segments);
  DirectHandle<FixedUInt32Array> data_segment_sizes =
      FixedUInt32Array::New(isolate, num_data_segments);

#if V8_ENABLE_DRUMBRAKE
  Handle<FixedInt32Array> imported_function_indices =
      FixedInt32Array::New(isolate, num_imported_functions);
#endif  // V8_ENABLE_DRUMBRAKE

  static_assert(wasm::kV8MaxWasmMemories < kMaxInt / 2);
  int num_memories = static_cast<int>(module->memories.size());
  DirectHandle<FixedArray> memory_objects =
      isolate->factory()->NewFixedArray(num_memories);
  DirectHandle<TrustedFixedAddressArray> memory_bases_and_sizes =
      TrustedFixedAddressArray::New(isolate, 2 * num_memories);

  // TODO(clemensb): Should we have singleton empty dispatch table in the
  // trusted space?
  DirectHandle<WasmDispatchTable> empty_dispatch_table =
      isolate->factory()->NewWasmDispatchTable(0, wasm::kCanonicalFuncRef);
  DirectHandle<ProtectedFixedArray> empty_protected_fixed_array =
      isolate->factory()->empty_protected_fixed_array();

  // Use the same memory estimate as the (untrusted) Managed in
  // WasmModuleObject. This is not security critical, and we at least always
  // read the memory estimation of *some* NativeModule here.
  size_t estimated_size =
      module_object->managed_native_module()->estimated_size();
  DirectHandle<TrustedManaged<wasm::NativeModule>>
      trusted_managed_native_module = TrustedManaged<wasm::NativeModule>::From(
          isolate, estimated_size, native_module);

  // Now allocate the WasmTrustedInstanceData.
  // During this step, no more allocations should happen because the instance is
  // incomplete yet, so we should not trigger heap verification at this point.
  Handle<WasmTrustedInstanceData> trusted_data =
      isolate->factory()->NewWasmTrustedInstanceData();
  {
    DisallowHeapAllocation no_gc;

    // Some constants:
    uint8_t* empty_backing_store_buffer =
        reinterpret_cast<uint8_t*>(EmptyBackingStoreBuffer());
    ReadOnlyRoots ro_roots{isolate};
    Tagged<FixedArray> empty_fixed_array = ro_roots.empty_fixed_array();

    trusted_data->set_dispatch_table_for_imports(*dispatch_table_for_imports);
    trusted_data->set_imported_mutable_globals(*imported_mutable_globals);
    trusted_data->set_dispatch_table0(*empty_dispatch_table);
    trusted_data->set_dispatch_tables(*empty_protected_fixed_array);
    trusted_data->set_shared_part(*trusted_data);  // TODO(14616): Good enough?
    trusted_data->set_data_segment_starts(*data_segment_starts);
    trusted_data->set_data_segment_sizes(*data_segment_sizes);
    trusted_data->set_element_segments(empty_fixed_array);
    trusted_data->set_managed_native_module(*trusted_managed_native_module);
    trusted_data->set_new_allocation_limit_address(
        isolate->heap()->NewSpaceAllocationLimitAddress());
    trusted_data->set_new_allocation_top_address(
        isolate->heap()->NewSpaceAllocationTopAddress());
    trusted_data->set_old_allocation_limit_address(
        isolate->heap()->OldSpaceAllocationLimitAddress());
    trusted_data->set_old_allocation_top_address(
        isolate->heap()->OldSpaceAllocationTopAddress());
    trusted_data->set_globals_start(empty_backing_store_buffer);
#if V8_ENABLE_DRUMBRAKE
    trusted_data->set_imported_function_indices(*imported_function_indices);
#endif  // V8_ENABLE_DRUMBRAKE
    trusted_data->set_native_context(*isolate->native_context());
    trusted_data->set_jump_table_start(native_module->jump_table_start());
    trusted_data->set_hook_on_function_call_address(
        isolate->debug()->hook_on_function_call_address());
    trusted_data->set_managed_object_maps(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_well_known_imports(*well_known_imports);
    trusted_data->set_func_refs(*func_refs);
    trusted_data->set_feedback_vectors(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_tiering_budget_array(
        native_module->tiering_budget_array());
    trusted_data->set_break_on_entry(module_object->script()->break_on_entry());
    trusted_data->InitDataSegmentArrays(native_module.get());
    trusted_data->set_memory0_start(empty_backing_store_buffer);
    trusted_data->set_memory0_size(0);
    trusted_data->set_memory_objects(*memory_objects);
    trusted_data->set_memory_bases_and_sizes(*memory_bases_and_sizes);
    trusted_data->set_stress_deopt_counter_address(
        ExternalReference::stress_deopt_count(isolate).address());

    for (int i = 0; i < num_memories; ++i) {
      memory_bases_and_sizes->set(
          2 * i, reinterpret_cast<Address>(empty_backing_store_buffer));
      memory_bases_and_sizes->set(2 * i + 1, 0);
    }
  }

  // Allocate the exports object, to be store in the instance object.
  DirectHandle<JSObject> exports_object =
      isolate->factory()->NewJSObjectWithNullProto();

  DirectHandle<WasmInstanceObject> instance_object;

  if (!shared) {
    // Allocate the WasmInstanceObject (JS wrapper).
    DirectHandle<JSFunction> instance_cons(
        isolate->native_context()->wasm_instance_constructor(), isolate);
    instance_object = Cast<WasmInstanceObject>(
        isolate->factory()->NewJSObject(instance_cons, AllocationType::kOld));
    instance_object->set_trusted_data(*trusted_data);
    instance_object->set_module_object(*module_object);
    instance_object->set_exports_object(*exports_object);
    trusted_data->set_instance_object(*instance_object);
  }

  // Insert the new instance into the scripts weak list of instances. This list
  // is used for breakpoints affecting all instances belonging to the script.
  if (module_object->script()->type() == Script::Type::kWasm &&
      !instance_object.is_null()) {
    Handle<WeakArrayList> weak_instance_list(
        module_object->script()->wasm_weak_instance_list(), isolate);
    weak_instance_list =
        WeakArrayList::Append(isolate, weak_instance_list,
                              MaybeObjectDirectHandle::Weak(instance_object));
    module_object->script()->set_wasm_weak_instance_list(*weak_instance_list);
  }

  return trusted_data;
}

void WasmTrustedInstanceData::InitDataSegmentArrays(
    const wasm::NativeModule* native_module) {
  const WasmModule* module = native_module->module();
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  uint32_t num_data_segments = module->num_declared_data_segments;
  // The number of declared data segments will be zero if there is no DataCount
  // section. These arrays will not be allocated nor initialized in that case,
  // since they cannot be used (since the validator checks that number of
  // declared data segments when validating the memory.init and memory.drop
  // instructions).
  DCHECK(num_data_segments == 0 ||
         num_data_segments == module->data_segments.size());
  for (uint32_t i = 0; i < num_data_segments; ++i) {
    const wasm::WasmDataSegment& segment = module->data_segments[i];
    // Initialize the pointer and size of passive segments.
    auto source_bytes = wire_bytes.SubVector(segment.source.offset(),
                                             segment.source.end_offset());
    data_segment_starts()->set(i,
                               reinterpret_cast<Address>(source_bytes.begin()));
    // Set the active segments to being already dropped, since memory.init on
    // a dropped passive segment and an active segment have the same
    // behavior.
    data_segment_sizes()->set(static_cast<int>(i),
                              segment.active ? 0 : source_bytes.length());
  }
}

WasmCodePointer WasmTrustedInstanceData::GetCallTarget(uint32_t func_index) {
  wasm::NativeModule* native_module = this->native_module();
  SBXCHECK_BOUNDS(func_index, native_module->num_functions());
  if (func_index < native_module->num_imported_functions()) {
    return dispatch_table_for_imports()->target(func_index);
  }

  if (v8_flags.wasm_jitless) {
    return wasm::kInvalidWasmCodePointer;
  }

  return native_module->GetCodePointerHandle(func_index);
}

// static
bool WasmTrustedInstanceData::CopyTableEntries(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    uint32_t table_dst_index, uint32_t table_src_index, uint32_t dst,
    uint32_t src, uint32_t count) {
  CHECK_LT(table_dst_index, trusted_instance_data->tables()->length());
  CHECK_LT(table_src_index, trusted_instance_data->tables()->length());
  auto table_dst =
      direct_handle(Cast<WasmTableObject>(
                        trusted_instance_data->tables()->get(table_dst_index)),
                    isolate);
  auto table_src =
      direct_handle(Cast<WasmTableObject>(
                        trusted_instance_data->tables()->get(table_src_index)),
                    isolate);
  uint32_t max_dst = table_dst->current_length();
  uint32_t max_src = table_src->current_length();
  bool copy_backward = src < dst;
  if (!base::IsInBounds(dst, count, max_dst) ||
      !base::IsInBounds(src, count, max_src)) {
    return false;
  }

  // no-op
  if ((dst == src && table_dst_index == table_src_index) || count == 0) {
    return true;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t src_index = copy_backward ? (src + count - i - 1) : src + i;
    uint32_t dst_index = copy_backward ? (dst + count - i - 1) : dst + i;
    auto value = WasmTableObject::Get(isolate, table_src, src_index);
    WasmTableObject::Set(isolate, table_dst, dst_index, value);
  }
  return true;
}

// static
std::optional<MessageTemplate> WasmTrustedInstanceData::InitTableEntries(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    Handle<WasmTrustedInstanceData> shared_trusted_instance_data,
    uint32_t table_index, uint32_t segment_index, uint32_t dst, uint32_t src,
    uint32_t count) {
  AccountingAllocator allocator;
  // This {Zone} will be used only by the temporary WasmFullDecoder allocated
  // down the line from this call. Therefore it is safe to stack-allocate it
  // here.
  Zone zone(&allocator, "LoadElemSegment");

  const WasmModule* module = trusted_instance_data->module();

  bool table_is_shared = module->tables[table_index].shared;
  bool segment_is_shared = module->elem_segments[segment_index].shared;

  DirectHandle<WasmTableObject> table_object(
      Cast<WasmTableObject>((table_is_shared ? shared_trusted_instance_data
                                             : trusted_instance_data)
                                ->tables()
                                ->get(table_index)),
      isolate);

  // If needed, try to lazily initialize the element segment.
  std::optional<MessageTemplate> opt_error = wasm::InitializeElementSegment(
      &zone, isolate, trusted_instance_data, shared_trusted_instance_data,
      segment_index);
  if (opt_error.has_value()) return opt_error;

  DirectHandle<FixedArray> elem_segment(
      Cast<FixedArray>((segment_is_shared ? shared_trusted_instance_data
                                          : trusted_instance_data)
                           ->element_segments()
                           ->get(segment_index)),
      isolate);
  if (!base::IsInBounds<uint64_t>(dst, count, table_object->current_length())) {
    return {MessageTemplate::kWasmTrapTableOutOfBounds};
  }
  if (!base::IsInBounds<uint64_t>(src, count, elem_segment->length())) {
    return {MessageTemplate::kWasmTrapElementSegmentOutOfBounds};
  }

  for (size_t i = 0; i < count; i++) {
    WasmTableObject::Set(
        isolate, table_object, static_cast<int>(dst + i),
        direct_handle(elem_segment->get(static_cast<int>(src + i)), isolate));
  }

  return {};
}

bool WasmTrustedInstanceData::try_get_func_ref(int index,
                                               Tagged<WasmFuncRef>* result) {
  Tagged<Object> val = func_refs()->get(index);
  if (IsSmi(val)) return false;
  *result = Cast<WasmFuncRef>(val);
  return true;
}

Handle<WasmFuncRef> WasmTrustedInstanceData::GetOrCreateFuncRef(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int function_index) {
  Tagged<WasmFuncRef> existing_func_ref;
  if (trusted_instance_data->try_get_func_ref(function_index,
                                              &existing_func_ref)) {
    return handle(existing_func_ref, isolate);
  }

  const WasmModule* module = trusted_instance_data->module();
  bool is_import =
      function_index < static_cast<int>(module->num_imported_functions);
  wasm::ModuleTypeIndex sig_index = module->functions[function_index].sig_index;
  DirectHandle<TrustedObject> implicit_arg =
      is_import ? direct_handle(
                      Cast<TrustedObject>(
                          trusted_instance_data->dispatch_table_for_imports()
                              ->implicit_arg(function_index)),
                      isolate)
                : trusted_instance_data;

  // TODO(14034): Create funcref RTTs lazily?
  DirectHandle<Map> rtt{
      Cast<Map>(
          trusted_instance_data->managed_object_maps()->get(sig_index.index)),
      isolate};

  DirectHandle<WasmInternalFunction> internal_function =
      isolate->factory()->NewWasmInternalFunction(implicit_arg, function_index);
  Handle<WasmFuncRef> func_ref =
      isolate->factory()->NewWasmFuncRef(internal_function, rtt);
  trusted_instance_data->func_refs()->set(function_index, *func_ref);

  // Reuse the call target of the instance. In case of import wrappers, the
  // wrapper will automatically get tiered up together since it will use the
  // same CPT entry.
  internal_function->set_call_target(
      trusted_instance_data->GetCallTarget(function_index));

  return func_ref;
}

bool WasmInternalFunction::try_get_external(Tagged<JSFunction>* result) {
  if (IsUndefined(external())) return false;
  *result = Cast<JSFunction>(external());
  return true;
}

// static
Handle<JSFunction> WasmInternalFunction::GetOrCreateExternal(
    DirectHandle<WasmInternalFunction> internal) {
  Isolate* isolate = GetIsolateFromWritableObject(*internal);

  Tagged<JSFunction> existing_external;
  if (internal->try_get_external(&existing_external)) {
    return handle(existing_external, isolate);
  }

  // {this} can either be:
  // - a declared function, i.e. {implicit_arg()} is a WasmTrustedInstanceData,
  // - or an imported callable, i.e. {implicit_arg()} is a WasmImportData which
  //   refers to the imported instance.
  // It cannot be a JS/C API function as for those, the external function is set
  // at creation.
  DirectHandle<TrustedObject> implicit_arg{internal->implicit_arg(), isolate};
  DirectHandle<WasmTrustedInstanceData> instance_data =
      IsWasmTrustedInstanceData(*implicit_arg)
          ? Cast<WasmTrustedInstanceData>(implicit_arg)
          : direct_handle(Cast<WasmImportData>(*implicit_arg)->instance_data(),
                          isolate);
  const WasmModule* module = instance_data->module();
  const WasmFunction& function = module->functions[internal->function_index()];
  wasm::CanonicalTypeIndex sig_id =
      module->canonical_sig_id(function.sig_index);
  const wasm::CanonicalSig* sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);
  wasm::TypeCanonicalizer::PrepareForCanonicalTypeId(isolate, sig_id);
  int wrapper_index = sig_id.index;

  Tagged<MaybeObject> entry =
      isolate->heap()->js_to_wasm_wrappers()->get(wrapper_index);

  DirectHandle<Code> wrapper_code;
  // {entry} can be cleared or a weak reference to a ready {CodeWrapper}.
  if (!entry.IsCleared()) {
    wrapper_code = direct_handle(
        Cast<CodeWrapper>(entry.GetHeapObjectAssumeWeak())->code(isolate),
        isolate);
#if V8_ENABLE_DRUMBRAKE
  } else if (v8_flags.wasm_jitless) {
    wrapper_code = isolate->builtins()->code_handle(
        Builtin::kGenericJSToWasmInterpreterWrapper);
#endif  // V8_ENABLE_DRUMBRAKE
  } else if (CanUseGenericJsToWasmWrapper(module, sig)) {
    if (v8_flags.stress_wasm_stack_switching) {
      wrapper_code =
          isolate->builtins()->code_handle(Builtin::kWasmStressSwitch);
    } else {
      wrapper_code =
          isolate->builtins()->code_handle(Builtin::kJSToWasmWrapper);
    }
  } else {
    // The wrapper does not exist yet; compile it now.
    wrapper_code = wasm::JSToWasmWrapperCompilationUnit::CompileJSToWasmWrapper(
        isolate, sig, sig_id);
    // This should have added an entry in the per-isolate cache.
    DCHECK_EQ(MakeWeak(wrapper_code->wrapper()),
              isolate->heap()->js_to_wasm_wrappers()->get(wrapper_index));
  }
  DirectHandle<WasmFuncRef> func_ref{
      Cast<WasmFuncRef>(
          instance_data->func_refs()->get(internal->function_index())),
      isolate};
  DCHECK_EQ(func_ref->internal(isolate), *internal);
  auto result = WasmExportedFunction::New(
      isolate, instance_data, func_ref, internal,
      static_cast<int>(sig->parameter_count()), wrapper_code);

  internal->set_external(*result);
  return result;
}

void WasmImportData::SetIndexInTableAsCallOrigin(
    Tagged<WasmDispatchTable> table, int entry_index) {
  set_call_origin(table);
  set_table_slot(entry_index);
}

void WasmImportData::SetFuncRefAsCallOrigin(Tagged<WasmInternalFunction> func) {
  set_call_origin(func);
}

uint8_t* WasmTrustedInstanceData::GetGlobalStorage(
    const wasm::WasmGlobal& global) {
  DCHECK(!global.type.is_reference());
  if (global.mutability && global.imported) {
    return reinterpret_cast<uint8_t*>(
        imported_mutable_globals()->get_sandboxed_pointer(global.index));
  }
  return globals_start() + global.offset;
}

std::pair<Tagged<FixedArray>, uint32_t>
WasmTrustedInstanceData::GetGlobalBufferAndIndex(
    const wasm::WasmGlobal& global) {
  DisallowGarbageCollection no_gc;
  DCHECK(global.type.is_reference());
  if (global.mutability && global.imported) {
    Tagged<FixedArray> buffer =
        Cast<FixedArray>(imported_mutable_globals_buffers()->get(global.index));
    Address idx = imported_mutable_globals()->get(global.index);
    DCHECK_LE(idx, std::numeric_limits<uint32_t>::max());
    return {buffer, static_cast<uint32_t>(idx)};
  }
  return {tagged_globals_buffer(), global.offset};
}

wasm::WasmValue WasmTrustedInstanceData::GetGlobalValue(
    Isolate* isolate, const wasm::WasmGlobal& global) {
  DisallowGarbageCollection no_gc;
  if (global.type.is_reference()) {
    Tagged<FixedArray> global_buffer;  // The buffer of the global.
    uint32_t global_index = 0;         // The index into the buffer.
    std::tie(global_buffer, global_index) = GetGlobalBufferAndIndex(global);
    return wasm::WasmValue(handle(global_buffer->get(global_index), isolate),
                           module()->canonical_type(global.type));
  }
  Address ptr = reinterpret_cast<Address>(GetGlobalStorage(global));
  switch (global.type.kind()) {
#define CASE_TYPE(valuetype, ctype) \
  case wasm::valuetype:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(ptr));
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    default:
      UNREACHABLE();
  }
}

const wasm::CanonicalStructType* WasmStruct::GcSafeType(Tagged<Map> map) {
  DCHECK_EQ(WASM_STRUCT_TYPE, map->instance_type());
  Tagged<HeapObject> raw = Cast<HeapObject>(map->constructor_or_back_pointer());
  // The {WasmTypeInfo} might be in the middle of being moved, which is why we
  // can't read its map for a checked cast. But we can rely on its native type
  // pointer being intact in the old location.
  Tagged<WasmTypeInfo> type_info = UncheckedCast<WasmTypeInfo>(raw);
  return wasm::GetTypeCanonicalizer()->LookupStruct(type_info->type_index());
}

wasm::WasmValue WasmStruct::GetFieldValue(uint32_t index) {
  const wasm::CanonicalStructType* type =
      wasm::GetTypeCanonicalizer()->LookupStruct(
          map()->wasm_type_info()->type_index());
  wasm::CanonicalValueType field_type = type->field(index);
  int field_offset = WasmStruct::kHeaderSize + type->field_offset(index);
  Address field_address = GetFieldAddress(field_offset);
  switch (field_type.kind()) {
#define CASE_TYPE(valuetype, ctype) \
  case wasm::valuetype:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(field_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kF16:
      return wasm::WasmValue(fp16_ieee_to_fp32_value(
          base::ReadUnalignedValue<uint16_t>(field_address)));
    case wasm::kRef:
    case wasm::kRefNull: {
      Handle<Object> ref(TaggedField<Object>::load(*this, field_offset),
                         GetIsolateFromWritableObject(*this));
      return wasm::WasmValue(ref, field_type);
    }
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kTop:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

wasm::WasmValue WasmArray::GetElement(uint32_t index) {
  wasm::CanonicalValueType element_type =
      map()->wasm_type_info()->element_type();
  int element_offset =
      WasmArray::kHeaderSize + index * element_type.value_kind_size();
  Address element_address = GetFieldAddress(element_offset);
  switch (element_type.kind()) {
#define CASE_TYPE(value_type, ctype) \
  case wasm::value_type:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(element_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kF16:
      return wasm::WasmValue(fp16_ieee_to_fp32_value(
          base::ReadUnalignedValue<uint16_t>(element_address)));
    case wasm::kRef:
    case wasm::kRefNull: {
      Handle<Object> ref(TaggedField<Object>::load(*this, element_offset),
                         GetIsolateFromWritableObject(*this));
      return wasm::WasmValue(ref, element_type);
    }
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kTop:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

void WasmArray::SetTaggedElement(uint32_t index, DirectHandle<Object> value,
                                 WriteBarrierMode mode) {
  DCHECK(map()->wasm_type_info()->element_type().is_reference());
  TaggedField<Object>::store(*this, element_offset(index), *value);
  CONDITIONAL_WRITE_BARRIER(*this, element_offset(index), *value, mode);
}

// static
Handle<WasmTagObject> WasmTagObject::New(
    Isolate* isolate, const wasm::FunctionSig* sig,
    wasm::CanonicalTypeIndex type_index, DirectHandle<HeapObject> tag,
    DirectHandle<WasmTrustedInstanceData> trusted_data) {
  DirectHandle<JSFunction> tag_cons(
      isolate->native_context()->wasm_tag_constructor(), isolate);

  // Serialize the signature.
  DCHECK_EQ(0, sig->return_count());
  DCHECK_LE(sig->parameter_count(), std::numeric_limits<int>::max());
  int sig_size = static_cast<int>(sig->parameter_count());
  DirectHandle<PodArray<wasm::ValueType>> serialized_sig =
      PodArray<wasm::ValueType>::New(isolate, sig_size, AllocationType::kOld);
  int index = 0;  // Index into the {PodArray} above.
  for (wasm::ValueType param : sig->parameters()) {
    serialized_sig->set(index++, param);
  }

  Handle<JSObject> tag_object =
      isolate->factory()->NewJSObject(tag_cons, AllocationType::kOld);
  Handle<WasmTagObject> tag_wrapper = Cast<WasmTagObject>(tag_object);
  tag_wrapper->set_serialized_signature(*serialized_sig);
  tag_wrapper->set_canonical_type_index(type_index.index);
  tag_wrapper->set_tag(*tag);
  if (!trusted_data.is_null()) {
    tag_wrapper->set_trusted_data(*trusted_data);
  } else {
    tag_wrapper->clear_trusted_data();
  }

  return tag_wrapper;
}

bool WasmTagObject::MatchesSignature(wasm::CanonicalTypeIndex expected_index) {
  return wasm::CanonicalTypeIndex{static_cast<uint32_t>(
             this->canonical_type_index())} == expected_index;
}

const wasm::CanonicalSig* WasmCapiFunction::sig() const {
  return shared()->wasm_capi_function_data()->sig();
}

#ifdef DEBUG
WasmCodePointer WasmDispatchTableData::WrapperCodePointerForDebugging(
    int index) {
  auto it = wrappers_.find(index);
  CHECK_NE(it, wrappers_.end());
  return it->second.call_target;
}
#endif

bool WasmDispatchTableData::IsAWrapper(int index) const {
  return wrappers_.contains(index);
}

WasmDispatchTableData::~WasmDispatchTableData() {
  if (wrappers_.empty()) return;
  std::vector<wasm::WasmCode*> codes;
  for (auto [index, entry] : wrappers_) {
    wasm::GetProcessWideWasmCodePointerTable()->FreeEntry(entry.call_target);
    if (entry.code) codes.push_back(entry.code);
  }
  wasm::WasmCode::DecrementRefCount(base::VectorOf(codes));
}

WasmCodePointer WasmDispatchTableData::Add(int index, Address call_target,
                                           wasm::WasmCode* compiled_wrapper,
                                           uint64_t signature_hash) {
  WasmCodePointer code_pointer;
  auto it = wrappers_.find(index);
  if (it == wrappers_.end()) {
    code_pointer =
        wasm::GetProcessWideWasmCodePointerTable()->AllocateAndInitializeEntry(
            call_target, signature_hash);
    auto [wrapper_cache, was_inserted] =
        wrappers_.emplace(index, WrapperEntry{code_pointer, compiled_wrapper});
    USE(was_inserted);
    DCHECK(was_inserted);
  } else {
    auto& [existing_code_pointer, wrapper_code] = it->second;
    code_pointer = existing_code_pointer;
    wasm::GetProcessWideWasmCodePointerTable()->UpdateEntrypoint(
        code_pointer, call_target, signature_hash);
    DCHECK_NULL(wrapper_code);
    DCHECK_NOT_NULL(compiled_wrapper);
    wrapper_code = compiled_wrapper;
  }
  if (compiled_wrapper) {
    compiled_wrapper->IncRef();
  } else {
    DCHECK_NULL(wasm::GetWasmImportWrapperCache()->FindWrapper(code_pointer));
  }

  return code_pointer;
}

void WasmDispatchTableData::Remove(int index, WasmCodePointer call_target) {
  if (call_target == wasm::kInvalidWasmCodePointer) return;

  auto entry = wrappers_.find(index);
  if (entry == wrappers_.end()) {
    // This is certainly not a wrapper.
    DCHECK_NULL(wasm::GetWasmImportWrapperCache()->FindWrapper(call_target));
    return;
  }
  auto& [code_pointer, wrapper_code] = entry->second;
  wasm::GetProcessWideWasmCodePointerTable()->FreeEntry(code_pointer);
  if (wrapper_code) {
    // TODO(clemensb): We should speed this up by doing
    // {WasmCodeRefScope::AddRef} and then {DecRefOnLiveCode}.
    wasm::WasmCode::DecrementRefCount({&wrapper_code, 1});
  }

  wrappers_.erase(entry);
}

void WasmDispatchTable::SetForNonWrapper(int index, Tagged<Object> implicit_arg,
                                         WasmCodePointer call_target,
                                         wasm::CanonicalTypeIndex sig_id,
#if V8_ENABLE_DRUMBRAKE
                                         uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
                                         NewOrExistingEntry new_or_existing) {
  if (implicit_arg == Smi::zero()) {
    DCHECK_EQ(wasm::kInvalidWasmCodePointer, call_target);
    Clear(index, new_or_existing);
    return;
  }

  SBXCHECK_BOUNDS(index, length());
  DCHECK(IsWasmImportData(implicit_arg) ||
         IsWasmTrustedInstanceData(implicit_arg));
  DCHECK(sig_id.valid());
  const int offset = OffsetOf(index);
  if (!v8_flags.wasm_jitless) {
    // When overwriting an existing entry, we must decrement the refcount
    // of any overwritten wrappers. When initializing an entry, we must not
    // read uninitialized memory.
    if (new_or_existing == kExistingEntry) {
      WasmCodePointer old_target =
          WasmCodePointer{ReadField<uint32_t>(offset + kTargetBias)};
      offheap_data()->Remove(index, old_target);
    }
    WriteField<uint32_t>(offset + kTargetBias, call_target.value());
  } else {
#if V8_ENABLE_DRUMBRAKE
    // Ignore call_target, not used in jitless mode.
    WriteField<int>(offset + kFunctionIndexBias, function_index);
#endif  // V8_ENABLE_DRUMBRAKE
  }
  WriteProtectedPointerField(offset + kImplicitArgBias,
                             Cast<TrustedObject>(implicit_arg));
  CONDITIONAL_WRITE_BARRIER(*this, offset + kImplicitArgBias, implicit_arg,
                            UPDATE_WRITE_BARRIER);
  WriteField<uint32_t>(offset + kSigBias, sig_id.index);
}

void WasmDispatchTable::SetForWrapper(int index, Tagged<Object> implicit_arg,
                                      Address call_target,
                                      wasm::CanonicalTypeIndex sig_id,
                                      uint64_t signature_hash,
#if V8_ENABLE_DRUMBRAKE
                                      uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
                                      wasm::WasmCode* compiled_wrapper,
                                      NewOrExistingEntry new_or_existing) {
  DCHECK_NE(implicit_arg, Smi::zero());
  SBXCHECK(!compiled_wrapper || !compiled_wrapper->is_dying());
  SBXCHECK_BOUNDS(index, length());
  DCHECK(IsWasmImportData(implicit_arg) ||
         IsWasmTrustedInstanceData(implicit_arg));
  DCHECK(sig_id.valid());
  const int offset = OffsetOf(index);
  WriteProtectedPointerField(offset + kImplicitArgBias,
                             Cast<TrustedObject>(implicit_arg));
  CONDITIONAL_WRITE_BARRIER(*this, offset + kImplicitArgBias, implicit_arg,
                            UPDATE_WRITE_BARRIER);
  if (!v8_flags.wasm_jitless) {
    // When overwriting an existing entry, we must decrement the refcount
    // of any overwritten wrappers. When initializing an entry, we must not
    // read uninitialized memory.
    if (new_or_existing == kExistingEntry) {
      WasmCodePointer old_target =
          WasmCodePointer{ReadField<uint32_t>(offset + kTargetBias)};
      offheap_data()->Remove(index, old_target);
    }
    WasmCodePointer code_pointer = offheap_data()->Add(
        index, call_target, compiled_wrapper, signature_hash);
    WriteField<uint32_t>(offset + kTargetBias, code_pointer.value());
  } else {
#if V8_ENABLE_DRUMBRAKE
    // Ignore call_target, not used in jitless mode.
    WriteField<int>(offset + kFunctionIndexBias, function_index);
#endif  // V8_ENABLE_DRUMBRAKE
  }

  WriteField<uint32_t>(offset + kSigBias, sig_id.index);
}

void WasmDispatchTable::Clear(int index, NewOrExistingEntry new_or_existing) {
  SBXCHECK_BOUNDS(index, length());
  const int offset = OffsetOf(index);
  // When clearing an existing entry, we must update the refcount of any
  // wrappers. When clear-initializing new entries, we must not read
  // uninitialized memory.
  if (new_or_existing == kExistingEntry) {
    WasmCodePointer old_target =
        WasmCodePointer{ReadField<uint32_t>(offset + kTargetBias)};
    offheap_data()->Remove(index, old_target);
  }
  ClearProtectedPointerField(offset + kImplicitArgBias);
  WriteField<uint32_t>(offset + kTargetBias,
                       wasm::kInvalidWasmCodePointer.value());
  WriteField<int>(offset + kSigBias, -1);
}

void WasmDispatchTable::InstallCompiledWrapper(int index,
                                               wasm::WasmCode* wrapper) {
  SBXCHECK_BOUNDS(index, length());
  if (v8_flags.wasm_jitless) return;  // Nothing to do.

  WasmCodePointer call_target = offheap_data()->Add(
      index, wrapper->instruction_start(), wrapper, wrapper->signature_hash());
  USE(call_target);
  // When installing a compiled wrapper, we already had the generic wrapper in
  // place, which shares the same code pointer table entry.
  DCHECK_EQ(WasmCodePointer{ReadField<uint32_t>(OffsetOf(index) + kTargetBias)},
            call_target);
}

bool WasmDispatchTable::IsAWrapper(int index) const {
  return offheap_data()->IsAWrapper(index);
}

// static
void WasmDispatchTable::AddUse(Isolate* isolate,
                               DirectHandle<WasmDispatchTable> dispatch_table,
                               DirectHandle<WasmTrustedInstanceData> instance,
                               int table_index) {
  Tagged<ProtectedWeakFixedArray> uses =
      MaybeGrowUsesList(isolate, dispatch_table);
  int cursor = GetUsedLength(uses);
  // {MaybeGrowUsesList} ensures that we have enough capacity.
  SetEntry(uses, cursor, *instance, table_index);
  SetUsedLength(uses, cursor + 2);
}

// static
Tagged<ProtectedWeakFixedArray> WasmDispatchTable::MaybeGrowUsesList(
    Isolate* isolate, DirectHandle<WasmDispatchTable> dispatch_table) {
  Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
  int capacity = uses->length();
  if (capacity == 0) {
    constexpr int kInitialLength = 3;  // 1 slot + 1 pair.
    DirectHandle<ProtectedWeakFixedArray> new_uses =
        isolate->factory()->NewProtectedWeakFixedArray(kInitialLength);
    SetUsedLength(*new_uses, kReservedSlotOffset);
    dispatch_table->set_protected_uses(*new_uses);
    return *new_uses;
  }
  DCHECK_GT(uses->length(), 0);
  int used_length = GetUsedLength(uses);
  if (used_length < capacity) return uses;
  // Try to compact, grow if that doesn't free up enough space.
  int cleared_entries = 0;
  int write_cursor = kReservedSlotOffset;
  for (int i = kReservedSlotOffset; i < capacity; i += 2) {
    DCHECK(uses->get(i).IsWeakOrCleared());
    if (uses->get(i).IsCleared()) {
      cleared_entries++;
      continue;
    }
    if (write_cursor != i) {
      CopyEntry(uses, write_cursor, uses, i);
    }
    write_cursor += 2;
  }
  // We need at least one free entry. We want at least half the array to be
  // empty; each entry needs two slots.
  int min_free_entries = 1 + (capacity >> 2);
  if (cleared_entries >= min_free_entries) {
    SetUsedLength(uses, write_cursor);
    return uses;
  }
  // Grow by 50%, at least one entry.
  DirectHandle<ProtectedWeakFixedArray> uses_handle(uses, isolate);
  uses = {};
  int old_entries = capacity >> 1;  // Two slots per entry.
  int new_entries = std::max(old_entries + 1, old_entries + (old_entries >> 1));
  int new_capacity = new_entries * 2 + kReservedSlotOffset;
  DirectHandle<ProtectedWeakFixedArray> new_uses =
      isolate->factory()->NewProtectedWeakFixedArray(new_capacity);
  for (int i = kReservedSlotOffset; i < write_cursor; i += 2) {
    CopyEntry(*new_uses, i, *uses_handle, i);
  }
  SetUsedLength(*new_uses, write_cursor);
  dispatch_table->set_protected_uses(*new_uses);
  return *new_uses;
}

// static
Handle<WasmDispatchTable> WasmDispatchTable::New(
    Isolate* isolate, int length, wasm::CanonicalValueType table_type) {
  return isolate->factory()->NewWasmDispatchTable(length, table_type);
}

// static
Handle<WasmDispatchTable> WasmDispatchTable::Grow(
    Isolate* isolate, Handle<WasmDispatchTable> old_table,
    uint32_t new_length) {
  uint32_t old_length = old_table->length();
  // This method should only be called if we actually grow. For sandbox
  // purposes we also want to ensure tables can never shrink below their
  // static minimum size.
  SBXCHECK_LT(old_length, new_length);

  uint32_t old_capacity = old_table->capacity();
  // Catch possible corruption. {new_length} is computed from untrusted data.
  SBXCHECK_LE(new_length, wasm::max_table_size());
  // {old_length} and {old_capacity} are read from trusted space, so we trust
  // them. The DCHECKs give fuzzers a chance to catch potential bugs.
  DCHECK_LE(old_length, wasm::max_table_size());
  DCHECK_LE(old_capacity, wasm::max_table_size());

  if (new_length < old_capacity) {
    RELEASE_WRITE_INT32_FIELD(*old_table, kLengthOffset, new_length);
    // All fields within the old capacity are already cleared (see below).
    return old_table;
  }

  // Grow table exponentially to guarantee amortized constant allocation and gc
  // time.
  uint32_t limit =
      std::min<uint32_t>(WasmDispatchTable::kMaxLength, wasm::max_table_size());
  uint32_t max_grow = limit - old_capacity;
  uint32_t min_grow = new_length - old_capacity;
  CHECK_LE(min_grow, max_grow);
  // Grow by old capacity, and at least by 8. Clamp to min_grow and max_grow.
  uint32_t exponential_grow = std::max(old_capacity, 8u);
  uint32_t grow = std::clamp(exponential_grow, min_grow, max_grow);
  uint32_t new_capacity = old_capacity + grow;
  DCHECK_LE(new_capacity, limit);
  Handle<WasmDispatchTable> new_table =
      WasmDispatchTable::New(isolate, new_capacity, old_table->table_type());

  DisallowGarbageCollection no_gc;
  // Writing non-atomically is fine here because this is a freshly allocated
  // object.
  new_table->WriteField<int>(kLengthOffset, new_length);
  for (uint32_t i = 0; i < old_length; ++i) {
    WasmCodePointer call_target = old_table->target(i);
    // Update any stored call origins, so that future compiled wrappers
    // get installed into the new dispatch table.
    Tagged<Object> implicit_arg = old_table->implicit_arg(i);
    if (IsWasmImportData(implicit_arg)) {
      Tagged<WasmImportData> import_data = Cast<WasmImportData>(implicit_arg);
      // After installing a compiled wrapper, we don't set or update
      // call origins any more.
      if (import_data->has_call_origin()) {
        if (import_data->call_origin() == *old_table) {
          import_data->set_call_origin(*new_table);
        } else {
#if DEBUG
          wasm::WasmCodeRefScope code_ref_scope;
          DCHECK_NOT_NULL(
              wasm::GetWasmImportWrapperCache()->FindWrapper(call_target));
#endif  // DEBUG
        }
      }
    }

    if (implicit_arg == Smi::zero()) {
      new_table->Clear(i, kNewEntry);
      continue;
    }

    const int offset = OffsetOf(i);
    if (!v8_flags.wasm_jitless) {
      new_table->WriteField<uint32_t>(offset + kTargetBias,
                                      call_target.value());
    } else {
#if V8_ENABLE_DRUMBRAKE
      // Ignore call_target, not used in jitless mode.
      new_table->WriteField<int>(offset + kFunctionIndexBias,
                                 old_table->function_index(i));
#endif  // V8_ENABLE_DRUMBRAKE
    }
    new_table->WriteProtectedPointerField(offset + kImplicitArgBias,
                                          Cast<TrustedObject>(implicit_arg));
    CONDITIONAL_WRITE_BARRIER(*new_table, offset + kImplicitArgBias,
                              implicit_arg, UPDATE_WRITE_BARRIER);
    new_table->WriteField<uint32_t>(offset + kSigBias, old_table->sig(i).index);
  }

  new_table->offheap_data()->wrappers_ =
      std::move(old_table->offheap_data()->wrappers_);

  // Update users.
  Tagged<ProtectedWeakFixedArray> uses = old_table->protected_uses();
  new_table->set_protected_uses(uses);
  int used_length = GetUsedLength(uses);
  for (int i = kReservedSlotOffset; i < used_length; i += 2) {
    if (uses->get(i).IsCleared()) continue;
    Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
    int table_index = GetTableIndex(uses, i);
    DCHECK_EQ(instance->dispatch_tables()->get(table_index), *old_table);
    instance->dispatch_tables()->set(table_index, *new_table);
    if (table_index == 0) {
      DCHECK_EQ(instance->dispatch_table0(), *old_table);
      instance->set_dispatch_table0(*new_table);
    }
  }
  return new_table;
}

bool WasmCapiFunction::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_sig_index) const {
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  for (wasm::CanonicalValueType type : this->sig()->all()) {
    CHECK(!type.has_index());
  }
#endif
  // TODO(14034): Check for subtyping instead if C API functions can define
  // signature supertype.
  return shared()->wasm_capi_function_data()->sig_index() ==
         other_canonical_sig_index;
}

// static
DirectHandle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag, int size) {
  DirectHandle<FixedArray> values = isolate->factory()->NewFixedArray(size);
  return New(isolate, exception_tag, values);
}

DirectHandle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
    DirectHandle<FixedArray> values) {
  DirectHandle<JSFunction> exception_cons(
      isolate->native_context()->wasm_exception_constructor(), isolate);
  DirectHandle<JSObject> exception =
      isolate->factory()->NewJSObject(exception_cons);
  exception->InObjectPropertyAtPut(kTagIndex, *exception_tag);
  exception->InObjectPropertyAtPut(kValuesIndex, *values);
  return Cast<WasmExceptionPackage>(exception);
}

// static
Handle<Object> WasmExceptionPackage::GetExceptionTag(
    Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package) {
  Handle<Object> tag;
  if (JSReceiver::GetProperty(isolate, exception_package,
                              isolate->factory()->wasm_exception_tag_symbol())
          .ToHandle(&tag)) {
    return tag;
  }
  return isolate->factory()->undefined_value();
}

// static
Handle<Object> WasmExceptionPackage::GetExceptionValues(
    Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package) {
  Handle<Object> values;
  if (JSReceiver::GetProperty(
          isolate, exception_package,
          isolate->factory()->wasm_exception_values_symbol())
          .ToHandle(&values)) {
    DCHECK_IMPLIES(!IsUndefined(*values), IsFixedArray(*values));
    return values;
  }
  return isolate->factory()->undefined_value();
}

void EncodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t value) {
  encoded_values->set((*encoded_index)++, Smi::FromInt(value >> 16));
  encoded_values->set((*encoded_index)++, Smi::FromInt(value & 0xffff));
}

void EncodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t value) {
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value >> 32));
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value));
}

void DecodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t* value) {
  uint32_t msb = Cast<Smi>(encoded_values->get((*encoded_index)++)).value();
  uint32_t lsb = Cast<Smi>(encoded_values->get((*encoded_index)++)).value();
  *value = (msb << 16) | (lsb & 0xffff);
}

void DecodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t* value) {
  uint32_t lsb = 0, msb = 0;
  DecodeI32ExceptionValue(encoded_values, encoded_index, &msb);
  DecodeI32ExceptionValue(encoded_values, encoded_index, &lsb);
  *value = (static_cast<uint64_t>(msb) << 32) | static_cast<uint64_t>(lsb);
}

// static
DirectHandle<WasmContinuationObject> WasmContinuationObject::New(
    Isolate* isolate, wasm::StackMemory* stack,
    wasm::JumpBuffer::StackState state, DirectHandle<HeapObject> parent,
    AllocationType allocation_type) {
  stack->jmpbuf()->stack_limit = stack->jslimit();
  stack->jmpbuf()->sp = stack->base();
  stack->jmpbuf()->fp = kNullAddress;
  stack->jmpbuf()->state = state;
  wasm::JumpBuffer* jmpbuf = stack->jmpbuf();
  DirectHandle<WasmContinuationObject> result =
      isolate->factory()->NewWasmContinuationObject(
          reinterpret_cast<Address>(jmpbuf), stack, parent, allocation_type);
  return result;
}

bool UseGenericWasmToJSWrapper(wasm::ImportCallKind kind,
                               const wasm::CanonicalSig* sig,
                               wasm::Suspend suspend) {
  if (kind != wasm::ImportCallKind::kJSFunctionArityMatch &&
      kind != wasm::ImportCallKind::kJSFunctionArityMismatch) {
    return false;
  }
  DCHECK(wasm::IsJSCompatibleSignature(sig));
#if !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_ARM64 && !V8_TARGET_ARCH_ARM && \
    !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_RISCV64 &&                     \
    !V8_TARGET_ARCH_RISCV32 && !V8_TARGET_ARCH_PPC64 &&                    \
    !V8_TARGET_ARCH_S390X && !V8_TARGET_ARCH_LOONG64 && !V8_TARGET_ARCH_MIPS64
  return false;
#else
  if (suspend != wasm::Suspend::kNoSuspend) return false;

  return v8_flags.wasm_generic_wrapper;
#endif
}

// static
DirectHandle<WasmContinuationObject> WasmContinuationObject::New(
    Isolate* isolate, wasm::StackMemory* stack,
    wasm::JumpBuffer::StackState state, AllocationType allocation_type) {
  auto parent = ReadOnlyRoots(isolate).undefined_value();
  return New(isolate, stack, state, direct_handle(parent, isolate),
             allocation_type);
}
#ifdef DEBUG

namespace {

constexpr uint32_t kBytesPerExceptionValuesArrayElement = 2;

size_t ComputeEncodedElementSize(wasm::ValueType type) {
  size_t byte_size = type.value_kind_size();
  DCHECK_EQ(byte_size % kBytesPerExceptionValuesArrayElement, 0);
  DCHECK_LE(1, byte_size / kBytesPerExceptionValuesArrayElement);
  return byte_size / kBytesPerExceptionValuesArrayElement;
}

}  // namespace

#endif  // DEBUG

// static
uint32_t WasmExceptionPackage::GetEncodedSize(const wasm::WasmTag* tag) {
  return GetEncodedSize(tag->sig);
}

// static
uint32_t WasmExceptionPackage::GetEncodedSize(const wasm::WasmTagSig* sig) {
  uint32_t encoded_size = 0;
  for (size_t i = 0; i < sig->parameter_count(); ++i) {
    switch (sig->GetParam(i).kind()) {
      case wasm::kI32:
      case wasm::kF32:
        DCHECK_EQ(2, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 2;
        break;
      case wasm::kI64:
      case wasm::kF64:
        DCHECK_EQ(4, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 4;
        break;
      case wasm::kS128:
        DCHECK_EQ(8, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 8;
        break;
      case wasm::kRef:
      case wasm::kRefNull:
        encoded_size += 1;
        break;
      case wasm::kRtt:
      case wasm::kVoid:
      case wasm::kTop:
      case wasm::kBottom:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
        UNREACHABLE();
    }
  }
  return encoded_size;
}

bool WasmExportedFunction::IsWasmExportedFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  Tagged<Code> code = js_function->code(GetIsolateForSandbox(js_function));
  if (CodeKind::JS_TO_WASM_FUNCTION != code->kind() &&
#if V8_ENABLE_DRUMBRAKE
      code->builtin_id() != Builtin::kGenericJSToWasmInterpreterWrapper &&
#endif  // V8_ENABLE_DRUMBRAKE
      code->builtin_id() != Builtin::kJSToWasmWrapper &&
      code->builtin_id() != Builtin::kWasmPromising &&
      code->builtin_id() != Builtin::kWasmStressSwitch) {
    return false;
  }
  DCHECK(js_function->shared()->HasWasmExportedFunctionData());
  return true;
}

bool WasmCapiFunction::IsWasmCapiFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  // TODO(jkummerow): Enable this when there is a JavaScript wrapper
  // able to call this function.
  // if (js_function->code()->kind() != CodeKind::WASM_TO_CAPI_FUNCTION) {
  //   return false;
  // }
  // DCHECK(js_function->shared()->HasWasmCapiFunctionData());
  // return true;
  return js_function->shared()->HasWasmCapiFunctionData();
}

DirectHandle<WasmCapiFunction> WasmCapiFunction::New(
    Isolate* isolate, Address call_target, DirectHandle<Foreign> embedder_data,
    wasm::CanonicalTypeIndex sig_index, const wasm::CanonicalSig* sig) {
  // TODO(jkummerow): Install a JavaScript wrapper. For now, calling
  // these functions directly is unsupported; they can only be called
  // from Wasm code.

  // To support simulator builds, we potentially have to redirect the
  // call target (which is an address pointing into the C++ binary).
  call_target = ExternalReference::Create(call_target).address();

  DirectHandle<Map> rtt = isolate->factory()->wasm_func_ref_map();
  DirectHandle<WasmCapiFunctionData> fun_data =
      isolate->factory()->NewWasmCapiFunctionData(
          call_target, embedder_data, BUILTIN_CODE(isolate, Illegal), rtt,
          sig_index, sig);
  DirectHandle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForWasmCapiFunction(fun_data);
  DirectHandle<JSFunction> result =
      Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
          .Build();
  fun_data->internal()->set_external(*result);
  return Cast<WasmCapiFunction>(result);
}

Handle<WasmExportedFunction> WasmExportedFunction::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
    DirectHandle<WasmFuncRef> func_ref,
    DirectHandle<WasmInternalFunction> internal_function, int arity,
    DirectHandle<Code> export_wrapper) {
#if V8_ENABLE_DRUMBRAKE
  DCHECK(CodeKind::JS_TO_WASM_FUNCTION == export_wrapper->kind() ||
         (export_wrapper->is_builtin() &&
          (export_wrapper->builtin_id() == Builtin::kJSToWasmWrapper ||
           export_wrapper->builtin_id() ==
               Builtin::kGenericJSToWasmInterpreterWrapper ||
           export_wrapper->builtin_id() == Builtin::kWasmPromising ||
           export_wrapper->builtin_id() == Builtin::kWasmStressSwitch)));
#else
  DCHECK(CodeKind::JS_TO_WASM_FUNCTION == export_wrapper->kind() ||
         (export_wrapper->is_builtin() &&
          (export_wrapper->builtin_id() == Builtin::kJSToWasmWrapper ||
           export_wrapper->builtin_id() == Builtin::kWasmPromising ||
           export_wrapper->builtin_id() == Builtin::kWasmStressSwitch)));
#endif  // V8_ENABLE_DRUMBRAKE
  int func_index = internal_function->function_index();
  Factory* factory = isolate->factory();
  DirectHandle<Map> rtt;
  wasm::Promise promise =
      export_wrapper->builtin_id() == Builtin::kWasmPromising
          ? wasm::kPromise
          : wasm::kNoPromise;
  const wasm::WasmModule* module = instance_data->module();
  wasm::CanonicalTypeIndex sig_id =
      module->canonical_sig_id(module->functions[func_index].sig_index);
  const wasm::CanonicalSig* sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);
  DirectHandle<WasmExportedFunctionData> function_data =
      factory->NewWasmExportedFunctionData(
          export_wrapper, instance_data, func_ref, internal_function, sig,
          sig_id, v8_flags.wasm_wrapper_tiering_budget, promise);

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    const wasm::FunctionSig* function_sig =
        reinterpret_cast<const wasm::FunctionSig*>(sig);
    uint32_t aligned_size =
        wasm::WasmBytecode::JSToWasmWrapperPackedArraySize(function_sig);
    bool hasRefArgs = wasm::WasmBytecode::RefArgsCount(function_sig) > 0;
    bool hasRefRets = wasm::WasmBytecode::RefRetsCount(function_sig) > 0;
    function_data->set_packed_args_size(
        wasm::WasmInterpreterRuntime::PackedArgsSizeField::encode(
            aligned_size) |
        wasm::WasmInterpreterRuntime::HasRefArgsField::encode(hasRefArgs) |
        wasm::WasmInterpreterRuntime::HasRefRetsField::encode(hasRefRets));
  }
#endif  // V8_ENABLE_DRUMBRAKE

  MaybeDirectHandle<String> maybe_name;
  bool is_asm_js_module = is_asmjs_module(module);
  if (is_asm_js_module) {
    // We can use the function name only for asm.js. For WebAssembly, the
    // function name is specified as the function_index.toString().
    maybe_name = WasmModuleObject::GetFunctionNameOrNull(
        isolate, direct_handle(instance_data->module_object(), isolate),
        func_index);
  }
  DirectHandle<String> name;
  if (!maybe_name.ToHandle(&name)) {
    base::EmbeddedVector<char, 16> buffer;
    int length = SNPrintF(buffer, "%d", func_index);
    name = factory
               ->NewStringFromOneByte(
                   base::Vector<uint8_t>::cast(buffer.SubVector(0, length)))
               .ToHandleChecked();
  }
  DirectHandle<Map> function_map;
  switch (module->origin) {
    case wasm::kWasmOrigin:
      function_map = isolate->wasm_exported_function_map();
      break;
    case wasm::kAsmJsSloppyOrigin:
      function_map = isolate->sloppy_function_map();
      break;
    case wasm::kAsmJsStrictOrigin:
      function_map = isolate->strict_function_map();
      break;
  }

  DirectHandle<NativeContext> context(isolate->native_context());
  DirectHandle<SharedFunctionInfo> shared =
      factory->NewSharedFunctionInfoForWasmExportedFunction(name, function_data,
                                                            arity, kAdapt);

  Handle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(function_map)
          .Build();

  // According to the spec, exported functions should not have a [[Construct]]
  // method. This does not apply to functions exported from asm.js however.
  DCHECK_EQ(is_asm_js_module, IsConstructor(*js_function));
  if (instance_data->has_instance_object()) {
    shared->set_script(instance_data->module_object()->script(), kReleaseStore);
  } else {
    shared->set_script(*isolate->factory()->undefined_value(), kReleaseStore);
  }
  function_data->internal()->set_external(*js_function);
  return Cast<WasmExportedFunction>(js_function);
}

bool WasmExportedFunctionData::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_type_index) {
  return wasm::GetTypeCanonicalizer()->IsCanonicalSubtype(
      sig_index(), other_canonical_type_index);
}

// static
std::unique_ptr<char[]> WasmExportedFunction::GetDebugName(
    const wasm::CanonicalSig* sig) {
  constexpr const char kPrefix[] = "js-to-wasm:";
  // prefix + parameters + delimiter + returns + zero byte
  size_t len = strlen(kPrefix) + sig->all().size() + 2;
  auto buffer = base::OwnedVector<char>::New(len);
  memcpy(buffer.begin(), kPrefix, strlen(kPrefix));
  PrintSignature(buffer.as_vector() + strlen(kPrefix), sig);
  return buffer.ReleaseData();
}

// static
bool WasmJSFunction::IsWasmJSFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  return js_function->shared()->HasWasmJSFunctionData();
}

DirectHandle<Map> CreateFuncRefMap(Isolate* isolate,
                                   wasm::CanonicalTypeIndex type,
                                   Handle<Map> opt_rtt_parent) {
  const int inobject_properties = 0;
  const InstanceType instance_type = WASM_FUNC_REF_TYPE;
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  const wasm::CanonicalValueType no_array_element =
      wasm::CanonicalValueType::Primitive(wasm::kBottom);
  DirectHandle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      type, no_array_element, opt_rtt_parent);
  constexpr int kInstanceSize = WasmFuncRef::kSize;
  DCHECK_EQ(
      kInstanceSize,
      Cast<Map>(isolate->root(RootIndex::kWasmFuncRefMap))->instance_size());
  DirectHandle<Map> map = isolate->factory()->NewContextlessMap(
      instance_type, kInstanceSize, elements_kind, inobject_properties);
  map->set_wasm_type_info(*type_info);
  return map;
}

DirectHandle<WasmJSFunction> WasmJSFunction::New(
    Isolate* isolate, const wasm::FunctionSig* sig,
    DirectHandle<JSReceiver> callable, wasm::Suspend suspend) {
  DCHECK_LE(sig->all().size(), kMaxInt);
  int parameter_count = static_cast<int>(sig->parameter_count());
  Factory* factory = isolate->factory();

  DirectHandle<Map> rtt;
  DirectHandle<NativeContext> context(isolate->native_context());

  static_assert(wasm::kMaxCanonicalTypes <= kMaxInt);
  // TODO(clemensb): Merge the next two lines into a single call.
  wasm::CanonicalTypeIndex sig_id =
      wasm::GetTypeCanonicalizer()->AddRecursiveGroup(sig);
  const wasm::CanonicalSig* canonical_sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);

  wasm::TypeCanonicalizer::PrepareForCanonicalTypeId(isolate, sig_id);

  DirectHandle<WeakFixedArray> canonical_rtts(
      isolate->heap()->wasm_canonical_rtts(), isolate);

  Tagged<MaybeObject> maybe_canonical_map = canonical_rtts->get(sig_id.index);

  if (!maybe_canonical_map.IsCleared()) {
    rtt = direct_handle(
        Cast<Map>(maybe_canonical_map.GetHeapObjectAssumeWeak()), isolate);
  } else {
    rtt = CreateFuncRefMap(isolate, sig_id, Handle<Map>());
    canonical_rtts->set(sig_id.index, MakeWeak(*rtt));
  }

  DirectHandle<Code> js_to_js_wrapper_code =
      wasm::IsJSCompatibleSignature(canonical_sig)
          ? isolate->builtins()->code_handle(Builtin::kJSToJSWrapper)
          : isolate->builtins()->code_handle(Builtin::kJSToJSWrapperInvalidSig);

  DirectHandle<WasmJSFunctionData> function_data =
      factory->NewWasmJSFunctionData(sig_id, callable, js_to_js_wrapper_code,
                                     rtt, suspend, wasm::kNoPromise);
  DirectHandle<WasmInternalFunction> internal_function{
      function_data->internal(), isolate};

  if (!wasm::IsJSCompatibleSignature(canonical_sig)) {
    Address builtin_entry =
        Builtins::EntryOf(Builtin::kWasmToJsWrapperInvalidSig, isolate);
    WasmCodePointer wrapper_code_pointer =
        function_data->offheap_data()->set_generic_wrapper(builtin_entry);
    internal_function->set_call_target(wrapper_code_pointer);
#if V8_ENABLE_DRUMBRAKE
  } else if (v8_flags.wasm_jitless) {
    Address builtin_entry =
        Builtins::EntryOf(Builtin::kGenericWasmToJSInterpreterWrapper, isolate);
    WasmCodePointer wrapper_code_pointer =
        function_data->offheap_data()->set_generic_wrapper(builtin_entry);
    internal_function->set_call_target(wrapper_code_pointer);
#endif  // V8_ENABLE_DRUMBRAKE
  } else {
    int expected_arity = parameter_count;
    wasm::ImportCallKind kind;
    if (IsJSFunction(*callable)) {
      Tagged<SharedFunctionInfo> shared = Cast<JSFunction>(callable)->shared();
      expected_arity =
          shared->internal_formal_parameter_count_without_receiver();
      if (expected_arity == parameter_count) {
        kind = wasm::ImportCallKind::kJSFunctionArityMatch;
      } else {
        kind = wasm::ImportCallKind::kJSFunctionArityMismatch;
      }
    } else {
      kind = wasm::ImportCallKind::kUseCallBuiltin;
    }
    wasm::WasmCodeRefScope code_ref_scope;
    wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
    wasm::WasmCode* wrapper =
        cache->MaybeGet(kind, sig_id, expected_arity, suspend);
    WasmCodePointer code_pointer;
    if (wrapper) {
      code_pointer =
          function_data->offheap_data()->set_compiled_wrapper(wrapper);
      // Some later DCHECKs assume that we don't have a {call_origin} when
      // the function already uses a compiled wrapper.
      Cast<WasmImportData>(internal_function->implicit_arg())
          ->clear_call_origin();
    } else if (UseGenericWasmToJSWrapper(kind, canonical_sig, suspend)) {
      Address code_entry =
          Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate);
      code_pointer =
          function_data->offheap_data()->set_generic_wrapper(code_entry);
    } else {
      // Initialize the import wrapper cache if that hasn't happened yet.
      cache->LazyInitialize(isolate);
      constexpr bool kNoSourcePositions = false;
      wrapper = cache->CompileWasmImportCallWrapper(
          isolate, kind, canonical_sig, sig_id, kNoSourcePositions,
          expected_arity, suspend);
      code_pointer =
          function_data->offheap_data()->set_compiled_wrapper(wrapper);
      // Some later DCHECKs assume that we don't have a {call_origin} when
      // the function already uses a compiled wrapper.
      Cast<WasmImportData>(internal_function->implicit_arg())
          ->clear_call_origin();
    }
    internal_function->set_call_target(code_pointer);
  }

  Handle<String> name = factory->Function_string();
  if (IsJSFunction(*callable)) {
    name = JSFunction::GetDebugName(Cast<JSFunction>(callable));
    name = String::Flatten(isolate, name);
  }
  DirectHandle<SharedFunctionInfo> shared =
      factory->NewSharedFunctionInfoForWasmJSFunction(name, function_data);
  shared->set_internal_formal_parameter_count(
      JSParameterCount(parameter_count));
  DirectHandle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(isolate->wasm_exported_function_map())
          .Build();
  internal_function->set_external(*js_function);
  return Cast<WasmJSFunction>(js_function);
}

WasmCodePointer WasmJSFunctionData::OffheapData::set_compiled_wrapper(
    wasm::WasmCode* wrapper) {
  DCHECK_NULL(wrapper_);  // We shouldn't overwrite existing wrappers.
  wrapper_ = wrapper;
  wrapper->IncRef();
  DCHECK_EQ(wrapper->signature_hash(), signature_hash_);
  if (wrapper_code_pointer_ == wasm::kInvalidWasmCodePointer) {
    wrapper_code_pointer_ =
        wasm::GetProcessWideWasmCodePointerTable()->AllocateAndInitializeEntry(
            wrapper->instruction_start(), wrapper->signature_hash());
  } else {
    wasm::GetProcessWideWasmCodePointerTable()->UpdateEntrypoint(
        wrapper_code_pointer_, wrapper->instruction_start(),
        wrapper->signature_hash());
  }
  return wrapper_code_pointer_;
}

WasmCodePointer WasmJSFunctionData::OffheapData::set_generic_wrapper(
    Address code_entry) {
  DCHECK_EQ(wrapper_code_pointer_, wasm::kInvalidWasmCodePointer);
  wrapper_code_pointer_ =
      wasm::GetProcessWideWasmCodePointerTable()->AllocateAndInitializeEntry(
          code_entry, signature_hash_);
  return wrapper_code_pointer_;
}

WasmJSFunctionData::OffheapData::~OffheapData() {
  if (wrapper_) {
    wasm::WasmCode::DecrementRefCount({&wrapper_, 1});
  }
  if (wrapper_code_pointer_ != wasm::kInvalidWasmCodePointer) {
    wasm::GetProcessWideWasmCodePointerTable()->FreeEntry(
        wrapper_code_pointer_);
  }
}

Tagged<JSReceiver> WasmJSFunctionData::GetCallable() const {
  return Cast<JSReceiver>(
      Cast<WasmImportData>(internal()->implicit_arg())->callable());
}

wasm::Suspend WasmJSFunctionData::GetSuspend() const {
  return Cast<WasmImportData>(internal()->implicit_arg())->suspend();
}

const wasm::CanonicalSig* WasmJSFunctionData::GetSignature() const {
  return wasm::GetWasmEngine()->type_canonicalizer()->LookupFunctionSignature(
      sig_index());
}

bool WasmJSFunctionData::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_sig_index) const {
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  const wasm::CanonicalSig* sig = GetSignature();
  for (wasm::CanonicalValueType type : sig->all()) CHECK(!type.has_index());
#endif
  // TODO(14034): Check for subtyping instead if WebAssembly.Function can define
  // signature supertype.
  return sig_index() == other_canonical_sig_index;
}

bool WasmExternalFunction::IsWasmExternalFunction(Tagged<Object> object) {
  return WasmExportedFunction::IsWasmExportedFunction(object) ||
         WasmJSFunction::IsWasmJSFunction(object) ||
         WasmCapiFunction::IsWasmCapiFunction(object);
}

Handle<WasmExceptionTag> WasmExceptionTag::New(Isolate* isolate, int index) {
  auto result = Cast<WasmExceptionTag>(isolate->factory()->NewStruct(
      WASM_EXCEPTION_TAG_TYPE, AllocationType::kOld));
  result->set_index(index);
  return result;
}

Handle<AsmWasmData> AsmWasmData::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    DirectHandle<HeapNumber> uses_bitset) {
  const WasmModule* module = native_module->module();
  const bool kUsesLiftoff = false;
  size_t memory_estimate =
      wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
          module, kUsesLiftoff, wasm::kNoDynamicTiering) +
      wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
  DirectHandle<Managed<wasm::NativeModule>> managed_native_module =
      Managed<wasm::NativeModule>::From(isolate, memory_estimate,
                                        std::move(native_module));
  auto result = Cast<AsmWasmData>(
      isolate->factory()->NewStruct(ASM_WASM_DATA_TYPE, AllocationType::kOld));
  result->set_managed_native_module(*managed_native_module);
  result->set_uses_bitset(*uses_bitset);
  return result;
}

namespace {
constexpr int32_t kInt31MaxValue = 0x3fffffff;
constexpr int32_t kInt31MinValue = -kInt31MaxValue - 1;

// Tries to canonicalize a HeapNumber to an i31ref Smi. Returns the original
// HeapNumber if it fails.
Handle<Object> CanonicalizeHeapNumber(Handle<Object> number, Isolate* isolate) {
  double double_value = Cast<HeapNumber>(number)->value();
  if (double_value >= kInt31MinValue && double_value <= kInt31MaxValue &&
      !IsMinusZero(double_value) &&
      double_value == FastI2D(FastD2I(double_value))) {
    return handle(Smi::FromInt(FastD2I(double_value)), isolate);
  }
  return number;
}

// Tries to canonicalize a Smi into an i31 Smi. Returns a HeapNumber if it
// fails.
Handle<Object> CanonicalizeSmi(Handle<Object> smi, Isolate* isolate) {
  if constexpr (SmiValuesAre31Bits()) return smi;

  int32_t value = Cast<Smi>(*smi).value();

  if (value <= kInt31MaxValue && value >= kInt31MinValue) {
    return smi;
  } else {
    return isolate->factory()->NewHeapNumber(value);
  }
}
}  // namespace

namespace wasm {
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, Handle<Object> value,
                                   CanonicalValueType expected,
                                   const char** error_message) {
  DCHECK(expected.is_object_reference());
  if (expected.kind() == kRefNull && IsNull(*value, isolate)) {
    switch (expected.heap_representation()) {
      case HeapType::kStringViewWtf8:
        *error_message = "stringview_wtf8 has no JS representation";
        return {};
      case HeapType::kStringViewWtf16:
        *error_message = "stringview_wtf16 has no JS representation";
        return {};
      case HeapType::kStringViewIter:
        *error_message = "stringview_iter has no JS representation";
        return {};
      case HeapType::kExn:
        *error_message = "invalid type (ref null exn)";
        return {};
      case HeapType::kNoExn:
        *error_message = "invalid type (ref null noexn)";
        return {};
      default:
        return expected.use_wasm_null() ? isolate->factory()->wasm_null()
                                        : value;
    }
  }

  switch (expected.heap_representation_non_shared()) {
    case HeapType::kFunc: {
      if (!(WasmExternalFunction::IsWasmExternalFunction(*value) ||
            WasmCapiFunction::IsWasmCapiFunction(*value))) {
        *error_message =
            "function-typed object must be null (if nullable) or a Wasm "
            "function object";
        return {};
      }
      return handle(
          Cast<JSFunction>(*value)->shared()->wasm_function_data()->func_ref(),
          isolate);
    }
    case HeapType::kExtern: {
      if (!IsNull(*value, isolate)) return value;
      *error_message = "null is not allowed for (ref extern)";
      return {};
    }
    case HeapType::kAny: {
      if (IsSmi(*value)) return CanonicalizeSmi(value, isolate);
      if (IsHeapNumber(*value)) {
        return CanonicalizeHeapNumber(value, isolate);
      }
      if (!IsNull(*value, isolate)) return value;
      *error_message = "null is not allowed for (ref any)";
      return {};
    }
    case HeapType::kExn:
      *error_message = "invalid type (ref exn)";
      return {};
    case HeapType::kStruct: {
      if (IsWasmStruct(*value)) {
        return value;
      }
      *error_message =
          "structref object must be null (if nullable) or a wasm struct";
      return {};
    }
    case HeapType::kArray: {
      if (IsWasmArray(*value)) {
        return value;
      }
      *error_message =
          "arrayref object must be null (if nullable) or a wasm array";
      return {};
    }
    case HeapType::kEq: {
      if (IsSmi(*value)) {
        Handle<Object> truncated = CanonicalizeSmi(value, isolate);
        if (IsSmi(*truncated)) return truncated;
      } else if (IsHeapNumber(*value)) {
        Handle<Object> truncated = CanonicalizeHeapNumber(value, isolate);
        if (IsSmi(*truncated)) return truncated;
      } else if (IsWasmStruct(*value) || IsWasmArray(*value)) {
        return value;
      }
      *error_message =
          "eqref object must be null (if nullable), or a wasm "
          "struct/array, or a Number that fits in i31ref range";
      return {};
    }
    case HeapType::kI31: {
      if (IsSmi(*value)) {
        Handle<Object> truncated = CanonicalizeSmi(value, isolate);
        if (IsSmi(*truncated)) return truncated;
      } else if (IsHeapNumber(*value)) {
        Handle<Object> truncated = CanonicalizeHeapNumber(value, isolate);
        if (IsSmi(*truncated)) return truncated;
      }
      *error_message =
          "i31ref object must be null (if nullable) or a Number that fits "
          "in i31ref range";
      return {};
    }
    case HeapType::kString:
      if (IsString(*value)) return value;
      *error_message = "wrong type (expected a string)";
      return {};
    case HeapType::kStringViewWtf8:
      *error_message = "stringview_wtf8 has no JS representation";
      return {};
    case HeapType::kStringViewWtf16:
      *error_message = "stringview_wtf16 has no JS representation";
      return {};
    case HeapType::kStringViewIter:
      *error_message = "stringview_iter has no JS representation";
      return {};
    case HeapType::kNoFunc:
    case HeapType::kNoExtern:
    case HeapType::kNoExn:
    case HeapType::kNone: {
      *error_message = "only null allowed for null types";
      return {};
    }
    default: {
      DCHECK(expected.has_index());
      CanonicalTypeIndex canonical_index = expected.ref_index();
      auto type_canonicalizer = GetWasmEngine()->type_canonicalizer();

      if (WasmExportedFunction::IsWasmExportedFunction(*value)) {
        Tagged<WasmExportedFunction> function =
            Cast<WasmExportedFunction>(*value);
        CanonicalTypeIndex real_type_index =
            function->shared()->wasm_exported_function_data()->sig_index();
        if (!type_canonicalizer->IsCanonicalSubtype(real_type_index,
                                                    canonical_index)) {
          *error_message =
              "assigned exported function has to be a subtype of the "
              "expected type";
          return {};
        }
        return handle(Cast<WasmExternalFunction>(*value)->func_ref(), isolate);
      } else if (WasmJSFunction::IsWasmJSFunction(*value)) {
        if (!Cast<WasmJSFunction>(*value)
                 ->shared()
                 ->wasm_js_function_data()
                 ->MatchesSignature(canonical_index)) {
          *error_message =
              "assigned WebAssembly.Function has to be a subtype of the "
              "expected type";
          return {};
        }
        return handle(Cast<WasmExternalFunction>(*value)->func_ref(), isolate);
      } else if (WasmCapiFunction::IsWasmCapiFunction(*value)) {
        if (!Cast<WasmCapiFunction>(*value)->MatchesSignature(
                canonical_index)) {
          *error_message =
              "assigned C API function has to be a subtype of the expected "
              "type";
          return {};
        }
        return handle(Cast<WasmExternalFunction>(*value)->func_ref(), isolate);
      } else if (IsWasmStruct(*value) || IsWasmArray(*value)) {
        Handle<WasmObject> wasm_obj = Cast<WasmObject>(value);
        Tagged<WasmTypeInfo> type_info = wasm_obj->map()->wasm_type_info();
        CanonicalTypeIndex actual_type = type_info->type_index();
        if (!type_canonicalizer->IsCanonicalSubtype(actual_type,
                                                    canonical_index)) {
          *error_message = "object is not a subtype of expected type";
          return {};
        }
        return value;
      } else {
        *error_message = "JS object does not match expected wasm type";
        return {};
      }
    }
  }
}

// Utility which canonicalizes {expected} in addition.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, const WasmModule* module,
                                   Handle<Object> value, ValueType expected,
                                   const char** error_message) {
  CanonicalValueType canonical;
  if (expected.has_index()) {
    CanonicalTypeIndex index = module->canonical_type_id(expected.ref_index());
    canonical = CanonicalValueType::FromIndex(expected.kind(), index);
  } else {
    canonical = CanonicalValueType{expected};
  }
  return JSToWasmObject(isolate, value, canonical, error_message);
}

Handle<Object> WasmToJSObject(Isolate* isolate, Handle<Object> value) {
  if (IsWasmNull(*value)) {
    return isolate->factory()->null_value();
  } else if (IsWasmFuncRef(*value)) {
    return i::WasmInternalFunction::GetOrCreateExternal(i::direct_handle(
        i::Cast<i::WasmFuncRef>(*value)->internal(isolate), isolate));
  } else {
    return value;
  }
}

}  // namespace wasm

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"
#undef TRACE_IFT
