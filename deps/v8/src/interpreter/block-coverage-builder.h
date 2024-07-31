// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_
#define V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_

#include "src/ast/ast-source-ranges.h"
#include "src/interpreter/bytecode-array-builder.h"

#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace interpreter {

// Used to generate IncBlockCounter bytecodes and the {source range, slot}
// mapping for block coverage.
class BlockCoverageBuilder final : public ZoneObject {
 public:
  BlockCoverageBuilder(Zone* zone, BytecodeArrayBuilder* builder,
                       SourceRangeMap* source_range_map)
      : slots_(0, zone),
        builder_(builder),
        source_range_map_(source_range_map) {
    DCHECK_NOT_NULL(builder);
    DCHECK_NOT_NULL(source_range_map);
  }

  static constexpr int kNoCoverageArraySlot = -1;

  int AllocateBlockCoverageSlot(ZoneObject* node, SourceRangeKind kind) {
    AstNodeSourceRanges* ranges = source_range_map_->Find(node);
    if (ranges == nullptr) return kNoCoverageArraySlot;

    SourceRange range = ranges->GetRange(kind);
    if (range.IsEmpty()) return kNoCoverageArraySlot;

    const int slot = static_cast<int>(slots_.size());
    slots_.emplace_back(range);
    return slot;
  }

  int AllocateNaryBlockCoverageSlot(NaryOperation* node, size_t index) {
    NaryOperationSourceRanges* ranges =
        static_cast<NaryOperationSourceRanges*>(source_range_map_->Find(node));
    if (ranges == nullptr) return kNoCoverageArraySlot;

    SourceRange range = ranges->GetRangeAtIndex(index);
    if (range.IsEmpty()) return kNoCoverageArraySlot;

    const int slot = static_cast<int>(slots_.size());
    slots_.emplace_back(range);
    return slot;
  }

  int AllocateConditionalChainBlockCoverageSlot(ConditionalChain* node,
                                                SourceRangeKind kind,
                                                size_t index) {
    ConditionalChainSourceRanges* ranges =
        static_cast<ConditionalChainSourceRanges*>(
            source_range_map_->Find(node));
    if (ranges == nullptr) return kNoCoverageArraySlot;

    SourceRange range = ranges->GetRangeAtIndex(kind, index);
    if (range.IsEmpty()) return kNoCoverageArraySlot;

    const int slot = static_cast<int>(slots_.size());
    slots_.emplace_back(range);
    return slot;
  }

  void IncrementBlockCounter(int coverage_array_slot) {
    if (coverage_array_slot == kNoCoverageArraySlot) return;
    builder_->IncBlockCounter(coverage_array_slot);
  }

  void IncrementBlockCounter(ZoneObject* node, SourceRangeKind kind) {
    int slot = AllocateBlockCoverageSlot(node, kind);
    IncrementBlockCounter(slot);
  }

  const ZoneVector<SourceRange>& slots() const { return slots_; }

 private:
  // Contains source range information for allocated block coverage counter
  // slots. Slot i covers range slots_[i].
  ZoneVector<SourceRange> slots_;
  BytecodeArrayBuilder* builder_;
  SourceRangeMap* source_range_map_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_
