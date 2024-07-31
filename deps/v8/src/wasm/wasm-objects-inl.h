// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_OBJECTS_INL_H_
#define V8_WASM_WASM_OBJECTS_INL_H_

#include <type_traits>

#include "src/base/memory.h"
#include "src/common/ptr-compr.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/managed.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/script-inl.h"
#include "src/roots/roots.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"

// Has to be the last include (doesn't have include guards)
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/wasm/wasm-objects-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(AsmWasmData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmApiFunctionRef)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmArray)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmCapiFunctionData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmContinuationObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmExceptionTag)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmExportedFunctionData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmFunctionData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmFuncRef)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmGlobalObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmInstanceObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmInternalFunction)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmJSFunctionData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmMemoryObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmModuleObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmNull)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmResumeData)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmStruct)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmSuspenderObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmTableObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmTagObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(WasmTypeInfo)

#define OPTIONAL_ACCESSORS(holder, name, type, offset)       \
  DEF_GETTER(holder, has_##name, bool) {                     \
    Tagged<Object> value =                                   \
        TaggedField<Object, offset>::load(cage_base, *this); \
    return !IsUndefined(value, GetReadOnlyRoots(cage_base)); \
  }                                                          \
  ACCESSORS_CHECKED2(holder, name, type, offset,             \
                     !IsUndefined(value, GetReadOnlyRoots(cage_base)), true)

#define PRIMITIVE_ACCESSORS(holder, name, type, offset)               \
  type holder::name() const {                                         \
    return ReadMaybeUnalignedValue<type>(FIELD_ADDR(*this, offset));  \
  }                                                                   \
  void holder::set_##name(type value) {                               \
    WriteMaybeUnalignedValue<type>(FIELD_ADDR(*this, offset), value); \
  }

#define SANDBOXED_POINTER_ACCESSORS(holder, name, type, offset)      \
  type holder::name() const {                                        \
    PtrComprCageBase sandbox_base = GetPtrComprCageBase();           \
    Address value = ReadSandboxedPointerField(offset, sandbox_base); \
    return reinterpret_cast<type>(value);                            \
  }                                                                  \
  void holder::set_##name(type value) {                              \
    PtrComprCageBase sandbox_base = GetPtrComprCageBase();           \
    Address addr = reinterpret_cast<Address>(value);                 \
    WriteSandboxedPointerField(offset, sandbox_base, addr);          \
  }

// WasmModuleObject
wasm::NativeModule* WasmModuleObject::native_module() const {
  return managed_native_module()->raw();
}
const std::shared_ptr<wasm::NativeModule>&
WasmModuleObject::shared_native_module() const {
  return managed_native_module()->get();
}
const wasm::WasmModule* WasmModuleObject::module() const {
  // TODO(clemensb): Remove this helper (inline in callers).
  return native_module()->module();
}
bool WasmModuleObject::is_asm_js() {
  bool asm_js = is_asmjs_module(module());
  DCHECK_EQ(asm_js, script()->IsUserJavaScript());
  return asm_js;
}

// WasmMemoryObject
ACCESSORS(WasmMemoryObject, instances, Tagged<WeakArrayList>, kInstancesOffset)

// WasmGlobalObject
ACCESSORS(WasmGlobalObject, untagged_buffer, Tagged<JSArrayBuffer>,
          kUntaggedBufferOffset)
ACCESSORS(WasmGlobalObject, tagged_buffer, Tagged<FixedArray>,
          kTaggedBufferOffset)

wasm::ValueType WasmGlobalObject::type() const {
  return wasm::ValueType::FromRawBitField(static_cast<uint32_t>(raw_type()));
}
void WasmGlobalObject::set_type(wasm::ValueType value) {
  set_raw_type(static_cast<int>(value.raw_bit_field()));
}

int WasmGlobalObject::type_size() const { return type().value_kind_size(); }

Address WasmGlobalObject::address() const {
  DCHECK_NE(type(), wasm::kWasmAnyRef);
  DCHECK_LE(offset() + type_size(), untagged_buffer()->byte_length());
  return reinterpret_cast<Address>(untagged_buffer()->backing_store()) +
         offset();
}

int32_t WasmGlobalObject::GetI32() {
  return base::ReadUnalignedValue<int32_t>(address());
}

int64_t WasmGlobalObject::GetI64() {
  return base::ReadUnalignedValue<int64_t>(address());
}

float WasmGlobalObject::GetF32() {
  return base::ReadUnalignedValue<float>(address());
}

double WasmGlobalObject::GetF64() {
  return base::ReadUnalignedValue<double>(address());
}

uint8_t* WasmGlobalObject::GetS128RawBytes() {
  return reinterpret_cast<uint8_t*>(address());
}

Handle<Object> WasmGlobalObject::GetRef() {
  // We use this getter for externref, funcref, and stringref.
  DCHECK(type().is_reference());
  return handle(tagged_buffer()->get(offset()), GetIsolate());
}

void WasmGlobalObject::SetI32(int32_t value) {
  base::WriteUnalignedValue(address(), value);
}

void WasmGlobalObject::SetI64(int64_t value) {
  base::WriteUnalignedValue(address(), value);
}

void WasmGlobalObject::SetF32(float value) {
  base::WriteUnalignedValue(address(), value);
}

void WasmGlobalObject::SetF64(double value) {
  base::WriteUnalignedValue(address(), value);
}

void WasmGlobalObject::SetRef(Handle<Object> value) {
  DCHECK(type().is_object_reference());
  tagged_buffer()->set(offset(), *value);
}

// WasmTrustedInstanceData
CAST_ACCESSOR(WasmTrustedInstanceData)
OBJECT_CONSTRUCTORS_IMPL(WasmTrustedInstanceData, ExposedTrustedObject)

PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, memory0_start, uint8_t*,
                    kMemory0StartOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, memory0_size, size_t,
                    kMemory0SizeOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, new_allocation_limit_address,
                    Address*, kNewAllocationLimitAddressOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, new_allocation_top_address,
                    Address*, kNewAllocationTopAddressOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, old_allocation_limit_address,
                    Address*, kOldAllocationLimitAddressOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, old_allocation_top_address,
                    Address*, kOldAllocationTopAddressOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, isorecursive_canonical_types,
                    const uint32_t*, kIsorecursiveCanonicalTypesOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, globals_start, uint8_t*,
                    kGlobalsStartOffset)
ACCESSORS(WasmTrustedInstanceData, imported_mutable_globals,
          Tagged<FixedAddressArray>, kImportedMutableGlobalsOffset)
ACCESSORS(WasmTrustedInstanceData, imported_function_targets,
          Tagged<FixedAddressArray>, kImportedFunctionTargetsOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, jump_table_start, Address,
                    kJumpTableStartOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, hook_on_function_call_address,
                    Address, kHookOnFunctionCallAddressOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, tiering_budget_array, uint32_t*,
                    kTieringBudgetArrayOffset)
ACCESSORS(WasmTrustedInstanceData, memory_bases_and_sizes,
          Tagged<FixedAddressArray>, kMemoryBasesAndSizesOffset)
ACCESSORS(WasmTrustedInstanceData, data_segment_starts,
          Tagged<FixedAddressArray>, kDataSegmentStartsOffset)
ACCESSORS(WasmTrustedInstanceData, data_segment_sizes, Tagged<FixedUInt32Array>,
          kDataSegmentSizesOffset)
ACCESSORS(WasmTrustedInstanceData, element_segments, Tagged<FixedArray>,
          kElementSegmentsOffset)
PRIMITIVE_ACCESSORS(WasmTrustedInstanceData, break_on_entry, uint8_t,
                    kBreakOnEntryOffset)

ACCESSORS(WasmTrustedInstanceData, instance_object, Tagged<WasmInstanceObject>,
          kInstanceObjectOffset)
ACCESSORS(WasmTrustedInstanceData, native_context, Tagged<Context>,
          kNativeContextOffset)
ACCESSORS(WasmTrustedInstanceData, memory_objects, Tagged<FixedArray>,
          kMemoryObjectsOffset)
OPTIONAL_ACCESSORS(WasmTrustedInstanceData, untagged_globals_buffer,
                   Tagged<JSArrayBuffer>, kUntaggedGlobalsBufferOffset)
OPTIONAL_ACCESSORS(WasmTrustedInstanceData, tagged_globals_buffer,
                   Tagged<FixedArray>, kTaggedGlobalsBufferOffset)
OPTIONAL_ACCESSORS(WasmTrustedInstanceData, imported_mutable_globals_buffers,
                   Tagged<FixedArray>, kImportedMutableGlobalsBuffersOffset)
OPTIONAL_ACCESSORS(WasmTrustedInstanceData, tables, Tagged<FixedArray>,
                   kTablesOffset)
PROTECTED_POINTER_ACCESSORS(WasmTrustedInstanceData, imported_function_refs,
                            ProtectedFixedArray, kImportedFunctionRefsOffset)
PROTECTED_POINTER_ACCESSORS(WasmTrustedInstanceData, dispatch_table0,
                            WasmDispatchTable, kDispatchTable0Offset)
PROTECTED_POINTER_ACCESSORS(WasmTrustedInstanceData, dispatch_tables,
                            ProtectedFixedArray, kDispatchTablesOffset)
OPTIONAL_ACCESSORS(WasmTrustedInstanceData, tags_table, Tagged<FixedArray>,
                   kTagsTableOffset)
ACCESSORS(WasmTrustedInstanceData, func_refs, Tagged<FixedArray>,
          kFuncRefsOffset)
ACCESSORS(WasmTrustedInstanceData, managed_object_maps, Tagged<FixedArray>,
          kManagedObjectMapsOffset)
ACCESSORS(WasmTrustedInstanceData, feedback_vectors, Tagged<FixedArray>,
          kFeedbackVectorsOffset)
ACCESSORS(WasmTrustedInstanceData, well_known_imports, Tagged<FixedArray>,
          kWellKnownImportsOffset)

void WasmTrustedInstanceData::clear_padding() {
  constexpr int kPaddingBytes = FIELD_SIZE(kOptionalPaddingOffset);
  static_assert(kPaddingBytes == 0 || kPaddingBytes == kIntSize);
  if constexpr (kPaddingBytes != 0) {
    WriteField<int>(kOptionalPaddingOffset, 0);
  }
}

Tagged<WasmMemoryObject> WasmTrustedInstanceData::memory_object(
    int memory_index) const {
  return WasmMemoryObject::cast(memory_objects()->get(memory_index));
}

uint8_t* WasmTrustedInstanceData::memory_base(int memory_index) const {
  DCHECK_EQ(memory0_start(),
            reinterpret_cast<uint8_t*>(
                memory_bases_and_sizes()->get_sandboxed_pointer(0)));
  return reinterpret_cast<uint8_t*>(
      memory_bases_and_sizes()->get_sandboxed_pointer(2 * memory_index));
}

size_t WasmTrustedInstanceData::memory_size(int memory_index) const {
  DCHECK_EQ(memory0_size(), memory_bases_and_sizes()->get(1));
  return memory_bases_and_sizes()->get(2 * memory_index + 1);
}

Tagged<WasmDispatchTable> WasmTrustedInstanceData::dispatch_table(
    uint32_t table_index) {
  Tagged<Object> table = dispatch_tables()->get(table_index);
  DCHECK(IsWasmDispatchTable(table));
  return WasmDispatchTable::cast(table);
}

bool WasmTrustedInstanceData::has_dispatch_table(uint32_t table_index) {
  Tagged<Object> maybe_table = dispatch_tables()->get(table_index);
  DCHECK(maybe_table == Smi::zero() || IsWasmDispatchTable(maybe_table));
  return maybe_table != Smi::zero();
}

Tagged<WasmModuleObject> WasmTrustedInstanceData::module_object() const {
  return instance_object()->module_object();
}

const wasm::WasmModule* WasmTrustedInstanceData::module() const {
  return module_object()->module();
}

// WasmInstanceObject
TRUSTED_POINTER_ACCESSORS(WasmInstanceObject, trusted_data,
                          WasmTrustedInstanceData, kTrustedDataOffset,
                          kWasmTrustedInstanceDataIndirectPointerTag)

const wasm::WasmModule* WasmInstanceObject::module() const {
  return module_object()->module();
}

ImportedFunctionEntry::ImportedFunctionEntry(
    Handle<WasmInstanceObject> instance_object, int index)
    : instance_object_(instance_object), index_(index) {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, instance_object->module()->num_imported_functions);
}

ImportedFunctionEntry::ImportedFunctionEntry(
    Isolate* isolate, Handle<WasmTrustedInstanceData> instance_data, int index)
    : ImportedFunctionEntry(handle(instance_data->instance_object(), isolate),
                            index) {}

// WasmDispatchTable
CAST_ACCESSOR(WasmDispatchTable)
OBJECT_CONSTRUCTORS_IMPL(WasmDispatchTable, TrustedObject)

void WasmDispatchTable::clear_entry_padding(int index) {
  static_assert(kEntryPaddingBytes == 0 || kEntryPaddingBytes == kIntSize);
  if constexpr (kEntryPaddingBytes != 0) {
    WriteField<int>(OffsetOf(index) + kEntryPaddingOffset, 0);
  }
}

int WasmDispatchTable::length(AcquireLoadTag) const {
  return ACQUIRE_READ_INT32_FIELD(*this, kLengthOffset);
}

int WasmDispatchTable::length() const { return ReadField<int>(kLengthOffset); }

int WasmDispatchTable::capacity() const {
  return ReadField<int>(kCapacityOffset);
}

inline Tagged<Object> WasmDispatchTable::ref(int index) const {
  DCHECK_LT(index, length());
  Tagged<Object> ref = ReadProtectedPointerField(OffsetOf(index) + kRefBias);
  DCHECK(IsWasmTrustedInstanceData(ref) || IsWasmApiFunctionRef(ref) ||
         ref == Smi::zero());
  return HeapObject::cast(ref);
}

inline Address WasmDispatchTable::target(int index) const {
  DCHECK_LT(index, length());
  return ReadField<Address>(OffsetOf(index) + kTargetBias);
}

inline int WasmDispatchTable::sig(int index) const {
  DCHECK_LT(index, length());
  return ReadField<int>(OffsetOf(index) + kSigBias);
}

// WasmExceptionPackage
OBJECT_CONSTRUCTORS_IMPL(WasmExceptionPackage, JSObject)
CAST_ACCESSOR(WasmExceptionPackage)

// WasmExportedFunction
WasmExportedFunction::WasmExportedFunction(Address ptr) : JSFunction(ptr) {
  SLOW_DCHECK(IsWasmExportedFunction(*this));
}
CAST_ACCESSOR(WasmExportedFunction)

// WasmInternalFunction
EXTERNAL_POINTER_ACCESSORS(WasmInternalFunction, call_target, Address,
                           kCallTargetOffset,
                           kWasmInternalFunctionCallTargetTag)
CODE_POINTER_ACCESSORS(WasmInternalFunction, code, kCodeOffset)

// {ref} will be a WasmTrustedInstanceData or a WasmApiFunctionRef.
// TODO(14564): Make this type-safe by moving WasmInternalFunction to
// the trusted space and using a protected pointer.
TRUSTED_POINTER_ACCESSORS(WasmInternalFunction, ref, ExposedTrustedObject,
                          kIndirectRefOffset, kUnknownIndirectPointerTag)

// WasmFuncRef
ACCESSORS(WasmFuncRef, internal, Tagged<WasmInternalFunction>, kInternalOffset)

// WasmFunctionData
CODE_POINTER_ACCESSORS(WasmFunctionData, wrapper_code, kWrapperCodeOffset)

ACCESSORS(WasmFunctionData, internal, Tagged<WasmInternalFunction>,
          kInternalOffset)

// WasmExportedFunctionData
CODE_POINTER_ACCESSORS(WasmExportedFunctionData, c_wrapper_code,
                       kCWrapperCodeOffset)

EXTERNAL_POINTER_ACCESSORS(WasmExportedFunctionData, sig, wasm::FunctionSig*,
                           kSigOffset, kWasmExportedFunctionDataSignatureTag)

// WasmJSFunction
WasmJSFunction::WasmJSFunction(Address ptr) : JSFunction(ptr) {
  SLOW_DCHECK(IsWasmJSFunction(*this));
}
CAST_ACCESSOR(WasmJSFunction)

// WasmCapiFunction
WasmCapiFunction::WasmCapiFunction(Address ptr) : JSFunction(ptr) {
  SLOW_DCHECK(IsWasmCapiFunction(*this));
}
CAST_ACCESSOR(WasmCapiFunction)

// WasmExternalFunction
WasmExternalFunction::WasmExternalFunction(Address ptr) : JSFunction(ptr) {
  SLOW_DCHECK(IsWasmExternalFunction(*this));
}
CAST_ACCESSOR(WasmExternalFunction)

Tagged<WasmInternalFunction> WasmExternalFunction::internal() const {
  return shared()->wasm_function_data()->internal();
}

// WasmTypeInfo
EXTERNAL_POINTER_ACCESSORS(WasmTypeInfo, native_type, Address,
                           kNativeTypeOffset, kWasmTypeInfoNativeTypeTag)

#undef OPTIONAL_ACCESSORS
#undef READ_PRIMITIVE_FIELD
#undef WRITE_PRIMITIVE_FIELD
#undef PRIMITIVE_ACCESSORS
#undef SANDBOXED_POINTER_ACCESSORS

wasm::ValueType WasmTableObject::type() {
  return wasm::ValueType::FromRawBitField(raw_type());
}

bool WasmMemoryObject::has_maximum_pages() { return maximum_pages() >= 0; }

bool WasmMemoryObject::is_memory64() const {
  int memory64_smi_value =
      TorqueGeneratedWasmMemoryObject<WasmMemoryObject,
                                      JSObject>::is_memory64();
  DCHECK_LE(0, memory64_smi_value);
  DCHECK_GE(1, memory64_smi_value);
  return memory64_smi_value != 0;
}

// static
Handle<Object> WasmObject::ReadValueAt(Isolate* isolate, Handle<HeapObject> obj,
                                       wasm::ValueType type, uint32_t offset) {
  Address field_address = obj->GetFieldAddress(offset);
  switch (type.kind()) {
    case wasm::kI8: {
      int8_t value = base::Memory<int8_t>(field_address);
      return handle(Smi::FromInt(value), isolate);
    }
    case wasm::kI16: {
      int16_t value = base::Memory<int16_t>(field_address);
      return handle(Smi::FromInt(value), isolate);
    }
    case wasm::kI32: {
      int32_t value = base::Memory<int32_t>(field_address);
      return isolate->factory()->NewNumberFromInt(value);
    }
    case wasm::kI64: {
      int64_t value = base::ReadUnalignedValue<int64_t>(field_address);
      return BigInt::FromInt64(isolate, value);
    }
    case wasm::kF32: {
      float value = base::Memory<float>(field_address);
      return isolate->factory()->NewNumber(value);
    }
    case wasm::kF64: {
      double value = base::ReadUnalignedValue<double>(field_address);
      return isolate->factory()->NewNumber(value);
    }
    case wasm::kS128:
      // TODO(v8:11804): implement
      UNREACHABLE();

    case wasm::kRef:
    case wasm::kRefNull: {
      ObjectSlot slot(field_address);
      return handle(slot.load(isolate), isolate);
    }

    case wasm::kRtt:
      // Rtt values are not supposed to be made available to JavaScript side.
      UNREACHABLE();

    case wasm::kVoid:
    case wasm::kBottom:
      UNREACHABLE();
  }
}


// Conversions from Numeric objects.
// static
template <typename ElementType>
ElementType WasmObject::FromNumber(Tagged<Object> value) {
  // The value must already be prepared for storing to numeric fields.
  DCHECK(IsNumber(value));
  if (IsSmi(value)) {
    return static_cast<ElementType>(Smi::ToInt(value));

  } else if (IsHeapNumber(value)) {
    double double_value = HeapNumber::cast(value)->value();
    if (std::is_same<ElementType, double>::value ||
        std::is_same<ElementType, float>::value) {
      return static_cast<ElementType>(double_value);
    } else {
      CHECK(std::is_integral<ElementType>::value);
      return static_cast<ElementType>(DoubleToInt32(double_value));
    }
  }
  UNREACHABLE();
}

wasm::StructType* WasmStruct::type(Tagged<Map> map) {
  Tagged<WasmTypeInfo> type_info = map->wasm_type_info();
  return reinterpret_cast<wasm::StructType*>(type_info->native_type());
}

wasm::StructType* WasmStruct::GcSafeType(Tagged<Map> map) {
  DCHECK_EQ(WASM_STRUCT_TYPE, map->instance_type());
  Tagged<HeapObject> raw = HeapObject::cast(map->constructor_or_back_pointer());
  // The {WasmTypeInfo} might be in the middle of being moved, which is why we
  // can't read its map for a checked cast. But we can rely on its native type
  // pointer being intact in the old location.
  Tagged<WasmTypeInfo> type_info = WasmTypeInfo::unchecked_cast(raw);
  return reinterpret_cast<wasm::StructType*>(type_info->native_type());
}

// static
void WasmStruct::EncodeInstanceSizeInMap(int instance_size, Tagged<Map> map) {
  // WasmStructs can be bigger than the {map.instance_size_in_words} field
  // can describe; yet we have to store the instance size somewhere on the
  // map so that the GC can read it without relying on any other objects
  // still being around. To solve this problem, we store the instance size
  // in two other fields that are otherwise unused for WasmStructs.
  static_assert(0xFFFF > ((kHeaderSize + wasm::kMaxValueTypeSize *
                                             wasm::kV8MaxWasmStructFields) >>
                          kObjectAlignmentBits));
  map->SetWasmByte1((instance_size >> kObjectAlignmentBits) & 0xff);
  map->SetWasmByte2(instance_size >> (8 + kObjectAlignmentBits));
}

// static
int WasmStruct::DecodeInstanceSizeFromMap(Tagged<Map> map) {
  return (map->WasmByte2() << (8 + kObjectAlignmentBits)) |
         (map->WasmByte1() << kObjectAlignmentBits);
}

int WasmStruct::GcSafeSize(Tagged<Map> map) {
  return DecodeInstanceSizeFromMap(map);
}

wasm::StructType* WasmStruct::type() const { return type(map()); }

Address WasmStruct::RawFieldAddress(int raw_offset) {
  int offset = WasmStruct::kHeaderSize + raw_offset;
  return FIELD_ADDR(*this, offset);
}

ObjectSlot WasmStruct::RawField(int raw_offset) {
  return ObjectSlot(RawFieldAddress(raw_offset));
}

wasm::ArrayType* WasmArray::type(Tagged<Map> map) {
  DCHECK_EQ(WASM_ARRAY_TYPE, map->instance_type());
  Tagged<WasmTypeInfo> type_info = map->wasm_type_info();
  return reinterpret_cast<wasm::ArrayType*>(type_info->native_type());
}

wasm::ArrayType* WasmArray::GcSafeType(Tagged<Map> map) {
  DCHECK_EQ(WASM_ARRAY_TYPE, map->instance_type());
  Tagged<HeapObject> raw = HeapObject::cast(map->constructor_or_back_pointer());
  // The {WasmTypeInfo} might be in the middle of being moved, which is why we
  // can't read its map for a checked cast. But we can rely on its native type
  // pointer being intact in the old location.
  Tagged<WasmTypeInfo> type_info = WasmTypeInfo::unchecked_cast(raw);
  return reinterpret_cast<wasm::ArrayType*>(type_info->native_type());
}

wasm::ArrayType* WasmArray::type() const { return type(map()); }

int WasmArray::SizeFor(Tagged<Map> map, int length) {
  int element_size = DecodeElementSizeFromMap(map);
  return kHeaderSize + RoundUp(element_size * length, kTaggedSize);
}

uint32_t WasmArray::element_offset(uint32_t index) {
  DCHECK_LE(index, length());
  return WasmArray::kHeaderSize +
         index * type()->element_type().value_kind_size();
}

Address WasmArray::ElementAddress(uint32_t index) {
  return ptr() + element_offset(index) - kHeapObjectTag;
}

ObjectSlot WasmArray::ElementSlot(uint32_t index) {
  DCHECK_LE(index, length());
  DCHECK(type()->element_type().is_reference());
  return RawField(kHeaderSize + kTaggedSize * index);
}

// static
Handle<Object> WasmArray::GetElement(Isolate* isolate, Handle<WasmArray> array,
                                     uint32_t index) {
  if (index >= array->length()) {
    return isolate->factory()->undefined_value();
  }
  wasm::ValueType element_type = array->type()->element_type();
  return ReadValueAt(isolate, array, element_type,
                     array->element_offset(index));
}

// static
void WasmArray::EncodeElementSizeInMap(int element_size, Tagged<Map> map) {
  map->SetWasmByte1(element_size);
}

// static
int WasmArray::DecodeElementSizeFromMap(Tagged<Map> map) {
  return map->WasmByte1();
}

EXTERNAL_POINTER_ACCESSORS(WasmContinuationObject, jmpbuf, Address,
                           kJmpbufOffset, kWasmContinuationJmpbufTag)

#include "src/objects/object-macros-undef.h"

}  // namespace v8::internal

#endif  // V8_WASM_WASM_OBJECTS_INL_H_
