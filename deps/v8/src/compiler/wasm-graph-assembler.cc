// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-graph-assembler.h"

#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/diamond.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/objects/string.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-objects.h"

namespace v8::internal::compiler {

// static
CallDescriptor* GetBuiltinCallDescriptor(Builtin name, Zone* zone,
                                         StubCallMode stub_mode,
                                         bool needs_frame_state,
                                         Operator::Properties properties) {
  CallInterfaceDescriptor interface_descriptor =
      Builtins::CallInterfaceDescriptorFor(name);
  return Linkage::GetStubCallDescriptor(
      zone,                                           // zone
      interface_descriptor,                           // descriptor
      interface_descriptor.GetStackParameterCount(),  // stack parameter count
      needs_frame_state ? CallDescriptor::kNeedsFrameState
                        : CallDescriptor::kNoFlags,  // flags
      properties,                                    // properties
      stub_mode);                                    // stub call mode
}

// static
ObjectAccess ObjectAccessForGCStores(wasm::ValueType type) {
  return ObjectAccess(
      MachineType::TypeForRepresentation(type.machine_representation(),
                                         !type.is_packed()),
      type.is_reference() ? kFullWriteBarrier : kNoWriteBarrier);
}

// Sets {true_node} and {false_node} to their corresponding Branch outputs.
// Returns the Branch node. Does not change control().
Node* WasmGraphAssembler::Branch(Node* cond, Node** true_node,
                                 Node** false_node, BranchHint hint) {
  DCHECK_NOT_NULL(cond);
  Node* branch =
      graph()->NewNode(mcgraph()->common()->Branch(hint), cond, control());
  *true_node = graph()->NewNode(mcgraph()->common()->IfTrue(), branch);
  *false_node = graph()->NewNode(mcgraph()->common()->IfFalse(), branch);
  return branch;
}

Node* WasmGraphAssembler::BuildTruncateIntPtrToInt32(Node* value) {
  return mcgraph()->machine()->Is64() ? TruncateInt64ToInt32(value) : value;
}

Node* WasmGraphAssembler::BuildChangeInt32ToIntPtr(Node* value) {
  return mcgraph()->machine()->Is64() ? ChangeInt32ToInt64(value) : value;
}

Node* WasmGraphAssembler::BuildChangeIntPtrToInt64(Node* value) {
  return mcgraph()->machine()->Is32() ? ChangeInt32ToInt64(value) : value;
}

Node* WasmGraphAssembler::BuildChangeUint32ToUintPtr(Node* node) {
  if (mcgraph()->machine()->Is32()) return node;
  // Fold instances of ChangeUint32ToUint64(IntConstant) directly.
  Uint32Matcher matcher(node);
  if (matcher.HasResolvedValue()) {
    uintptr_t value = matcher.ResolvedValue();
    return mcgraph()->IntPtrConstant(base::bit_cast<intptr_t>(value));
  }
  return ChangeUint32ToUint64(node);
}

Node* WasmGraphAssembler::BuildSmiShiftBitsConstant() {
  return IntPtrConstant(kSmiShiftSize + kSmiTagSize);
}

Node* WasmGraphAssembler::BuildSmiShiftBitsConstant32() {
  return Int32Constant(kSmiShiftSize + kSmiTagSize);
}

Node* WasmGraphAssembler::BuildChangeInt32ToSmi(Node* value) {
  // With pointer compression, only the lower 32 bits are used.
  return COMPRESS_POINTERS_BOOL ? BitcastWord32ToWord64(Word32Shl(
                                      value, BuildSmiShiftBitsConstant32()))
                                : WordShl(BuildChangeInt32ToIntPtr(value),
                                          BuildSmiShiftBitsConstant());
}

Node* WasmGraphAssembler::BuildChangeUint31ToSmi(Node* value) {
  return COMPRESS_POINTERS_BOOL
             ? Word32Shl(value, BuildSmiShiftBitsConstant32())
             : WordShl(BuildChangeUint32ToUintPtr(value),
                       BuildSmiShiftBitsConstant());
}

Node* WasmGraphAssembler::BuildChangeSmiToInt32(Node* value) {
  return COMPRESS_POINTERS_BOOL
             ? Word32Sar(value, BuildSmiShiftBitsConstant32())
             : BuildTruncateIntPtrToInt32(
                   WordSar(value, BuildSmiShiftBitsConstant()));
}

Node* WasmGraphAssembler::BuildConvertUint32ToSmiWithSaturation(
    Node* value, uint32_t maxval) {
  DCHECK(Smi::IsValid(maxval));
  Node* max = mcgraph()->Uint32Constant(maxval);
  Node* check = Uint32LessThanOrEqual(value, max);
  Node* valsmi = BuildChangeUint31ToSmi(value);
  Node* maxsmi = NumberConstant(maxval);
  Diamond d(graph(), mcgraph()->common(), check, BranchHint::kTrue);
  d.Chain(control());
  return d.Phi(MachineRepresentation::kTagged, valsmi, maxsmi);
}

Node* WasmGraphAssembler::BuildChangeSmiToIntPtr(Node* value) {
  return COMPRESS_POINTERS_BOOL ? BuildChangeInt32ToIntPtr(Word32Sar(
                                      value, BuildSmiShiftBitsConstant32()))
                                : WordSar(value, BuildSmiShiftBitsConstant());
}

// Helper functions for dealing with HeapObjects.
// Rule of thumb: if access to a given field in an object is required in
// at least two places, put a helper function here.

Node* WasmGraphAssembler::Allocate(int size) {
  return Allocate(Int32Constant(size));
}

Node* WasmGraphAssembler::Allocate(Node* size) {
  return AddNode(graph()->NewNode(
      simplified_.AllocateRaw(Type::Any(), AllocationType::kYoung), size,
      effect(), control()));
}

Node* WasmGraphAssembler::LoadFromObject(MachineType type, Node* base,
                                         Node* offset) {
  return AddNode(graph()->NewNode(
      simplified_.LoadFromObject(ObjectAccess(type, kNoWriteBarrier)), base,
      offset, effect(), control()));
}

Node* WasmGraphAssembler::LoadProtectedPointerFromObject(Node* object,
                                                         Node* offset) {
  return LoadFromObject(V8_ENABLE_SANDBOX_BOOL ? MachineType::ProtectedPointer()
                                               : MachineType::AnyTagged(),
                        object, offset);
}

Node* WasmGraphAssembler::LoadImmutableProtectedPointerFromObject(
    Node* object, Node* offset) {
  return LoadImmutableFromObject(V8_ENABLE_SANDBOX_BOOL
                                     ? MachineType::ProtectedPointer()
                                     : MachineType::AnyTagged(),
                                 object, offset);
}

Node* WasmGraphAssembler::LoadImmutableFromObject(MachineType type, Node* base,
                                                  Node* offset) {
  return AddNode(graph()->NewNode(
      simplified_.LoadImmutableFromObject(ObjectAccess(type, kNoWriteBarrier)),
      base, offset, effect(), control()));
}

Node* WasmGraphAssembler::LoadImmutable(LoadRepresentation rep, Node* base,
                                        Node* offset) {
  return AddNode(
      graph()->NewNode(mcgraph()->machine()->LoadImmutable(rep), base, offset));
}

Node* WasmGraphAssembler::StoreToObject(ObjectAccess access, Node* base,
                                        Node* offset, Node* value) {
  return AddNode(graph()->NewNode(simplified_.StoreToObject(access), base,
                                  offset, value, effect(), control()));
}

Node* WasmGraphAssembler::InitializeImmutableInObject(ObjectAccess access,
                                                      Node* base, Node* offset,
                                                      Node* value) {
  return AddNode(
      graph()->NewNode(simplified_.InitializeImmutableInObject(access), base,
                       offset, value, effect(), control()));
}

Node* WasmGraphAssembler::BuildDecodeSandboxedExternalPointer(
    Node* handle, ExternalPointerTag tag, Node* isolate_root) {
#if V8_ENABLE_SANDBOX
  Node* index = Word32Shr(handle, Int32Constant(kExternalPointerIndexShift));
  Node* offset = ChangeUint32ToUint64(
      Word32Shl(index, Int32Constant(kExternalPointerTableEntrySizeLog2)));
  Node* table;
  if (IsSharedExternalPointerType(tag)) {
    Node* table_address =
        Load(MachineType::Pointer(), isolate_root,
             IsolateData::shared_external_pointer_table_offset());
    table = Load(MachineType::Pointer(), table_address,
                 Internals::kExternalPointerTableBasePointerOffset);
  } else {
    table = Load(MachineType::Pointer(), isolate_root,
                 IsolateData::external_pointer_table_offset() +
                     Internals::kExternalPointerTableBasePointerOffset);
  }
  Node* decoded_ptr = Load(MachineType::Pointer(), table, offset);
  return WordAnd(decoded_ptr, IntPtrConstant(~tag));
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

Node* WasmGraphAssembler::BuildDecodeTrustedPointer(Node* handle,
                                                    IndirectPointerTag tag) {
#if V8_ENABLE_SANDBOX
  Node* index = Word32Shr(handle, Int32Constant(kTrustedPointerHandleShift));
  Node* offset = ChangeUint32ToUint64(
      Word32Shl(index, Int32Constant(kTrustedPointerTableEntrySizeLog2)));
  Node* table = Load(MachineType::Pointer(), LoadRootRegister(),
                     IsolateData::trusted_pointer_table_offset() +
                         Internals::kTrustedPointerTableBasePointerOffset);
  Node* decoded_ptr = Load(MachineType::Pointer(), table, offset);
  // Untag the pointer and remove the marking bit in one operation.
  decoded_ptr = WordAnd(decoded_ptr,
                        IntPtrConstant(~(tag | kTrustedPointerTableMarkBit)));
  // We have to change the type of the result value to Tagged, so if the value
  // gets spilled on the stack, it will get processed by the GC.
  decoded_ptr = BitcastWordToTagged(decoded_ptr);
  return decoded_ptr;
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

Node* WasmGraphAssembler::BuildLoadExternalPointerFromObject(
    Node* object, int field_offset, ExternalPointerTag tag,
    Node* isolate_root) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(tag, kExternalPointerNullTag);
  Node* handle = LoadFromObject(MachineType::Uint32(), object,
                                wasm::ObjectAccess::ToTagged(field_offset));
  return BuildDecodeSandboxedExternalPointer(handle, tag, isolate_root);
#else
  return LoadFromObject(MachineType::Pointer(), object,
                        wasm::ObjectAccess::ToTagged(field_offset));
#endif  // V8_ENABLE_SANDBOX
}

Node* WasmGraphAssembler::IsSmi(Node* object) {
  if (COMPRESS_POINTERS_BOOL) {
    return Word32Equal(Word32And(object, Int32Constant(kSmiTagMask)),
                       Int32Constant(kSmiTag));
  } else {
    return WordEqual(WordAnd(object, IntPtrConstant(kSmiTagMask)),
                     IntPtrConstant(kSmiTag));
  }
}

// Maps and their contents.
Node* WasmGraphAssembler::LoadMap(Node* object) {
  Node* map_word =
      LoadImmutableFromObject(MachineType::TaggedPointer(), object,
                              HeapObject::kMapOffset - kHeapObjectTag);
#ifdef V8_MAP_PACKING
  return UnpackMapWord(map_word);
#else
  return map_word;
#endif
}

void WasmGraphAssembler::StoreMap(Node* heap_object, Node* map) {
  ObjectAccess access(MachineType::TaggedPointer(), kMapWriteBarrier);
#ifdef V8_MAP_PACKING
  map = PackMapWord(TNode<Map>::UncheckedCast(map));
#endif
  InitializeImmutableInObject(access, heap_object,
                              HeapObject::kMapOffset - kHeapObjectTag, map);
}

Node* WasmGraphAssembler::LoadInstanceType(Node* map) {
  return LoadImmutableFromObject(
      MachineType::Uint16(), map,
      wasm::ObjectAccess::ToTagged(Map::kInstanceTypeOffset));
}
Node* WasmGraphAssembler::LoadWasmTypeInfo(Node* map) {
  int offset = Map::kConstructorOrBackPointerOrNativeContextOffset;
  return LoadImmutableFromObject(MachineType::TaggedPointer(), map,
                                 wasm::ObjectAccess::ToTagged(offset));
}

// FixedArrays.

Node* WasmGraphAssembler::LoadFixedArrayLengthAsSmi(Node* fixed_array) {
  return LoadImmutableFromObject(
      MachineType::TaggedSigned(), fixed_array,
      wasm::ObjectAccess::ToTagged(FixedArray::kLengthOffset));
}

Node* WasmGraphAssembler::LoadFixedArrayElement(Node* fixed_array,
                                                Node* index_intptr,
                                                MachineType type) {
  DCHECK(IsSubtype(type.representation(), MachineRepresentation::kTagged));
  Node* offset = IntAdd(
      IntMul(index_intptr, IntPtrConstant(kTaggedSize)),
      IntPtrConstant(wasm::ObjectAccess::ToTagged(FixedArray::kHeaderSize)));
  return LoadFromObject(type, fixed_array, offset);
}

Node* WasmGraphAssembler::LoadWeakArrayListElement(Node* fixed_array,
                                                   Node* index_intptr,
                                                   MachineType type) {
  Node* offset = IntAdd(
      IntMul(index_intptr, IntPtrConstant(kTaggedSize)),
      IntPtrConstant(wasm::ObjectAccess::ToTagged(WeakArrayList::kHeaderSize)));
  return LoadFromObject(type, fixed_array, offset);
}

Node* WasmGraphAssembler::LoadImmutableFixedArrayElement(Node* fixed_array,
                                                         Node* index_intptr,
                                                         MachineType type) {
  Node* offset = IntAdd(
      IntMul(index_intptr, IntPtrConstant(kTaggedSize)),
      IntPtrConstant(wasm::ObjectAccess::ToTagged(FixedArray::kHeaderSize)));
  return LoadImmutableFromObject(type, fixed_array, offset);
}

Node* WasmGraphAssembler::LoadFixedArrayElement(Node* array, int index,
                                                MachineType type) {
  return LoadFromObject(
      type, array, wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(index));
}

Node* WasmGraphAssembler::LoadProtectedFixedArrayElement(Node* array,
                                                         int index) {
  return LoadProtectedPointerFromObject(
      array, wasm::ObjectAccess::ElementOffsetInProtectedFixedArray(index));
}

Node* WasmGraphAssembler::LoadProtectedFixedArrayElement(Node* array,
                                                         Node* index_intptr) {
  Node* offset = IntAdd(WordShl(index_intptr, IntPtrConstant(kTaggedSizeLog2)),
                        IntPtrConstant(wasm::ObjectAccess::ToTagged(
                            ProtectedFixedArray::kHeaderSize)));
  return LoadProtectedPointerFromObject(array, offset);
}

Node* WasmGraphAssembler::LoadByteArrayElement(Node* byte_array,
                                               Node* index_intptr,
                                               MachineType type) {
  int element_size = ElementSizeInBytes(type.representation());
  Node* offset = IntAdd(
      IntMul(index_intptr, IntPtrConstant(element_size)),
      IntPtrConstant(wasm::ObjectAccess::ToTagged(ByteArray::kHeaderSize)));
  return LoadFromObject(type, byte_array, offset);
}

Node* WasmGraphAssembler::LoadExternalPointerArrayElement(
    Node* array, Node* index_intptr, ExternalPointerTag tag,
    Node* isolate_root) {
  Node* offset = IntAdd(
      IntMul(index_intptr, IntPtrConstant(kExternalPointerSlotSize)),
      IntPtrConstant(
          wasm::ObjectAccess::ToTagged(ExternalPointerArray::kHeaderSize)));
#ifdef V8_ENABLE_SANDBOX
  Node* handle = LoadFromObject(MachineType::Uint32(), array, offset);
  return BuildDecodeSandboxedExternalPointer(handle, tag, isolate_root);
#else
  return LoadFromObject(MachineType::Pointer(), array, offset);
#endif
}

Node* WasmGraphAssembler::LoadImmutableTrustedPointerFromObject(
    Node* object, int field_offset, IndirectPointerTag tag) {
  Node* offset = IntPtrConstant(field_offset);
#ifdef V8_ENABLE_SANDBOX
  Node* handle = LoadImmutableFromObject(MachineType::Uint32(), object, offset);
  return BuildDecodeTrustedPointer(handle, tag);
#else
  return LoadImmutableFromObject(MachineType::TaggedPointer(), object, offset);
#endif
}

Node* WasmGraphAssembler::LoadTrustedPointerFromObject(Node* object,
                                                       int field_offset,
                                                       IndirectPointerTag tag) {
  Node* offset = IntPtrConstant(field_offset);
#ifdef V8_ENABLE_SANDBOX
  Node* handle = LoadFromObject(MachineType::Uint32(), object, offset);
  return BuildDecodeTrustedPointer(handle, tag);
#else
  return LoadFromObject(MachineType::TaggedPointer(), object, offset);
#endif
}

std::pair<Node*, Node*>
WasmGraphAssembler::LoadTrustedPointerFromObjectTrapOnNull(
    Node* object, int field_offset, IndirectPointerTag tag) {
  Node* offset = IntPtrConstant(field_offset);
#ifdef V8_ENABLE_SANDBOX
  Node* handle = LoadTrapOnNull(MachineType::Uint32(), object, offset);
  return {handle, BuildDecodeTrustedPointer(handle, tag)};
#else
  Node* value = LoadTrapOnNull(MachineType::TaggedPointer(), object, offset);
  return {value, value};
#endif
}

Node* WasmGraphAssembler::StoreFixedArrayElement(Node* array, int index,
                                                 Node* value,
                                                 ObjectAccess access) {
  return StoreToObject(
      access, array, wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(index),
      value);
}

// Functions, SharedFunctionInfos, FunctionData.

Node* WasmGraphAssembler::LoadSharedFunctionInfo(Node* js_function) {
  return LoadFromObject(
      MachineType::TaggedPointer(), js_function,
      wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction());
}
Node* WasmGraphAssembler::LoadContextFromJSFunction(Node* js_function) {
  return LoadFromObject(MachineType::TaggedPointer(), js_function,
                        wasm::ObjectAccess::ContextOffsetInTaggedJSFunction());
}

Node* WasmGraphAssembler::LoadFunctionDataFromJSFunction(Node* js_function) {
  Node* shared = LoadSharedFunctionInfo(js_function);
  return LoadTrustedPointerFromObject(
      shared,
      wasm::ObjectAccess::ToTagged(
          SharedFunctionInfo::kTrustedFunctionDataOffset),
      kWasmFunctionDataIndirectPointerTag);
}

Node* WasmGraphAssembler::LoadExportedFunctionIndexAsSmi(
    Node* exported_function_data) {
  return LoadImmutableFromObject(
      MachineType::TaggedSigned(), exported_function_data,
      wasm::ObjectAccess::ToTagged(
          WasmExportedFunctionData::kFunctionIndexOffset));
}
Node* WasmGraphAssembler::LoadExportedFunctionInstanceData(
    Node* exported_function_data) {
  return LoadImmutableProtectedPointerFromObject(
      exported_function_data,
      wasm::ObjectAccess::ToTagged(
          WasmExportedFunctionData::kProtectedInstanceDataOffset));
}

// JavaScript objects.

Node* WasmGraphAssembler::LoadJSArrayElements(Node* js_array) {
  return LoadFromObject(
      MachineType::AnyTagged(), js_array,
      wasm::ObjectAccess::ToTagged(JSObject::kElementsOffset));
}

// WasmGC objects.

Node* WasmGraphAssembler::FieldOffset(const wasm::StructType* type,
                                      uint32_t field_index) {
  return IntPtrConstant(wasm::ObjectAccess::ToTagged(
      WasmStruct::kHeaderSize + type->field_offset(field_index)));
}

Node* WasmGraphAssembler::WasmArrayElementOffset(Node* index,
                                                 wasm::ValueType element_type) {
  Node* index_intptr =
      mcgraph()->machine()->Is64() ? ChangeInt32ToInt64(index) : index;
  return IntAdd(
      IntPtrConstant(wasm::ObjectAccess::ToTagged(WasmArray::kHeaderSize)),
      IntMul(index_intptr, IntPtrConstant(element_type.value_kind_size())));
}

Node* WasmGraphAssembler::IsDataRefMap(Node* map) {
  Node* instance_type = LoadInstanceType(map);
  // We're going to test a range of WasmObject instance types with a single
  // unsigned comparison.
  Node* comparison_value =
      Int32Sub(instance_type, Int32Constant(FIRST_WASM_OBJECT_TYPE));
  return Uint32LessThanOrEqual(
      comparison_value,
      Int32Constant(LAST_WASM_OBJECT_TYPE - FIRST_WASM_OBJECT_TYPE));
}

Node* WasmGraphAssembler::WasmTypeCheck(Node* object, Node* rtt,
                                        WasmTypeCheckConfig config) {
  return AddNode(graph()->NewNode(simplified_.WasmTypeCheck(config), object,
                                  rtt, effect(), control()));
}

Node* WasmGraphAssembler::WasmTypeCheckAbstract(Node* object,
                                                WasmTypeCheckConfig config) {
  return AddNode(graph()->NewNode(simplified_.WasmTypeCheckAbstract(config),
                                  object, effect(), control()));
}

Node* WasmGraphAssembler::WasmTypeCast(Node* object, Node* rtt,
                                       WasmTypeCheckConfig config) {
  return AddNode(graph()->NewNode(simplified_.WasmTypeCast(config), object, rtt,
                                  effect(), control()));
}

Node* WasmGraphAssembler::WasmTypeCastAbstract(Node* object,
                                               WasmTypeCheckConfig config) {
  return AddNode(graph()->NewNode(simplified_.WasmTypeCastAbstract(config),
                                  object, effect(), control()));
}

Node* WasmGraphAssembler::Null(wasm::ValueType type) {
  return AddNode(graph()->NewNode(simplified_.Null(type)));
}

Node* WasmGraphAssembler::IsNull(Node* object, wasm::ValueType type) {
  return AddNode(graph()->NewNode(simplified_.IsNull(type), object, control()));
}

Node* WasmGraphAssembler::IsNotNull(Node* object, wasm::ValueType type) {
  return AddNode(
      graph()->NewNode(simplified_.IsNotNull(type), object, control()));
}

Node* WasmGraphAssembler::AssertNotNull(Node* object, wasm::ValueType type,
                                        TrapId trap_id) {
  return AddNode(graph()->NewNode(simplified_.AssertNotNull(type, trap_id),
                                  object, effect(), control()));
}

Node* WasmGraphAssembler::WasmAnyConvertExtern(Node* object) {
  return AddNode(graph()->NewNode(simplified_.WasmAnyConvertExtern(), object,
                                  effect(), control()));
}

Node* WasmGraphAssembler::WasmExternConvertAny(Node* object) {
  return AddNode(graph()->NewNode(simplified_.WasmExternConvertAny(), object,
                                  effect(), control()));
}

Node* WasmGraphAssembler::StructGet(Node* object, const wasm::StructType* type,
                                    int field_index, bool is_signed,
                                    CheckForNull null_check) {
  return AddNode(graph()->NewNode(
      simplified_.WasmStructGet(type, field_index, is_signed, null_check),
      object, effect(), control()));
}

void WasmGraphAssembler::StructSet(Node* object, Node* value,
                                   const wasm::StructType* type,
                                   int field_index, CheckForNull null_check) {
  AddNode(
      graph()->NewNode(simplified_.WasmStructSet(type, field_index, null_check),
                       object, value, effect(), control()));
}

Node* WasmGraphAssembler::ArrayGet(Node* array, Node* index,
                                   const wasm::ArrayType* type,
                                   bool is_signed) {
  return AddNode(graph()->NewNode(simplified_.WasmArrayGet(type, is_signed),
                                  array, index, effect(), control()));
}

void WasmGraphAssembler::ArraySet(Node* array, Node* index, Node* value,
                                  const wasm::ArrayType* type) {
  AddNode(graph()->NewNode(simplified_.WasmArraySet(type), array, index, value,
                           effect(), control()));
}

Node* WasmGraphAssembler::ArrayLength(Node* array, CheckForNull null_check) {
  return AddNode(graph()->NewNode(simplified_.WasmArrayLength(null_check),
                                  array, effect(), control()));
}

void WasmGraphAssembler::ArrayInitializeLength(Node* array, Node* length) {
  AddNode(graph()->NewNode(simplified_.WasmArrayInitializeLength(), array,
                           length, effect(), control()));
}

Node* WasmGraphAssembler::LoadStringLength(Node* string) {
  return LoadImmutableFromObject(
      MachineType::Int32(), string,
      wasm::ObjectAccess::ToTagged(AccessBuilder::ForStringLength().offset));
}

Node* WasmGraphAssembler::StringAsWtf16(Node* string) {
  return AddNode(graph()->NewNode(simplified_.StringAsWtf16(), string, effect(),
                                  control()));
}

Node* WasmGraphAssembler::StringPrepareForGetCodeunit(Node* string) {
  return AddNode(graph()->NewNode(simplified_.StringPrepareForGetCodeunit(),
                                  string, effect(), control()));
}

Node* WasmGraphAssembler::LoadTrustedDataFromInstanceObject(
    Node* instance_object) {
  return LoadImmutableTrustedPointerFromObject(
      instance_object,
      wasm::ObjectAccess::ToTagged(WasmInstanceObject::kTrustedDataOffset),
      kWasmTrustedInstanceDataIndirectPointerTag);
}

// Generic HeapObject helpers.

Node* WasmGraphAssembler::HasInstanceType(Node* heap_object,
                                          InstanceType type) {
  Node* map = LoadMap(heap_object);
  Node* instance_type = LoadInstanceType(map);
  return Word32Equal(instance_type, Int32Constant(type));
}

}  // namespace v8::internal::compiler
