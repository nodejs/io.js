// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_H_
#define V8_MAGLEV_MAGLEV_GRAPH_H_

#include <vector>

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/const-tracking-let-helpers.h"
#include "src/compiler/heap-refs.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

using BlockConstIterator = ZoneVector<BasicBlock*>::const_iterator;
using BlockConstReverseIterator =
    ZoneVector<BasicBlock*>::const_reverse_iterator;

class Graph final : public ZoneObject {
 public:
  static Graph* New(Zone* zone, bool is_osr) {
    return zone->New<Graph>(zone, is_osr);
  }

  // Shouldn't be used directly; public so that Zone::New can access it.
  Graph(Zone* zone, bool is_osr)
      : blocks_(zone),
        root_(zone),
        osr_values_(zone),
        smi_(zone),
        tagged_index_(zone),
        int32_(zone),
        uint32_(zone),
        float_(zone),
        external_references_(zone),
        parameters_(zone),
        allocations_escape_map_(zone),
        allocations_elide_map_(zone),
        register_inputs_(),
        constants_(zone),
        trusted_constants_(zone),
        inlined_functions_(zone),
        is_osr_(is_osr),
        scope_infos_(zone) {}

  BasicBlock* operator[](int i) { return blocks_[i]; }
  const BasicBlock* operator[](int i) const { return blocks_[i]; }

  int num_blocks() const { return static_cast<int>(blocks_.size()); }
  ZoneVector<BasicBlock*>& blocks() { return blocks_; }

  BlockConstIterator begin() const { return blocks_.begin(); }
  BlockConstIterator end() const { return blocks_.end(); }
  BlockConstReverseIterator rbegin() const { return blocks_.rbegin(); }
  BlockConstReverseIterator rend() const { return blocks_.rend(); }

  BasicBlock* last_block() const { return blocks_.back(); }

  void Add(BasicBlock* block) { blocks_.push_back(block); }

  void set_blocks(ZoneVector<BasicBlock*> blocks) { blocks_ = blocks; }

  uint32_t tagged_stack_slots() const { return tagged_stack_slots_; }
  uint32_t untagged_stack_slots() const { return untagged_stack_slots_; }
  uint32_t max_call_stack_args() const { return max_call_stack_args_; }
  uint32_t max_deopted_stack_size() const { return max_deopted_stack_size_; }
  void set_tagged_stack_slots(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, tagged_stack_slots_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    tagged_stack_slots_ = stack_slots;
  }
  void set_untagged_stack_slots(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, untagged_stack_slots_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    untagged_stack_slots_ = stack_slots;
  }
  void set_max_call_stack_args(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, max_call_stack_args_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    max_call_stack_args_ = stack_slots;
  }
  void set_max_deopted_stack_size(uint32_t size) {
    DCHECK_EQ(kMaxUInt32, max_deopted_stack_size_);
    DCHECK_NE(kMaxUInt32, size);
    max_deopted_stack_size_ = size;
  }

  int total_inlined_bytecode_size() const {
    return total_inlined_bytecode_size_;
  }
  void add_inlined_bytecode_size(int size) {
    total_inlined_bytecode_size_ += size;
  }

  ZoneMap<RootIndex, RootConstant*>& root() { return root_; }
  ZoneVector<InitialValue*>& osr_values() { return osr_values_; }
  ZoneMap<int, SmiConstant*>& smi() { return smi_; }
  ZoneMap<int, TaggedIndexConstant*>& tagged_index() { return tagged_index_; }
  ZoneMap<int32_t, Int32Constant*>& int32() { return int32_; }
  ZoneMap<uint32_t, Uint32Constant*>& uint32() { return uint32_; }
  ZoneMap<uint64_t, Float64Constant*>& float64() { return float_; }
  ZoneMap<Address, ExternalConstant*>& external_references() {
    return external_references_;
  }
  ZoneVector<InitialValue*>& parameters() { return parameters_; }

  // Running JS2, 99.99% of the cases, we have less than 2 dependencies.
  using SmallAllocationVector = SmallZoneVector<InlinedAllocation*, 2>;

  // If the key K of the map escape, all the set allocations_escape_map[K] must
  // also escape.
  ZoneMap<InlinedAllocation*, SmallAllocationVector>& allocations_escape_map() {
    return allocations_escape_map_;
  }
  // The K of the map can be elided if it hasn't escaped and all the set
  // allocations_elide_map[K] can also be elided.
  ZoneMap<InlinedAllocation*, SmallAllocationVector>& allocations_elide_map() {
    return allocations_elide_map_;
  }

  RegList& register_inputs() { return register_inputs_; }
  compiler::ZoneRefMap<compiler::ObjectRef, Constant*>& constants() {
    return constants_;
  }

  compiler::ZoneRefMap<compiler::HeapObjectRef, TrustedConstant*>&
  trusted_constants() {
    return trusted_constants_;
  }

  ZoneVector<OptimizedCompilationInfo::InlinedFunctionHolder>&
  inlined_functions() {
    return inlined_functions_;
  }
  bool has_recursive_calls() const { return has_recursive_calls_; }
  void set_has_recursive_calls(bool value) { has_recursive_calls_ = value; }

  bool is_osr() const { return is_osr_; }
  uint32_t min_maglev_stackslots_for_unoptimized_frame_size() {
    DCHECK(is_osr());
    if (osr_values().size() == 0) {
      return InitialValue::stack_slot(0);
    }
    return osr_values().back()->stack_slot() + 1;
  }

  uint32_t NewObjectId() { return object_ids_++; }

  void set_has_resumable_generator() { has_resumable_generator_ = true; }
  bool has_resumable_generator() const { return has_resumable_generator_; }

  // Resolve the scope info of a context value.
  // An empty result means we don't statically know the context's scope.
  compiler::OptionalScopeInfoRef TryGetScopeInfo(
      ValueNode* context, compiler::JSHeapBroker* broker) {
    auto it = scope_infos_.find(context);
    if (it != scope_infos_.end()) {
      return it->second;
    }
    compiler::OptionalScopeInfoRef res;
    if (auto context_const = context->TryCast<Constant>()) {
      res = context_const->object().AsContext().scope_info(broker);
      DCHECK(res->HasContext());
    } else if (auto load = context->TryCast<LoadTaggedFieldForContextSlot>()) {
      compiler::OptionalScopeInfoRef cur =
          TryGetScopeInfo(load->input(0).node(), broker);
      DCHECK(load->offset() ==
                 Context::OffsetOfElementAt(Context::EXTENSION_INDEX) ||
             load->offset() ==
                 Context::OffsetOfElementAt(Context::PREVIOUS_INDEX));
      if (load->offset() ==
          Context::OffsetOfElementAt(Context::EXTENSION_INDEX)) {
        res = cur;
      } else if (load->offset() ==
                 Context::OffsetOfElementAt(Context::PREVIOUS_INDEX)) {
        if (cur.has_value()) {
          cur = (*cur).OuterScopeInfo(broker);
          while (!cur->HasContext() && cur->HasOuterScopeInfo()) {
            cur = cur->OuterScopeInfo(broker);
          }
          if (cur->HasContext()) {
            res = cur;
          }
        }
      }
    } else if (context->Is<InitialValue>()) {
      // We should only fail to keep track of initial contexts originating from
      // the OSR prequel.
      // TODO(olivf): Keep track of contexts when analyzing OSR Prequel.
      DCHECK(is_osr());
    } else {
      // Any context created within a function must be registered in
      // graph()->scope_infos(). Initial contexts must be registered before
      // BuildBody. We don't track context in generators (yet) and around eval
      // the bytecode compiler creates contexts by calling
      // Runtime::kNewFunctionInfo directly.
      DCHECK(context->Is<Phi>() || context->Is<GeneratorRestoreRegister>() ||
             context->Is<RegisterInput>() || context->Is<CallRuntime>());
    }
    return scope_infos_[context] = res;
  }

  void record_scope_info(ValueNode* context,
                         compiler::OptionalScopeInfoRef scope_info) {
    scope_infos_[context] = scope_info;
  }

 private:
  uint32_t tagged_stack_slots_ = kMaxUInt32;
  uint32_t untagged_stack_slots_ = kMaxUInt32;
  uint32_t max_call_stack_args_ = kMaxUInt32;
  uint32_t max_deopted_stack_size_ = kMaxUInt32;
  ZoneVector<BasicBlock*> blocks_;
  ZoneMap<RootIndex, RootConstant*> root_;
  ZoneVector<InitialValue*> osr_values_;
  ZoneMap<int, SmiConstant*> smi_;
  ZoneMap<int, TaggedIndexConstant*> tagged_index_;
  ZoneMap<int32_t, Int32Constant*> int32_;
  ZoneMap<uint32_t, Uint32Constant*> uint32_;
  // Use the bits of the float as the key.
  ZoneMap<uint64_t, Float64Constant*> float_;
  ZoneMap<Address, ExternalConstant*> external_references_;
  ZoneVector<InitialValue*> parameters_;
  ZoneMap<InlinedAllocation*, SmallAllocationVector> allocations_escape_map_;
  ZoneMap<InlinedAllocation*, SmallAllocationVector> allocations_elide_map_;
  RegList register_inputs_;
  compiler::ZoneRefMap<compiler::ObjectRef, Constant*> constants_;
  compiler::ZoneRefMap<compiler::HeapObjectRef, TrustedConstant*>
      trusted_constants_;
  ZoneVector<OptimizedCompilationInfo::InlinedFunctionHolder>
      inlined_functions_;
  bool has_recursive_calls_ = false;
  int total_inlined_bytecode_size_ = 0;
  bool is_osr_ = false;
  uint32_t object_ids_ = 0;
  bool has_resumable_generator_ = false;
  ZoneUnorderedMap<ValueNode*, compiler::OptionalScopeInfoRef> scope_infos_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_H_
