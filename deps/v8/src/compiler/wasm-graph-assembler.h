// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_GRAPH_ASSEMBLER_H_
#define V8_COMPILER_WASM_GRAPH_ASSEMBLER_H_

#include "src/compiler/graph-assembler.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {
namespace compiler {

CallDescriptor* GetBuiltinCallDescriptor(
    Builtin name, Zone* zone, StubCallMode stub_mode,
    bool needs_frame_state = false,
    Operator::Properties properties = Operator::kNoProperties);

ObjectAccess ObjectAccessForGCStores(wasm::ValueType type);

class WasmGraphAssembler : public GraphAssembler {
 public:
  WasmGraphAssembler(MachineGraph* mcgraph, Zone* zone)
      : GraphAssembler(mcgraph, zone, BranchSemantics::kMachine),
        simplified_(zone) {}

  // While CallBuiltin() translates to a direct call to the address of the
  // builtin, CallBuiltinThroughJumptable instead jumps to a slot in a jump
  // table that then calls the builtin. As the jump table is "close" to the
  // generated code, this is encoded as a near call resulting in the instruction
  // being shorter than a direct call to the builtin.
  template <typename... Args>
  Node* CallBuiltinThroughJumptable(Builtin builtin,
                                    Operator::Properties properties,
                                    Args... args) {
    auto* call_descriptor = GetBuiltinCallDescriptor(
        builtin, temp_zone(), StubCallMode::kCallWasmRuntimeStub, false,
        properties);
    // A direct call to a wasm runtime stub defined in this module.
    // Just encode the stub index. This will be patched at relocation.
    Node* call_target = mcgraph()->RelocatableWasmBuiltinCallTarget(builtin);
    return Call(call_descriptor, call_target, args...);
  }

  Node* GetBuiltinPointerTarget(Builtin builtin) {
    static_assert(std::is_same<Smi, BuiltinPtr>(), "BuiltinPtr must be Smi");
    return NumberConstant(static_cast<int>(builtin));
  }

  template <typename... Args>
  Node* CallBuiltin(Builtin name, Operator::Properties properties,
                    Args... args) {
    return CallBuiltinImpl(name, false, properties, args...);
  }

  template <typename... Args>
  Node* CallBuiltinWithFrameState(Builtin name, Operator::Properties properties,
                                  Node* frame_state, Args... args) {
    DCHECK_EQ(frame_state->opcode(), IrOpcode::kFrameState);
    return CallBuiltinImpl(name, true, properties, frame_state, args...);
  }

  // Sets {true_node} and {false_node} to their corresponding Branch outputs.
  // Returns the Branch node. Does not change control().
  Node* Branch(Node* cond, Node** true_node, Node** false_node,
               BranchHint hint);

  Node* NumberConstant(double value) {
    return graph()->NewNode(mcgraph()->common()->NumberConstant(value));
  }

  Node* SmiConstant(Tagged_t value) {
    Address tagged_value = Internals::IntegralToSmi(static_cast<int>(value));
    return kTaggedSize == kInt32Size
               ? Int32Constant(static_cast<int32_t>(tagged_value))
               : Int64Constant(static_cast<int64_t>(tagged_value));
  }

  void MergeControlToEnd(Node* control) {
    NodeProperties::MergeControlToEnd(graph(), common(), control);
  }

  // Numeric conversions
  Node* BuildTruncateIntPtrToInt32(Node* value);

  Node* BuildChangeInt32ToIntPtr(Node* value);

  Node* BuildChangeIntPtrToInt64(Node* value);

  Node* BuildChangeUint32ToUintPtr(Node* node);

  Node* BuildSmiShiftBitsConstant();

  Node* BuildSmiShiftBitsConstant32();

  Node* BuildChangeInt32ToSmi(Node* value);

  Node* BuildChangeUint31ToSmi(Node* value);

  Node* BuildChangeSmiToInt32(Node* value);

  Node* BuildConvertUint32ToSmiWithSaturation(Node* value, uint32_t maxval);

  Node* BuildChangeSmiToIntPtr(Node* value);

  // Helper functions for dealing with HeapObjects.
  // Rule of thumb: if access to a given field in an object is required in
  // at least two places, put a helper function here.

  Node* Allocate(int size);

  Node* Allocate(Node* size);

  Node* LoadFromObject(MachineType type, Node* base, Node* offset);

  Node* LoadFromObject(MachineType type, Node* base, int offset) {
    return LoadFromObject(type, base, IntPtrConstant(offset));
  }

  Node* LoadProtectedPointerFromObject(Node* object, Node* offset);
  Node* LoadProtectedPointerFromObject(Node* object, int offset) {
    return LoadProtectedPointerFromObject(object, IntPtrConstant(offset));
  }

  Node* LoadImmutableProtectedPointerFromObject(Node* object, Node* offset);
  Node* LoadImmutableProtectedPointerFromObject(Node* object, int offset) {
    return LoadImmutableProtectedPointerFromObject(object,
                                                   IntPtrConstant(offset));
  }

  Node* LoadImmutableFromObject(MachineType type, Node* base, Node* offset);

  Node* LoadImmutableFromObject(MachineType type, Node* base, int offset) {
    return LoadImmutableFromObject(type, base, IntPtrConstant(offset));
  }

  Node* LoadImmutable(LoadRepresentation rep, Node* base, Node* offset);

  Node* LoadImmutable(LoadRepresentation rep, Node* base, int offset) {
    return LoadImmutable(rep, base, IntPtrConstant(offset));
  }

  Node* StoreToObject(ObjectAccess access, Node* base, Node* offset,
                      Node* value);

  Node* StoreToObject(ObjectAccess access, Node* base, int offset,
                      Node* value) {
    return StoreToObject(access, base, IntPtrConstant(offset), value);
  }

  Node* InitializeImmutableInObject(ObjectAccess access, Node* base,
                                    Node* offset, Node* value);

  Node* InitializeImmutableInObject(ObjectAccess access, Node* base, int offset,
                                    Node* value) {
    return InitializeImmutableInObject(access, base, IntPtrConstant(offset),
                                       value);
  }

  Node* BuildDecodeSandboxedExternalPointer(Node* handle,
                                            ExternalPointerTag tag,
                                            Node* isolate_root);
  Node* BuildLoadExternalPointerFromObject(Node* object, int offset,
                                           ExternalPointerTag tag,
                                           Node* isolate_root);

  Node* BuildLoadExternalPointerFromObject(Node* object, int offset,
                                           Node* index, ExternalPointerTag tag,
                                           Node* isolate_root);

  Node* LoadImmutableTrustedPointerFromObject(Node* object, int offset,
                                              IndirectPointerTag tag);
  Node* LoadTrustedPointerFromObject(Node* object, int offset,
                                     IndirectPointerTag tag);
  // Returns the load node (where the source position for the trap needs to be
  // set by the caller) and the result.
  std::pair<Node*, Node*> LoadTrustedPointerFromObjectTrapOnNull(
      Node* object, int offset, IndirectPointerTag tag);
  Node* BuildDecodeTrustedPointer(Node* handle, IndirectPointerTag tag);

  Node* IsSmi(Node* object);

  // Maps and their contents.

  Node* LoadMap(Node* object);

  void StoreMap(Node* heap_object, Node* map);

  Node* LoadInstanceType(Node* map);

  Node* LoadWasmTypeInfo(Node* map);

  // FixedArrays.

  Node* LoadFixedArrayLengthAsSmi(Node* fixed_array);

  Node* LoadFixedArrayElement(Node* fixed_array, Node* index_intptr,
                              MachineType type = MachineType::AnyTagged());

  Node* LoadImmutableFixedArrayElement(
      Node* fixed_array, Node* index_intptr,
      MachineType type = MachineType::AnyTagged());

  Node* LoadFixedArrayElement(Node* array, int index, MachineType type);

  Node* LoadFixedArrayElementSmi(Node* array, int index) {
    return LoadFixedArrayElement(array, index, MachineType::TaggedSigned());
  }

  Node* LoadFixedArrayElementPtr(Node* array, int index) {
    return LoadFixedArrayElement(array, index, MachineType::TaggedPointer());
  }

  Node* LoadFixedArrayElementAny(Node* array, int index) {
    return LoadFixedArrayElement(array, index, MachineType::AnyTagged());
  }

  Node* LoadProtectedFixedArrayElement(Node* array, int index);
  Node* LoadProtectedFixedArrayElement(Node* array, Node* index_intptr);

  Node* LoadByteArrayElement(Node* byte_array, Node* index_intptr,
                             MachineType type);

  Node* LoadExternalPointerArrayElement(Node* array, Node* index_intptr,
                                        ExternalPointerTag tag,
                                        Node* isolate_root);

  Node* StoreFixedArrayElement(Node* array, int index, Node* value,
                               ObjectAccess access);

  Node* StoreFixedArrayElementSmi(Node* array, int index, Node* value) {
    return StoreFixedArrayElement(
        array, index, value,
        ObjectAccess(MachineType::TaggedSigned(), kNoWriteBarrier));
  }

  Node* StoreFixedArrayElementAny(Node* array, int index, Node* value) {
    return StoreFixedArrayElement(
        array, index, value,
        ObjectAccess(MachineType::AnyTagged(), kFullWriteBarrier));
  }

  Node* LoadWeakArrayListElement(Node* fixed_array, Node* index_intptr,
                                 MachineType type = MachineType::AnyTagged());

  // Functions, SharedFunctionInfos, FunctionData.

  Node* LoadSharedFunctionInfo(Node* js_function);

  Node* LoadContextFromJSFunction(Node* js_function);

  Node* LoadFunctionDataFromJSFunction(Node* js_function);

  Node* LoadExportedFunctionIndexAsSmi(Node* exported_function_data);

  Node* LoadExportedFunctionInstanceData(Node* exported_function_data);

  // JavaScript objects.

  Node* LoadJSArrayElements(Node* js_array);

  // WasmGC objects.

  Node* FieldOffset(const wasm::StructType* type, uint32_t field_index);

  Node* WasmArrayElementOffset(Node* index, wasm::ValueType element_type);

  Node* IsDataRefMap(Node* map);

  Node* WasmTypeCheck(Node* object, Node* rtt, WasmTypeCheckConfig config);
  Node* WasmTypeCheckAbstract(Node* object, WasmTypeCheckConfig config);

  Node* WasmTypeCast(Node* object, Node* rtt, WasmTypeCheckConfig config);
  Node* WasmTypeCastAbstract(Node* object, WasmTypeCheckConfig config);

  Node* Null(wasm::ValueType type);

  Node* IsNull(Node* object, wasm::ValueType type);

  Node* IsNotNull(Node* object, wasm::ValueType type);

  Node* AssertNotNull(Node* object, wasm::ValueType type, TrapId trap_id);

  Node* WasmAnyConvertExtern(Node* object);

  Node* WasmExternConvertAny(Node* object);

  Node* StructGet(Node* object, const wasm::StructType* type, int field_index,
                  bool is_signed, CheckForNull null_check);

  void StructSet(Node* object, Node* value, const wasm::StructType* type,
                 int field_index, CheckForNull null_check);

  Node* ArrayGet(Node* array, Node* index, const wasm::ArrayType* type,
                 bool is_signed);

  void ArraySet(Node* array, Node* index, Node* value,
                const wasm::ArrayType* type);

  Node* ArrayLength(Node* array, CheckForNull null_check);

  void ArrayInitializeLength(Node* array, Node* length);

  Node* LoadStringLength(Node* string);

  Node* StringAsWtf16(Node* string);

  Node* StringPrepareForGetCodeunit(Node* string);

  // Generic helpers.

  Node* HasInstanceType(Node* heap_object, InstanceType type);

  void TrapIf(Node* condition, TrapId reason) {
    // Initially wasm traps don't have a FrameState.
    const bool has_frame_state = false;
    AddNode(
        graph()->NewNode(mcgraph()->common()->TrapIf(reason, has_frame_state),
                         condition, effect(), control()));
  }

  void TrapUnless(Node* condition, TrapId reason) {
    // Initially wasm traps don't have a FrameState.
    const bool has_frame_state = false;
    AddNode(graph()->NewNode(
        mcgraph()->common()->TrapUnless(reason, has_frame_state), condition,
        effect(), control()));
  }

  Node* LoadTrustedDataFromInstanceObject(Node* instance_object);

  SimplifiedOperatorBuilder* simplified() override { return &simplified_; }

 private:
  template <typename... Args>
  Node* CallBuiltinImpl(Builtin name, bool needs_frame_state,
                        Operator::Properties properties, Args... args) {
    auto* call_descriptor = GetBuiltinCallDescriptor(
        name, temp_zone(), StubCallMode::kCallBuiltinPointer, needs_frame_state,
        properties);
    Node* call_target = GetBuiltinPointerTarget(name);
    return Call(call_descriptor, call_target, args...);
  }

  SimplifiedOperatorBuilder simplified_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_GRAPH_ASSEMBLER_H_
