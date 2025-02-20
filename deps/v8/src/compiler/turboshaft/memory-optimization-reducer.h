// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_REDUCER_H_

#include <optional>

#include "src/base/template-utils.h"
#include "src/builtins/builtins.h"
#include "src/codegen/external-reference.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

const TSCallDescriptor* CreateAllocateBuiltinDescriptor(Zone* zone,
                                                        Isolate* isolate);

inline bool ValueNeedsWriteBarrier(const Graph* graph, const Operation& value,
                                   Isolate* isolate) {
  if (value.Is<Opmask::kBitcastWordPtrToSmi>()) {
    return false;
  } else if (const ConstantOp* constant = value.TryCast<ConstantOp>()) {
    if (constant->kind == ConstantOp::Kind::kHeapObject) {
      RootIndex root_index;
      if (isolate->roots_table().IsRootHandle(constant->handle(),
                                              &root_index) &&
          RootsTable::IsImmortalImmovable(root_index)) {
        return false;
      }
    }
  } else if (const PhiOp* phi = value.TryCast<PhiOp>()) {
    if (phi->rep == RegisterRepresentation::Tagged()) {
      return base::any_of(phi->inputs(), [graph, isolate](OpIndex input) {
        const Operation& input_op = graph->Get(input);
        // If we have a Phi as the Phi's input, we give up to avoid infinite
        // recursion.
        if (input_op.Is<PhiOp>()) return true;
        return ValueNeedsWriteBarrier(graph, input_op, isolate);
      });
    }
  }
  return true;
}

inline const AllocateOp* UnwrapAllocate(const Graph* graph,
                                        const Operation* op) {
  while (true) {
    if (const AllocateOp* allocate = op->TryCast<AllocateOp>()) {
      return allocate;
    } else if (const TaggedBitcastOp* bitcast =
                   op->TryCast<TaggedBitcastOp>()) {
      op = &graph->Get(bitcast->input());
    } else if (const WordBinopOp* binop = op->TryCast<WordBinopOp>();
               binop && binop->kind == any_of(WordBinopOp::Kind::kAdd,
                                              WordBinopOp::Kind::kSub)) {
      op = &graph->Get(binop->left());
    } else {
      return nullptr;
    }
  }
}

// The main purpose of memory optimization is folding multiple allocations
// into one. For this, the first allocation reserves additional space, that is
// consumed by subsequent allocations, which only move the allocation top
// pointer and are therefore guaranteed to succeed. Another nice side-effect
// of allocation folding is that more stores are performed on the most recent
// allocation, which allows us to eliminate the write barrier for the store.
//
// This analysis works by keeping track of the most recent non-folded
// allocation, as well as the number of bytes this allocation needs to reserve
// to satisfy all subsequent allocations.
// We can do write barrier elimination across loops if the loop does not
// contain any potentially allocating operations.
struct MemoryAnalyzer {
  enum class AllocationFolding { kDoAllocationFolding, kDontAllocationFolding };

  PipelineData* data;
  Zone* phase_zone;
  const Graph& input_graph;
  Isolate* isolate_ = data->isolate();
  AllocationFolding allocation_folding;
  bool is_wasm;
  MemoryAnalyzer(PipelineData* data, Zone* phase_zone, const Graph& input_graph,
                 AllocationFolding allocation_folding, bool is_wasm)
      : data(data),
        phase_zone(phase_zone),
        input_graph(input_graph),
        allocation_folding(allocation_folding),
        is_wasm(is_wasm) {}

  struct BlockState {
    const AllocateOp* last_allocation = nullptr;
    std::optional<uint32_t> reserved_size = std::nullopt;

    bool operator!=(const BlockState& other) {
      return last_allocation != other.last_allocation ||
             reserved_size != other.reserved_size;
    }
  };
  FixedBlockSidetable<std::optional<BlockState>> block_states{
      input_graph.block_count(), phase_zone};
  ZoneAbslFlatHashMap<const AllocateOp*, const AllocateOp*> folded_into{
      phase_zone};
  ZoneAbslFlatHashSet<V<None>> skipped_write_barriers{phase_zone};
  ZoneAbslFlatHashMap<const AllocateOp*, uint32_t> reserved_size{phase_zone};
  BlockIndex current_block = BlockIndex(0);
  BlockState state;
  TurboshaftPipelineKind pipeline_kind = data->pipeline_kind();

  bool IsPartOfLastAllocation(const Operation* op) {
    const AllocateOp* allocation = UnwrapAllocate(&input_graph, op);
    if (allocation == nullptr) return false;
    if (state.last_allocation == nullptr) return false;
    if (state.last_allocation->type != AllocationType::kYoung) return false;
    if (state.last_allocation == allocation) return true;
    auto it = folded_into.find(allocation);
    if (it == folded_into.end()) return false;
    return it->second == state.last_allocation;
  }

  bool SkipWriteBarrier(const StoreOp& store) {
    const Operation& object = input_graph.Get(store.base());
    const Operation& value = input_graph.Get(store.value());

    WriteBarrierKind write_barrier_kind = store.write_barrier;
    if (write_barrier_kind != WriteBarrierKind::kAssertNoWriteBarrier) {
      // If we have {kAssertNoWriteBarrier}, we cannot skip elimination
      // checks.
      if (ShouldSkipOptimizationStep()) return false;
    }
    if (IsPartOfLastAllocation(&object)) return true;
    if (!ValueNeedsWriteBarrier(&input_graph, value, isolate_)) return true;
    if (v8_flags.disable_write_barriers) return true;
    if (write_barrier_kind == WriteBarrierKind::kAssertNoWriteBarrier) {
      std::stringstream str;
      str << "MemoryOptimizationReducer could not remove write barrier for "
             "operation\n  #"
          << input_graph.Index(store) << ": " << store.ToString() << "\n";
      FATAL("%s", str.str().c_str());
    }
    return false;
  }

  bool IsFoldedAllocation(V<AnyOrNone> op) {
    return folded_into.count(
        input_graph.Get(op).template TryCast<AllocateOp>());
  }

  std::optional<uint32_t> ReservedSize(V<AnyOrNone> alloc) {
    if (auto it = reserved_size.find(
            input_graph.Get(alloc).template TryCast<AllocateOp>());
        it != reserved_size.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void Run();

  void Process(const Operation& op);
  void ProcessBlockTerminator(const Operation& op);
  void ProcessAllocation(const AllocateOp& alloc);
  void ProcessStore(const StoreOp& store);
  void MergeCurrentStateIntoSuccessor(const Block* successor);
};

template <class Next>
class MemoryOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(MemoryOptimization)
  // TODO(dmercadier): Add static_assert that this is ran as part of a
  // CopyingPhase.

  void Analyze() {
    auto* info = __ data() -> info();
#if V8_ENABLE_WEBASSEMBLY
    bool is_wasm = info->IsWasm() || info->IsWasmBuiltin();
#else
    bool is_wasm = false;
#endif
    analyzer_.emplace(
        __ data(), __ phase_zone(), __ input_graph(),
        info->allocation_folding()
            ? MemoryAnalyzer::AllocationFolding::kDoAllocationFolding
            : MemoryAnalyzer::AllocationFolding::kDontAllocationFolding,
        is_wasm);
    analyzer_->Run();
    Next::Analyze();
  }

  V<None> REDUCE_INPUT_GRAPH(Store)(V<None> ig_index, const StoreOp& store) {
    if (store.write_barrier != WriteBarrierKind::kAssertNoWriteBarrier) {
      // We cannot skip this optimization if we have to eliminate a
      // {kAssertNoWriteBarrier}.
      if (ShouldSkipOptimizationStep()) {
        return Next::ReduceInputGraphStore(ig_index, store);
      }
    }
    if (analyzer_->skipped_write_barriers.count(ig_index)) {
      __ Store(__ MapToNewGraph(store.base()), __ MapToNewGraph(store.index()),
               __ MapToNewGraph(store.value()), store.kind, store.stored_rep,
               WriteBarrierKind::kNoWriteBarrier, store.offset,
               store.element_size_log2,
               store.maybe_initializing_or_transitioning,
               store.indirect_pointer_tag());
      return V<None>::Invalid();
    }
    DCHECK_NE(store.write_barrier, WriteBarrierKind::kAssertNoWriteBarrier);
    return Next::ReduceInputGraphStore(ig_index, store);
  }

  V<HeapObject> REDUCE(Allocate)(V<WordPtr> size, AllocationType type) {
    DCHECK_EQ(type, any_of(AllocationType::kYoung, AllocationType::kOld));

    if (v8_flags.single_generation && type == AllocationType::kYoung) {
      type = AllocationType::kOld;
    }

    V<WordPtr> top_address;
    if (isolate_ != nullptr) {
      top_address = __ ExternalConstant(
          type == AllocationType::kYoung
              ? ExternalReference::new_space_allocation_top_address(isolate_)
              : ExternalReference::old_space_allocation_top_address(isolate_));
    } else {
      // Wasm mode: producing isolate-independent code, loading the isolate
      // address at runtime.
#if V8_ENABLE_WEBASSEMBLY
      V<WasmTrustedInstanceData> instance_data = __ WasmInstanceDataParameter();
      int top_address_offset =
          type == AllocationType::kYoung
              ? WasmTrustedInstanceData::kNewAllocationTopAddressOffset
              : WasmTrustedInstanceData::kOldAllocationTopAddressOffset;
      top_address =
          __ Load(instance_data, LoadOp::Kind::TaggedBase().Immutable(),
                  MemoryRepresentation::UintPtr(), top_address_offset);
#else
      UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
    }

    if (analyzer_->IsFoldedAllocation(__ current_operation_origin())) {
      DCHECK_NE(__ GetVariable(top(type)), V<WordPtr>::Invalid());
      V<WordPtr> obj_addr = __ GetVariable(top(type));
      __ SetVariable(top(type), __ WordPtrAdd(__ GetVariable(top(type)), size));
      __ StoreOffHeap(top_address, __ GetVariable(top(type)),
                      MemoryRepresentation::UintPtr());
      return __ BitcastWordPtrToHeapObject(
          __ WordPtrAdd(obj_addr, __ IntPtrConstant(kHeapObjectTag)));
    }

    __ SetVariable(top(type), __ LoadOffHeap(top_address,
                                             MemoryRepresentation::UintPtr()));

    V<CallTarget> allocate_builtin;
    if (!analyzer_->is_wasm) {
      if (type == AllocationType::kYoung) {
        allocate_builtin =
            __ BuiltinCode(Builtin::kAllocateInYoungGeneration, isolate_);
      } else {
        allocate_builtin =
            __ BuiltinCode(Builtin::kAllocateInOldGeneration, isolate_);
      }
    } else {
#if V8_ENABLE_WEBASSEMBLY
      // This lowering is used by Wasm, where we compile isolate-independent
      // code. Builtin calls simply encode the target builtin ID, which will
      // be patched to the builtin's address later.
      if (isolate_ == nullptr) {
        Builtin builtin;
        if (type == AllocationType::kYoung) {
          builtin = Builtin::kWasmAllocateInYoungGeneration;
        } else {
          builtin = Builtin::kWasmAllocateInOldGeneration;
        }
        static_assert(std::is_same<Smi, BuiltinPtr>(),
                      "BuiltinPtr must be Smi");
        allocate_builtin = __ NumberConstant(static_cast<int>(builtin));
      } else {
        if (type == AllocationType::kYoung) {
          allocate_builtin =
              __ BuiltinCode(Builtin::kWasmAllocateInYoungGeneration, isolate_);
        } else {
          allocate_builtin =
              __ BuiltinCode(Builtin::kWasmAllocateInOldGeneration, isolate_);
        }
      }
#else
      UNREACHABLE();
#endif
    }

    Block* call_runtime = __ NewBlock();
    Block* done = __ NewBlock();

    V<WordPtr> limit_address = GetLimitAddress(type);

    // If the allocation size is not statically known or is known to be larger
    // than kMaxRegularHeapObjectSize, do not update {top(type)} in case of a
    // runtime call. This is needed because we cannot allocation-fold large and
    // normal-sized objects.
    uint64_t constant_size{};
    if (!__ matcher().MatchIntegralWordConstant(
            size, WordRepresentation::WordPtr(), &constant_size) ||
        constant_size > kMaxRegularHeapObjectSize) {
      Variable result =
          __ NewLoopInvariantVariable(RegisterRepresentation::Tagged());
      if (!constant_size) {
        // Check if we can do bump pointer allocation here.
        V<WordPtr> top_value = __ GetVariable(top(type));
        __ SetVariable(result,
                       __ BitcastWordPtrToHeapObject(__ WordPtrAdd(
                           top_value, __ IntPtrConstant(kHeapObjectTag))));
        V<WordPtr> new_top = __ WordPtrAdd(top_value, size);
        V<WordPtr> limit =
            __ LoadOffHeap(limit_address, MemoryRepresentation::UintPtr());
        __ GotoIfNot(LIKELY(__ UintPtrLessThan(new_top, limit)), call_runtime);
        __ GotoIfNot(LIKELY(__ UintPtrLessThan(
                         size, __ IntPtrConstant(kMaxRegularHeapObjectSize))),
                     call_runtime);
        __ SetVariable(top(type), new_top);
        __ StoreOffHeap(top_address, new_top, MemoryRepresentation::UintPtr());
        __ Goto(done);
      }
      if (constant_size || __ Bind(call_runtime)) {
        __ SetVariable(
            result, __ template Call<HeapObject>(allocate_builtin, {size},
                                                 AllocateBuiltinDescriptor()));
        __ Goto(done);
      }

      __ BindReachable(done);
      return __ GetVariable(result);
    }

    V<WordPtr> reservation_size;
    if (auto c = analyzer_->ReservedSize(__ current_operation_origin())) {
      reservation_size = __ UintPtrConstant(*c);
    } else {
      reservation_size = size;
    }
    // Check if we can do bump pointer allocation here.
    bool reachable =
        __ GotoIfNot(__ UintPtrLessThan(
                         size, __ IntPtrConstant(kMaxRegularHeapObjectSize)),
                     call_runtime, BranchHint::kTrue) !=
        ConditionalGotoStatus::kGotoDestination;
    if (reachable) {
      V<WordPtr> limit =
          __ LoadOffHeap(limit_address, MemoryRepresentation::UintPtr());
      __ Branch(__ UintPtrLessThan(
                    __ WordPtrAdd(__ GetVariable(top(type)), reservation_size),
                    limit),
                done, call_runtime, BranchHint::kTrue);
    }

    // Call the runtime if bump pointer area exhausted.
    if (__ Bind(call_runtime)) {
      V<HeapObject> allocated = __ template Call<HeapObject>(
          allocate_builtin, {reservation_size}, AllocateBuiltinDescriptor());
      __ SetVariable(top(type),
                     __ WordPtrSub(__ BitcastHeapObjectToWordPtr(allocated),
                                   __ IntPtrConstant(kHeapObjectTag)));
      __ Goto(done);
    }

    __ BindReachable(done);
    // Compute the new top and write it back.
    V<WordPtr> obj_addr = __ GetVariable(top(type));
    __ SetVariable(top(type), __ WordPtrAdd(__ GetVariable(top(type)), size));
    __ StoreOffHeap(top_address, __ GetVariable(top(type)),
                    MemoryRepresentation::UintPtr());
    return __ BitcastWordPtrToHeapObject(
        __ WordPtrAdd(obj_addr, __ IntPtrConstant(kHeapObjectTag)));
  }

  OpIndex REDUCE(DecodeExternalPointer)(OpIndex handle,
                                        ExternalPointerTagRange tag_range) {
#ifdef V8_ENABLE_SANDBOX
    // Decode loaded external pointer.
    V<WordPtr> table;
    if (isolate_ != nullptr) {
      // Here we access the external pointer table through an ExternalReference.
      // Alternatively, we could also hardcode the address of the table since it
      // is never reallocated. However, in that case we must be able to
      // guarantee that the generated code is never executed under a different
      // Isolate, as that would allow access to external objects from different
      // Isolates. It also would break if the code is serialized/deserialized at
      // some point.
      V<WordPtr> table_address =
          IsSharedExternalPointerType(tag_range)
              ? __
                LoadOffHeap(
                    __ ExternalConstant(
                        ExternalReference::
                            shared_external_pointer_table_address_address(
                                isolate_)),
                    MemoryRepresentation::UintPtr())
              : __ ExternalConstant(
                    ExternalReference::external_pointer_table_address(
                        isolate_));
      table = __ LoadOffHeap(table_address,
                             Internals::kExternalPointerTableBasePointerOffset,
                             MemoryRepresentation::UintPtr());
    } else {
#if V8_ENABLE_WEBASSEMBLY
      V<WordPtr> isolate_root = __ LoadRootRegister();
      if (IsSharedExternalPointerType(tag_range)) {
        V<WordPtr> table_address =
            __ Load(isolate_root, LoadOp::Kind::RawAligned(),
                    MemoryRepresentation::UintPtr(),
                    IsolateData::shared_external_pointer_table_offset());
        table = __ Load(table_address, LoadOp::Kind::RawAligned(),
                        MemoryRepresentation::UintPtr(),
                        Internals::kExternalPointerTableBasePointerOffset);
      } else {
        table = __ Load(isolate_root, LoadOp::Kind::RawAligned(),
                        MemoryRepresentation::UintPtr(),
                        IsolateData::external_pointer_table_offset() +
                            Internals::kExternalPointerTableBasePointerOffset);
      }
#else
      UNREACHABLE();
#endif
    }

    V<Word32> index =
        __ Word32ShiftRightLogical(handle, kExternalPointerIndexShift);
    V<Word64> pointer = __ LoadOffHeap(table, __ ChangeUint32ToUint64(index), 0,
                                       MemoryRepresentation::Uint64());

    // We don't expect to see empty fields here. If this is ever needed,
    // consider using an dedicated empty value entry for those tags instead
    // (i.e. an entry with the right tag and nullptr payload).
    DCHECK(!ExternalPointerCanBeEmpty(tag_range));

    Block* done = __ NewBlock();
    if (tag_range.Size() == 1) {
      // The common and simple case: we expect a specific tag.
      V<Word64> tag_bits = __ Word64BitwiseAnd(
          pointer, __ Word64Constant(kExternalPointerTagMask));
      tag_bits = __ Word64ShiftRightLogical(tag_bits, kExternalPointerTagShift);
      V<Word32> tag = __ TruncateWord64ToWord32(tag_bits);
      V<Word32> expected_tag = __ Word32Constant(tag_range.first);
      __ GotoIf(__ Word32Equal(tag, expected_tag), done, BranchHint::kTrue);
      // TODO(saelo): it would be nicer to abort here with
      // AbortReason::kExternalPointerTagMismatch. That might require adding a
      // builtin call here though, which is not currently available.
      __ Unreachable();
    } else {
      // Not currently supported. Implement once needed.
      DCHECK_NE(tag_range, kAnyExternalPointerTagRange);
      UNREACHABLE();
    }
    __ BindReachable(done);
    return __ Word64BitwiseAnd(pointer, kExternalPointerPayloadMask);
#else   // V8_ENABLE_SANDBOX
    UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
  }

 private:
  std::optional<MemoryAnalyzer> analyzer_;
  Isolate* isolate_ = __ data() -> isolate();
  const TSCallDescriptor* allocate_builtin_descriptor_ = nullptr;
  std::optional<Variable> top_[2];

  static_assert(static_cast<int>(AllocationType::kYoung) == 0);
  static_assert(static_cast<int>(AllocationType::kOld) == 1);
  Variable top(AllocationType type) {
    DCHECK(type == AllocationType::kYoung || type == AllocationType::kOld);
    if (V8_UNLIKELY(!top_[static_cast<int>(type)].has_value())) {
      top_[static_cast<int>(type)].emplace(
          __ NewLoopInvariantVariable(RegisterRepresentation::WordPtr()));
    }
    return top_[static_cast<int>(type)].value();
  }

  const TSCallDescriptor* AllocateBuiltinDescriptor() {
    if (allocate_builtin_descriptor_ == nullptr) {
      allocate_builtin_descriptor_ =
          CreateAllocateBuiltinDescriptor(__ graph_zone(), isolate_);
    }
    return allocate_builtin_descriptor_;
  }

  V<WordPtr> GetLimitAddress(AllocationType type) {
    V<WordPtr> limit_address;
    if (isolate_ != nullptr) {
      limit_address = __ ExternalConstant(
          type == AllocationType::kYoung
              ? ExternalReference::new_space_allocation_limit_address(isolate_)
              : ExternalReference::old_space_allocation_limit_address(
                    isolate_));
    } else {
      // Wasm mode: producing isolate-independent code, loading the isolate
      // address at runtime.
#if V8_ENABLE_WEBASSEMBLY
      V<WasmTrustedInstanceData> instance_node = __ WasmInstanceDataParameter();
      int limit_address_offset =
          type == AllocationType::kYoung
              ? WasmTrustedInstanceData::kNewAllocationLimitAddressOffset
              : WasmTrustedInstanceData::kOldAllocationLimitAddressOffset;
      limit_address =
          __ Load(instance_node, LoadOp::Kind::TaggedBase(),
                  MemoryRepresentation::UintPtr(), limit_address_offset);
#else
      UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    return limit_address;
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_REDUCER_H_
