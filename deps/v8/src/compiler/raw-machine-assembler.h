// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_RAW_MACHINE_ASSEMBLER_H_
#define V8_COMPILER_RAW_MACHINE_ASSEMBLER_H_

#include <initializer_list>
#include <type_traits>

#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {
namespace compiler {

class BasicBlock;
class RawMachineLabel;
class Schedule;
class SourcePositionTable;

// The RawMachineAssembler produces a low-level IR graph. All nodes are wired
// into a graph and also placed into a schedule immediately, hence subsequent
// code generation can happen without the need for scheduling.
//
// In order to create a schedule on-the-fly, the assembler keeps track of basic
// blocks by having one current basic block being populated and by referencing
// other basic blocks through the use of labels.
//
// Also note that the generated graph is only valid together with the generated
// schedule, using one without the other is invalid as the graph is inherently
// non-schedulable due to missing control and effect dependencies.
class V8_EXPORT_PRIVATE RawMachineAssembler {
 public:
  RawMachineAssembler(
      Isolate* isolate, Graph* graph, CallDescriptor* call_descriptor,
      MachineRepresentation word = MachineType::PointerRepresentation(),
      MachineOperatorBuilder::Flags flags =
          MachineOperatorBuilder::Flag::kNoFlags,
      MachineOperatorBuilder::AlignmentRequirements alignment_requirements =
          MachineOperatorBuilder::AlignmentRequirements::
              FullUnalignedAccessSupport());
  ~RawMachineAssembler() = default;

  RawMachineAssembler(const RawMachineAssembler&) = delete;
  RawMachineAssembler& operator=(const RawMachineAssembler&) = delete;

  Isolate* isolate() const { return isolate_; }
  Graph* graph() const { return graph_; }
  Zone* zone() const { return graph()->zone(); }
  MachineOperatorBuilder* machine() { return &machine_; }
  CommonOperatorBuilder* common() { return &common_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }
  CallDescriptor* call_descriptor() const { return call_descriptor_; }

  // Only used for tests: Finalizes the schedule and exports it to be used for
  // code generation. Note that this RawMachineAssembler becomes invalid after
  // export.
  Schedule* ExportForTest();
  // Finalizes the schedule and transforms it into a graph that's suitable for
  // it to be used for Turbofan optimization and re-scheduling. Note that this
  // RawMachineAssembler becomes invalid after export.
  Graph* ExportForOptimization();

  // ===========================================================================
  // The following utility methods create new nodes with specific operators and
  // place them into the current basic block. They don't perform control flow,
  // hence will not switch the current basic block.

  Node* NullConstant();
  Node* UndefinedConstant();

  // Constants.
  Node* PointerConstant(void* value) {
    return IntPtrConstant(reinterpret_cast<intptr_t>(value));
  }
  Node* IntPtrConstant(intptr_t value) {
    // TODO(dcarney): mark generated code as unserializable if value != 0.
    return kSystemPointerSize == 8 ? Int64Constant(value)
                                   : Int32Constant(static_cast<int>(value));
  }
  Node* RelocatableIntPtrConstant(intptr_t value, RelocInfo::Mode rmode);
  Node* Int32Constant(int32_t value) {
    return AddNode(common()->Int32Constant(value));
  }
  Node* StackSlot(MachineRepresentation rep, int alignment = 0) {
    return AddNode(machine()->StackSlot(rep, alignment));
  }
  Node* StackSlot(int size, int alignment) {
    return AddNode(machine()->StackSlot(size, alignment));
  }
  Node* Int64Constant(int64_t value) {
    return AddNode(common()->Int64Constant(value));
  }
  Node* NumberConstant(double value) {
    return AddNode(common()->NumberConstant(value));
  }
  Node* Float32Constant(float value) {
    return AddNode(common()->Float32Constant(value));
  }
  Node* Float64Constant(double value) {
    return AddNode(common()->Float64Constant(value));
  }
  Node* HeapConstant(Handle<HeapObject> object) {
    return AddNode(common()->HeapConstant(object));
  }
  Node* ExternalConstant(ExternalReference address) {
    return AddNode(common()->ExternalConstant(address));
  }
  Node* RelocatableInt32Constant(int32_t value, RelocInfo::Mode rmode) {
    return AddNode(common()->RelocatableInt32Constant(value, rmode));
  }
  Node* RelocatableInt64Constant(int64_t value, RelocInfo::Mode rmode) {
    return AddNode(common()->RelocatableInt64Constant(value, rmode));
  }

  Node* Projection(int index, Node* a) {
    return AddNode(common()->Projection(index), a);
  }

  // Memory Operations.
  Node* Load(MachineType type, Node* base) {
    return Load(type, base, IntPtrConstant(0));
  }
  Node* Load(MachineType type, Node* base, Node* index) {
    const Operator* op = machine()->Load(type);
    Node* load = AddNode(op, base, index);
    return load;
  }
  Node* LoadImmutable(MachineType type, Node* base) {
    return LoadImmutable(type, base, IntPtrConstant(0));
  }
  Node* LoadImmutable(MachineType type, Node* base, Node* index) {
    const Operator* op = machine()->LoadImmutable(type);
    return AddNode(op, base, index);
  }
  bool IsMapOffsetConstant(Node* node) {
    Int64Matcher m(node);
    if (m.Is(HeapObject::kMapOffset)) return true;
    // Test if `node` is a `Phi(Int64Constant(0))`
    if (node->opcode() == IrOpcode::kPhi) {
      for (Node* input : node->inputs()) {
        if (!Int64Matcher(input).Is(HeapObject::kMapOffset)) return false;
      }
      return true;
    }
    return false;
  }
  bool IsMapOffsetConstantMinusTag(Node* node) {
    Int64Matcher m(node);
    return m.Is(HeapObject::kMapOffset - kHeapObjectTag);
  }
  bool IsMapOffsetConstantMinusTag(int offset) {
    return offset == HeapObject::kMapOffset - kHeapObjectTag;
  }
  Node* LoadFromObject(MachineType type, Node* base, Node* offset) {
    DCHECK_IMPLIES(V8_MAP_PACKING_BOOL && IsMapOffsetConstantMinusTag(offset),
                   type == MachineType::MapInHeader());
    ObjectAccess access = {type, WriteBarrierKind::kNoWriteBarrier};
    Node* load = AddNode(simplified()->LoadFromObject(access), base, offset);
    return load;
  }

  Node* LoadProtectedPointerFromObject(Node* base, Node* offset) {
#if V8_ENABLE_SANDBOX
    static_assert(COMPRESS_POINTERS_BOOL);
    Node* tagged = LoadFromObject(MachineType::Int32(), base, offset);
    Node* trusted_cage_base =
        LoadImmutable(MachineType::Pointer(), LoadRootRegister(),
                      IntPtrConstant(IsolateData::trusted_cage_base_offset()));
    return BitcastWordToTagged(
        WordOr(trusted_cage_base, ChangeUint32ToUint64(tagged)));
#else
    return LoadFromObject(MachineType::AnyTagged(), base, offset);
#endif  // V8_ENABLE_SANDBOX
  }

  Node* Store(MachineRepresentation rep, Node* base, Node* value,
              WriteBarrierKind write_barrier) {
    return Store(rep, base, IntPtrConstant(0), value, write_barrier);
  }
  Node* Store(MachineRepresentation rep, Node* base, Node* index, Node* value,
              WriteBarrierKind write_barrier) {
    return AddNode(machine()->Store(StoreRepresentation(rep, write_barrier)),
                   base, index, value);
  }
  void StoreToObject(MachineRepresentation rep, Node* object, Node* offset,
                     Node* value, WriteBarrierKind write_barrier) {
    ObjectAccess access = {MachineType::TypeForRepresentation(rep),
                           write_barrier};
    DCHECK(!IsMapOffsetConstantMinusTag(offset));
    AddNode(simplified()->StoreToObject(access), object, offset, value);
  }
  void OptimizedStoreField(MachineRepresentation rep, Node* object, int offset,
                           Node* value, WriteBarrierKind write_barrier) {
    DCHECK(!IsMapOffsetConstantMinusTag(offset));
    DCHECK_NE(rep, MachineRepresentation::kIndirectPointer);
    AddNode(simplified()->StoreField(
                FieldAccess(BaseTaggedness::kTaggedBase, offset,
                            MaybeHandle<Name>(), OptionalMapRef(), Type::Any(),
                            MachineType::TypeForRepresentation(rep),
                            write_barrier, "OptimizedStoreField")),
            object, value);
  }
  void OptimizedStoreIndirectPointerField(Node* object, int offset,
                                          IndirectPointerTag tag, Node* value,
                                          WriteBarrierKind write_barrier) {
    DCHECK(!IsMapOffsetConstantMinusTag(offset));
    DCHECK(write_barrier == WriteBarrierKind::kNoWriteBarrier ||
           write_barrier == WriteBarrierKind::kIndirectPointerWriteBarrier);
    FieldAccess access(BaseTaggedness::kTaggedBase, offset, MaybeHandle<Name>(),
                       OptionalMapRef(), Type::Any(),
                       MachineType::IndirectPointer(), write_barrier,
                       "OptimizedStoreIndirectPointerField");
    access.indirect_pointer_tag = tag;
    AddNode(simplified()->StoreField(access), object, value);
  }
  void OptimizedStoreMap(Node* object, Node* value,
                         WriteBarrierKind write_barrier = kMapWriteBarrier) {
    AddNode(simplified()->StoreField(AccessBuilder::ForMap(write_barrier)),
            object, value);
  }
  Node* Retain(Node* value) { return AddNode(common()->Retain(), value); }

  Node* OptimizedAllocate(Node* size, AllocationType allocation);

  // Unaligned memory operations
  Node* UnalignedLoad(MachineType type, Node* base) {
    return UnalignedLoad(type, base, IntPtrConstant(0));
  }
  Node* UnalignedLoad(MachineType type, Node* base, Node* index) {
    MachineRepresentation rep = type.representation();
    // Tagged or compressed should never be unaligned
    DCHECK(!(IsAnyTagged(rep) || IsAnyCompressed(rep)));
    if (machine()->UnalignedLoadSupported(rep)) {
      return AddNode(machine()->Load(type), base, index);
    } else {
      return AddNode(machine()->UnalignedLoad(type), base, index);
    }
  }
  Node* UnalignedStore(MachineRepresentation rep, Node* base, Node* value) {
    return UnalignedStore(rep, base, IntPtrConstant(0), value);
  }
  Node* UnalignedStore(MachineRepresentation rep, Node* base, Node* index,
                       Node* value) {
    // Tagged or compressed should never be unaligned
    DCHECK(!(IsAnyTagged(rep) || IsAnyCompressed(rep)));
    if (machine()->UnalignedStoreSupported(rep)) {
      return AddNode(machine()->Store(StoreRepresentation(
                         rep, WriteBarrierKind::kNoWriteBarrier)),
                     base, index, value);
    } else {
      return AddNode(
          machine()->UnalignedStore(UnalignedStoreRepresentation(rep)), base,
          index, value);
    }
  }

  // Atomic memory operations.
  Node* AtomicLoad(AtomicLoadParameters rep, Node* base, Node* index) {
    DCHECK_NE(rep.representation().representation(),
              MachineRepresentation::kWord64);
    return AddNode(machine()->Word32AtomicLoad(rep), base, index);
  }

  Node* AtomicLoad64(AtomicLoadParameters rep, Node* base, Node* index) {
    if (machine()->Is64()) {
      // This uses Uint64() intentionally: AtomicLoad is not implemented for
      // Int64(), which is fine because the machine instruction only cares
      // about words.
      return AddNode(machine()->Word64AtomicLoad(rep), base, index);
    } else {
      return AddNode(machine()->Word32AtomicPairLoad(rep.order()), base, index);
    }
  }

#if defined(V8_TARGET_BIG_ENDIAN)
#define VALUE_HALVES value_high, value
#else
#define VALUE_HALVES value, value_high
#endif

  Node* AtomicStore(AtomicStoreParameters params, Node* base, Node* index,
                    Node* value) {
    DCHECK(!IsMapOffsetConstantMinusTag(index));
    DCHECK_NE(params.representation(), MachineRepresentation::kWord64);
    return AddNode(machine()->Word32AtomicStore(params), base, index, value);
  }

  Node* AtomicStore64(AtomicStoreParameters params, Node* base, Node* index,
                      Node* value, Node* value_high) {
    if (machine()->Is64()) {
      DCHECK_NULL(value_high);
      return AddNode(machine()->Word64AtomicStore(params), base, index, value);
    } else {
      DCHECK(params.representation() != MachineRepresentation::kTaggedPointer &&
             params.representation() != MachineRepresentation::kTaggedSigned &&
             params.representation() != MachineRepresentation::kTagged);
      return AddNode(machine()->Word32AtomicPairStore(params.order()), base,
                     index, VALUE_HALVES);
    }
  }

#define ATOMIC_FUNCTION(name)                                                  \
  Node* Atomic##name(MachineType type, Node* base, Node* index, Node* value) { \
    DCHECK_NE(type.representation(), MachineRepresentation::kWord64);          \
    return AddNode(machine()->Word32Atomic##name(type), base, index, value);   \
  }                                                                            \
  Node* Atomic##name##64(Node * base, Node * index, Node * value,              \
                         Node * value_high) {                                  \
    if (machine()->Is64()) {                                                   \
      DCHECK_NULL(value_high);                                                 \
      /* This uses Uint64() intentionally: Atomic operations are not  */       \
      /* implemented for Int64(), which is fine because the machine   */       \
      /* instruction only cares about words.                          */       \
      return AddNode(machine()->Word64Atomic##name(MachineType::Uint64()),     \
                     base, index, value);                                      \
    } else {                                                                   \
      return AddNode(machine()->Word32AtomicPair##name(), base, index,         \
                     VALUE_HALVES);                                            \
    }                                                                          \
  }
  ATOMIC_FUNCTION(Exchange)
  ATOMIC_FUNCTION(Add)
  ATOMIC_FUNCTION(Sub)
  ATOMIC_FUNCTION(And)
  ATOMIC_FUNCTION(Or)
  ATOMIC_FUNCTION(Xor)
#undef ATOMIC_FUNCTION
#undef VALUE_HALVES

  Node* AtomicCompareExchange(MachineType type, Node* base, Node* index,
                              Node* old_value, Node* new_value) {
    DCHECK_NE(type.representation(), MachineRepresentation::kWord64);
    return AddNode(machine()->Word32AtomicCompareExchange(type), base, index,
                   old_value, new_value);
  }

  Node* AtomicCompareExchange64(Node* base, Node* index, Node* old_value,
                                Node* old_value_high, Node* new_value,
                                Node* new_value_high) {
    if (machine()->Is64()) {
      DCHECK_NULL(old_value_high);
      DCHECK_NULL(new_value_high);
      // This uses Uint64() intentionally: AtomicCompareExchange is not
      // implemented for Int64(), which is fine because the machine instruction
      // only cares about words.
      return AddNode(
          machine()->Word64AtomicCompareExchange(MachineType::Uint64()), base,
          index, old_value, new_value);
    } else {
      return AddNode(machine()->Word32AtomicPairCompareExchange(), base, index,
                     old_value, old_value_high, new_value, new_value_high);
    }
  }

  Node* MemoryBarrier(AtomicMemoryOrder order) {
    return AddNode(machine()->MemoryBarrier(order));
  }

  // Arithmetic Operations.
  Node* WordAnd(Node* a, Node* b) {
    return AddNode(machine()->WordAnd(), a, b);
  }
  Node* WordOr(Node* a, Node* b) { return AddNode(machine()->WordOr(), a, b); }
  Node* WordXor(Node* a, Node* b) {
    return AddNode(machine()->WordXor(), a, b);
  }
  Node* WordShl(Node* a, Node* b) {
    return AddNode(machine()->WordShl(), a, b);
  }
  Node* WordShr(Node* a, Node* b) {
    return AddNode(machine()->WordShr(), a, b);
  }
  Node* WordSar(Node* a, Node* b) {
    return AddNode(machine()->WordSar(), a, b);
  }
  Node* WordSarShiftOutZeros(Node* a, Node* b) {
    return AddNode(machine()->WordSarShiftOutZeros(), a, b);
  }
  Node* WordRor(Node* a, Node* b) {
    return AddNode(machine()->WordRor(), a, b);
  }
  Node* WordEqual(Node* a, Node* b) {
    return AddNode(machine()->WordEqual(), a, b);
  }
  Node* WordNotEqual(Node* a, Node* b) {
    return Word32BinaryNot(WordEqual(a, b));
  }
  Node* WordNot(Node* a) {
    if (machine()->Is32()) {
      return Word32BitwiseNot(a);
    } else {
      return Word64Not(a);
    }
  }

  Node* Word32And(Node* a, Node* b) {
    return AddNode(machine()->Word32And(), a, b);
  }
  Node* Word32Or(Node* a, Node* b) {
    return AddNode(machine()->Word32Or(), a, b);
  }
  Node* Word32Xor(Node* a, Node* b) {
    return AddNode(machine()->Word32Xor(), a, b);
  }
  Node* Word32Shl(Node* a, Node* b) {
    return AddNode(machine()->Word32Shl(), a, b);
  }
  Node* Word32Shr(Node* a, Node* b) {
    return AddNode(machine()->Word32Shr(), a, b);
  }
  Node* Word32Sar(Node* a, Node* b) {
    return AddNode(machine()->Word32Sar(), a, b);
  }
  Node* Word32SarShiftOutZeros(Node* a, Node* b) {
    return AddNode(machine()->Word32SarShiftOutZeros(), a, b);
  }
  Node* Word32Ror(Node* a, Node* b) {
    return AddNode(machine()->Word32Ror(), a, b);
  }
  Node* Word32Clz(Node* a) { return AddNode(machine()->Word32Clz(), a); }
  Node* Word32Equal(Node* a, Node* b) {
    return AddNode(machine()->Word32Equal(), a, b);
  }
  Node* Word32NotEqual(Node* a, Node* b) {
    return Word32BinaryNot(Word32Equal(a, b));
  }
  Node* Word32BitwiseNot(Node* a) { return Word32Xor(a, Int32Constant(-1)); }
  Node* Word32BinaryNot(Node* a) { return Word32Equal(a, Int32Constant(0)); }

  Node* Word64And(Node* a, Node* b) {
    return AddNode(machine()->Word64And(), a, b);
  }
  Node* Word64Or(Node* a, Node* b) {
    return AddNode(machine()->Word64Or(), a, b);
  }
  Node* Word64Xor(Node* a, Node* b) {
    return AddNode(machine()->Word64Xor(), a, b);
  }
  Node* Word64Shl(Node* a, Node* b) {
    return AddNode(machine()->Word64Shl(), a, b);
  }
  Node* Word64Shr(Node* a, Node* b) {
    return AddNode(machine()->Word64Shr(), a, b);
  }
  Node* Word64Sar(Node* a, Node* b) {
    return AddNode(machine()->Word64Sar(), a, b);
  }
  Node* Word64Ror(Node* a, Node* b) {
    return AddNode(machine()->Word64Ror(), a, b);
  }
  Node* Word64Clz(Node* a) { return AddNode(machine()->Word64Clz(), a); }
  Node* Word64Equal(Node* a, Node* b) {
    return AddNode(machine()->Word64Equal(), a, b);
  }
  Node* Word64NotEqual(Node* a, Node* b) {
    return Word32BinaryNot(Word64Equal(a, b));
  }
  Node* Word64Not(Node* a) { return Word64Xor(a, Int64Constant(-1)); }

  Node* Int32Add(Node* a, Node* b) {
    return AddNode(machine()->Int32Add(), a, b);
  }
  Node* Int32AddWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int32AddWithOverflow(), a, b);
  }
  Node* Int32Sub(Node* a, Node* b) {
    return AddNode(machine()->Int32Sub(), a, b);
  }
  Node* Int32SubWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int32SubWithOverflow(), a, b);
  }
  Node* Int32Mul(Node* a, Node* b) {
    return AddNode(machine()->Int32Mul(), a, b);
  }
  Node* Int32MulHigh(Node* a, Node* b) {
    return AddNode(machine()->Int32MulHigh(), a, b);
  }
  Node* Int32MulWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int32MulWithOverflow(), a, b);
  }
  Node* Int32Div(Node* a, Node* b) {
    return AddNode(machine()->Int32Div(), a, b);
  }
  Node* Int32Mod(Node* a, Node* b) {
    return AddNode(machine()->Int32Mod(), a, b);
  }
  Node* Int32LessThan(Node* a, Node* b) {
    return AddNode(machine()->Int32LessThan(), a, b);
  }
  Node* Int32LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Int32LessThanOrEqual(), a, b);
  }
  Node* Uint32Div(Node* a, Node* b) {
    return AddNode(machine()->Uint32Div(), a, b);
  }
  Node* Uint32LessThan(Node* a, Node* b) {
    return AddNode(machine()->Uint32LessThan(), a, b);
  }
  Node* Uint32LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Uint32LessThanOrEqual(), a, b);
  }
  Node* Uint32Mod(Node* a, Node* b) {
    return AddNode(machine()->Uint32Mod(), a, b);
  }
  Node* Uint32MulHigh(Node* a, Node* b) {
    return AddNode(machine()->Uint32MulHigh(), a, b);
  }
  Node* Int32GreaterThan(Node* a, Node* b) { return Int32LessThan(b, a); }
  Node* Int32GreaterThanOrEqual(Node* a, Node* b) {
    return Int32LessThanOrEqual(b, a);
  }
  Node* Uint32GreaterThan(Node* a, Node* b) { return Uint32LessThan(b, a); }
  Node* Uint32GreaterThanOrEqual(Node* a, Node* b) {
    return Uint32LessThanOrEqual(b, a);
  }
  Node* Int32Neg(Node* a) { return Int32Sub(Int32Constant(0), a); }

  Node* Int64Add(Node* a, Node* b) {
    return AddNode(machine()->Int64Add(), a, b);
  }
  Node* Int64AddWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int64AddWithOverflow(), a, b);
  }
  Node* Int64Sub(Node* a, Node* b) {
    return AddNode(machine()->Int64Sub(), a, b);
  }
  Node* Int64SubWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int64SubWithOverflow(), a, b);
  }
  Node* Int64Mul(Node* a, Node* b) {
    return AddNode(machine()->Int64Mul(), a, b);
  }
  Node* Int64MulHigh(Node* a, Node* b) {
    return AddNode(machine()->Int64MulHigh(), a, b);
  }
  Node* Uint64MulHigh(Node* a, Node* b) {
    return AddNode(machine()->Uint64MulHigh(), a, b);
  }
  Node* Int64MulWithOverflow(Node* a, Node* b) {
    return AddNode(machine()->Int64MulWithOverflow(), a, b);
  }
  Node* Int64Div(Node* a, Node* b) {
    return AddNode(machine()->Int64Div(), a, b);
  }
  Node* Int64Mod(Node* a, Node* b) {
    return AddNode(machine()->Int64Mod(), a, b);
  }
  Node* Int64Neg(Node* a) { return Int64Sub(Int64Constant(0), a); }
  Node* Int64LessThan(Node* a, Node* b) {
    return AddNode(machine()->Int64LessThan(), a, b);
  }
  Node* Int64LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Int64LessThanOrEqual(), a, b);
  }
  Node* Uint64LessThan(Node* a, Node* b) {
    return AddNode(machine()->Uint64LessThan(), a, b);
  }
  Node* Uint64LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Uint64LessThanOrEqual(), a, b);
  }
  Node* Int64GreaterThan(Node* a, Node* b) { return Int64LessThan(b, a); }
  Node* Int64GreaterThanOrEqual(Node* a, Node* b) {
    return Int64LessThanOrEqual(b, a);
  }
  Node* Uint64GreaterThan(Node* a, Node* b) { return Uint64LessThan(b, a); }
  Node* Uint64GreaterThanOrEqual(Node* a, Node* b) {
    return Uint64LessThanOrEqual(b, a);
  }
  Node* Uint64Div(Node* a, Node* b) {
    return AddNode(machine()->Uint64Div(), a, b);
  }
  Node* Uint64Mod(Node* a, Node* b) {
    return AddNode(machine()->Uint64Mod(), a, b);
  }
  Node* Int32PairAdd(Node* a_low, Node* a_high, Node* b_low, Node* b_high) {
    return AddNode(machine()->Int32PairAdd(), a_low, a_high, b_low, b_high);
  }
  Node* Int32PairSub(Node* a_low, Node* a_high, Node* b_low, Node* b_high) {
    return AddNode(machine()->Int32PairSub(), a_low, a_high, b_low, b_high);
  }
  Node* Int32PairMul(Node* a_low, Node* a_high, Node* b_low, Node* b_high) {
    return AddNode(machine()->Int32PairMul(), a_low, a_high, b_low, b_high);
  }
  Node* Word32PairShl(Node* low_word, Node* high_word, Node* shift) {
    return AddNode(machine()->Word32PairShl(), low_word, high_word, shift);
  }
  Node* Word32PairShr(Node* low_word, Node* high_word, Node* shift) {
    return AddNode(machine()->Word32PairShr(), low_word, high_word, shift);
  }
  Node* Word32PairSar(Node* low_word, Node* high_word, Node* shift) {
    return AddNode(machine()->Word32PairSar(), low_word, high_word, shift);
  }
  Node* Word32Popcnt(Node* a) {
    return AddNode(machine()->Word32Popcnt().op(), a);
  }
  Node* Word64Popcnt(Node* a) {
    return AddNode(machine()->Word64Popcnt().op(), a);
  }
  Node* Word32Ctz(Node* a) { return AddNode(machine()->Word32Ctz().op(), a); }
  Node* Word64Ctz(Node* a) { return AddNode(machine()->Word64Ctz().op(), a); }

  Node* Word32Select(Node* condition, Node* b, Node* c) {
    return AddNode(machine()->Word32Select().op(), condition, b, c);
  }

  Node* Word64Select(Node* condition, Node* b, Node* c) {
    return AddNode(machine()->Word64Select().op(), condition, b, c);
  }

  Node* StackPointerGreaterThan(Node* value) {
    return AddNode(
        machine()->StackPointerGreaterThan(StackCheckKind::kCodeStubAssembler),
        value);
  }

#define INTPTR_BINOP(prefix, name)                           \
  Node* IntPtr##name(Node* a, Node* b) {                     \
    return kSystemPointerSize == 8 ? prefix##64##name(a, b)  \
                                   : prefix##32##name(a, b); \
  }

  INTPTR_BINOP(Int, Add)
  INTPTR_BINOP(Int, AddWithOverflow)
  INTPTR_BINOP(Int, Sub)
  INTPTR_BINOP(Int, SubWithOverflow)
  INTPTR_BINOP(Int, Mul)
  INTPTR_BINOP(Int, MulHigh)
  INTPTR_BINOP(Int, MulWithOverflow)
  INTPTR_BINOP(Int, Div)
  INTPTR_BINOP(Int, Mod)
  INTPTR_BINOP(Int, LessThan)
  INTPTR_BINOP(Int, LessThanOrEqual)
  INTPTR_BINOP(Word, Equal)
  INTPTR_BINOP(Word, NotEqual)
  INTPTR_BINOP(Int, GreaterThanOrEqual)
  INTPTR_BINOP(Int, GreaterThan)

#undef INTPTR_BINOP

#define UINTPTR_BINOP(prefix, name)                          \
  Node* UintPtr##name(Node* a, Node* b) {                    \
    return kSystemPointerSize == 8 ? prefix##64##name(a, b)  \
                                   : prefix##32##name(a, b); \
  }

  UINTPTR_BINOP(Uint, LessThan)
  UINTPTR_BINOP(Uint, LessThanOrEqual)
  UINTPTR_BINOP(Uint, GreaterThanOrEqual)
  UINTPTR_BINOP(Uint, GreaterThan)
  UINTPTR_BINOP(Uint, MulHigh)

#undef UINTPTR_BINOP

  Node* Int32AbsWithOverflow(Node* a) {
    return AddNode(machine()->Int32AbsWithOverflow().op(), a);
  }

  Node* Int64AbsWithOverflow(Node* a) {
    return AddNode(machine()->Int64AbsWithOverflow().op(), a);
  }

  Node* IntPtrAbsWithOverflow(Node* a) {
    return kSystemPointerSize == 8 ? Int64AbsWithOverflow(a)
                                   : Int32AbsWithOverflow(a);
  }

  Node* Float32Add(Node* a, Node* b) {
    return AddNode(machine()->Float32Add(), a, b);
  }
  Node* Float32Sub(Node* a, Node* b) {
    return AddNode(machine()->Float32Sub(), a, b);
  }
  Node* Float32Mul(Node* a, Node* b) {
    return AddNode(machine()->Float32Mul(), a, b);
  }
  Node* Float32Div(Node* a, Node* b) {
    return AddNode(machine()->Float32Div(), a, b);
  }
  Node* Float32Abs(Node* a) { return AddNode(machine()->Float32Abs(), a); }
  Node* Float32Neg(Node* a) { return AddNode(machine()->Float32Neg(), a); }
  Node* Float32Sqrt(Node* a) { return AddNode(machine()->Float32Sqrt(), a); }
  Node* Float32Equal(Node* a, Node* b) {
    return AddNode(machine()->Float32Equal(), a, b);
  }
  Node* Float32NotEqual(Node* a, Node* b) {
    return Word32BinaryNot(Float32Equal(a, b));
  }
  Node* Float32LessThan(Node* a, Node* b) {
    return AddNode(machine()->Float32LessThan(), a, b);
  }
  Node* Float32LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Float32LessThanOrEqual(), a, b);
  }
  Node* Float32GreaterThan(Node* a, Node* b) { return Float32LessThan(b, a); }
  Node* Float32GreaterThanOrEqual(Node* a, Node* b) {
    return Float32LessThanOrEqual(b, a);
  }
  Node* Float32Max(Node* a, Node* b) {
    return AddNode(machine()->Float32Max(), a, b);
  }
  Node* Float32Min(Node* a, Node* b) {
    return AddNode(machine()->Float32Min(), a, b);
  }
  Node* Float64Add(Node* a, Node* b) {
    return AddNode(machine()->Float64Add(), a, b);
  }
  Node* Float64Sub(Node* a, Node* b) {
    return AddNode(machine()->Float64Sub(), a, b);
  }
  Node* Float64Mul(Node* a, Node* b) {
    return AddNode(machine()->Float64Mul(), a, b);
  }
  Node* Float64Div(Node* a, Node* b) {
    return AddNode(machine()->Float64Div(), a, b);
  }
  Node* Float64Mod(Node* a, Node* b) {
    return AddNode(machine()->Float64Mod(), a, b);
  }
  Node* Float64Max(Node* a, Node* b) {
    return AddNode(machine()->Float64Max(), a, b);
  }
  Node* Float64Min(Node* a, Node* b) {
    return AddNode(machine()->Float64Min(), a, b);
  }
  Node* Float64Abs(Node* a) { return AddNode(machine()->Float64Abs(), a); }
  Node* Float64Neg(Node* a) { return AddNode(machine()->Float64Neg(), a); }
  Node* Float64Acos(Node* a) { return AddNode(machine()->Float64Acos(), a); }
  Node* Float64Acosh(Node* a) { return AddNode(machine()->Float64Acosh(), a); }
  Node* Float64Asin(Node* a) { return AddNode(machine()->Float64Asin(), a); }
  Node* Float64Asinh(Node* a) { return AddNode(machine()->Float64Asinh(), a); }
  Node* Float64Atan(Node* a) { return AddNode(machine()->Float64Atan(), a); }
  Node* Float64Atanh(Node* a) { return AddNode(machine()->Float64Atanh(), a); }
  Node* Float64Atan2(Node* a, Node* b) {
    return AddNode(machine()->Float64Atan2(), a, b);
  }
  Node* Float64Cbrt(Node* a) { return AddNode(machine()->Float64Cbrt(), a); }
  Node* Float64Cos(Node* a) { return AddNode(machine()->Float64Cos(), a); }
  Node* Float64Cosh(Node* a) { return AddNode(machine()->Float64Cosh(), a); }
  Node* Float64Exp(Node* a) { return AddNode(machine()->Float64Exp(), a); }
  Node* Float64Expm1(Node* a) { return AddNode(machine()->Float64Expm1(), a); }
  Node* Float64Log(Node* a) { return AddNode(machine()->Float64Log(), a); }
  Node* Float64Log1p(Node* a) { return AddNode(machine()->Float64Log1p(), a); }
  Node* Float64Log10(Node* a) { return AddNode(machine()->Float64Log10(), a); }
  Node* Float64Log2(Node* a) { return AddNode(machine()->Float64Log2(), a); }
  Node* Float64Pow(Node* a, Node* b) {
    return AddNode(machine()->Float64Pow(), a, b);
  }
  Node* Float64Sin(Node* a) { return AddNode(machine()->Float64Sin(), a); }
  Node* Float64Sinh(Node* a) { return AddNode(machine()->Float64Sinh(), a); }
  Node* Float64Sqrt(Node* a) { return AddNode(machine()->Float64Sqrt(), a); }
  Node* Float64Tan(Node* a) { return AddNode(machine()->Float64Tan(), a); }
  Node* Float64Tanh(Node* a) { return AddNode(machine()->Float64Tanh(), a); }
  Node* Float64Equal(Node* a, Node* b) {
    return AddNode(machine()->Float64Equal(), a, b);
  }
  Node* Float64NotEqual(Node* a, Node* b) {
    return Word32BinaryNot(Float64Equal(a, b));
  }
  Node* Float64LessThan(Node* a, Node* b) {
    return AddNode(machine()->Float64LessThan(), a, b);
  }
  Node* Float64LessThanOrEqual(Node* a, Node* b) {
    return AddNode(machine()->Float64LessThanOrEqual(), a, b);
  }
  Node* Float64GreaterThan(Node* a, Node* b) { return Float64LessThan(b, a); }
  Node* Float64GreaterThanOrEqual(Node* a, Node* b) {
    return Float64LessThanOrEqual(b, a);
  }
  Node* Float32Select(Node* condition, Node* b, Node* c) {
    return AddNode(machine()->Float32Select().op(), condition, b, c);
  }
  Node* Float64Select(Node* condition, Node* b, Node* c) {
    return AddNode(machine()->Float64Select().op(), condition, b, c);
  }

  // Conversions.
  Node* BitcastTaggedToWord(Node* a) {
      return AddNode(machine()->BitcastTaggedToWord(), a);
  }
  Node* BitcastTaggedToWordForTagAndSmiBits(Node* a) {
    return AddNode(machine()->BitcastTaggedToWordForTagAndSmiBits(), a);
  }
  Node* BitcastMaybeObjectToWord(Node* a) {
    return AddNode(machine()->BitcastMaybeObjectToWord(), a);
  }
  Node* BitcastWordToTagged(Node* a) {
    return AddNode(machine()->BitcastWordToTagged(), a);
  }
  Node* BitcastWordToTaggedSigned(Node* a) {
      return AddNode(machine()->BitcastWordToTaggedSigned(), a);
  }
  Node* TruncateFloat64ToWord32(Node* a) {
    return AddNode(machine()->TruncateFloat64ToWord32(), a);
  }
  Node* ChangeFloat32ToFloat64(Node* a) {
    return AddNode(machine()->ChangeFloat32ToFloat64(), a);
  }
  Node* ChangeInt32ToFloat64(Node* a) {
    return AddNode(machine()->ChangeInt32ToFloat64(), a);
  }
  Node* ChangeInt64ToFloat64(Node* a) {
    return AddNode(machine()->ChangeInt64ToFloat64(), a);
  }
  Node* ChangeUint32ToFloat64(Node* a) {
    return AddNode(machine()->ChangeUint32ToFloat64(), a);
  }
  Node* ChangeFloat64ToInt32(Node* a) {
    return AddNode(machine()->ChangeFloat64ToInt32(), a);
  }
  Node* ChangeFloat64ToInt64(Node* a) {
    return AddNode(machine()->ChangeFloat64ToInt64(), a);
  }
  Node* ChangeFloat64ToUint32(Node* a) {
    return AddNode(machine()->ChangeFloat64ToUint32(), a);
  }
  Node* ChangeFloat64ToUint64(Node* a) {
    return AddNode(machine()->ChangeFloat64ToUint64(), a);
  }
  Node* TruncateFloat64ToUint32(Node* a) {
    return AddNode(machine()->TruncateFloat64ToUint32(), a);
  }
  Node* TruncateFloat32ToInt32(Node* a, TruncateKind kind) {
    return AddNode(machine()->TruncateFloat32ToInt32(kind), a);
  }
  Node* TruncateFloat32ToUint32(Node* a, TruncateKind kind) {
    return AddNode(machine()->TruncateFloat32ToUint32(kind), a);
  }
  Node* TruncateFloat64ToInt64(Node* a, TruncateKind kind) {
    return AddNode(machine()->TruncateFloat64ToInt64(kind), a);
  }
  Node* TryTruncateFloat32ToInt64(Node* a) {
    return AddNode(machine()->TryTruncateFloat32ToInt64(), a);
  }
  Node* TryTruncateFloat64ToInt64(Node* a) {
    return AddNode(machine()->TryTruncateFloat64ToInt64(), a);
  }
  Node* TryTruncateFloat32ToUint64(Node* a) {
    return AddNode(machine()->TryTruncateFloat32ToUint64(), a);
  }
  Node* TryTruncateFloat64ToUint64(Node* a) {
    return AddNode(machine()->TryTruncateFloat64ToUint64(), a);
  }
  Node* TryTruncateFloat64ToInt32(Node* a) {
    return AddNode(machine()->TryTruncateFloat64ToInt32(), a);
  }
  Node* TryTruncateFloat64ToUint32(Node* a) {
    return AddNode(machine()->TryTruncateFloat64ToUint32(), a);
  }
  Node* ChangeInt32ToInt64(Node* a) {
    return AddNode(machine()->ChangeInt32ToInt64(), a);
  }
  Node* ChangeUint32ToUint64(Node* a) {
    return AddNode(machine()->ChangeUint32ToUint64(), a);
  }
  Node* TruncateFloat64ToFloat32(Node* a) {
    return AddNode(machine()->TruncateFloat64ToFloat32(), a);
  }
  Node* TruncateInt64ToInt32(Node* a) {
    return AddNode(machine()->TruncateInt64ToInt32(), a);
  }
  Node* RoundFloat64ToInt32(Node* a) {
    return AddNode(machine()->RoundFloat64ToInt32(), a);
  }
  Node* RoundInt32ToFloat32(Node* a) {
    return AddNode(machine()->RoundInt32ToFloat32(), a);
  }
  Node* RoundInt64ToFloat32(Node* a) {
    return AddNode(machine()->RoundInt64ToFloat32(), a);
  }
  Node* RoundInt64ToFloat64(Node* a) {
    return AddNode(machine()->RoundInt64ToFloat64(), a);
  }
  Node* RoundUint32ToFloat32(Node* a) {
    return AddNode(machine()->RoundUint32ToFloat32(), a);
  }
  Node* RoundUint64ToFloat32(Node* a) {
    return AddNode(machine()->RoundUint64ToFloat32(), a);
  }
  Node* RoundUint64ToFloat64(Node* a) {
    return AddNode(machine()->RoundUint64ToFloat64(), a);
  }
  Node* BitcastFloat32ToInt32(Node* a) {
    return AddNode(machine()->BitcastFloat32ToInt32(), a);
  }
  Node* BitcastFloat64ToInt64(Node* a) {
    return AddNode(machine()->BitcastFloat64ToInt64(), a);
  }
  Node* BitcastInt32ToFloat32(Node* a) {
    return AddNode(machine()->BitcastInt32ToFloat32(), a);
  }
  Node* BitcastInt64ToFloat64(Node* a) {
    return AddNode(machine()->BitcastInt64ToFloat64(), a);
  }
  Node* Float32RoundDown(Node* a) {
    return AddNode(machine()->Float32RoundDown().op(), a);
  }
  Node* Float64RoundDown(Node* a) {
    return AddNode(machine()->Float64RoundDown().placeholder(), a);
  }
  Node* Float32RoundUp(Node* a) {
    return AddNode(machine()->Float32RoundUp().op(), a);
  }
  Node* Float64RoundUp(Node* a) {
    return AddNode(machine()->Float64RoundUp().placeholder(), a);
  }
  Node* Float32RoundTruncate(Node* a) {
    return AddNode(machine()->Float32RoundTruncate().op(), a);
  }
  Node* Float64RoundTruncate(Node* a) {
    return AddNode(machine()->Float64RoundTruncate().placeholder(), a);
  }
  Node* Float64RoundTiesAway(Node* a) {
    return AddNode(machine()->Float64RoundTiesAway().op(), a);
  }
  Node* Float32RoundTiesEven(Node* a) {
    return AddNode(machine()->Float32RoundTiesEven().op(), a);
  }
  Node* Float64RoundTiesEven(Node* a) {
    return AddNode(machine()->Float64RoundTiesEven().placeholder(), a);
  }
  Node* Word32ReverseBytes(Node* a) {
    return AddNode(machine()->Word32ReverseBytes(), a);
  }
  Node* Word64ReverseBytes(Node* a) {
    return AddNode(machine()->Word64ReverseBytes(), a);
  }

  // Float64 bit operations.
  Node* Float64ExtractLowWord32(Node* a) {
    return AddNode(machine()->Float64ExtractLowWord32(), a);
  }
  Node* Float64ExtractHighWord32(Node* a) {
    return AddNode(machine()->Float64ExtractHighWord32(), a);
  }
  Node* Float64InsertLowWord32(Node* a, Node* b) {
    return AddNode(machine()->Float64InsertLowWord32(), a, b);
  }
  Node* Float64InsertHighWord32(Node* a, Node* b) {
    return AddNode(machine()->Float64InsertHighWord32(), a, b);
  }
  Node* Float64SilenceNaN(Node* a) {
    return AddNode(machine()->Float64SilenceNaN(), a);
  }

  // Stack operations.
  Node* LoadFramePointer() { return AddNode(machine()->LoadFramePointer()); }
  Node* LoadParentFramePointer() {
    return AddNode(machine()->LoadParentFramePointer());
  }

  // SIMD operations that are needed outside of Wasm (e.g. in swisstable).
  Node* I8x16Splat(Node* a) { return AddNode(machine()->I8x16Splat(), a); }
  Node* I8x16BitMask(Node* a) { return AddNode(machine()->I8x16BitMask(), a); }
  Node* I8x16Eq(Node* a, Node* b) {
    return AddNode(machine()->I8x16Eq(), a, b);
  }

#if V8_ENABLE_WEBASSEMBLY
  // SIMD operations.
  Node* S128Const(const uint8_t value[16]) {
    return AddNode(machine()->S128Const(value));
  }
  Node* I64x2Splat(Node* a) { return AddNode(machine()->I64x2Splat(), a); }
  Node* I64x2SplatI32Pair(Node* a, Node* b) {
    return AddNode(machine()->I64x2SplatI32Pair(), a, b);
  }
  Node* I32x4Splat(Node* a) { return AddNode(machine()->I32x4Splat(), a); }
  Node* I16x8Splat(Node* a) { return AddNode(machine()->I16x8Splat(), a); }

  Node* LoadStackPointer() { return AddNode(machine()->LoadStackPointer()); }
  void SetStackPointer(Node* ptr, wasm::FPRelativeScope fp_scope) {
    AddNode(machine()->SetStackPointer(fp_scope), ptr);
  }
#endif

  // Parameters.
  Node* TargetParameter();
  Node* Parameter(size_t index);
  Node* LoadRootRegister() { return AddNode(machine()->LoadRootRegister()); }

  // Pointer utilities.
  Node* LoadFromPointer(void* address, MachineType type, int32_t offset = 0) {
    return Load(type, PointerConstant(address), Int32Constant(offset));
  }
  Node* StoreToPointer(void* address, MachineRepresentation rep, Node* node) {
    return Store(rep, PointerConstant(address), node, kNoWriteBarrier);
  }
  Node* UnalignedLoadFromPointer(void* address, MachineType type,
                                 int32_t offset = 0) {
    return UnalignedLoad(type, PointerConstant(address), Int32Constant(offset));
  }
  Node* UnalignedStoreToPointer(void* address, MachineRepresentation rep,
                                Node* node) {
    return UnalignedStore(rep, PointerConstant(address), node);
  }
  Node* StringConstant(const char* string) {
    return HeapConstant(isolate()->factory()->InternalizeUtf8String(string));
  }

  // Call a given call descriptor and the given arguments.
  // The call target is passed as part of the {inputs} array.
  Node* CallN(CallDescriptor* call_descriptor, int input_count,
              Node* const* inputs);

  // Call a given call descriptor and the given arguments and frame-state.
  // The call target and frame state are passed as part of the {inputs} array.
  Node* CallNWithFrameState(CallDescriptor* call_descriptor, int input_count,
                            Node* const* inputs);

  // Tail call a given call descriptor and the given arguments.
  // The call target is passed as part of the {inputs} array.
  void TailCallN(CallDescriptor* call_descriptor, int input_count,
                 Node* const* inputs);

  // Type representing C function argument with type info.
  using CFunctionArg = std::pair<MachineType, Node*>;

  // Call to a C function.
  template <class... CArgs>
  Node* CallCFunction(Node* function, base::Optional<MachineType> return_type,
                      CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunction(function, return_type, {cargs...});
  }

  Node* CallCFunction(Node* function, base::Optional<MachineType> return_type,
                      std::initializer_list<CFunctionArg> args);

  // Call to a C function without a function discriptor on AIX.
  template <class... CArgs>
  Node* CallCFunctionWithoutFunctionDescriptor(Node* function,
                                               MachineType return_type,
                                               CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunctionWithoutFunctionDescriptor(function, return_type,
                                                  {cargs...});
  }

  Node* CallCFunctionWithoutFunctionDescriptor(
      Node* function, MachineType return_type,
      std::initializer_list<CFunctionArg> args);

  // Call to a C function, while saving/restoring caller registers.
  template <class... CArgs>
  Node* CallCFunctionWithCallerSavedRegisters(Node* function,
                                              MachineType return_type,
                                              SaveFPRegsMode mode,
                                              CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunctionWithCallerSavedRegisters(function, return_type, mode,
                                                 {cargs...});
  }

  Node* CallCFunctionWithCallerSavedRegisters(
      Node* function, MachineType return_type, SaveFPRegsMode mode,
      std::initializer_list<CFunctionArg> args);

  // ===========================================================================
  // The following utility methods deal with control flow, hence might switch
  // the current basic block or create new basic blocks for labels.

  // Control flow.
  void Goto(RawMachineLabel* label);
  void Branch(Node* condition, RawMachineLabel* true_val,
              RawMachineLabel* false_val);
  void Switch(Node* index, RawMachineLabel* default_label,
              const int32_t* case_values, RawMachineLabel** case_labels,
              size_t case_count);
  void Return(Node* value);
  void Return(Node* v1, Node* v2);
  void Return(Node* v1, Node* v2, Node* v3);
  void Return(Node* v1, Node* v2, Node* v3, Node* v4);
  void Return(int count, Node* v[]);
  void PopAndReturn(Node* pop, Node* value);
  void PopAndReturn(Node* pop, Node* v1, Node* v2);
  void PopAndReturn(Node* pop, Node* v1, Node* v2, Node* v3);
  void PopAndReturn(Node* pop, Node* v1, Node* v2, Node* v3, Node* v4);
  void Bind(RawMachineLabel* label);
  void Deoptimize(Node* state);
  void AbortCSADcheck(Node* message);
  void DebugBreak();
  void Unreachable();
  void Comment(const std::string& msg);
  void StaticAssert(Node* value, const char* source);

#if DEBUG
  void Bind(RawMachineLabel* label, AssemblerDebugInfo info);
  void SetInitialDebugInformation(AssemblerDebugInfo info);
  void PrintCurrentBlock(std::ostream& os);
#endif  // DEBUG
  bool InsideBlock();

  // Add success / exception successor blocks and ends the current block ending
  // in a potentially throwing call node.
  void Continuations(Node* call, RawMachineLabel* if_success,
                     RawMachineLabel* if_exception);

  // Variables.
  Node* Phi(MachineRepresentation rep, Node* n1, Node* n2) {
    return AddNode(common()->Phi(rep, 2), n1, n2, graph()->start());
  }
  Node* Phi(MachineRepresentation rep, Node* n1, Node* n2, Node* n3) {
    return AddNode(common()->Phi(rep, 3), n1, n2, n3, graph()->start());
  }
  Node* Phi(MachineRepresentation rep, Node* n1, Node* n2, Node* n3, Node* n4) {
    return AddNode(common()->Phi(rep, 4), n1, n2, n3, n4, graph()->start());
  }
  Node* Phi(MachineRepresentation rep, int input_count, Node* const* inputs);
  void AppendPhiInput(Node* phi, Node* new_input);

  // ===========================================================================
  // The following generic node creation methods can be used for operators that
  // are not covered by the above utility methods. There should rarely be a need
  // to do that outside of testing though.

  Node* AddNode(const Operator* op, int input_count, Node* const* inputs);

  Node* AddNode(const Operator* op) {
    return AddNode(op, 0, static_cast<Node* const*>(nullptr));
  }

  template <class... TArgs>
  Node* AddNode(const Operator* op, Node* n1, TArgs... args) {
    Node* buffer[] = {n1, args...};
    return AddNode(op, sizeof...(args) + 1, buffer);
  }

  void SetCurrentExternalSourcePosition(FileAndLine file_and_line);
  FileAndLine GetCurrentExternalSourcePosition() const;
  SourcePositionTable* source_positions() { return source_positions_; }

 private:
  Node* MakeNode(const Operator* op, int input_count, Node* const* inputs);
  BasicBlock* Use(RawMachineLabel* label);
  BasicBlock* EnsureBlock(RawMachineLabel* label);
  BasicBlock* CurrentBlock();

  // A post-processing pass to add effect and control edges so that the graph
  // can be optimized and re-scheduled.
  // TODO(turbofan): Move this to a separate class.
  void MakeReschedulable();
  Node* CreateNodeFromPredecessors(const std::vector<BasicBlock*>& predecessors,
                                   const std::vector<Node*>& sidetable,
                                   const Operator* op,
                                   const std::vector<Node*>& additional_inputs);
  void MakePhiBinary(Node* phi, int split_point, Node* left_control,
                     Node* right_control);
  void MarkControlDeferred(Node* control_input);

  Schedule* schedule() { return schedule_; }
  size_t parameter_count() const { return call_descriptor_->ParameterCount(); }

  static void OptimizeControlFlow(Schedule* schedule, Graph* graph,
                                  CommonOperatorBuilder* common);

  Isolate* isolate_;

  Graph* graph_;
  Schedule* schedule_;
  SourcePositionTable* source_positions_;
  MachineOperatorBuilder machine_;
  CommonOperatorBuilder common_;
  SimplifiedOperatorBuilder simplified_;
  CallDescriptor* call_descriptor_;
  Node* target_parameter_;
  NodeVector parameters_;
  BasicBlock* current_block_;
};

class V8_EXPORT_PRIVATE RawMachineLabel final {
 public:
  enum Type { kDeferred, kNonDeferred };

  explicit RawMachineLabel(Type type = kNonDeferred)
      : deferred_(type == kDeferred) {}
  ~RawMachineLabel();
  RawMachineLabel(const RawMachineLabel&) = delete;
  RawMachineLabel& operator=(const RawMachineLabel&) = delete;

  BasicBlock* block() const { return block_; }

 private:
  BasicBlock* block_ = nullptr;
  bool used_ = false;
  bool bound_ = false;
  bool deferred_;
  friend class RawMachineAssembler;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_RAW_MACHINE_ASSEMBLER_H_
