// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/safepoint-table.h"

#include <iomanip>

#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/diagnostics/disasm.h"
#include "src/execution/frames-inl.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-code-manager.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

SafepointTable::SafepointTable(Isolate* isolate, Address pc, Tagged<Code> code)
    : SafepointTable(code->InstructionStart(isolate, pc),
                     code->safepoint_table_address()) {
  DCHECK(code->is_turbofanned());
}

SafepointTable::SafepointTable(Isolate* isolate, Address pc,
                               Tagged<GcSafeCode> code)
    : SafepointTable(code->InstructionStart(isolate, pc),
                     code->safepoint_table_address()) {
  DCHECK(code->is_turbofanned());
}

#if V8_ENABLE_WEBASSEMBLY
SafepointTable::SafepointTable(const wasm::WasmCode* code)
    : SafepointTable(
          code->instruction_start(),
          code->instruction_start() + code->safepoint_table_offset()) {}
#endif  // V8_ENABLE_WEBASSEMBLY

SafepointTable::SafepointTable(Address instruction_start,
                               Address safepoint_table_address)
    : instruction_start_(instruction_start),
      safepoint_table_address_(safepoint_table_address),
      stack_slots_(base::Memory<SafepointTableStackSlotsField_t>(
          safepoint_table_address + kStackSlotsOffset)),
      length_(base::Memory<int>(safepoint_table_address + kLengthOffset)),
      entry_configuration_(base::Memory<uint32_t>(safepoint_table_address +
                                                  kEntryConfigurationOffset)) {}

int SafepointTable::find_return_pc(int pc_offset) {
  for (int i = 0; i < length(); i++) {
    SafepointEntry entry = GetEntry(i);
    if (entry.trampoline_pc() == pc_offset || entry.pc() == pc_offset) {
      return entry.pc();
    }
  }
  UNREACHABLE();
}

SafepointEntry SafepointTable::TryFindEntry(Address pc) const {
  int pc_offset = static_cast<int>(pc - instruction_start_);

  // Check if the PC is pointing at a trampoline.
  if (has_deopt_data()) {
    int candidate = -1;
    for (int i = 0; i < length_; ++i) {
      int trampoline_pc = GetEntry(i).trampoline_pc();
      if (trampoline_pc != -1 && trampoline_pc <= pc_offset) candidate = i;
      if (trampoline_pc > pc_offset) break;
    }
    if (candidate != -1) return GetEntry(candidate);
  }

  for (int i = 0; i < length_; ++i) {
    SafepointEntry entry = GetEntry(i);
    if (i == length_ - 1 || GetEntry(i + 1).pc() > pc_offset) {
      if (entry.pc() > pc_offset) return {};
      return entry;
    }
  }
  return {};
}

SafepointEntry SafepointTable::FindEntry(Address pc) const {
  SafepointEntry result = TryFindEntry(pc);
  CHECK(result.is_initialized());
  return result;
}

// static
SafepointEntry SafepointTable::FindEntry(Isolate* isolate,
                                         Tagged<GcSafeCode> code, Address pc) {
  SafepointTable table(isolate, pc, code);
  return table.FindEntry(pc);
}

void SafepointTable::Print(std::ostream& os) const {
  os << "Safepoints (stack slots = " << stack_slots_
     << ", entries = " << length_ << ", byte size = " << byte_size() << ")\n";

  for (int index = 0; index < length_; index++) {
    SafepointEntry entry = GetEntry(index);
    os << reinterpret_cast<const void*>(instruction_start_ + entry.pc()) << " "
       << std::setw(6) << std::hex << entry.pc() << std::dec;

    if (!entry.tagged_slots().empty()) {
      os << "  slots (sp->fp): ";
      uint32_t i = 0;
      for (uint8_t bits : entry.tagged_slots()) {
        for (int bit = 0; bit < kBitsPerByte && i < stack_slots_; ++bit, ++i) {
          os << ((bits >> bit) & 1);
        }
      }
      // The tagged slots bitfield ends at the min stack slot (rounded up to the
      // nearest byte) -- we might have some slots left over in the stack frame
      // before the fp, so print zeros for those.
      for (; i < stack_slots_; ++i) {
        os << 0;
      }
    }

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

SafepointTableBuilder::Safepoint SafepointTableBuilder::DefineSafepoint(
    Assembler* assembler, int pc_offset) {
  pc_offset = pc_offset ? pc_offset : assembler->pc_offset_for_safepoint();
  entries_.emplace_back(zone_, pc_offset);
  return SafepointTableBuilder::Safepoint(&entries_.back(), this);
}

int SafepointTableBuilder::UpdateDeoptimizationInfo(int pc, int trampoline,
                                                    int start,
                                                    int deopt_index) {
  DCHECK_NE(SafepointEntry::kNoTrampolinePC, trampoline);
  DCHECK_NE(SafepointEntry::kNoDeoptIndex, deopt_index);
  auto it = entries_.begin() + start;
  DCHECK(std::any_of(it, entries_.end(),
                     [pc](auto& entry) { return entry.pc == pc; }));
  int index = start;
  while (it->pc != pc) ++it, ++index;
  it->trampoline = trampoline;
  it->deopt_index = deopt_index;
  return index;
}

void SafepointTableBuilder::Emit(Assembler* assembler, int stack_slot_count) {
  DCHECK_LT(max_stack_index_, stack_slot_count);

#ifdef DEBUG
  int last_pc = -1;
  int last_trampoline = -1;
  for (const EntryBuilder& entry : entries_) {
    // Entries are ordered by PC.
    DCHECK_LT(last_pc, entry.pc);
    last_pc = entry.pc;
    // Trampoline PCs are increasing, and larger than regular PCs.
    if (entry.trampoline != SafepointEntry::kNoTrampolinePC) {
      DCHECK_LT(last_trampoline, entry.trampoline);
      DCHECK_LT(entries_.back().pc, entry.trampoline);
      last_trampoline = entry.trampoline;
    }
    // An entry either has trampoline and deopt index, or none of the two.
    DCHECK_EQ(entry.trampoline == SafepointEntry::kNoTrampolinePC,
              entry.deopt_index == SafepointEntry::kNoDeoptIndex);
  }
#endif  // DEBUG

  RemoveDuplicates();

  // The encoding is compacted by translating stack slot indices s.t. they
  // start at 0. See also below.
  int tagged_slots_size = stack_slot_count - min_stack_index();

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64
  // We cannot emit a const pool within the safepoint table.
  Assembler::BlockConstPoolScope block_const_pool(assembler);
#endif

  // Make sure the safepoint table is properly aligned. Pad with nops.
  assembler->Align(InstructionStream::kMetadataAlignment);
  assembler->RecordComment(";;; Safepoint table.");
  set_safepoint_table_offset(assembler->pc_offset());

  // Compute the required sizes of the fields.
  int used_register_indexes = 0;
  static_assert(SafepointEntry::kNoTrampolinePC == -1);
  int max_pc = SafepointEntry::kNoTrampolinePC;
  static_assert(SafepointEntry::kNoDeoptIndex == -1);
  int max_deopt_index = SafepointEntry::kNoDeoptIndex;
  for (const EntryBuilder& entry : entries_) {
    used_register_indexes |= entry.register_indexes;
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
  static_assert(SafepointEntry::kNoDeoptIndex == -1);
  static_assert(SafepointEntry::kNoTrampolinePC == -1);
  int pc_size = value_to_bytes(max_pc + 1);
  int deopt_index_size = value_to_bytes(max_deopt_index + 1);
  int tagged_slots_bytes =
      (tagged_slots_size + kBitsPerByte - 1) / kBitsPerByte;

  // Add a CHECK to ensure we never overflow the space in the bitfield, even for
  // huge functions which might not be covered by tests.
  CHECK(SafepointTable::RegisterIndexesSizeField::is_valid(
      register_indexes_size));
  CHECK(SafepointTable::PcSizeField::is_valid(pc_size));
  CHECK(SafepointTable::DeoptIndexSizeField::is_valid(deopt_index_size));
  CHECK(SafepointTable::TaggedSlotsBytesField::is_valid(tagged_slots_bytes));

  uint32_t entry_configuration =
      SafepointTable::HasDeoptDataField::encode(has_deopt_data) |
      SafepointTable::RegisterIndexesSizeField::encode(register_indexes_size) |
      SafepointTable::PcSizeField::encode(pc_size) |
      SafepointTable::DeoptIndexSizeField::encode(deopt_index_size) |
      SafepointTable::TaggedSlotsBytesField::encode(tagged_slots_bytes);

  // Emit the table header.
  static_assert(SafepointTable::kStackSlotsOffset == 0 * kIntSize);
  static_assert(SafepointTable::kLengthOffset == 1 * kIntSize);
  static_assert(SafepointTable::kEntryConfigurationOffset == 2 * kIntSize);
  static_assert(SafepointTable::kHeaderSize == 3 * kIntSize);
  int length = static_cast<int>(entries_.size());
  assembler->dd(stack_slot_count);
  assembler->dd(length);
  assembler->dd(entry_configuration);

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
      static_assert(SafepointEntry::kNoDeoptIndex == -1);
      static_assert(SafepointEntry::kNoTrampolinePC == -1);
      emit_bytes(entry.deopt_index + 1, deopt_index_size);
      emit_bytes(entry.trampoline + 1, pc_size);
    }
    emit_bytes(entry.register_indexes, register_indexes_size);
  }

  // Emit bitmaps of tagged stack slots. Note the slot list is reversed in the
  // encoding.
  // TODO(jgruber): Avoid building a reversed copy of the bit vector.
  ZoneVector<uint8_t> bits(tagged_slots_bytes, 0, zone_);
  for (const EntryBuilder& entry : entries_) {
    std::fill(bits.begin(), bits.end(), 0);

    // Run through the indexes and build a bitmap.
    for (int idx : *entry.stack_indexes) {
      // The encoding is compacted by translating stack slot indices s.t. they
      // start at 0. See also above.
      const int adjusted_idx = idx - min_stack_index();
      DCHECK_GT(tagged_slots_size, adjusted_idx);
      int index = tagged_slots_size - 1 - adjusted_idx;
      int byte_index = index >> kBitsPerByteLog2;
      int bit_index = index & (kBitsPerByte - 1);
      bits[byte_index] |= (1u << bit_index);
    }

    // Emit the bitmap for the current entry.
    for (uint8_t byte : bits) assembler->db(byte);
  }
}

void SafepointTableBuilder::RemoveDuplicates() {
  // Remove any duplicate entries, i.e. succeeding entries that are identical
  // except for the PC. During lookup, we will find the first entry whose PC is
  // not larger than the PC at hand, and find the first non-duplicate.

  if (entries_.size() < 2) return;

  auto is_identical_except_for_pc = [](const EntryBuilder& entry1,
                                       const EntryBuilder& entry2) {
    if (entry1.deopt_index != entry2.deopt_index) return false;
    DCHECK_EQ(entry1.trampoline, entry2.trampoline);
    return entry1.register_indexes == entry2.register_indexes &&
           entry1.stack_indexes->Equals(*entry2.stack_indexes);
  };

  auto remaining_it = entries_.begin();
  auto end = entries_.end();

  for (auto it = entries_.begin(); it != end; ++remaining_it) {
    if (remaining_it != it) *remaining_it = *it;
    // Merge identical entries.
    do {
      ++it;
    } while (it != end && is_identical_except_for_pc(*it, *remaining_it));
  }

  entries_.erase(remaining_it, end);
}

}  // namespace internal
}  // namespace v8
