// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/maglev-safepoint-table.h"

#include <iomanip>

#include "src/codegen/macro-assembler.h"
#include "src/objects/code-inl.h"

namespace v8 {
namespace internal {

MaglevSafepointTable::MaglevSafepointTable(Isolate* isolate, Address pc,
                                           Tagged<Code> code)
    : MaglevSafepointTable(code->InstructionStart(isolate, pc),
                           code->safepoint_table_address()) {
  DCHECK(code->is_maglevved());
}

MaglevSafepointTable::MaglevSafepointTable(Isolate* isolate, Address pc,
                                           Tagged<GcSafeCode> code)
    : MaglevSafepointTable(code->InstructionStart(isolate, pc),
                           code->safepoint_table_address()) {
  DCHECK(code->is_maglevved());
}

MaglevSafepointTable::MaglevSafepointTable(Address instruction_start,
                                           Address safepoint_table_address)
    : instruction_start_(instruction_start),
      safepoint_table_address_(safepoint_table_address),
      stack_slots_(base::Memory<SafepointTableStackSlotsField_t>(
          safepoint_table_address + kStackSlotsOffset)),
      length_(base::Memory<int>(safepoint_table_address + kLengthOffset)),
      entry_configuration_(base::Memory<uint32_t>(safepoint_table_address +
                                                  kEntryConfigurationOffset)),
      num_tagged_slots_(base::Memory<uint32_t>(safepoint_table_address +
                                               kNumTaggedSlotsOffset)) {}

int MaglevSafepointTable::find_return_pc(int pc_offset) {
  for (int i = 0; i < length(); i++) {
    MaglevSafepointEntry entry = GetEntry(i);
    if (entry.trampoline_pc() == pc_offset || entry.pc() == pc_offset) {
      return entry.pc();
    }
  }
  UNREACHABLE();
}

MaglevSafepointEntry MaglevSafepointTable::FindEntry(Address pc) const {
  int pc_offset = static_cast<int>(pc - instruction_start_);

  // Check if the PC is pointing at a trampoline.
  if (has_deopt_data()) {
    for (int i = 0; i < length_; ++i) {
      MaglevSafepointEntry entry = GetEntry(i);
      int trampoline_pc = GetEntry(i).trampoline_pc();
      if (trampoline_pc != -1 && trampoline_pc == pc_offset) return entry;
      if (trampoline_pc > pc_offset) break;
    }
  }

  // Try to find an exact pc match.
  for (int i = 0; i < length_; ++i) {
    MaglevSafepointEntry entry = GetEntry(i);
    if (entry.pc() == pc_offset) {
      return entry;
    }
  }

  // Return a default entry which has no deopt data and no pushed registers.
  // This allows us to elide emitting entries for trivial calls.
  int deopt_index = MaglevSafepointEntry::kNoDeoptIndex;
  int trampoline_pc = MaglevSafepointEntry::kNoTrampolinePC;
  uint8_t num_extra_spill_slots = 0;
  int tagged_register_indexes = 0;

  return MaglevSafepointEntry(pc_offset, deopt_index, num_tagged_slots_,
                              num_extra_spill_slots, tagged_register_indexes,
                              trampoline_pc);
}

// static
MaglevSafepointEntry MaglevSafepointTable::FindEntry(Isolate* isolate,
                                                     Tagged<GcSafeCode> code,
                                                     Address pc) {
  MaglevSafepointTable table(isolate, pc, code);
  return table.FindEntry(pc);
}

void MaglevSafepointTable::Print(std::ostream& os) const {
  os << "Safepoints (stack slots = " << stack_slots_
     << ", entries = " << length_ << ", byte size = " << byte_size()
     << ", tagged slots = " << num_tagged_slots_ << ")\n";

  for (int index = 0; index < length_; index++) {
    MaglevSafepointEntry entry = GetEntry(index);
    os << reinterpret_cast<const void*>(instruction_start_ + entry.pc()) << " "
       << std::setw(6) << std::hex << entry.pc() << std::dec;

    os << "  num extra spill slots: "
       << static_cast<int>(entry.num_extra_spill_slots());

    if (entry.tagged_register_indexes() != 0) {
      os << "  registers: ";
      uint32_t register_bits = entry.tagged_register_indexes();
      int bits = 32 - base::bits::CountLeadingZeros32(register_bits);
      for (int j = bits - 1; j >= 0; --j) {
        os << ((register_bits >> j) & 1);
      }
    }

    if (entry.has_deoptimization_index()) {
      os << "  deopt " << std::setw(6) << entry.deoptimization_index()
         << " trampoline: " << std::setw(6) << std::hex
         << entry.trampoline_pc();
    }
    os << "\n";
  }
}

MaglevSafepointTableBuilder::Safepoint
MaglevSafepointTableBuilder::DefineSafepoint(Assembler* assembler) {
  entries_.push_back(EntryBuilder(assembler->pc_offset_for_safepoint()));
  return MaglevSafepointTableBuilder::Safepoint(&entries_.back());
}

int MaglevSafepointTableBuilder::UpdateDeoptimizationInfo(int pc,
                                                          int trampoline,
                                                          int start,
                                                          int deopt_index) {
  DCHECK_NE(MaglevSafepointEntry::kNoTrampolinePC, trampoline);
  DCHECK_NE(MaglevSafepointEntry::kNoDeoptIndex, deopt_index);
  auto it = entries_.Find(start);
  DCHECK(std::any_of(it, entries_.end(),
                     [pc](auto& entry) { return entry.pc == pc; }));
  int index = start;
  while (it->pc != pc) ++it, ++index;
  it->trampoline = trampoline;
  it->deopt_index = deopt_index;
  return index;
}

void MaglevSafepointTableBuilder::Emit(Assembler* assembler, int stack_slots) {
#ifdef DEBUG
  int last_pc = -1;
  int last_trampoline = -1;
  for (const EntryBuilder& entry : entries_) {
    // Entries are ordered by PC.
    DCHECK_LT(last_pc, entry.pc);
    last_pc = entry.pc;
    // Trampoline PCs are increasing, and larger than regular PCs.
    if (entry.trampoline != MaglevSafepointEntry::kNoTrampolinePC) {
      DCHECK_LT(last_trampoline, entry.trampoline);
      DCHECK_LT(entries_.back().pc, entry.trampoline);
      last_trampoline = entry.trampoline;
    }
    // An entry either has trampoline and deopt index, or none of the two.
    DCHECK_EQ(entry.trampoline == MaglevSafepointEntry::kNoTrampolinePC,
              entry.deopt_index == MaglevSafepointEntry::kNoDeoptIndex);
  }
#endif  // DEBUG

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64
  // We cannot emit a const pool within the safepoint table.
  Assembler::BlockConstPoolScope block_const_pool(assembler);
#endif

  // Make sure the safepoint table is properly aligned. Pad with nops.
  assembler->Align(InstructionStream::kMetadataAlignment);
  assembler->RecordComment(";;; Maglev safepoint table.");
  set_safepoint_table_offset(assembler->pc_offset());

  // Compute the required sizes of the fields.
  int used_register_indexes = 0;
  static_assert(MaglevSafepointEntry::kNoTrampolinePC == -1);
  int max_pc = MaglevSafepointEntry::kNoTrampolinePC;
  static_assert(MaglevSafepointEntry::kNoDeoptIndex == -1);
  int max_deopt_index = MaglevSafepointEntry::kNoDeoptIndex;
  for (const EntryBuilder& entry : entries_) {
    used_register_indexes |= entry.tagged_register_indexes;
    max_pc = std::max(max_pc, std::max(entry.pc, entry.trampoline));
    max_deopt_index = std::max(max_deopt_index, entry.deopt_index);
  }

  // Derive the bytes and bools for the entry configuration from the values.
  auto value_to_bytes = [](int value) {
    DCHECK_LE(0, value);
    if (value == 0) return 0;
    if (value <= 0xff) return 1;
    if (value <= 0xffff) return 2;
    if (value <= 0xffffff) return 3;
    return 4;
  };
  bool has_deopt_data = max_deopt_index != -1;
  int register_indexes_size = value_to_bytes(used_register_indexes);
  // Add 1 so all values (including kNoDeoptIndex and kNoTrampolinePC) are
  // non-negative.
  static_assert(MaglevSafepointEntry::kNoDeoptIndex == -1);
  static_assert(MaglevSafepointEntry::kNoTrampolinePC == -1);
  int pc_size = value_to_bytes(max_pc + 1);
  int deopt_index_size = value_to_bytes(max_deopt_index + 1);

  // Add a CHECK to ensure we never overflow the space in the bitfield, even for
  // huge functions which might not be covered by tests.
  CHECK(MaglevSafepointTable::RegisterIndexesSizeField::is_valid(
      register_indexes_size));
  CHECK(MaglevSafepointTable::PcSizeField::is_valid(pc_size));
  CHECK(MaglevSafepointTable::DeoptIndexSizeField::is_valid(deopt_index_size));

  uint32_t entry_configuration =
      MaglevSafepointTable::HasDeoptDataField::encode(has_deopt_data) |
      MaglevSafepointTable::RegisterIndexesSizeField::encode(
          register_indexes_size) |
      MaglevSafepointTable::PcSizeField::encode(pc_size) |
      MaglevSafepointTable::DeoptIndexSizeField::encode(deopt_index_size);

  // Emit the table header.
  static_assert(MaglevSafepointTable::kStackSlotsOffset == 0 * kIntSize);
  static_assert(MaglevSafepointTable::kLengthOffset == 1 * kIntSize);
  static_assert(MaglevSafepointTable::kEntryConfigurationOffset ==
                2 * kIntSize);
  static_assert(MaglevSafepointTable::kNumTaggedSlotsOffset == 3 * kIntSize);
  static_assert(MaglevSafepointTable::kHeaderSize == 4 * kIntSize);
  int length = static_cast<int>(entries_.size());
  assembler->dd(stack_slots);
  assembler->dd(length);
  assembler->dd(entry_configuration);
  assembler->dd(num_tagged_slots_);

  auto emit_bytes = [assembler](int value, int bytes) {
    DCHECK_LE(0, value);
    for (; bytes > 0; --bytes, value >>= 8) assembler->db(value);
    DCHECK_EQ(0, value);
  };
  // Emit entries, sorted by pc offsets.
  for (const EntryBuilder& entry : entries_) {
    emit_bytes(entry.pc, pc_size);
    if (has_deopt_data) {
      // Add 1 so all values (including kNoDeoptIndex and kNoTrampolinePC) are
      // non-negative.
      static_assert(MaglevSafepointEntry::kNoDeoptIndex == -1);
      static_assert(MaglevSafepointEntry::kNoTrampolinePC == -1);
      emit_bytes(entry.deopt_index + 1, deopt_index_size);
      emit_bytes(entry.trampoline + 1, pc_size);
    }
    assembler->db(entry.num_extra_spill_slots);
    emit_bytes(entry.tagged_register_indexes, register_indexes_size);
  }
}

}  // namespace internal
}  // namespace v8
