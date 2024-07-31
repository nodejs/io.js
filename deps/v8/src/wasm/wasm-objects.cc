// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-objects.h"

#include "src/base/iterator.h"
#include "src/base/vector.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/logging/counters.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/utils.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/serialized-signature-inl.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"

// Needs to be last so macros do not get undefined.
#include "src/objects/object-macros.h"

#define TRACE_IFT(...)              \
  do {                              \
    if (false) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {

// Import a few often used types from the wasm namespace.
using WasmFunction = wasm::WasmFunction;
using WasmModule = wasm::WasmModule;

namespace {

// The WasmTableObject::uses field holds pairs of <instance, index>. This enum
// helps compute the respective offset.
enum TableUses : int {
  kInstanceOffset,
  kIndexOffset,
  // Marker:
  kNumElements
};

}  // namespace

// static
Handle<WasmModuleObject> WasmModuleObject::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    Handle<Script> script) {
  Handle<Managed<wasm::NativeModule>> managed_native_module;
  if (script->type() == Script::Type::kWasm) {
    managed_native_module = handle(
        Managed<wasm::NativeModule>::cast(script->wasm_managed_native_module()),
        isolate);
  } else {
    const WasmModule* module = native_module->module();
    size_t memory_estimate =
        native_module->committed_code_space() +
        wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
    managed_native_module = Managed<wasm::NativeModule>::FromSharedPtr(
        isolate, memory_estimate, std::move(native_module));
  }
  Handle<WasmModuleObject> module_object = Handle<WasmModuleObject>::cast(
      isolate->factory()->NewJSObject(isolate->wasm_module_constructor()));
  module_object->set_managed_native_module(*managed_native_module);
  module_object->set_script(*script);
  return module_object;
}

Handle<String> WasmModuleObject::ExtractUtf8StringFromModuleBytes(
    Isolate* isolate, Handle<WasmModuleObject> module_object,
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

MaybeHandle<String> WasmModuleObject::GetModuleNameOrNull(
    Isolate* isolate, Handle<WasmModuleObject> module_object) {
  const WasmModule* module = module_object->module();
  if (!module->name.is_set()) return {};
  return ExtractUtf8StringFromModuleBytes(isolate, module_object, module->name,
                                          kNoInternalize);
}

MaybeHandle<String> WasmModuleObject::GetFunctionNameOrNull(
    Isolate* isolate, Handle<WasmModuleObject> module_object,
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
    Isolate* isolate, Handle<WasmInstanceObject> instance_object,
    wasm::ValueType type, uint32_t initial, bool has_maximum, uint32_t maximum,
    Handle<Object> initial_value) {
  CHECK(type.is_object_reference());

  Handle<FixedArray> entries = isolate->factory()->NewFixedArray(initial);
  for (int i = 0; i < static_cast<int>(initial); ++i) {
    entries->set(i, *initial_value);
  }

  Handle<Object> max;
  if (has_maximum) {
    max = isolate->factory()->NewNumberFromUint(maximum);
  } else {
    max = isolate->factory()->undefined_value();
  }

  Handle<JSFunction> table_ctor(
      isolate->native_context()->wasm_table_constructor(), isolate);
  auto table_obj = Handle<WasmTableObject>::cast(
      isolate->factory()->NewJSObject(table_ctor));
  DisallowGarbageCollection no_gc;

  if (!instance_object.is_null()) table_obj->set_instance(*instance_object);
  table_obj->set_entries(*entries);
  table_obj->set_current_length(initial);
  table_obj->set_maximum_length(*max);
  table_obj->set_raw_type(static_cast<int>(type.raw_bit_field()));

  table_obj->set_uses(ReadOnlyRoots(isolate).empty_fixed_array());
  return table_obj;
}

void WasmTableObject::AddUse(
    Isolate* isolate, Handle<WasmTableObject> table_obj,
    Handle<WasmTrustedInstanceData> trusted_instance_data, int table_index) {
  Handle<FixedArray> old_uses(table_obj->uses(), isolate);
  int old_length = old_uses->length();
  DCHECK_EQ(0, old_length % TableUses::kNumElements);

  if (trusted_instance_data.is_null()) return;
  // TODO(titzer): use weak cells here to avoid leaking instances.

  // Grow the uses table and add a new entry at the end.
  Handle<FixedArray> new_uses = isolate->factory()->CopyFixedArrayAndGrow(
      old_uses, TableUses::kNumElements);

  new_uses->set(old_length + TableUses::kInstanceOffset,
                trusted_instance_data->instance_object());
  new_uses->set(old_length + TableUses::kIndexOffset,
                Smi::FromInt(table_index));

  table_obj->set_uses(*new_uses);
}

int WasmTableObject::Grow(Isolate* isolate, Handle<WasmTableObject> table,
                          uint32_t count, Handle<Object> init_value) {
  uint32_t old_size = table->current_length();
  if (count == 0) return old_size;  // Degenerate case: nothing to do.

  // Check if growing by {count} is valid.
  uint32_t max_size;
  if (!Object::ToUint32(table->maximum_length(), &max_size)) {
    max_size = v8_flags.wasm_max_table_size;
  }
  max_size = std::min(max_size, v8_flags.wasm_max_table_size.value());
  DCHECK_LE(old_size, max_size);
  if (max_size - old_size < count) return -1;

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
        handle(table->entries(), isolate), grow);
    table->set_entries(*new_store, WriteBarrierMode::UPDATE_WRITE_BARRIER);
  }
  table->set_current_length(new_size);

  Handle<FixedArray> uses(table->uses(), isolate);
  DCHECK_EQ(0, uses->length() % TableUses::kNumElements);
  // Tables are stored in the instance object, no code patching is
  // necessary. We simply have to grow the raw tables in each instance
  // that has imported this table.

  // TODO(titzer): replace the dispatch table with a weak list of all
  // the instances that import a given table.
  for (int i = 0; i < uses->length(); i += TableUses::kNumElements) {
    int table_index = Smi::cast(uses->get(i + TableUses::kIndexOffset)).value();

    Handle<WasmTrustedInstanceData> trusted_instance_data{
        WasmInstanceObject::cast(uses->get(i + TableUses::kInstanceOffset))
            ->trusted_data(isolate),
        isolate};

    DCHECK_EQ(old_size,
              trusted_instance_data->dispatch_table(table_index)->length());
    WasmTrustedInstanceData::EnsureMinimumDispatchTableSize(
        isolate, trusted_instance_data, table_index, new_size);
  }

  for (uint32_t entry = old_size; entry < new_size; ++entry) {
    WasmTableObject::Set(isolate, table, entry, init_value);
  }
  return old_size;
}

bool WasmTableObject::is_in_bounds(uint32_t entry_index) {
  return entry_index < static_cast<uint32_t>(current_length());
}

MaybeHandle<Object> WasmTableObject::JSToWasmElement(
    Isolate* isolate, Handle<WasmTableObject> table, Handle<Object> entry,
    const char** error_message) {
  const WasmModule* module =
      IsUndefined(table->instance())
          ? nullptr
          : WasmInstanceObject::cast(table->instance())->module();
  return wasm::JSToWasmObject(isolate, module, entry, table->type(),
                              error_message);
}

void WasmTableObject::SetFunctionTableEntry(Isolate* isolate,
                                            Handle<WasmTableObject> table,
                                            int entry_index,
                                            Handle<Object> entry) {
  if (IsWasmNull(*entry, isolate)) {
    table->ClearDispatchTables(entry_index);  // Degenerate case.
    table->entries()->set(entry_index, ReadOnlyRoots(isolate).wasm_null());
    return;
  }
  DCHECK(IsWasmFuncRef(*entry));
  Handle<Object> external = WasmInternalFunction::GetOrCreateExternal(
      handle(WasmFuncRef::cast(*entry)->internal(), isolate));

  if (WasmExportedFunction::IsWasmExportedFunction(*external)) {
    auto exported_function = Handle<WasmExportedFunction>::cast(external);
    Handle<WasmTrustedInstanceData> target_instance_data(
        exported_function->instance()->trusted_data(isolate), isolate);
    int func_index = exported_function->function_index();
    auto* wasm_function =
        &target_instance_data->module()->functions[func_index];
    UpdateDispatchTables(isolate, table, entry_index, wasm_function,
                         target_instance_data);
  } else if (WasmJSFunction::IsWasmJSFunction(*external)) {
    UpdateDispatchTables(isolate, table, entry_index,
                         Handle<WasmJSFunction>::cast(external));
  } else {
    DCHECK(WasmCapiFunction::IsWasmCapiFunction(*external));
    UpdateDispatchTables(isolate, table, entry_index,
                         Handle<WasmCapiFunction>::cast(external));
  }
  table->entries()->set(entry_index, *entry);
}

// Note: This needs to be handlified because it transitively calls
// {ImportWasmJSFunctionIntoTable} which calls {NewWasmApiFunctionRef}.
void WasmTableObject::Set(Isolate* isolate, Handle<WasmTableObject> table,
                          uint32_t index, Handle<Object> entry) {
  // Callers need to perform bounds checks, type check, and error handling.
  DCHECK(table->is_in_bounds(index));

  Handle<FixedArray> entries(table->entries(), isolate);
  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  switch (table->type().heap_representation_non_shared()) {
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
      UNREACHABLE();
    default:
      DCHECK(!IsUndefined(table->instance()));
      if (WasmInstanceObject::cast(table->instance())
              ->module()
              ->has_signature(table->type().ref_index())) {
        SetFunctionTableEntry(isolate, table, entry_index, entry);
        return;
      }
      entries->set(entry_index, *entry);
      return;
  }
}

Handle<Object> WasmTableObject::Get(Isolate* isolate,
                                    Handle<WasmTableObject> table,
                                    uint32_t index) {
  Handle<FixedArray> entries(table->entries(), isolate);
  // Callers need to perform bounds checks and error handling.
  DCHECK(table->is_in_bounds(index));

  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  Handle<Object> entry(entries->get(entry_index), isolate);

  if (IsWasmNull(*entry, isolate)) return entry;
  if (IsWasmFuncRef(*entry)) return entry;

  switch (table->type().heap_representation_non_shared()) {
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
      UNREACHABLE();
    default:
      DCHECK(!IsUndefined(table->instance()));
      const WasmModule* module =
          WasmInstanceObject::cast(table->instance())->module();
      if (module->has_array(table->type().ref_index()) ||
          module->has_struct(table->type().ref_index())) {
        return entry;
      }
      DCHECK(module->has_signature(table->type().ref_index()));
      break;
  }

  // {entry} is not a valid entry in the table. It has to be a placeholder
  // for lazy initialization.
  Handle<Tuple2> tuple = Handle<Tuple2>::cast(entry);
  auto trusted_instance_data =
      handle(WasmInstanceObject::cast(tuple->value1())->trusted_data(isolate),
             isolate);
  int function_index = Smi::cast(tuple->value2()).value();

  // Create a WasmInternalFunction and WasmFuncRef for the function if it does
  // not exist yet, and store it in the table.
  Handle<WasmFuncRef> func_ref = WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate, trusted_instance_data, function_index);
  entries->set(entry_index, *func_ref);
  return func_ref;
}

void WasmTableObject::Fill(Isolate* isolate, Handle<WasmTableObject> table,
                           uint32_t start, Handle<Object> entry,
                           uint32_t count) {
  // Bounds checks must be done by the caller.
  DCHECK_LE(start, table->current_length());
  DCHECK_LE(count, table->current_length());
  DCHECK_LE(start + count, table->current_length());

  for (uint32_t i = 0; i < count; i++) {
    WasmTableObject::Set(isolate, table, start + i, entry);
  }
}

// static
void WasmTableObject::UpdateDispatchTables(
    Isolate* isolate, Handle<WasmTableObject> table, int entry_index,
    const wasm::WasmFunction* func,
    Handle<WasmTrustedInstanceData> target_instance_data) {
  // We simply need to update the IFTs for each instance that imports
  // this table.
  Handle<FixedArray> uses = handle(table->uses(), isolate);
  DCHECK_EQ(0, uses->length() % TableUses::kNumElements);

  Handle<ExposedTrustedObject> call_ref =
      func->imported
          // The function in the target instance was imported. Use its imports
          // table, which contains a tuple needed by the import wrapper.
          ? handle(ExposedTrustedObject::cast(
                       target_instance_data->imported_function_refs()->get(
                           func->func_index)),
                   isolate)
          // For wasm functions, just pass the target instance data.
          : target_instance_data;
  Address call_target = target_instance_data->GetCallTarget(func->func_index);

  int original_sig_id = func->sig_index;

  for (int i = 0, len = uses->length(); i < len; i += TableUses::kNumElements) {
    int table_index = Smi::cast(uses->get(i + TableUses::kIndexOffset)).value();
    Handle<WasmInstanceObject> instance_object = handle(
        WasmInstanceObject::cast(uses->get(i + TableUses::kInstanceOffset)),
        isolate);
    int sig_id = target_instance_data->module()
                     ->isorecursive_canonical_type_ids[original_sig_id];
    if (v8_flags.wasm_to_js_generic_wrapper &&
        IsWasmApiFunctionRef(*call_ref)) {
      Handle<WasmApiFunctionRef> orig_ref =
          Handle<WasmApiFunctionRef>::cast(call_ref);
      Handle<WasmApiFunctionRef> new_ref =
          isolate->factory()->NewWasmApiFunctionRef(orig_ref);
      if (new_ref->instance() == *instance_object) {
        WasmApiFunctionRef::SetIndexInTableAsCallOrigin(new_ref, entry_index);
      } else {
        WasmApiFunctionRef::SetCrossInstanceTableIndexAsCallOrigin(
            isolate, new_ref, instance_object, entry_index);
      }
      call_ref = new_ref;
    }
    Tagged<WasmTrustedInstanceData> instance_data =
        instance_object->trusted_data(isolate);
    instance_data->dispatch_table(table_index)
        ->Set(entry_index, *call_ref, call_target, sig_id);
  }
}

// static
void WasmTableObject::UpdateDispatchTables(Isolate* isolate,
                                           Handle<WasmTableObject> table,
                                           int entry_index,
                                           Handle<WasmJSFunction> function) {
  // We simply need to update the IFTs for each instance that imports
  // this table.
  Handle<FixedArray> uses(table->uses(), isolate);
  DCHECK_EQ(0, uses->length() % TableUses::kNumElements);

  for (int i = 0; i < uses->length(); i += TableUses::kNumElements) {
    int table_index = Smi::cast(uses->get(i + TableUses::kIndexOffset)).value();
    Handle<WasmTrustedInstanceData> trusted_instance_data(
        WasmInstanceObject::cast(uses->get(i + TableUses::kInstanceOffset))
            ->trusted_data(isolate),
        isolate);
    WasmTrustedInstanceData::ImportWasmJSFunctionIntoTable(
        isolate, trusted_instance_data, table_index, entry_index, function);
  }
}

// static
void WasmTableObject::UpdateDispatchTables(
    Isolate* isolate, Handle<WasmTableObject> table, int entry_index,
    Handle<WasmCapiFunction> capi_function) {
  // We simply need to update the IFTs for each instance that imports
  // this table.
  Handle<FixedArray> uses(table->uses(), isolate);
  DCHECK_EQ(0, uses->length() % TableUses::kNumElements);

  // Reconstruct signature.
  std::unique_ptr<wasm::ValueType[]> reps;
  wasm::FunctionSig sig = wasm::SerializedSignatureHelper::DeserializeSignature(
      capi_function->GetSerializedSignature(), &reps);

  for (int i = 0; i < uses->length(); i += TableUses::kNumElements) {
    int table_index = Smi::cast(uses->get(i + TableUses::kIndexOffset)).value();
    Handle<WasmTrustedInstanceData> trusted_instance_data(
        WasmInstanceObject::cast(uses->get(i + TableUses::kInstanceOffset))
            ->trusted_data(isolate),
        isolate);
    wasm::NativeModule* native_module =
        trusted_instance_data->module_object()->native_module();
    wasm::WasmImportWrapperCache* cache = native_module->import_wrapper_cache();
    auto kind = wasm::ImportCallKind::kWasmToCapi;
    uint32_t canonical_type_index =
        wasm::GetTypeCanonicalizer()->AddRecursiveGroup(&sig);
    int param_count = static_cast<int>(sig.parameter_count());
    wasm::WasmCode* wasm_code = cache->MaybeGet(kind, canonical_type_index,
                                                param_count, wasm::kNoSuspend);
    if (wasm_code == nullptr) {
      wasm::WasmCodeRefScope code_ref_scope;
      wasm::WasmImportWrapperCache::ModificationScope cache_scope(cache);
      wasm_code = compiler::CompileWasmCapiCallWrapper(native_module, &sig);
      wasm::WasmImportWrapperCache::CacheKey key(kind, canonical_type_index,
                                                 param_count, wasm::kNoSuspend);
      cache_scope[key] = wasm_code;
      wasm_code->IncRef();
      isolate->counters()->wasm_generated_code_size()->Increment(
          wasm_code->instructions().length());
      isolate->counters()->wasm_reloc_size()->Increment(
          wasm_code->reloc_info().length());
    }
    Tagged<HeapObject> ref = capi_function->shared()
                                 ->wasm_capi_function_data()
                                 ->internal(isolate)
                                 ->ref(isolate);
    Address call_target = wasm_code->instruction_start();
    trusted_instance_data->dispatch_table(table_index)
        ->Set(entry_index, ref, call_target, canonical_type_index);
  }
}

void WasmTableObject::ClearDispatchTables(int index) {
  DisallowGarbageCollection no_gc;
  Isolate* isolate = GetIsolate();
  Tagged<FixedArray> uses = this->uses();
  DCHECK_EQ(0, uses->length() % TableUses::kNumElements);
  for (int i = 0, e = uses->length(); i < e; i += TableUses::kNumElements) {
    int table_index = Smi::cast(uses->get(i + TableUses::kIndexOffset)).value();
    Tagged<WasmInstanceObject> target_instance_object =
        WasmInstanceObject::cast(uses->get(i + TableUses::kInstanceOffset));
    Tagged<WasmTrustedInstanceData> target_instance_data =
        target_instance_object->trusted_data(isolate);
    target_instance_data->dispatch_table(table_index)->Clear(index);
  }
}

// static
void WasmTableObject::SetFunctionTablePlaceholder(
    Isolate* isolate, Handle<WasmTableObject> table, int entry_index,
    Handle<WasmTrustedInstanceData> trusted_instance_data, int func_index) {
  // Put (instance, func_index) as a Tuple2 into the entry_index.
  // The {WasmExportedFunction} will be created lazily.
  // Allocate directly in old space as the tuples are typically long-lived, and
  // we create many of them, which would result in lots of GC when initializing
  // large tables.
  Handle<Tuple2> tuple = isolate->factory()->NewTuple2(
      handle(trusted_instance_data->instance_object(), isolate),
      handle(Smi::FromInt(func_index), isolate), AllocationType::kOld);
  table->entries()->set(entry_index, *tuple);
}

// static
void WasmTableObject::GetFunctionTableEntry(
    Isolate* isolate, const WasmModule* module, Handle<WasmTableObject> table,
    int entry_index, bool* is_valid, bool* is_null,
    MaybeHandle<WasmInstanceObject>* instance_object, int* function_index,
    MaybeHandle<WasmJSFunction>* maybe_js_function) {
  DCHECK(wasm::IsSubtypeOf(table->type(), wasm::kWasmFuncRef, module));
  DCHECK_LT(entry_index, table->current_length());
  // We initialize {is_valid} with {true}. We may change it later.
  *is_valid = true;
  Handle<Object> element(table->entries()->get(entry_index), isolate);

  *is_null = IsWasmNull(*element, isolate);
  if (*is_null) return;

  if (IsWasmFuncRef(*element)) {
    Handle<WasmInternalFunction> internal{
        WasmFuncRef::cast(*element)->internal(isolate), isolate};
    element = WasmInternalFunction::GetOrCreateExternal(internal);
  }
  if (WasmExportedFunction::IsWasmExportedFunction(*element)) {
    auto target_func = Handle<WasmExportedFunction>::cast(element);
    *instance_object = handle(target_func->instance(), isolate);
    *function_index = target_func->function_index();
    *maybe_js_function = MaybeHandle<WasmJSFunction>();
    return;
  }
  if (WasmJSFunction::IsWasmJSFunction(*element)) {
    *instance_object = MaybeHandle<WasmInstanceObject>();
    *maybe_js_function = Handle<WasmJSFunction>::cast(element);
    return;
  }
  if (IsTuple2(*element)) {
    auto tuple = Handle<Tuple2>::cast(element);
    *instance_object =
        handle(WasmInstanceObject::cast(tuple->value1()), isolate);
    *function_index = Smi::cast(tuple->value2()).value();
    *maybe_js_function = MaybeHandle<WasmJSFunction>();
    return;
  }
  *is_valid = false;
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
  CHECK_IMPLIES(use_trap_handler, backing_store->has_guard_regions());

  trusted_instance_data->SetRawMemory(
      memory_index, reinterpret_cast<uint8_t*>(buffer->backing_store()),
      buffer->byte_length());
}

}  // namespace

Handle<WasmMemoryObject> WasmMemoryObject::New(Isolate* isolate,
                                               Handle<JSArrayBuffer> buffer,
                                               int maximum,
                                               WasmMemoryFlag memory_type) {
  Handle<JSFunction> memory_ctor(
      isolate->native_context()->wasm_memory_constructor(), isolate);

  auto memory_object = Handle<WasmMemoryObject>::cast(
      isolate->factory()->NewJSObject(memory_ctor, AllocationType::kOld));
  memory_object->set_array_buffer(*buffer);
  memory_object->set_maximum_pages(maximum);
  memory_object->set_is_memory64(memory_type == WasmMemoryFlag::kWasmMemory64);
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
  // to it's owning WasmMemoryObject instance.
  Handle<Symbol> symbol = isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, buffer, symbol, memory_object).Check();

  return memory_object;
}

MaybeHandle<WasmMemoryObject> WasmMemoryObject::New(
    Isolate* isolate, int initial, int maximum, SharedFlag shared,
    WasmMemoryFlag memory_type) {
  bool has_maximum = maximum != kNoMaximum;

  int engine_maximum = memory_type == WasmMemoryFlag::kWasmMemory64
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
                                       memory_type, shared);

  if (!backing_store) return {};

  Handle<JSArrayBuffer> buffer =
      shared == SharedFlag::kShared
          ? isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store))
          : isolate->factory()->NewJSArrayBuffer(std::move(backing_store));

  return New(isolate, buffer, maximum, memory_type);
}

void WasmMemoryObject::UseInInstance(
    Isolate* isolate, Handle<WasmMemoryObject> memory,
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    int memory_index_in_instance) {
  SetInstanceMemory(*trusted_instance_data, memory->array_buffer(),
                    memory_index_in_instance);
  Handle<WeakArrayList> instances{memory->instances(), isolate};
  auto weak_instance_object = MaybeObjectHandle::Weak(
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
        WasmInstanceObject::cast(elem.GetHeapObjectAssumeWeak());
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
                               Handle<WasmMemoryObject> memory_object,
                               uint32_t pages) {
  TRACE_EVENT0("v8.wasm", "wasm.GrowMemory");
  Handle<JSArrayBuffer> old_buffer(memory_object->array_buffer(), isolate);

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

  base::Optional<size_t> result_inplace =
      try_grow_in_place
          ? backing_store->GrowWasmMemoryInPlace(isolate, pages, max_pages)
          : base::nullopt;
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
    Handle<JSArrayBuffer> new_buffer =
        isolate->factory()->NewJSArrayBuffer(std::move(backing_store));
    memory_object->SetNewBuffer(*new_buffer);
    // For debugging purposes we memorize a link from the JSArrayBuffer
    // to it's owning WasmMemoryObject instance.
    Handle<Symbol> symbol =
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
    return -1;
  }

  // Detach old and create a new one with the new backing store.
  JSArrayBuffer::Detach(old_buffer, true).Check();
  Handle<JSArrayBuffer> new_buffer =
      isolate->factory()->NewJSArrayBuffer(std::move(new_backing_store));
  memory_object->SetNewBuffer(*new_buffer);
  // For debugging purposes we memorize a link from the JSArrayBuffer
  // to it's owning WasmMemoryObject instance.
  Handle<Symbol> symbol = isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, new_buffer, symbol, memory_object).Check();
  return static_cast<int32_t>(old_pages);  // success
}

// static
MaybeHandle<WasmGlobalObject> WasmGlobalObject::New(
    Isolate* isolate, Handle<WasmInstanceObject> instance_object,
    MaybeHandle<JSArrayBuffer> maybe_untagged_buffer,
    MaybeHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
    int32_t offset, bool is_mutable) {
  Handle<JSFunction> global_ctor(
      isolate->native_context()->wasm_global_constructor(), isolate);
  auto global_obj = Handle<WasmGlobalObject>::cast(
      isolate->factory()->NewJSObject(global_ctor));
  {
    // Disallow GC until all fields have acceptable types.
    DisallowGarbageCollection no_gc;
    if (!instance_object.is_null()) global_obj->set_instance(*instance_object);
    global_obj->set_type(type);
    global_obj->set_offset(offset);
    global_obj->set_is_mutable(is_mutable);
  }

  if (type.is_reference()) {
    DCHECK(maybe_untagged_buffer.is_null());
    Handle<FixedArray> tagged_buffer;
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

    Handle<JSArrayBuffer> untagged_buffer;
    if (!maybe_untagged_buffer.ToHandle(&untagged_buffer)) {
      MaybeHandle<JSArrayBuffer> result =
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

FunctionTargetAndRef::FunctionTargetAndRef(
    Handle<WasmInstanceObject> target_instance_object, int target_func_index) {
  Isolate* isolate = target_instance_object->GetIsolate();
  Handle<WasmTrustedInstanceData> trusted_target_instance_data{
      target_instance_object->trusted_data(isolate), isolate};
  ref_ = trusted_target_instance_data;
  if (target_func_index <
      static_cast<int>(
          trusted_target_instance_data->module()->num_imported_functions)) {
    // The function in the target instance was imported. Use its imports table,
    // which contains a tuple needed by the import wrapper.
    ref_ =
        handle(ExposedTrustedObject::cast(
                   trusted_target_instance_data->imported_function_refs()->get(
                       target_func_index)),
               isolate);
  } else {
    // The function in the target instance was not imported.
  }
  call_target_ = trusted_target_instance_data->GetCallTarget(target_func_index);
}

void ImportedFunctionEntry::SetWasmToJs(Isolate* isolate,
                                        Handle<JSReceiver> callable,
                                        wasm::Suspend suspend,
                                        const wasm::FunctionSig* sig) {
  Address wrapper;
  if (wasm::IsJSCompatibleSignature(sig)) {
    DCHECK(
        UseGenericWasmToJSWrapper(wasm::kDefaultImportCallKind, sig, suspend));
    wrapper = isolate->builtins()
                  ->code(Builtin::kWasmToJsWrapperAsm)
                  ->instruction_start();
  } else {
    wrapper = isolate->builtins()
                  ->code(Builtin::kWasmToJsWrapperInvalidSig)
                  ->instruction_start();
  }
  TRACE_IFT("Import callable 0x%" PRIxPTR "[%d] = {callable=0x%" PRIxPTR
            ", target=0x%" PRIxPTR "}\n",
            instance_object_->ptr(), index_, callable->ptr(), wrapper);
  Handle<WasmApiFunctionRef> ref = isolate->factory()->NewWasmApiFunctionRef(
      callable, suspend, instance_object_,
      wasm::SerializedSignatureHelper::SerializeSignature(isolate, sig));
  WasmApiFunctionRef::SetImportIndexAsCallOrigin(ref, index_);
  DisallowGarbageCollection no_gc;
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      instance_object_->trusted_data(isolate);
  trusted_instance_data->imported_function_refs()->set(index_, *ref);
  trusted_instance_data->imported_function_targets()->set(index_, wrapper);
}

void ImportedFunctionEntry::SetWasmToJs(
    Isolate* isolate, Handle<JSReceiver> callable,
    const wasm::WasmCode* wasm_to_js_wrapper, wasm::Suspend suspend,
    const wasm::FunctionSig* sig) {
  TRACE_IFT("Import callable 0x%" PRIxPTR "[%d] = {callable=0x%" PRIxPTR
            ", target=%p}\n",
            instance_object_->ptr(), index_, callable->ptr(),
            wasm_to_js_wrapper->instructions().begin());
  DCHECK(wasm_to_js_wrapper->kind() == wasm::WasmCode::kWasmToJsWrapper ||
         wasm_to_js_wrapper->kind() == wasm::WasmCode::kWasmToCapiWrapper);
  Handle<WasmApiFunctionRef> ref = isolate->factory()->NewWasmApiFunctionRef(
      callable, suspend, instance_object_,
      wasm::SerializedSignatureHelper::SerializeSignature(isolate, sig));
  // The wasm-to-js wrapper is already optimized, the call_origin should never
  // be accessed.
  ref->set_call_origin(Smi::FromInt(WasmApiFunctionRef::kInvalidCallOrigin));
  DisallowGarbageCollection no_gc;
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      instance_object_->trusted_data(isolate);
  trusted_instance_data->imported_function_refs()->set(index_, *ref);
  trusted_instance_data->imported_function_targets()->set(
      index_, wasm_to_js_wrapper->instruction_start());
}

void ImportedFunctionEntry::SetWasmToWasm(
    Tagged<WasmTrustedInstanceData> target_instance_data, Address call_target) {
  TRACE_IFT("Import Wasm 0x%" PRIxPTR "[%d] = {instance_data=0x%" PRIxPTR
            ", target=0x%" PRIxPTR "}\n",
            instance_object_->ptr(), index_, target_instance_data.ptr(),
            call_target);
  DisallowGarbageCollection no_gc;
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      instance_object_->trusted_data(instance_object_->GetIsolate());
  trusted_instance_data->imported_function_refs()->set(index_,
                                                       target_instance_data);
  trusted_instance_data->imported_function_targets()->set(index_, call_target);
}

// Returns an empty Tagged<Object>() if no callable is available, a JSReceiver
// otherwise.
Tagged<Object> ImportedFunctionEntry::maybe_callable() {
  Tagged<Object> value = object_ref();
  if (!IsWasmApiFunctionRef(value)) return Tagged<Object>();
  return JSReceiver::cast(WasmApiFunctionRef::cast(value)->callable());
}

Tagged<JSReceiver> ImportedFunctionEntry::callable() {
  return JSReceiver::cast(WasmApiFunctionRef::cast(object_ref())->callable());
}

Tagged<Object> ImportedFunctionEntry::object_ref() {
  return instance_object_->trusted_data(instance_object_->GetIsolate())
      ->imported_function_refs()
      ->get(index_);
}

Address ImportedFunctionEntry::target() {
  return instance_object_->trusted_data(instance_object_->GetIsolate())
      ->imported_function_targets()
      ->get(index_);
}

void ImportedFunctionEntry::set_target(Address new_target) {
  return instance_object_->trusted_data(instance_object_->GetIsolate())
      ->imported_function_targets()
      ->set(index_, new_target);
}

// static
constexpr std::array<uint16_t, 18> WasmTrustedInstanceData::kTaggedFieldOffsets;
// static
constexpr std::array<const char*, 18>
    WasmTrustedInstanceData::kTaggedFieldNames;
// static
constexpr std::array<uint16_t, 3>
    WasmTrustedInstanceData::kProtectedFieldOffsets;
// static
constexpr std::array<const char*, 3>
    WasmTrustedInstanceData::kProtectedFieldNames;

// static
void WasmTrustedInstanceData::EnsureMinimumDispatchTableSize(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    int table_index, int minimum_size) {
  Handle<WasmDispatchTable> old_dispatch_table{
      trusted_instance_data->dispatch_table(table_index), isolate};
  if (old_dispatch_table->length() >= minimum_size) return;
  Handle<WasmDispatchTable> new_dispatch_table =
      WasmDispatchTable::Grow(isolate, old_dispatch_table, minimum_size);

  if (*old_dispatch_table == *new_dispatch_table) return;
  trusted_instance_data->dispatch_tables()->set(table_index,
                                                *new_dispatch_table);
  if (table_index == 0) {
    trusted_instance_data->set_dispatch_table0(*new_dispatch_table);
  }
}

void WasmTrustedInstanceData::SetRawMemory(int memory_index, uint8_t* mem_start,
                                           size_t mem_size) {
  CHECK_LE(memory_index, module()->memories.size());

  CHECK_LE(mem_size, module()->memories[memory_index].is_memory64
                         ? wasm::max_mem64_bytes()
                         : wasm::max_mem32_bytes());
  // All memory bases and sizes are stored in a FixedAddressArray.
  Tagged<FixedAddressArray> bases_and_sizes = memory_bases_and_sizes();
  bases_and_sizes->set_sandboxed_pointer(memory_index * 2,
                                         reinterpret_cast<Address>(mem_start));
  bases_and_sizes->set(memory_index * 2 + 1, mem_size);
  // Memory 0 has fast-access fields.
  if (memory_index == 0) {
    set_memory0_start(mem_start);
    set_memory0_size(mem_size);
  }
}

const WasmModule* WasmTrustedInstanceData::module() {
  return module_object()->module();
}

Handle<WasmTrustedInstanceData> WasmTrustedInstanceData::New(
    Isolate* isolate, Handle<WasmModuleObject> module_object) {
  // Do first allocate all objects that will be stored in instance fields,
  // because otherwise we would have to allocate when the instance is not fully
  // initialized yet, which can lead to heap verification errors.
  const WasmModule* module = module_object->module();

  int num_imported_functions = module->num_imported_functions;
  Handle<FixedAddressArray> imported_function_targets =
      FixedAddressArray::New(isolate, num_imported_functions);
  Handle<ProtectedFixedArray> imported_function_refs =
      isolate->factory()->NewProtectedFixedArray(num_imported_functions);
  Handle<FixedArray> well_known_imports =
      isolate->factory()->NewFixedArray(num_imported_functions);

  Handle<FixedArray> func_refs = isolate->factory()->NewFixedArrayWithZeroes(
      static_cast<int>(module->functions.size()));

  int num_imported_mutable_globals = module->num_imported_mutable_globals;
  // The imported_mutable_globals is essentially a FixedAddressArray (storing
  // sandboxed pointers), but some entries (the indices for reference-type
  // globals) are accessed as 32-bit integers which is more convenient with a
  // raw ByteArray.
  Handle<FixedAddressArray> imported_mutable_globals =
      FixedAddressArray::New(isolate, num_imported_mutable_globals);

  int num_data_segments = module->num_declared_data_segments;
  Handle<FixedAddressArray> data_segment_starts =
      FixedAddressArray::New(isolate, num_data_segments);
  Handle<FixedUInt32Array> data_segment_sizes =
      FixedUInt32Array::New(isolate, num_data_segments);

  static_assert(wasm::kV8MaxWasmMemories < kMaxInt / 2);
  int num_memories = static_cast<int>(module->memories.size());
  Handle<FixedArray> memory_objects =
      isolate->factory()->NewFixedArray(num_memories);
  Handle<FixedAddressArray> memory_bases_and_sizes =
      FixedAddressArray::New(isolate, 2 * num_memories);

  // TODO(clemensb): Should we have singleton empty dispatch table in the
  // trusted space?
  Handle<WasmDispatchTable> empty_dispatch_table =
      isolate->factory()->NewWasmDispatchTable(0);
  Handle<ProtectedFixedArray> empty_protected_fixed_array =
      isolate->factory()->empty_protected_fixed_array();

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

    trusted_data->set_imported_function_targets(*imported_function_targets);
    trusted_data->set_imported_mutable_globals(*imported_mutable_globals);
    trusted_data->set_dispatch_table0(*empty_dispatch_table);
    trusted_data->set_dispatch_tables(*empty_protected_fixed_array);
    trusted_data->set_data_segment_starts(*data_segment_starts);
    trusted_data->set_data_segment_sizes(*data_segment_sizes);
    trusted_data->set_element_segments(empty_fixed_array);
    trusted_data->set_imported_function_refs(*imported_function_refs);
    trusted_data->set_new_allocation_limit_address(
        isolate->heap()->NewSpaceAllocationLimitAddress());
    trusted_data->set_new_allocation_top_address(
        isolate->heap()->NewSpaceAllocationTopAddress());
    trusted_data->set_old_allocation_limit_address(
        isolate->heap()->OldSpaceAllocationLimitAddress());
    trusted_data->set_old_allocation_top_address(
        isolate->heap()->OldSpaceAllocationTopAddress());
    trusted_data->set_globals_start(empty_backing_store_buffer);
    trusted_data->set_native_context(*isolate->native_context());
    trusted_data->set_jump_table_start(
        module_object->native_module()->jump_table_start());
    trusted_data->set_hook_on_function_call_address(
        isolate->debug()->hook_on_function_call_address());
    trusted_data->set_managed_object_maps(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_well_known_imports(*well_known_imports);
    trusted_data->set_func_refs(*func_refs);
    trusted_data->set_feedback_vectors(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_tiering_budget_array(
        module_object->native_module()->tiering_budget_array());
    trusted_data->set_break_on_entry(module_object->script()->break_on_entry());
    trusted_data->InitDataSegmentArrays(*module_object);
    trusted_data->set_memory0_start(empty_backing_store_buffer);
    trusted_data->set_memory0_size(0);
    trusted_data->set_memory_objects(*memory_objects);
    trusted_data->set_memory_bases_and_sizes(*memory_bases_and_sizes);

    for (int i = 0; i < num_memories; ++i) {
      memory_bases_and_sizes->set_sandboxed_pointer(
          2 * i, reinterpret_cast<Address>(empty_backing_store_buffer));
      memory_bases_and_sizes->set(2 * i + 1, 0);
    }
  }

  // Allocate the exports object, to be store in the instance object.
  Handle<JSObject> exports_object =
      isolate->factory()->NewJSObjectWithNullProto();

  // Allocate the WasmInstanceObject (JS wrapper).
  Handle<JSFunction> instance_cons(
      isolate->native_context()->wasm_instance_constructor(), isolate);
  Handle<WasmInstanceObject> instance_object = Handle<WasmInstanceObject>::cast(
      isolate->factory()->NewJSObject(instance_cons, AllocationType::kOld));
  instance_object->set_trusted_data(*trusted_data);
  instance_object->set_module_object(*module_object);
  instance_object->set_exports_object(*exports_object);
  trusted_data->set_instance_object(*instance_object);

  // Insert the new instance into the scripts weak list of instances. This list
  // is used for breakpoints affecting all instances belonging to the script.
  if (module_object->script()->type() == Script::Type::kWasm) {
    Handle<WeakArrayList> weak_instance_list(
        module_object->script()->wasm_weak_instance_list(), isolate);
    weak_instance_list = WeakArrayList::Append(
        isolate, weak_instance_list, MaybeObjectHandle::Weak(instance_object));
    module_object->script()->set_wasm_weak_instance_list(*weak_instance_list);
  }

  return trusted_data;
}

void WasmTrustedInstanceData::InitDataSegmentArrays(
    Tagged<WasmModuleObject> module_object) {
  auto module = module_object->module();
  auto wire_bytes = module_object->native_module()->wire_bytes();
  auto num_data_segments = module->num_declared_data_segments;
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

Address WasmTrustedInstanceData::GetCallTarget(uint32_t func_index) {
  wasm::NativeModule* native_module = module_object()->native_module();
  if (func_index < native_module->num_imported_functions()) {
    return imported_function_targets()->get(func_index);
  }
  return jump_table_start() +
         JumpTableOffset(native_module->module(), func_index);
}

// static
bool WasmTrustedInstanceData::CopyTableEntries(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    uint32_t table_dst_index, uint32_t table_src_index, uint32_t dst,
    uint32_t src, uint32_t count) {
  CHECK_LT(table_dst_index, trusted_instance_data->tables()->length());
  CHECK_LT(table_src_index, trusted_instance_data->tables()->length());
  auto table_dst =
      handle(WasmTableObject::cast(
                 trusted_instance_data->tables()->get(table_dst_index)),
             isolate);
  auto table_src =
      handle(WasmTableObject::cast(
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
base::Optional<MessageTemplate> WasmTrustedInstanceData::InitTableEntries(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    uint32_t table_index, uint32_t segment_index, uint32_t dst, uint32_t src,
    uint32_t count) {
  AccountingAllocator allocator;
  // This {Zone} will be used only by the temporary WasmFullDecoder allocated
  // down the line from this call. Therefore it is safe to stack-allocate it
  // here.
  Zone zone(&allocator, "LoadElemSegment");

  Handle<WasmTableObject> table_object = handle(
      WasmTableObject::cast(trusted_instance_data->tables()->get(table_index)),
      isolate);

  // If needed, try to lazily initialize the element segment.
  base::Optional<MessageTemplate> opt_error = wasm::InitializeElementSegment(
      &zone, isolate, trusted_instance_data, segment_index);
  if (opt_error.has_value()) return opt_error;

  Handle<FixedArray> elem_segment =
      handle(FixedArray::cast(
                 trusted_instance_data->element_segments()->get(segment_index)),
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
        handle(elem_segment->get(static_cast<int>(src + i)), isolate));
  }

  return {};
}

bool WasmTrustedInstanceData::try_get_func_ref(int index,
                                               Tagged<WasmFuncRef>* result) {
  Tagged<Object> val = func_refs()->get(index);
  if (IsSmi(val)) return false;
  *result = WasmFuncRef::cast(val);
  return true;
}

Handle<WasmFuncRef> WasmTrustedInstanceData::GetOrCreateFuncRef(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    int function_index) {
  Tagged<WasmFuncRef> existing_func_ref;
  if (trusted_instance_data->try_get_func_ref(function_index,
                                              &existing_func_ref)) {
    return handle(existing_func_ref, isolate);
  }

  const wasm::NativeModule* native_module =
      trusted_instance_data->module_object()->native_module();
  const WasmModule* module = native_module->module();
  Handle<ExposedTrustedObject> ref =
      function_index >= static_cast<int>(module->num_imported_functions)
          ? trusted_instance_data
          : handle(ExposedTrustedObject::cast(
                       trusted_instance_data->imported_function_refs()->get(
                           function_index)),
                   isolate);

  bool setup_new_ref_with_generic_wrapper =
      v8_flags.wasm_to_js_generic_wrapper && IsWasmApiFunctionRef(*ref);

  if (setup_new_ref_with_generic_wrapper) {
    Handle<WasmApiFunctionRef> wafr = Handle<WasmApiFunctionRef>::cast(ref);
    ref = isolate->factory()->NewWasmApiFunctionRef(
        handle(wafr->callable(), isolate),
        static_cast<wasm::Suspend>(wafr->suspend()),
        handle(wafr->instance(), isolate), handle(wafr->sig(), isolate));
  }

  int sig_index = module->functions[function_index].sig_index;
  // TODO(14034): Create funcref RTTs lazily?
  Handle<Map> rtt = handle(
      Map::cast(trusted_instance_data->managed_object_maps()->get(sig_index)),
      isolate);

  // Only set the call target if we do not use the generic wasm-to-js wrapper.
  // The reason is that after wrapper tier-up the call target cannot be set
  // anymore for imported functions, because the slot in the imported function
  // table cannot be found anymore. Avoiding setting the call target makes the
  // wrapper tiers behave more consistently, which can prevent surprising bugs.
  // Background: the WasmInternalFunction has two fields to store a reference to
  // wrapper code: 1) the `call_target` field, and 2) the `code` field. In
  // generated code, we use the `call_target` if it is set, and if it is not
  // set, the `code` field is used. During wrapper tier-up, only the `code`
  // field can be updated, not the `call_target` field, because the slot in the
  // imported function table cannot be found anymore. For the newly created
  // WasmInternalFunction, this would mean that calls with the generic wrapper
  // would be done with the `call_target`, but after tier-up, the call with the
  // optimized wrapper would be done with the 'code' field.
  auto internal_function = isolate->factory()->NewWasmInternalFunction(
      setup_new_ref_with_generic_wrapper
          ? 0
          : trusted_instance_data->GetCallTarget(function_index),
      ref, rtt, function_index);

  if (setup_new_ref_with_generic_wrapper) {
    Handle<WasmApiFunctionRef> wafr = Handle<WasmApiFunctionRef>::cast(ref);
    const wasm::FunctionSig* sig =
        module->signature(module->functions[function_index].sig_index);
    if (wasm::IsJSCompatibleSignature(sig)) {
      DCHECK(UseGenericWasmToJSWrapper(wasm::kDefaultImportCallKind, sig,
                                       wasm::Suspend::kNoSuspend));
      WasmApiFunctionRef::SetInternalFunctionAsCallOrigin(wafr,
                                                          internal_function);
      internal_function->set_code(
          isolate->builtins()->code(Builtin::kWasmToJsWrapperAsm));
    } else {
      internal_function->set_code(
          isolate->builtins()->code(Builtin::kWasmToJsWrapperInvalidSig));
    }
  }
  Handle<WasmFuncRef> func_ref{WasmFuncRef::cast(internal_function->func_ref()),
                               isolate};
  trusted_instance_data->func_refs()->set(function_index, *func_ref);
  return func_ref;
}

bool WasmInternalFunction::try_get_external(Tagged<JSFunction>* result) {
  if (IsUndefined(external())) return false;
  *result = JSFunction::cast(external());
  return true;
}

// static
Handle<JSFunction> WasmInternalFunction::GetOrCreateExternal(
    Handle<WasmInternalFunction> internal) {
  Isolate* isolate = GetIsolateFromWritableObject(*internal);

  Tagged<JSFunction> existing_external;
  if (internal->try_get_external(&existing_external)) {
    return handle(existing_external, isolate);
  }

  // {this} can either be:
  // - a declared function, i.e. {ref()} is a WasmTrustedInstanceData,
  // - or an imported callable, i.e. {ref()} is a WasmApiFunctionRef which
  //   refers to the imported instance.
  // It cannot be a JS/C API function as for those, the external function is set
  // at creation.
  Handle<ExposedTrustedObject> ref{internal->ref(isolate), isolate};
  Handle<WasmTrustedInstanceData> instance_data =
      IsWasmTrustedInstanceData(*ref)
          ? Handle<WasmTrustedInstanceData>::cast(ref)
          : handle(WasmInstanceObject::cast(
                       WasmApiFunctionRef::cast(*ref)->instance())
                       ->trusted_data(isolate),
                   isolate);
  const WasmModule* module = instance_data->module();
  const WasmFunction& function = module->functions[internal->function_index()];
  uint32_t canonical_sig_index =
      module->isorecursive_canonical_type_ids[function.sig_index];
  isolate->heap()->EnsureWasmCanonicalRttsSize(canonical_sig_index + 1);
  int wrapper_index =
      wasm::GetExportWrapperIndex(canonical_sig_index, function.imported);

  Tagged<MaybeObject> entry =
      isolate->heap()->js_to_wasm_wrappers()->Get(wrapper_index);

  Handle<Code> wrapper_code;
  // {entry} can be cleared, {undefined}, or a ready {CodeWrapper}.
  DCHECK(entry.IsCleared() || IsUndefined(entry.GetHeapObject()) ||
         IsCodeWrapper(entry.GetHeapObject()));
  if (entry.IsStrongOrWeak() && IsCodeWrapper(entry.GetHeapObject())) {
    wrapper_code = handle(
        CodeWrapper::cast(entry.GetHeapObject())->code(isolate), isolate);
  } else if (!function.imported &&
             CanUseGenericJsToWasmWrapper(module, function.sig)) {
    wrapper_code = isolate->builtins()->code_handle(Builtin::kJSToWasmWrapper);
  } else {
    // The wrapper may not exist yet if no function in the exports section has
    // this signature. We compile it and store the wrapper in the module for
    // later use.
    wrapper_code = wasm::JSToWasmWrapperCompilationUnit::CompileJSToWasmWrapper(
        isolate, function.sig, canonical_sig_index, module, function.imported);
  }
  if (!wrapper_code->is_builtin()) {
    // Store the wrapper in the isolate, or make its reference weak now that we
    // have a function referencing it.
    isolate->heap()->js_to_wasm_wrappers()->Set(
        wrapper_index, MakeWeak(wrapper_code->wrapper()));
  }
  auto result = WasmExportedFunction::New(
      isolate, instance_data, internal, internal->function_index(),
      static_cast<int>(function.sig->parameter_count()), wrapper_code);

  internal->set_external(*result);
  return result;
}

// static
void WasmApiFunctionRef::SetImportIndexAsCallOrigin(
    Handle<WasmApiFunctionRef> ref, int entry_index) {
  ref->set_call_origin(Smi::FromInt(-entry_index - 1));
}

// static
void WasmApiFunctionRef::SetIndexInTableAsCallOrigin(
    Handle<WasmApiFunctionRef> ref, int entry_index) {
  ref->set_call_origin(Smi::FromInt(entry_index + 1));
}

// static
bool WasmApiFunctionRef::CallOriginIsImportIndex(Handle<Object> call_origin) {
  return Smi::cast(*call_origin).value() < 0;
}

// static
bool WasmApiFunctionRef::CallOriginIsIndexInTable(Handle<Object> call_origin) {
  return Smi::cast(*call_origin).value() > 0;
}

// static
int WasmApiFunctionRef::CallOriginAsIndex(Handle<Object> call_origin) {
  int raw_index = Smi::cast(*call_origin).value();
  CHECK_NE(raw_index, kInvalidCallOrigin);
  if (raw_index < 0) {
    raw_index = -raw_index;
  }
  return raw_index - 1;
}

// static
void WasmApiFunctionRef::SetCrossInstanceTableIndexAsCallOrigin(
    Isolate* isolate, Handle<WasmApiFunctionRef> ref,
    Handle<WasmInstanceObject> instance_object, int entry_index) {
  Handle<Tuple2> tuple = isolate->factory()->NewTuple2(
      instance_object, handle(Smi::FromInt(entry_index + 1), isolate),
      AllocationType::kOld);
  ref->set_call_origin(*tuple);
}

// static
void WasmApiFunctionRef::SetInternalFunctionAsCallOrigin(
    Handle<WasmApiFunctionRef> ref, Handle<WasmInternalFunction> internal) {
  ref->set_call_origin(internal->func_ref());
}

// static
void WasmTrustedInstanceData::ImportWasmJSFunctionIntoTable(
    Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
    int table_index, int entry_index, Handle<WasmJSFunction> js_function) {
  // Deserialize the signature encapsulated with the {WasmJSFunction}.
  // Note that {SignatureMap::Find} may return {-1} if the signature is
  // not found; it will simply never match any check.
  Zone zone(isolate->allocator(), ZONE_NAME);
  const wasm::FunctionSig* sig = js_function->GetSignature(&zone);
  // Get the function's canonical signature index. Note that the function's
  // signature may not be present in the importing module.
  uint32_t canonical_sig_index =
      wasm::GetTypeCanonicalizer()->AddRecursiveGroup(sig);

  // Compile a wrapper for the target callable.
  Handle<JSReceiver> callable(js_function->GetCallable(), isolate);
  wasm::Suspend suspend = js_function->GetSuspend();
  wasm::WasmCodeRefScope code_ref_scope;

  const wasm::WasmModule* module = trusted_instance_data->module();
  auto module_canonical_ids = module->isorecursive_canonical_type_ids;
  // TODO(manoskouk): Consider adding a set of canonical indices to the module
  // to avoid this linear search.
  auto sig_in_module =
      std::find(module_canonical_ids.begin(), module_canonical_ids.end(),
                canonical_sig_index);

  if (sig_in_module == module_canonical_ids.end()) {
    trusted_instance_data->dispatch_table(table_index)->Clear(entry_index);
    return;
  }

  wasm::NativeModule* native_module =
      trusted_instance_data->module_object()->native_module();
  wasm::WasmImportData resolved({}, -1, callable, sig, canonical_sig_index,
                                wasm::WellKnownImport::kUninstantiated);
  wasm::ImportCallKind kind = resolved.kind();
  callable = resolved.callable();  // Update to ultimate target.
  DCHECK_NE(wasm::ImportCallKind::kLinkError, kind);
  // {expected_arity} should only be used if kind != kJSFunctionArityMismatch.
  int expected_arity = -1;
  if (kind == wasm::ImportCallKind ::kJSFunctionArityMismatch) {
    expected_arity = Handle<JSFunction>::cast(callable)
                         ->shared()
                         ->internal_formal_parameter_count_without_receiver();
  }

  wasm::WasmImportWrapperCache* cache = native_module->import_wrapper_cache();
  wasm::WasmCode* wasm_code =
      cache->MaybeGet(kind, canonical_sig_index, expected_arity, suspend);
  Address call_target;
  if (wasm_code) {
    call_target = wasm_code->instruction_start();
  } else if (UseGenericWasmToJSWrapper(kind, sig, resolved.suspend())) {
    call_target = isolate->builtins()
                      ->code(Builtin::kWasmToJsWrapperAsm)
                      ->instruction_start();
  } else {
    wasm::CompilationEnv env = wasm::CompilationEnv::ForModule(native_module);
    wasm::WasmCompilationResult result = compiler::CompileWasmImportCallWrapper(
        &env, kind, sig, false, expected_arity, suspend);
    std::unique_ptr<wasm::WasmCode> compiled_code = native_module->AddCode(
        result.func_index, result.code_desc, result.frame_slot_count,
        result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), GetCodeKind(result),
        wasm::ExecutionTier::kNone, wasm::kNotForDebugging);
    wasm_code = native_module->PublishCode(std::move(compiled_code));
    isolate->counters()->wasm_generated_code_size()->Increment(
        wasm_code->instructions().length());
    isolate->counters()->wasm_reloc_size()->Increment(
        wasm_code->reloc_info().length());

    wasm::WasmImportWrapperCache::ModificationScope cache_scope(cache);
    wasm::WasmImportWrapperCache::CacheKey key(kind, canonical_sig_index,
                                               expected_arity, suspend);
    cache_scope[key] = wasm_code;
    call_target = wasm_code->instruction_start();
  }

  // Update the dispatch table.
  int sig_id = static_cast<int>(
      std::distance(module_canonical_ids.begin(), sig_in_module));
  Handle<WasmApiFunctionRef> ref = isolate->factory()->NewWasmApiFunctionRef(
      callable, suspend,
      handle(trusted_instance_data->instance_object(), isolate),
      wasm::SerializedSignatureHelper::SerializeSignature(
          isolate, module->signature(sig_id)));

  WasmApiFunctionRef::SetIndexInTableAsCallOrigin(ref, entry_index);
  trusted_instance_data->dispatch_table(table_index)
      ->Set(entry_index, *ref, call_target, canonical_sig_index);
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
        FixedArray::cast(imported_mutable_globals_buffers()->get(global.index));
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
                           global.type);
  }
  Address ptr = reinterpret_cast<Address>(GetGlobalStorage(global));
  using wasm::Simd128;
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

wasm::WasmValue WasmStruct::GetFieldValue(uint32_t index) {
  wasm::ValueType field_type = type()->field(index);
  int field_offset = WasmStruct::kHeaderSize + type()->field_offset(index);
  Address field_address = GetFieldAddress(field_offset);
  using wasm::Simd128;
  switch (field_type.kind()) {
#define CASE_TYPE(valuetype, ctype) \
  case wasm::valuetype:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(field_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kRef:
    case wasm::kRefNull: {
      Handle<Object> ref(TaggedField<Object>::load(*this, field_offset),
                         GetIsolateFromWritableObject(*this));
      return wasm::WasmValue(ref, field_type);
    }
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

wasm::WasmValue WasmArray::GetElement(uint32_t index) {
  wasm::ValueType element_type = type()->element_type();
  int element_offset =
      WasmArray::kHeaderSize + index * element_type.value_kind_size();
  Address element_address = GetFieldAddress(element_offset);
  using wasm::Simd128;
  switch (element_type.kind()) {
#define CASE_TYPE(value_type, ctype) \
  case wasm::value_type:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(element_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kRef:
    case wasm::kRefNull: {
      Handle<Object> ref(TaggedField<Object>::load(*this, element_offset),
                         GetIsolateFromWritableObject(*this));
      return wasm::WasmValue(ref, element_type);
    }
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

void WasmArray::SetTaggedElement(uint32_t index, Handle<Object> value,
                                 WriteBarrierMode mode) {
  DCHECK(type()->element_type().is_reference());
  TaggedField<Object>::store(*this, element_offset(index), *value);
  CONDITIONAL_WRITE_BARRIER(*this, element_offset(index), *value, mode);
}

// static
Handle<WasmTagObject> WasmTagObject::New(Isolate* isolate,
                                         const wasm::FunctionSig* sig,
                                         uint32_t canonical_type_index,
                                         Handle<HeapObject> tag) {
  Handle<JSFunction> tag_cons(isolate->native_context()->wasm_tag_constructor(),
                              isolate);

  // Serialize the signature.
  DCHECK_EQ(0, sig->return_count());
  DCHECK_LE(sig->parameter_count(), std::numeric_limits<int>::max());
  int sig_size = static_cast<int>(sig->parameter_count());
  Handle<PodArray<wasm::ValueType>> serialized_sig =
      PodArray<wasm::ValueType>::New(isolate, sig_size, AllocationType::kOld);
  int index = 0;  // Index into the {PodArray} above.
  for (wasm::ValueType param : sig->parameters()) {
    serialized_sig->set(index++, param);
  }

  Handle<JSObject> tag_object =
      isolate->factory()->NewJSObject(tag_cons, AllocationType::kOld);
  Handle<WasmTagObject> tag_wrapper = Handle<WasmTagObject>::cast(tag_object);
  tag_wrapper->set_serialized_signature(*serialized_sig);
  tag_wrapper->set_canonical_type_index(canonical_type_index);
  tag_wrapper->set_tag(*tag);

  return tag_wrapper;
}

bool WasmTagObject::MatchesSignature(uint32_t expected_canonical_type_index) {
  return wasm::GetWasmEngine()->type_canonicalizer()->IsCanonicalSubtype(
      this->canonical_type_index(), expected_canonical_type_index);
}

const wasm::FunctionSig* WasmCapiFunction::GetSignature(Zone* zone) const {
  Tagged<WasmCapiFunctionData> function_data =
      shared()->wasm_capi_function_data();
  return wasm::SerializedSignatureHelper::DeserializeSignature(
      zone, function_data->serialized_signature());
}

void WasmDispatchTable::Set(int index, Tagged<Object> ref, Address call_target,
                            int sig_id) {
  if (ref == Smi::zero()) {
    DCHECK_EQ(kNullAddress, call_target);
    Clear(index);
    return;
  }

  DCHECK_LT(index, length());
  DCHECK(IsWasmApiFunctionRef(ref) || IsWasmTrustedInstanceData(ref));
  DCHECK_EQ(ref == Smi::zero(), call_target == kNullAddress);
  const int offset = OffsetOf(index);
  WriteProtectedPointerField(offset + kRefBias, TrustedObject::cast(ref));
  CONDITIONAL_WRITE_BARRIER(*this, offset + kRefBias, ref,
                            UPDATE_WRITE_BARRIER);
  WriteField<Address>(offset + kTargetBias, call_target);
  WriteField<int>(offset + kSigBias, sig_id);
}

void WasmDispatchTable::Clear(int index) {
  DCHECK_LT(index, length());
  const int offset = OffsetOf(index);
  ClearProtectedPointerField(offset + kRefBias);
  WriteField<Address>(offset + kTargetBias, kNullAddress);
  WriteField<int>(offset + kSigBias, -1);
}

void WasmDispatchTable::SetTarget(int index, Address call_target) {
  DCHECK_LT(index, length());
  const int offset = OffsetOf(index) + kTargetBias;
  WriteField<Address>(offset, call_target);
}

// static
Handle<WasmDispatchTable> WasmDispatchTable::New(Isolate* isolate, int length) {
  return isolate->factory()->NewWasmDispatchTable(length);
}

// static
Handle<WasmDispatchTable> WasmDispatchTable::Grow(
    Isolate* isolate, Handle<WasmDispatchTable> old_table, int new_length) {
  int old_length = old_table->length();
  // This method should only be called if we actually grow.
  DCHECK_LT(old_length, new_length);

  int old_capacity = old_table->capacity();
  if (new_length < old_table->capacity()) {
    RELEASE_WRITE_INT32_FIELD(*old_table, kLengthOffset, new_length);
    // All fields within the old capacity are already cleared (see below).
    return old_table;
  }

  // Grow table exponentially to guarantee amortized constant allocation and gc
  // time.
  int max_grow = WasmDispatchTable::kMaxLength - old_length;
  int min_grow = new_length - old_capacity;
  CHECK_LE(min_grow, max_grow);
  // Grow by old capacity, and at least by 8. Clamp to min_grow and max_grow.
  int exponential_grow = std::max(old_capacity, 8);
  int grow = std::clamp(exponential_grow, min_grow, max_grow);
  int new_capacity = old_capacity + grow;
  Handle<WasmDispatchTable> new_table =
      WasmDispatchTable::New(isolate, new_capacity);

  // Writing non-atomically is fine here because this is a freshly allocated
  // object.
  new_table->WriteField<int>(kLengthOffset, new_length);
  for (int i = 0; i < old_length; ++i) {
    new_table->Set(i, old_table->ref(i), old_table->target(i),
                   old_table->sig(i));
  }
  return new_table;
}

bool WasmCapiFunction::MatchesSignature(
    uint32_t other_canonical_sig_index) const {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  const wasm::FunctionSig* sig = GetSignature(&zone);
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  for (wasm::ValueType type : sig->all()) CHECK(!type.has_index());
#endif
  // TODO(14034): Check for subtyping instead if C API functions can define
  // signature supertype.
  return wasm::GetWasmEngine()->type_canonicalizer()->AddRecursiveGroup(sig) ==
         other_canonical_sig_index;
}

// static
Handle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, Handle<WasmExceptionTag> exception_tag, int size) {
  Handle<FixedArray> values = isolate->factory()->NewFixedArray(size);
  return New(isolate, exception_tag, values);
}

Handle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, Handle<WasmExceptionTag> exception_tag,
    Handle<FixedArray> values) {
  Handle<JSFunction> exception_cons(
      isolate->native_context()->wasm_exception_constructor(), isolate);
  Handle<JSObject> exception = isolate->factory()->NewJSObject(exception_cons);
  exception->InObjectPropertyAtPut(kTagIndex, *exception_tag);
  exception->InObjectPropertyAtPut(kValuesIndex, *values);
  return Handle<WasmExceptionPackage>::cast(exception);
}

// static
Handle<Object> WasmExceptionPackage::GetExceptionTag(
    Isolate* isolate, Handle<WasmExceptionPackage> exception_package) {
  Handle<Object> tag;
  if (JSReceiver::GetProperty(isolate, exception_package,
                              isolate->factory()->wasm_exception_tag_symbol())
          .ToHandle(&tag)) {
    return tag;
  }
  return ReadOnlyRoots(isolate).undefined_value_handle();
}

// static
Handle<Object> WasmExceptionPackage::GetExceptionValues(
    Isolate* isolate, Handle<WasmExceptionPackage> exception_package) {
  Handle<Object> values;
  if (JSReceiver::GetProperty(
          isolate, exception_package,
          isolate->factory()->wasm_exception_values_symbol())
          .ToHandle(&values)) {
    DCHECK_IMPLIES(!IsUndefined(*values), IsFixedArray(*values));
    return values;
  }
  return ReadOnlyRoots(isolate).undefined_value_handle();
}

void EncodeI32ExceptionValue(Handle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t value) {
  encoded_values->set((*encoded_index)++, Smi::FromInt(value >> 16));
  encoded_values->set((*encoded_index)++, Smi::FromInt(value & 0xffff));
}

void EncodeI64ExceptionValue(Handle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t value) {
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value >> 32));
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value));
}

void DecodeI32ExceptionValue(Handle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t* value) {
  uint32_t msb = Smi::cast(encoded_values->get((*encoded_index)++)).value();
  uint32_t lsb = Smi::cast(encoded_values->get((*encoded_index)++)).value();
  *value = (msb << 16) | (lsb & 0xffff);
}

void DecodeI64ExceptionValue(Handle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t* value) {
  uint32_t lsb = 0, msb = 0;
  DecodeI32ExceptionValue(encoded_values, encoded_index, &msb);
  DecodeI32ExceptionValue(encoded_values, encoded_index, &lsb);
  *value = (static_cast<uint64_t>(msb) << 32) | static_cast<uint64_t>(lsb);
}

// static
Handle<WasmContinuationObject> WasmContinuationObject::New(
    Isolate* isolate, std::unique_ptr<wasm::StackMemory> stack,
    wasm::JumpBuffer::StackState state, Handle<HeapObject> parent,
    AllocationType allocation_type) {
  stack->jmpbuf()->stack_limit = stack->jslimit();
  stack->jmpbuf()->sp = stack->base();
  stack->jmpbuf()->fp = kNullAddress;
  stack->jmpbuf()->state = state;
  wasm::JumpBuffer* jmpbuf = stack->jmpbuf();
  size_t external_size = stack->owned_size();
  Handle<Foreign> managed_stack = Managed<wasm::StackMemory>::FromUniquePtr(
      isolate, external_size, std::move(stack), allocation_type);
  Handle<WasmContinuationObject> result =
      isolate->factory()->NewWasmContinuationObject(
          reinterpret_cast<Address>(jmpbuf), managed_stack, parent,
          allocation_type);
  return result;
}

bool UseGenericWasmToJSWrapper(wasm::ImportCallKind kind,
                               const wasm::FunctionSig* sig,
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
  if (suspend == wasm::kSuspend) return false;

  return v8_flags.wasm_to_js_generic_wrapper;
#endif
}

// static
Handle<WasmContinuationObject> WasmContinuationObject::New(
    Isolate* isolate, std::unique_ptr<wasm::StackMemory> stack,
    wasm::JumpBuffer::StackState state, AllocationType allocation_type) {
  auto parent = ReadOnlyRoots(isolate).undefined_value();
  return New(isolate, std::move(stack), state, handle(parent, isolate),
             allocation_type);
}

// static
Handle<WasmContinuationObject> WasmContinuationObject::New(
    Isolate* isolate, wasm::JumpBuffer::StackState state,
    Handle<WasmContinuationObject> parent) {
  auto stack =
      std::unique_ptr<wasm::StackMemory>(wasm::StackMemory::New(isolate));
  return New(isolate, std::move(stack), state, parent);
}

// static
Handle<WasmSuspenderObject> WasmSuspenderObject::New(Isolate* isolate) {
  Handle<JSFunction> suspender_cons(
      isolate->native_context()->wasm_suspender_constructor(), isolate);
  Handle<JSPromise> promise = isolate->factory()->NewJSPromise();
  auto suspender = Handle<WasmSuspenderObject>::cast(
      isolate->factory()->NewJSObject(suspender_cons));
  // Fields are pre-initialized to undefined, but undefined is not a valid value
  // for has_js_frames (Smi), state (Smi) and promise (JSPromise). Ensure
  // that they are initialized before the allocation below.
  suspender->set_has_js_frames(0);
  suspender->set_promise(*promise);
  suspender->set_state(kInactive);
  // Instantiate the callable object which resumes this Suspender. This will be
  // used implicitly as the onFulfilled callback of the returned JS promise.
  Handle<WasmResumeData> resume_data = isolate->factory()->NewWasmResumeData(
      suspender, wasm::OnResume::kContinue);
  Handle<SharedFunctionInfo> resume_sfi =
      isolate->factory()->NewSharedFunctionInfoForWasmResume(resume_data);
  Handle<Context> context(isolate->native_context());
  Handle<JSObject> resume =
      Factory::JSFunctionBuilder{isolate, resume_sfi, context}.Build();

  Handle<WasmResumeData> reject_data =
      isolate->factory()->NewWasmResumeData(suspender, wasm::OnResume::kThrow);
  Handle<SharedFunctionInfo> reject_sfi =
      isolate->factory()->NewSharedFunctionInfoForWasmResume(reject_data);
  Handle<JSObject> reject =
      Factory::JSFunctionBuilder{isolate, reject_sfi, context}.Build();
  suspender->set_resume(*resume);
  suspender->set_reject(*reject);
  return suspender;
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
      case wasm::kBottom:
      case wasm::kI8:
      case wasm::kI16:
        UNREACHABLE();
    }
  }
  return encoded_size;
}

bool WasmExportedFunction::IsWasmExportedFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = JSFunction::cast(object);
  Tagged<Code> code = js_function->code(GetIsolateForSandbox(js_function));
  if (CodeKind::JS_TO_WASM_FUNCTION != code->kind() &&
      code->builtin_id() != Builtin::kJSToWasmWrapper &&
      code->builtin_id() != Builtin::kWasmReturnPromiseOnSuspend) {
    return false;
  }
  DCHECK(js_function->shared()->HasWasmExportedFunctionData());
  return true;
}

bool WasmCapiFunction::IsWasmCapiFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = JSFunction::cast(object);
  // TODO(jkummerow): Enable this when there is a JavaScript wrapper
  // able to call this function.
  // if (js_function->code()->kind() != CodeKind::WASM_TO_CAPI_FUNCTION) {
  //   return false;
  // }
  // DCHECK(js_function->shared()->HasWasmCapiFunctionData());
  // return true;
  return js_function->shared()->HasWasmCapiFunctionData();
}

Handle<WasmCapiFunction> WasmCapiFunction::New(
    Isolate* isolate, Address call_target, Handle<Foreign> embedder_data,
    Handle<PodArray<wasm::ValueType>> serialized_signature) {
  // TODO(jkummerow): Install a JavaScript wrapper. For now, calling
  // these functions directly is unsupported; they can only be called
  // from Wasm code.

  // To support simulator builds, we potentially have to redirect the
  // call target (which is an address pointing into the C++ binary).
  call_target = ExternalReference::Create(call_target).address();

  Handle<Map> rtt = isolate->factory()->wasm_func_ref_map();
  Handle<WasmCapiFunctionData> fun_data =
      isolate->factory()->NewWasmCapiFunctionData(
          call_target, embedder_data, BUILTIN_CODE(isolate, Illegal), rtt,
          serialized_signature);
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForWasmCapiFunction(fun_data);
  Handle<JSFunction> result =
      Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
          .Build();
  fun_data->internal()->set_external(*result);
  return Handle<WasmCapiFunction>::cast(result);
}

Tagged<WasmInstanceObject> WasmExportedFunction::instance() {
  return shared()->wasm_exported_function_data()->instance();
}

int WasmExportedFunction::function_index() {
  return shared()->wasm_exported_function_data()->function_index();
}

Handle<WasmExportedFunction> WasmExportedFunction::New(
    Isolate* isolate, Handle<WasmTrustedInstanceData> instance_data,
    Handle<WasmInternalFunction> internal, int func_index, int arity,
    Handle<Code> export_wrapper) {
  DCHECK(
      CodeKind::JS_TO_WASM_FUNCTION == export_wrapper->kind() ||
      (export_wrapper->is_builtin() &&
       (export_wrapper->builtin_id() == Builtin::kJSToWasmWrapper ||
        export_wrapper->builtin_id() == Builtin::kWasmReturnPromiseOnSuspend)));
  Factory* factory = isolate->factory();
  Handle<WasmInstanceObject> instance_object{instance_data->instance_object(),
                                             isolate};
  const wasm::WasmModule* module = instance_data->module();
  const wasm::FunctionSig* sig = module->functions[func_index].sig;
  Handle<Map> rtt;
  wasm::Promise promise =
      export_wrapper->builtin_id() == Builtin::kWasmReturnPromiseOnSuspend
          ? wasm::kPromise
          : wasm::kNoPromise;
  uint32_t sig_index = module->functions[func_index].sig_index;
  uint32_t canonical_type_index =
      module->isorecursive_canonical_type_ids[sig_index];
  Handle<WasmExportedFunctionData> function_data =
      factory->NewWasmExportedFunctionData(
          export_wrapper, instance_object, internal, func_index, sig,
          canonical_type_index, v8_flags.wasm_wrapper_tiering_budget, promise);

  MaybeHandle<String> maybe_name;
  bool is_asm_js_module = is_asmjs_module(module);
  if (is_asm_js_module) {
    // We can use the function name only for asm.js. For WebAssembly, the
    // function name is specified as the function_index.toString().
    maybe_name = WasmModuleObject::GetFunctionNameOrNull(
        isolate, handle(instance_object->module_object(), isolate), func_index);
  }
  Handle<String> name;
  if (!maybe_name.ToHandle(&name)) {
    base::EmbeddedVector<char, 16> buffer;
    int length = SNPrintF(buffer, "%d", func_index);
    name = factory
               ->NewStringFromOneByte(
                   base::Vector<uint8_t>::cast(buffer.SubVector(0, length)))
               .ToHandleChecked();
  }
  Handle<Map> function_map;
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

  Handle<NativeContext> context(isolate->native_context());
  Handle<SharedFunctionInfo> shared =
      factory->NewSharedFunctionInfoForWasmExportedFunction(name,
                                                            function_data);
  Handle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(function_map)
          .Build();

  // According to the spec, exported functions should not have a [[Construct]]
  // method. This does not apply to functions exported from asm.js however.
  DCHECK_EQ(is_asm_js_module, IsConstructor(*js_function));
  shared->set_length(arity);
  shared->set_internal_formal_parameter_count(JSParameterCount(arity));
  shared->set_script(instance_object->module_object()->script(), kReleaseStore);
  function_data->internal()->set_external(*js_function);
  return Handle<WasmExportedFunction>::cast(js_function);
}

Address WasmExportedFunction::GetWasmCallTarget() {
  return instance()
      ->trusted_data(GetIsolate())
      ->GetCallTarget(function_index());
}

const wasm::FunctionSig* WasmExportedFunction::sig() {
  return instance()
      ->trusted_data(GetIsolate())
      ->module()
      ->functions[function_index()]
      .sig;
}

bool WasmExportedFunction::MatchesSignature(
    uint32_t other_canonical_type_index) {
  return wasm::GetWasmEngine()->type_canonicalizer()->IsCanonicalSubtype(
      this->shared()->wasm_exported_function_data()->canonical_type_index(),
      other_canonical_type_index);
}

// static
std::unique_ptr<char[]> WasmExportedFunction::GetDebugName(
    const wasm::FunctionSig* sig) {
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
  Tagged<JSFunction> js_function = JSFunction::cast(object);
  return js_function->shared()->HasWasmJSFunctionData();
}

Handle<Map> CreateFuncRefMap(Isolate* isolate, Handle<Map> opt_rtt_parent) {
  const int inobject_properties = 0;
  const InstanceType instance_type = WASM_FUNC_REF_TYPE;
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  constexpr uint32_t kNoIndex = ~0u;
  Handle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      kNullAddress, opt_rtt_parent, Handle<WasmInstanceObject>(), kNoIndex);
  constexpr int kInstanceSize = WasmFuncRef::kSize;
  DCHECK_EQ(
      kInstanceSize,
      Map::cast(isolate->root(RootIndex::kWasmFuncRefMap))->instance_size());
  Handle<Map> map = isolate->factory()->NewContextlessMap(
      instance_type, kInstanceSize, elements_kind, inobject_properties);
  map->set_wasm_type_info(*type_info);
  return map;
}

Handle<WasmJSFunction> WasmJSFunction::New(Isolate* isolate,
                                           const wasm::FunctionSig* sig,
                                           Handle<JSReceiver> callable,
                                           wasm::Suspend suspend) {
  DCHECK_LE(sig->all().size(), kMaxInt);
  int parameter_count = static_cast<int>(sig->parameter_count());
  Handle<PodArray<wasm::ValueType>> serialized_sig =
      wasm::SerializedSignatureHelper::SerializeSignature(isolate, sig);
  Handle<Code> wrapper_code;
    if (wasm::IsJSCompatibleSignature(sig)) {
      wrapper_code = isolate->builtins()->code_handle(Builtin::kJSToJSWrapper);
    } else {
      wrapper_code =
          isolate->builtins()->code_handle(Builtin::kJSToJSWrapperInvalidSig);
    }

  // WasmJSFunctions use on-heap Code objects as call targets, so we can't
  // cache the target address, unless the WasmJSFunction wraps a
  // WasmExportedFunction.
  Address call_target = kNullAddress;
  if (WasmExportedFunction::IsWasmExportedFunction(*callable)) {
    call_target = WasmExportedFunction::cast(*callable)->GetWasmCallTarget();
  }

  Factory* factory = isolate->factory();

  Handle<Map> rtt;
  Handle<NativeContext> context(isolate->native_context());

  uint32_t canonical_type_index =
      wasm::GetWasmEngine()->type_canonicalizer()->AddRecursiveGroup(sig);

  isolate->heap()->EnsureWasmCanonicalRttsSize(canonical_type_index + 1);

  Handle<WeakArrayList> canonical_rtts =
      handle(isolate->heap()->wasm_canonical_rtts(), isolate);

  Tagged<MaybeObject> maybe_canonical_map =
      canonical_rtts->Get(canonical_type_index);

  if (maybe_canonical_map.IsStrongOrWeak() &&
      IsMap(maybe_canonical_map.GetHeapObject())) {
    rtt = handle(Map::cast(maybe_canonical_map.GetHeapObject()), isolate);
  } else {
    rtt = CreateFuncRefMap(isolate, Handle<Map>());
    canonical_rtts->Set(canonical_type_index, MakeWeak(*rtt));
  }

  Handle<WasmJSFunctionData> function_data = factory->NewWasmJSFunctionData(
      call_target, callable, serialized_sig, wrapper_code, rtt, suspend,
      wasm::kNoPromise);

  Handle<Code> wasm_to_js_wrapper_code;
  if (!wasm::IsJSCompatibleSignature(sig)) {
    wasm_to_js_wrapper_code =
        isolate->builtins()->code_handle(Builtin::kWasmToJsWrapperInvalidSig);
  } else if (UseGenericWasmToJSWrapper(wasm::kDefaultImportCallKind, sig,
                                       suspend)) {
    wasm_to_js_wrapper_code =
        isolate->builtins()->code_handle(Builtin::kWasmToJsWrapperAsm);
  } else {
    int expected_arity = parameter_count - suspend;
    wasm::ImportCallKind kind = wasm::kDefaultImportCallKind;
    if (IsJSFunction(*callable)) {
      Tagged<SharedFunctionInfo> shared =
          Handle<JSFunction>::cast(callable)->shared();
      expected_arity =
          shared->internal_formal_parameter_count_without_receiver();
      if (expected_arity != parameter_count - suspend) {
        kind = wasm::ImportCallKind::kJSFunctionArityMismatch;
      }
    }
    // TODO(wasm): Think about caching and sharing the wasm-to-JS wrappers per
    // signature instead of compiling a new one for every instantiation.
    if (UseGenericWasmToJSWrapper(kind, sig, suspend)) {
      wasm_to_js_wrapper_code = handle(
          isolate->builtins()->code(Builtin::kWasmToJsWrapperAsm), isolate);
    } else {
      wasm_to_js_wrapper_code = compiler::CompileWasmToJSWrapper(
                                    isolate, sig, kind, expected_arity, suspend)
                                    .ToHandleChecked();
    }
  }
  function_data->internal()->set_code(*wasm_to_js_wrapper_code);

  Handle<String> name = factory->Function_string();
  if (IsJSFunction(*callable)) {
    name = JSFunction::GetDebugName(Handle<JSFunction>::cast(callable));
    name = String::Flatten(isolate, name);
  }
  Handle<SharedFunctionInfo> shared =
      factory->NewSharedFunctionInfoForWasmJSFunction(name, function_data);
  Handle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(isolate->wasm_exported_function_map())
          .Build();
  js_function->shared()->set_internal_formal_parameter_count(
      JSParameterCount(parameter_count));
  function_data->internal()->set_external(*js_function);
  return Handle<WasmJSFunction>::cast(js_function);
}

Tagged<JSReceiver> WasmJSFunction::GetCallable() const {
  return JSReceiver::cast(
      WasmApiFunctionRef::cast(
          shared()->wasm_js_function_data()->internal()->ref(GetIsolate()))
          ->callable());
}

wasm::Suspend WasmJSFunction::GetSuspend() const {
  return static_cast<wasm::Suspend>(
      WasmApiFunctionRef::cast(
          shared()->wasm_js_function_data()->internal()->ref(GetIsolate()))
          ->suspend());
}

const wasm::FunctionSig* WasmJSFunction::GetSignature(Zone* zone) const {
  Tagged<WasmJSFunctionData> function_data = shared()->wasm_js_function_data();
  return wasm::SerializedSignatureHelper::DeserializeSignature(
      zone, function_data->serialized_signature());
}

bool WasmJSFunction::MatchesSignature(
    uint32_t other_canonical_sig_index) const {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  const wasm::FunctionSig* sig = GetSignature(&zone);
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  for (wasm::ValueType type : sig->all()) CHECK(!type.has_index());
#endif
  // TODO(14034): Check for subtyping instead if WebAssembly.Function can define
  // signature supertype.
  return wasm::GetWasmEngine()->type_canonicalizer()->AddRecursiveGroup(sig) ==
         other_canonical_sig_index;
}

Tagged<PodArray<wasm::ValueType>> WasmCapiFunction::GetSerializedSignature()
    const {
  return shared()->wasm_capi_function_data()->serialized_signature();
}

bool WasmExternalFunction::IsWasmExternalFunction(Tagged<Object> object) {
  return WasmExportedFunction::IsWasmExportedFunction(object) ||
         WasmJSFunction::IsWasmJSFunction(object) ||
         WasmCapiFunction::IsWasmCapiFunction(object);
}


Handle<WasmExceptionTag> WasmExceptionTag::New(Isolate* isolate, int index) {
  Handle<WasmExceptionTag> result =
      Handle<WasmExceptionTag>::cast(isolate->factory()->NewStruct(
          WASM_EXCEPTION_TAG_TYPE, AllocationType::kOld));
  result->set_index(index);
  return result;
}

Handle<AsmWasmData> AsmWasmData::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    Handle<HeapNumber> uses_bitset) {
  const WasmModule* module = native_module->module();
  const bool kUsesLiftoff = false;
  size_t memory_estimate =
      wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
          module, kUsesLiftoff, wasm::kNoDynamicTiering) +
      wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
  Handle<Managed<wasm::NativeModule>> managed_native_module =
      Managed<wasm::NativeModule>::FromSharedPtr(isolate, memory_estimate,
                                                 std::move(native_module));
  Handle<AsmWasmData> result = Handle<AsmWasmData>::cast(
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
  double double_value = Handle<HeapNumber>::cast(number)->value();
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

  int32_t value = Smi::cast(*smi).value();

  if (value <= kInt31MaxValue && value >= kInt31MinValue) {
    return smi;
  } else {
    return isolate->factory()->NewHeapNumber(value);
  }
}
}  // namespace

namespace wasm {
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, Handle<Object> value,
                                   ValueType expected_canonical,
                                   const char** error_message) {
  DCHECK(expected_canonical.is_object_reference());
  if (expected_canonical.kind() == kRefNull && IsNull(*value, isolate)) {
    switch (expected_canonical.heap_representation()) {
      case HeapType::kStringViewWtf8:
        *error_message = "stringview_wtf8 has no JS representation";
        return {};
      case HeapType::kStringViewWtf16:
        *error_message = "stringview_wtf16 has no JS representation";
        return {};
      case HeapType::kStringViewIter:
        *error_message = "stringview_iter has no JS representation";
        return {};
      default: {
        HeapType::Representation repr =
            expected_canonical.heap_representation_non_shared();
        bool is_extern_subtype =
            repr == HeapType::kExtern || repr == HeapType::kNoExtern ||
            repr == HeapType::kExn || repr == HeapType::kNoExn;
        return is_extern_subtype ? value : isolate->factory()->wasm_null();
      }
    }
  }

  switch (expected_canonical.heap_representation_non_shared()) {
    case HeapType::kFunc: {
      if (!(WasmExternalFunction::IsWasmExternalFunction(*value) ||
            WasmCapiFunction::IsWasmCapiFunction(*value))) {
        *error_message =
            "function-typed object must be null (if nullable) or a Wasm "
            "function object";
        return {};
      }
      return handle(JSFunction::cast(*value)
                        ->shared()
                        ->wasm_function_data()
                        ->internal()
                        ->func_ref(),
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
      if (!IsNull(*value, isolate)) return value;
      *error_message = "null is not allowed for (ref exn)";
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
      auto type_canonicalizer = GetWasmEngine()->type_canonicalizer();

      if (WasmExportedFunction::IsWasmExportedFunction(*value)) {
        Tagged<WasmExportedFunction> function =
            WasmExportedFunction::cast(*value);
        uint32_t real_type_index = function->shared()
                                       ->wasm_exported_function_data()
                                       ->canonical_type_index();
        if (!type_canonicalizer->IsCanonicalSubtype(
                real_type_index, expected_canonical.ref_index())) {
          *error_message =
              "assigned exported function has to be a subtype of the "
              "expected type";
          return {};
        }
        return handle(
            WasmExternalFunction::cast(*value)->internal()->func_ref(),
            isolate);
      } else if (WasmJSFunction::IsWasmJSFunction(*value)) {
        if (!WasmJSFunction::cast(*value)->MatchesSignature(
                expected_canonical.ref_index())) {
          *error_message =
              "assigned WebAssembly.Function has to be a subtype of the "
              "expected type";
          return {};
        }
        return handle(
            WasmExternalFunction::cast(*value)->internal()->func_ref(),
            isolate);
      } else if (WasmCapiFunction::IsWasmCapiFunction(*value)) {
        if (!WasmCapiFunction::cast(*value)->MatchesSignature(
                expected_canonical.ref_index())) {
          *error_message =
              "assigned C API function has to be a subtype of the expected "
              "type";
          return {};
        }
        return handle(
            WasmExternalFunction::cast(*value)->internal()->func_ref(),
            isolate);
      } else if (IsWasmStruct(*value) || IsWasmArray(*value)) {
        auto wasm_obj = Handle<WasmObject>::cast(value);
        Tagged<WasmTypeInfo> type_info = wasm_obj->map()->wasm_type_info();
        uint32_t real_idx = type_info->type_index();
        const WasmModule* real_module =
            WasmInstanceObject::cast(type_info->instance())->module();
        uint32_t real_canonical_index =
            real_module->isorecursive_canonical_type_ids[real_idx];
        if (!type_canonicalizer->IsCanonicalSubtype(
                real_canonical_index, expected_canonical.ref_index())) {
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
  ValueType expected_canonical = expected;
  if (expected_canonical.has_index()) {
    uint32_t canonical_index =
        module->isorecursive_canonical_type_ids[expected_canonical.ref_index()];
    expected_canonical = ValueType::RefMaybeNull(
        canonical_index, expected_canonical.nullability());
  }
  return JSToWasmObject(isolate, value, expected_canonical, error_message);
}

Handle<Object> WasmToJSObject(Isolate* isolate, Handle<Object> value) {
  if (IsWasmNull(*value)) {
    return isolate->factory()->null_value();
  } else if (IsWasmFuncRef(*value)) {
    return i::WasmInternalFunction::GetOrCreateExternal(
        i::handle(i::WasmFuncRef::cast(*value)->internal(), isolate));
  } else {
    return value;
  }
}

}  // namespace wasm

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"
#undef TRACE_IFT
