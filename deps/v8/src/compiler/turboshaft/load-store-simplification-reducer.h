// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operation-matcher.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

struct LoadStoreSimplificationConfiguration {
  // TODO(12783): This needs to be extended for all architectures that don't
  // have loads with the base + index * element_size + offset pattern.
#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_RISCV64 ||    \
    V8_TARGET_ARCH_LOONG64 || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_PPC64 || \
    V8_TARGET_ARCH_RISCV32
  // As tagged loads result in modfiying the offset by -1, those loads are
  // converted into raw loads.
  static constexpr bool kNeedsUntaggedBase = true;
  // By setting {kMinOffset} > {kMaxOffset}, we ensure that all offsets
  // (including 0) are merged into the computed index.
  static constexpr int32_t kMinOffset = 1;
  static constexpr int32_t kMaxOffset = 0;
  // Turboshaft's loads and stores follow the pattern of
  // *(base + index * element_size_log2 + displacement), but architectures
  // typically support only a limited `element_size_log2`.
  static constexpr int kMaxElementSizeLog2 = 0;
#elif V8_TARGET_ARCH_S390X
  static constexpr bool kNeedsUntaggedBase = false;
  // s390x supports *(base + index + displacement), element_size isn't
  // supported.
  static constexpr int32_t kDisplacementBits = 20;  // 20 bit signed integer.
  static constexpr int32_t kMinOffset =
      -(static_cast<int32_t>(1) << (kDisplacementBits - 1));
  static constexpr int32_t kMaxOffset =
      (static_cast<int32_t>(1) << (kDisplacementBits - 1)) - 1;
  static constexpr int kMaxElementSizeLog2 = 0;
#else
  static constexpr bool kNeedsUntaggedBase = false;
  // We don't want to encode INT32_MIN in the offset becauce instruction
  // selection might not be able to put this into an immediate operand.
  static constexpr int32_t kMinOffset = std::numeric_limits<int32_t>::min() + 1;
  static constexpr int32_t kMaxOffset = std::numeric_limits<int32_t>::max();
  // Turboshaft's loads and stores follow the pattern of
  // *(base + index * element_size_log2 + displacement), but architectures
  // typically support only a limited `element_size_log2`.
  static constexpr int kMaxElementSizeLog2 = 3;
#endif
};

// This reducer simplifies Turboshaft's "complex" loads and stores into
// simplified ones that are supported on the given target architecture.
template <class Next>
class LoadStoreSimplificationReducer : public Next,
                                       LoadStoreSimplificationConfiguration {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LoadStoreSimplification)

  OpIndex REDUCE(Load)(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_size_log2) {
    SimplifyLoadStore(base, index, kind, offset, element_size_log2);
    return Next::ReduceLoad(base, index, kind, loaded_rep, result_rep, offset,
                            element_size_log2);
  }

  OpIndex REDUCE(Store)(OpIndex base, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_size_log2,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    SimplifyLoadStore(base, index, kind, offset, element_size_log2);
    if (write_barrier != WriteBarrierKind::kNoWriteBarrier &&
        !index.has_value() && __ Get(base).template Is<ConstantOp>()) {
      const ConstantOp& const_base = __ Get(base).template Cast<ConstantOp>();
      if (const_base.IsIntegral() ||
          const_base.kind == ConstantOp::Kind::kSmi) {
        // It never makes sense to have a WriteBarrier for a store to a raw
        // address. We should thus be in unreachable code.
        // The instruction selector / register allocator don't handle this very
        // well, so it's easier to emit an Unreachable rather than emitting a
        // weird store that will never be executed.
        __ Unreachable();
        return OpIndex::Invalid();
      }
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_size_log2,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }

  OpIndex REDUCE(AtomicWord32Pair)(V<WordPtr> base, OptionalV<WordPtr> index,
                                   OptionalV<Word32> value_low,
                                   OptionalV<Word32> value_high,
                                   OptionalV<Word32> expected_low,
                                   OptionalV<Word32> expected_high,
                                   AtomicWord32PairOp::Kind kind,
                                   int32_t offset) {
    if (kind == AtomicWord32PairOp::Kind::kStore ||
        kind == AtomicWord32PairOp::Kind::kLoad) {
      if (!index.valid()) {
        index = __ IntPtrConstant(offset);
        offset = 0;
      } else if (offset != 0) {
        index = __ WordPtrAdd(index.value(), offset);
        offset = 0;
      }
    }
    return Next::ReduceAtomicWord32Pair(base, index, value_low, value_high,
                                        expected_low, expected_high, kind,
                                        offset);
  }

 private:
  bool CanEncodeOffset(int32_t offset, bool tagged_base) const {
    // If the base is tagged we also need to subtract the kHeapObjectTag
    // eventually.
    const int32_t min = kMinOffset + (tagged_base ? kHeapObjectTag : 0);
    if (min <= offset && offset <= kMaxOffset) {
      DCHECK(LoadOp::OffsetIsValid(offset, tagged_base));
      return true;
    }
    return false;
  }

  bool CanEncodeAtomic(OptionalOpIndex index, uint8_t element_size_log2,
                       int32_t offset) const {
    if (element_size_log2 != 0) return false;
    return !(index.has_value() && offset != 0);
  }

  void SimplifyLoadStore(OpIndex& base, OptionalOpIndex& index,
                         LoadOp::Kind& kind, int32_t& offset,
                         uint8_t& element_size_log2) {
    if (element_size_log2 > kMaxElementSizeLog2) {
      DCHECK(index.valid());
      index = __ WordPtrShiftLeft(index.value(), element_size_log2);
      element_size_log2 = 0;
    }

    if (kNeedsUntaggedBase) {
      if (kind.tagged_base) {
        kind.tagged_base = false;
        DCHECK_LE(std::numeric_limits<int32_t>::min() + kHeapObjectTag, offset);
        offset -= kHeapObjectTag;
        base = __ BitcastHeapObjectToWordPtr(base);
      }
    }

    // TODO(nicohartmann@): Remove the case for atomics once crrev.com/c/5237267
    // is ported to x64.
    if (!CanEncodeOffset(offset, kind.tagged_base) ||
        (kind.is_atomic &&
         !CanEncodeAtomic(index, element_size_log2, offset))) {
      // If an index is present, the element_size_log2 is changed to zero.
      // So any load follows the form *(base + offset). To simplify
      // instruction selection, both static and dynamic offsets are stored in
      // the index input.
      // As tagged loads result in modifying the offset by -1, those loads are
      // converted into raw loads (above).
      if (!index.has_value() || matcher_.MatchIntegralZero(index.value())) {
        index = __ IntPtrConstant(offset);
        element_size_log2 = 0;
        offset = 0;
      } else if (element_size_log2 != 0) {
        index = __ WordPtrShiftLeft(index.value(), element_size_log2);
        element_size_log2 = 0;
      }
      if (offset != 0) {
        index = __ WordPtrAdd(index.value(), offset);
        offset = 0;
      }
      DCHECK_EQ(offset, 0);
      DCHECK_EQ(element_size_log2, 0);
    }
  }

  OperationMatcher matcher_{__ output_graph()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_
