// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MAGLEV_SAFEPOINT_TABLE_H_
#define V8_CODEGEN_MAGLEV_SAFEPOINT_TABLE_H_

#include <cstdint>

#include "src/base/bit-field.h"
#include "src/codegen/safepoint-table-base.h"
#include "src/common/assert-scope.h"
#include "src/utils/allocation.h"
#include "src/zone/zone-chunk-list.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class GcSafeCode;

class MaglevSafepointEntry : public SafepointEntryBase {
 public:
  static constexpr int kNoDeoptIndex = -1;
  static constexpr int kNoTrampolinePC = -1;

  MaglevSafepointEntry() = default;

  MaglevSafepointEntry(int pc, int deopt_index, uint32_t num_tagged_slots,
                       uint8_t num_extra_spill_slots,
                       uint32_t tagged_register_indexes, int trampoline_pc)
      : SafepointEntryBase(pc, deopt_index, trampoline_pc),
        num_tagged_slots_(num_tagged_slots),
        num_extra_spill_slots_(num_extra_spill_slots),
        tagged_register_indexes_(tagged_register_indexes) {
    DCHECK(is_initialized());
  }

  bool operator==(const MaglevSafepointEntry& other) const {
    return this->SafepointEntryBase::operator==(other) &&
           num_tagged_slots_ == other.num_tagged_slots_ &&
           num_extra_spill_slots_ == other.num_extra_spill_slots_ &&
           tagged_register_indexes_ == other.tagged_register_indexes_;
  }

  uint32_t num_tagged_slots() const { return num_tagged_slots_; }
  uint8_t num_extra_spill_slots() const { return num_extra_spill_slots_; }
  uint32_t tagged_register_indexes() const { return tagged_register_indexes_; }

  uint32_t register_input_count() const { return tagged_register_indexes_; }

 private:
  uint32_t num_tagged_slots_ = 0;
  uint8_t num_extra_spill_slots_ = 0;
  uint32_t tagged_register_indexes_ = 0;
};

// A wrapper class for accessing the safepoint table embedded into the
// InstructionStream object.
class MaglevSafepointTable {
 public:
  // The isolate and pc arguments are used for figuring out whether pc
  // belongs to the embedded or un-embedded code blob.
  explicit MaglevSafepointTable(Isolate* isolate, Address pc,
                                Tagged<Code> code);
  MaglevSafepointTable(const MaglevSafepointTable&) = delete;
  MaglevSafepointTable& operator=(const MaglevSafepointTable&) = delete;

  int length() const { return length_; }

  int byte_size() const { return kHeaderSize + length_ * entry_size(); }

  int find_return_pc(int pc_offset);

  uint32_t stack_slots() { return stack_slots_; }

  MaglevSafepointEntry GetEntry(int index) const {
    DCHECK_GT(length_, index);
    Address entry_ptr =
        safepoint_table_address_ + kHeaderSize + index * entry_size();

    int pc = read_bytes(&entry_ptr, pc_size());
    int deopt_index = MaglevSafepointEntry::kNoDeoptIndex;
    int trampoline_pc = MaglevSafepointEntry::kNoTrampolinePC;
    if (has_deopt_data()) {
      static_assert(MaglevSafepointEntry::kNoDeoptIndex == -1);
      static_assert(MaglevSafepointEntry::kNoTrampolinePC == -1);
      // `-1` to restore the original value, see also
      // MaglevSafepointTableBuilder::Emit.
      deopt_index = read_bytes(&entry_ptr, deopt_index_size()) - 1;
      trampoline_pc = read_bytes(&entry_ptr, pc_size()) - 1;
      DCHECK(deopt_index >= 0 ||
             deopt_index == MaglevSafepointEntry::kNoDeoptIndex);
      DCHECK(trampoline_pc >= 0 ||
             trampoline_pc == MaglevSafepointEntry::kNoTrampolinePC);
    }
    uint8_t num_extra_spill_slots = read_byte(&entry_ptr);
    int tagged_register_indexes =
        read_bytes(&entry_ptr, register_indexes_size());

    return MaglevSafepointEntry(pc, deopt_index, num_tagged_slots_,
                                num_extra_spill_slots, tagged_register_indexes,
                                trampoline_pc);
  }

  // Returns the entry for the given pc.
  MaglevSafepointEntry FindEntry(Address pc) const;
  static MaglevSafepointEntry FindEntry(Isolate* isolate,
                                        Tagged<GcSafeCode> code, Address pc);

  void Print(std::ostream&) const;

 private:
  MaglevSafepointTable(Isolate* isolate, Address pc, Tagged<GcSafeCode> code);

  // Layout information.
#define FIELD_LIST(V)                                                      \
  V(kStackSlotsOffset, sizeof(SafepointTableStackSlotsField_t))            \
  V(kLengthOffset, kIntSize)                                               \
  V(kEntryConfigurationOffset, kUInt32Size)                                \
  /* The number of tagged/untagged slots is constant for the whole code so \
     just store it in the header. */                                       \
  V(kNumTaggedSlotsOffset, kUInt32Size)                                    \
  V(kHeaderSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(0, FIELD_LIST)
#undef FIELD_LIST

  static_assert(kStackSlotsOffset == kSafepointTableStackSlotsOffset);

  using HasDeoptDataField = base::BitField<bool, 0, 1>;
  using RegisterIndexesSizeField = HasDeoptDataField::Next<int, 3>;
  using PcSizeField = RegisterIndexesSizeField::Next<int, 3>;
  using DeoptIndexSizeField = PcSizeField::Next<int, 3>;

  MaglevSafepointTable(Address instruction_start,
                       Address safepoint_table_address);

  int entry_size() const {
    int deopt_data_size = has_deopt_data() ? pc_size() + deopt_index_size() : 0;
    const int num_pushed_registers_size = 1;
    return pc_size() + deopt_data_size + num_pushed_registers_size +
           register_indexes_size();
  }

  bool has_deopt_data() const {
    return HasDeoptDataField::decode(entry_configuration_);
  }
  int pc_size() const { return PcSizeField::decode(entry_configuration_); }
  int deopt_index_size() const {
    return DeoptIndexSizeField::decode(entry_configuration_);
  }
  int register_indexes_size() const {
    return RegisterIndexesSizeField::decode(entry_configuration_);
  }

  static int read_bytes(Address* ptr, int bytes) {
    uint32_t result = 0;
    for (int b = 0; b < bytes; ++b) {
      result |= uint32_t{read_byte(ptr)} << (8 * b);
    }
    return static_cast<int>(result);
  }

  static uint8_t read_byte(Address* ptr) {
    uint8_t result = *reinterpret_cast<uint8_t*>(*ptr);
    ++*ptr;
    return result;
  }

  DISALLOW_GARBAGE_COLLECTION(no_gc_)

  const Address instruction_start_;

  // Safepoint table layout.
  const Address safepoint_table_address_;
  const SafepointTableStackSlotsField_t stack_slots_;
  const int length_;
  const uint32_t entry_configuration_;
  const uint32_t num_tagged_slots_;

  friend class MaglevSafepointTableBuilder;
  friend class MaglevSafepointEntry;
};

class MaglevSafepointTableBuilder : public SafepointTableBuilderBase {
 private:
  struct EntryBuilder {
    int pc;
    int deopt_index = MaglevSafepointEntry::kNoDeoptIndex;
    int trampoline = MaglevSafepointEntry::kNoTrampolinePC;
    uint8_t num_extra_spill_slots = 0;
    uint32_t tagged_register_indexes = 0;
    explicit EntryBuilder(int pc) : pc(pc) {}
  };

 public:
  explicit MaglevSafepointTableBuilder(Zone* zone, uint32_t num_tagged_slots)
      : num_tagged_slots_(num_tagged_slots), entries_(zone) {}

  MaglevSafepointTableBuilder(const MaglevSafepointTableBuilder&) = delete;
  MaglevSafepointTableBuilder& operator=(const MaglevSafepointTableBuilder&) =
      delete;

  class Safepoint {
   public:
    void DefineTaggedRegister(int reg_code) {
      DCHECK_LT(reg_code,
                kBitsPerByte * sizeof(EntryBuilder::tagged_register_indexes));
      entry_->tagged_register_indexes |= 1u << reg_code;
    }
    void SetNumExtraSpillSlots(uint8_t num_slots) {
      entry_->num_extra_spill_slots = num_slots;
    }

   private:
    friend class MaglevSafepointTableBuilder;
    explicit Safepoint(EntryBuilder* entry) : entry_(entry) {}
    EntryBuilder* const entry_;
  };

  // Define a new safepoint for the current position in the body.
  Safepoint DefineSafepoint(Assembler* assembler);

  // Emit the safepoint table after the body.
  V8_EXPORT_PRIVATE void Emit(Assembler* assembler, int stack_slots);

  // Find the Deoptimization Info with pc offset {pc} and update its
  // trampoline field. Calling this function ensures that the safepoint
  // table contains the trampoline PC {trampoline} that replaced the
  // return PC {pc} on the stack.
  int UpdateDeoptimizationInfo(int pc, int trampoline, int start,
                               int deopt_index);

 private:
  const uint32_t num_tagged_slots_;
  ZoneChunkList<EntryBuilder> entries_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_MAGLEV_SAFEPOINT_TABLE_H_
