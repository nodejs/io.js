// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CONSTANT_POOL_H_
#define V8_CODEGEN_CONSTANT_POOL_H_

#include <map>

#include "src/base/numbers/double.h"
#include "src/codegen/label.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Instruction;

// -----------------------------------------------------------------------------
// Constant pool support

class ConstantPoolEntry {
 public:
  ConstantPoolEntry() = default;
  ConstantPoolEntry(int position, intptr_t value, bool sharing_ok,
                    RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : position_(position),
        merged_index_(sharing_ok ? SHARING_ALLOWED : SHARING_PROHIBITED),
        value_(value),
        rmode_(rmode) {}
  ConstantPoolEntry(int position, base::Double value,
                    RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : position_(position),
        merged_index_(SHARING_ALLOWED),
        value64_(value.AsUint64()),
        rmode_(rmode) {}

  int position() const { return position_; }
  bool sharing_ok() const { return merged_index_ != SHARING_PROHIBITED; }
  bool is_merged() const { return merged_index_ >= 0; }
  int merged_index() const {
    DCHECK(is_merged());
    return merged_index_;
  }
  void set_merged_index(int index) {
    DCHECK(sharing_ok());
    merged_index_ = index;
    DCHECK(is_merged());
  }
  int offset() const {
    DCHECK_GE(merged_index_, 0);
    return merged_index_;
  }
  void set_offset(int offset) {
    DCHECK_GE(offset, 0);
    merged_index_ = offset;
  }
  intptr_t value() const { return value_; }
  uint64_t value64() const { return value64_; }
  RelocInfo::Mode rmode() const { return rmode_; }

  enum Type { INTPTR, DOUBLE, NUMBER_OF_TYPES };

  static int size(Type type) {
    return (type == INTPTR) ? kSystemPointerSize : kDoubleSize;
  }

  enum Access { REGULAR, OVERFLOWED };

 private:
  int position_;
  int merged_index_;
  union {
    intptr_t value_;
    uint64_t value64_;
  };
  // TODO(leszeks): The way we use this, it could probably be packed into
  // merged_index_ if size is a concern.
  RelocInfo::Mode rmode_;
  enum { SHARING_PROHIBITED = -2, SHARING_ALLOWED = -1 };
};

#if defined(V8_TARGET_ARCH_PPC64)

// -----------------------------------------------------------------------------
// Embedded constant pool support

class ConstantPoolBuilder {
 public:
  ConstantPoolBuilder(int ptr_reach_bits, int double_reach_bits);

#ifdef DEBUG
  ~ConstantPoolBuilder() {
    // Unused labels to prevent DCHECK failures.
    emitted_label_.Unuse();
    emitted_label_.UnuseNear();
  }
#endif

  // Add pointer-sized constant to the embedded constant pool
  ConstantPoolEntry::Access AddEntry(int position, intptr_t value,
                                     bool sharing_ok) {
    ConstantPoolEntry entry(position, value, sharing_ok);
    return AddEntry(&entry, ConstantPoolEntry::INTPTR);
  }

  // Add double constant to the embedded constant pool
  ConstantPoolEntry::Access AddEntry(int position, base::Double value) {
    ConstantPoolEntry entry(position, value);
    return AddEntry(&entry, ConstantPoolEntry::DOUBLE);
  }

  // Add double constant to the embedded constant pool
  ConstantPoolEntry::Access AddEntry(int position, double value) {
    return AddEntry(position, base::Double(value));
  }

  // Previews the access type required for the next new entry to be added.
  ConstantPoolEntry::Access NextAccess(ConstantPoolEntry::Type type) const;

  bool IsEmpty() {
    return info_[ConstantPoolEntry::INTPTR].entries.empty() &&
           info_[ConstantPoolEntry::INTPTR].shared_entries.empty() &&
           info_[ConstantPoolEntry::DOUBLE].entries.empty() &&
           info_[ConstantPoolEntry::DOUBLE].shared_entries.empty();
  }

  // Emit the constant pool.  Invoke only after all entries have been
  // added and all instructions have been emitted.
  // Returns position of the emitted pool (zero implies no constant pool).
  int Emit(Assembler* assm);

  // Returns the label associated with the start of the constant pool.
  // Linking to this label in the function prologue may provide an
  // efficient means of constant pool pointer register initialization
  // on some architectures.
  inline Label* EmittedPosition() { return &emitted_label_; }

 private:
  ConstantPoolEntry::Access AddEntry(ConstantPoolEntry* entry,
                                     ConstantPoolEntry::Type type);
  void EmitSharedEntries(Assembler* assm, ConstantPoolEntry::Type type);
  void EmitGroup(Assembler* assm, ConstantPoolEntry::Access access,
                 ConstantPoolEntry::Type type);

  struct PerTypeEntryInfo {
    PerTypeEntryInfo() : regular_count(0), overflow_start(-1) {}
    bool overflow() const {
      return (overflow_start >= 0 &&
              overflow_start < static_cast<int>(entries.size()));
    }
    int regular_reach_bits;
    int regular_count;
    int overflow_start;
    std::vector<ConstantPoolEntry> entries;
    std::vector<ConstantPoolEntry> shared_entries;
  };

  Label emitted_label_;  // Records pc_offset of emitted pool
  PerTypeEntryInfo info_[ConstantPoolEntry::NUMBER_OF_TYPES];
};

#endif  // defined(V8_TARGET_ARCH_PPC64)

#if defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_RISCV32)

class ConstantPoolKey {
 public:
  explicit ConstantPoolKey(uint64_t value,
                           RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : is_value32_(false), value64_(value), rmode_(rmode) {}

  explicit ConstantPoolKey(uint32_t value,
                           RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : is_value32_(true), value32_(value), rmode_(rmode) {}

  uint64_t value64() const {
    CHECK(!is_value32_);
    return value64_;
  }
  uint32_t value32() const {
    CHECK(is_value32_);
    return value32_;
  }

  bool is_value32() const { return is_value32_; }
  RelocInfo::Mode rmode() const { return rmode_; }

  bool AllowsDeduplication() const {
    DCHECK(rmode_ != RelocInfo::CONST_POOL &&
           rmode_ != RelocInfo::VENEER_POOL &&
           rmode_ != RelocInfo::DEOPT_SCRIPT_OFFSET &&
           rmode_ != RelocInfo::DEOPT_INLINING_ID &&
           rmode_ != RelocInfo::DEOPT_REASON && rmode_ != RelocInfo::DEOPT_ID &&
           rmode_ != RelocInfo::DEOPT_NODE_ID);
    // CODE_TARGETs can be shared because they aren't patched anymore,
    // and we make sure we emit only one reloc info for them (thus delta
    // patching) will apply the delta only once. At the moment, we do not dedup
    // code targets if they are wrapped in a heap object request (value == 0).
    bool is_sharable_code_target =
        rmode_ == RelocInfo::CODE_TARGET &&
        (is_value32() ? (value32() != 0) : (value64() != 0));
    bool is_sharable_embedded_object = RelocInfo::IsEmbeddedObjectMode(rmode_);
    return RelocInfo::IsShareableRelocMode(rmode_) || is_sharable_code_target ||
           is_sharable_embedded_object;
  }

 private:
  bool is_value32_;
  union {
    uint64_t value64_;
    uint32_t value32_;
  };
  RelocInfo::Mode rmode_;
};

// Order for pool entries. 64bit entries go first.
inline bool operator<(const ConstantPoolKey& a, const ConstantPoolKey& b) {
  if (a.is_value32() < b.is_value32()) return true;
  if (a.is_value32() > b.is_value32()) return false;
  if (a.rmode() < b.rmode()) return true;
  if (a.rmode() > b.rmode()) return false;
  if (a.is_value32()) return a.value32() < b.value32();
  return a.value64() < b.value64();
}

inline bool operator==(const ConstantPoolKey& a, const ConstantPoolKey& b) {
  if (a.rmode() != b.rmode() || a.is_value32() != b.is_value32()) {
    return false;
  }
  if (a.is_value32()) return a.value32() == b.value32();
  return a.value64() == b.value64();
}

// Constant pool generation
enum class Jump { kOmitted, kRequired };
enum class Emission { kIfNeeded, kForced };
enum class Alignment { kOmitted, kRequired };
enum class RelocInfoStatus { kMustRecord, kMustOmitForDuplicate };
enum class PoolEmissionCheck { kSkip };

// Pools are emitted in the instruction stream, preferably after unconditional
// jumps or after returns from functions (in dead code locations).
// If a long code sequence does not contain unconditional jumps, it is
// necessary to emit the constant pool before the pool gets too far from the
// location it is accessed from. In this case, we emit a jump over the emitted
// constant pool.
// Constants in the pool may be addresses of functions that gets relocated;
// if so, a relocation info entry is associated to the constant pool entry.
class ConstantPool {
 public:
  explicit ConstantPool(Assembler* assm);
  ~ConstantPool();

  // Returns true when we need to write RelocInfo and false when we do not.
  RelocInfoStatus RecordEntry(uint32_t data, RelocInfo::Mode rmode);
  RelocInfoStatus RecordEntry(uint64_t data, RelocInfo::Mode rmode);

  size_t Entry32Count() const { return entry32_count_; }
  size_t Entry64Count() const { return entry64_count_; }
  bool IsEmpty() const { return entries_.empty(); }
  // Check if pool will be out of range at {pc_offset}.
  bool IsInImmRangeIfEmittedAt(int pc_offset);
  // Size in bytes of the constant pool. Depending on parameters, the size will
  // include the branch over the pool and alignment padding.
  int ComputeSize(Jump require_jump, Alignment require_alignment) const;

  // Emit the pool at the current pc with a branch over the pool if requested.
  void EmitAndClear(Jump require);
  bool ShouldEmitNow(Jump require_jump, size_t margin = 0) const;
  V8_EXPORT_PRIVATE void Check(Emission force_emission, Jump require_jump,
                               size_t margin = 0);

  V8_EXPORT_PRIVATE void MaybeCheck();
  void Clear();

  // Constant pool emission can be blocked temporarily.
  bool IsBlocked() const;

  // Repeated checking whether the constant pool should be emitted is expensive;
  // only check once a number of instructions have been generated.
  void SetNextCheckIn(size_t instructions);

  // Class for scoping postponing the constant pool generation.
  class V8_EXPORT_PRIVATE V8_NODISCARD BlockScope {
   public:
    // BlockScope immediatelly emits the pool if necessary to ensure that
    // during the block scope at least {margin} bytes can be emitted without
    // pool emission becomming necessary.
    explicit BlockScope(Assembler* pool, size_t margin = 0);
    BlockScope(Assembler* pool, PoolEmissionCheck);
    ~BlockScope();

   private:
    ConstantPool* pool_;
    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockScope);
  };

  // Hard limit to the const pool which must not be exceeded.
  static const size_t kMaxDistToPool32;
  static const size_t kMaxDistToPool64;
  // Approximate distance where the pool should be emitted.
  static const size_t kApproxDistToPool32;
  V8_EXPORT_PRIVATE static const size_t kApproxDistToPool64;
  // Approximate distance where the pool may be emitted if
  // no jump is required (due to a recent unconditional jump).
  static const size_t kOpportunityDistToPool32;
  static const size_t kOpportunityDistToPool64;
  // PC distance between constant pool checks.
  V8_EXPORT_PRIVATE static const size_t kCheckInterval;
  // Number of entries in the pool which trigger a check.
  static const size_t kApproxMaxEntryCount;

 private:
  void StartBlock();
  void EndBlock();

  void EmitEntries();
  void EmitPrologue(Alignment require_alignment);
  int PrologueSize(Jump require_jump) const;
  RelocInfoStatus RecordKey(ConstantPoolKey key, int offset);
  RelocInfoStatus GetRelocInfoStatusFor(const ConstantPoolKey& key);
  void Emit(const ConstantPoolKey& key);
  void SetLoadOffsetToConstPoolEntry(int load_offset, Instruction* entry_offset,
                                     const ConstantPoolKey& key);
  Alignment IsAlignmentRequiredIfEmittedAt(Jump require_jump,
                                           int pc_offset) const;

  Assembler* assm_;
  // Keep track of the first instruction requiring a constant pool entry
  // since the previous constant pool was emitted.
  int first_use_32_ = -1;
  int first_use_64_ = -1;
  // We sort not according to insertion order, but since we do not insert
  // addresses (for heap objects we insert an index which is created in
  // increasing order), the order is deterministic. We map each entry to the
  // pc offset of the load. We use a multimap because we need to record the
  // pc offset of each load of the same constant so that the immediate of the
  // loads can be back-patched when the pool is emitted.
  std::multimap<ConstantPoolKey, int> entries_;
  size_t entry32_count_ = 0;
  size_t entry64_count_ = 0;
  int next_check_ = 0;
  int old_next_check_ = 0;
  int blocked_nesting_ = 0;
};

#endif  // defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_RISCV64) ||
        // defined(V8_TARGET_ARCH_RISCV32)

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_CONSTANT_POOL_H_
