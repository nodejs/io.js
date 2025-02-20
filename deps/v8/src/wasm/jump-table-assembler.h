// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_JUMP_TABLE_ASSEMBLER_H_
#define V8_WASM_JUMP_TABLE_ASSEMBLER_H_

#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/macro-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

// The jump table is the central dispatch point for all (direct and indirect)
// invocations in WebAssembly. It holds one slot per function in a module, with
// each slot containing a dispatch to the currently published {WasmCode} that
// corresponds to the function.
//
// Additionally to this main jump table, there exist special jump tables for
// other purposes:
// - the far stub table contains one entry per wasm runtime stub (see
//   {WasmCode::RuntimeStubId}, which jumps to the corresponding embedded
//   builtin, plus (if not the full address space can be reached via the jump
//   table) one entry per wasm function.
// - the lazy compile table contains one entry per wasm function which jumps to
//   the common {WasmCompileLazy} builtin and passes the function index that was
//   invoked.
//
// The main jump table is split into lines of fixed size, with lines laid out
// consecutively within the executable memory of the {NativeModule}. The slots
// in turn are consecutive within a line, but do not cross line boundaries.
//
//   +- L1 -------------------+ +- L2 -------------------+ +- L3 ...
//   | S1 | S2 | ... | Sn | x | | S1 | S2 | ... | Sn | x | | S1  ...
//   +------------------------+ +------------------------+ +---- ...
//
// The above illustrates jump table lines {Li} containing slots {Si} with each
// line containing {n} slots and some padding {x} for alignment purposes.
// Other jump tables are just consecutive.
//
// The main jump table will be patched concurrently while other threads execute
// it. The code at the new target might also have been emitted concurrently, so
// we need to ensure that there is proper synchronization between code emission,
// jump table patching and code execution.
// On Intel platforms, this all works out of the box because there is cache
// coherency between i-cache and d-cache.
// On ARM, it is safe because the i-cache flush after code emission executes an
// "ic ivau" (Instruction Cache line Invalidate by Virtual Address to Point of
// Unification), which broadcasts to all cores. A core which sees the jump table
// update thus also sees the new code. Since the other core does not explicitly
// execute an "isb" (Instruction Synchronization Barrier), it might still
// execute the old code afterwards, which is no problem, since that code remains
// available until it is garbage collected. Garbage collection itself is a
// synchronization barrier though.
class V8_EXPORT_PRIVATE JumpTableAssembler {
 public:
  // Translate an offset into the continuous jump table to a jump table index.
  static uint32_t SlotOffsetToIndex(uint32_t slot_offset) {
    uint32_t line_index = slot_offset / kJumpTableLineSize;
    uint32_t line_offset = slot_offset % kJumpTableLineSize;
    DCHECK_EQ(0, line_offset % kJumpTableSlotSize);
    return line_index * kJumpTableSlotsPerLine +
           line_offset / kJumpTableSlotSize;
  }

  // Translate a jump table index to an offset into the continuous jump table.
  static uint32_t JumpSlotIndexToOffset(uint32_t slot_index) {
    uint32_t line_index = slot_index / kJumpTableSlotsPerLine;
    uint32_t line_offset =
        (slot_index % kJumpTableSlotsPerLine) * kJumpTableSlotSize;
    return line_index * kJumpTableLineSize + line_offset;
  }

  // Determine the size of a jump table containing the given number of slots.
  static constexpr uint32_t SizeForNumberOfSlots(uint32_t slot_count) {
    return ((slot_count + kJumpTableSlotsPerLine - 1) /
            kJumpTableSlotsPerLine) *
           kJumpTableLineSize;
  }

  // Translate a far jump table index to an offset into the table.
  static uint32_t FarJumpSlotIndexToOffset(uint32_t slot_index) {
    return slot_index * kFarJumpTableSlotSize;
  }

  // Translate a far jump table offset to the index into the table.
  static uint32_t FarJumpSlotOffsetToIndex(uint32_t offset) {
    DCHECK_EQ(0, offset % kFarJumpTableSlotSize);
    return offset / kFarJumpTableSlotSize;
  }

  // Determine the size of a far jump table containing the given number of
  // slots.
  static constexpr uint32_t SizeForNumberOfFarJumpSlots(
      int num_runtime_slots, int num_function_slots) {
    int num_entries = num_runtime_slots + num_function_slots;
    return num_entries * kFarJumpTableSlotSize;
  }

  // Translate a slot index to an offset into the lazy compile table.
  static uint32_t LazyCompileSlotIndexToOffset(uint32_t slot_index) {
    return slot_index * kLazyCompileTableSlotSize;
  }

  // Determine the size of a lazy compile table.
  static constexpr uint32_t SizeForNumberOfLazyFunctions(uint32_t slot_count) {
    return slot_count * kLazyCompileTableSlotSize;
  }

  static void GenerateLazyCompileTable(Address base, uint32_t num_slots,
                                       uint32_t num_imported_functions,
                                       Address wasm_compile_lazy_target);

  // Initializes the jump table starting at {base} with jumps to the lazy
  // compile table starting at {lazy_compile_table_start}.
  static void InitializeJumpsToLazyCompileTable(
      Address base, uint32_t num_slots, Address lazy_compile_table_start);

  static void GenerateFarJumpTable(WritableJitAllocation& jit_allocation,
                                   Address base, Address* stub_targets,
                                   int num_runtime_slots,
                                   int num_function_slots) {
    uint32_t table_size =
        SizeForNumberOfFarJumpSlots(num_runtime_slots, num_function_slots);
    // Assume enough space, so the Assembler does not try to grow the buffer.
    JumpTableAssembler jtasm(jit_allocation, base);
    int offset = 0;
    for (int index = 0; index < num_runtime_slots + num_function_slots;
         ++index) {
      DCHECK_EQ(offset, FarJumpSlotIndexToOffset(index));
      // Functions slots initially jump to themselves. They are patched before
      // being used.
      Address target =
          index < num_runtime_slots ? stub_targets[index] : base + offset;
      jtasm.EmitFarJumpSlot(target);
      offset += kFarJumpTableSlotSize;
      DCHECK_EQ(offset, jtasm.pc_offset());
    }
    FlushInstructionCache(base, table_size);
  }

  static void PatchJumpTableSlot(WritableJumpTablePair& jump_table_pair,
                                 Address jump_table_slot,
                                 Address far_jump_table_slot, Address target) {
    // First, try to patch the jump table slot.
    JumpTableAssembler jtasm(jump_table_pair.jump_table(), jump_table_slot);
    if (!jtasm.EmitJumpSlot(target)) {
      // If that fails, we need to patch the far jump table slot, and then
      // update the jump table slot to jump to this far jump table slot.
      DCHECK_NE(kNullAddress, far_jump_table_slot);
      JumpTableAssembler::PatchFarJumpSlot(jump_table_pair.far_jump_table(),
                                           far_jump_table_slot, target);
      CHECK(jtasm.EmitJumpSlot(far_jump_table_slot));
    }
    // We write nops here instead of skipping to avoid partial instructions in
    // the jump table. Partial instructions can cause problems for the
    // disassembler.
    DCHECK_EQ(kJumpTableSlotSize, jtasm.pc_offset());
    FlushInstructionCache(jump_table_slot, kJumpTableSlotSize);
  }

 private:
  // Instantiate a {JumpTableAssembler} for patching.
  explicit JumpTableAssembler(WritableJitAllocation& jit_allocation,
                              Address slot_addr)
      : jit_allocation_(jit_allocation),
        buffer_start_(slot_addr),
        pc_(slot_addr) {}

  WritableJitAllocation& jit_allocation_;
  const Address buffer_start_;
  Address pc_;

  // To allow concurrent patching of the jump table entries, we need to ensure
  // atomicity of the jump table updates. On most architectures, unaligned
  // writes are atomic if they don't cross a cache line. The AMD manual however
  // only guarantees atomicity if the write happens inside a naturally aligned
  // qword. The jump table line size has been chosen to satisfy this.
#if V8_TARGET_ARCH_X64
#ifdef V8_ENABLE_CET_IBT
  static constexpr int kJumpTableSlotSize = 16;
#else  // V8_ENABLE_CET_IBT
  static constexpr int kJumpTableSlotSize = 8;
#endif
  static constexpr int kJumpTableLineSize = kJumpTableSlotSize;
  static constexpr int kFarJumpTableSlotSize = 16;
  static constexpr int kLazyCompileTableSlotSize = 10;
#elif V8_TARGET_ARCH_IA32
  static constexpr int kJumpTableLineSize = 64;
  static constexpr int kJumpTableSlotSize = 5;
  static constexpr int kFarJumpTableSlotSize = 5;
  static constexpr int kLazyCompileTableSlotSize = 10;
#elif V8_TARGET_ARCH_ARM
  static constexpr int kJumpTableLineSize = 2 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 2 * kInstrSize;
  static constexpr int kFarJumpTableSlotSize = 2 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 4 * kInstrSize;
#elif V8_TARGET_ARCH_ARM64
#if V8_ENABLE_CONTROL_FLOW_INTEGRITY
  static constexpr int kJumpTableLineSize = 2 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 2 * kInstrSize;
#else
  static constexpr int kJumpTableLineSize = 1 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 1 * kInstrSize;
#endif
  static constexpr int kFarJumpTableSlotSize = 4 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 4 * kInstrSize;
#elif V8_TARGET_ARCH_S390X
  static constexpr int kJumpTableLineSize = 128;
  static constexpr int kJumpTableSlotSize = 8;
  static constexpr int kFarJumpTableSlotSize = 24;
  static constexpr int kLazyCompileTableSlotSize = 32;
#elif V8_TARGET_ARCH_PPC64
  static constexpr int kJumpTableLineSize = 64;
  static constexpr int kJumpTableSlotSize = 1 * kInstrSize;
  static constexpr int kFarJumpTableSlotSize = 12 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 12 * kInstrSize;
#elif V8_TARGET_ARCH_MIPS
  static constexpr int kJumpTableLineSize = 8 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 8 * kInstrSize;
  static constexpr int kFarJumpTableSlotSize = 4 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 6 * kInstrSize;
#elif V8_TARGET_ARCH_MIPS64
  static constexpr int kJumpTableLineSize = 8 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 8 * kInstrSize;
  static constexpr int kFarJumpTableSlotSize = 8 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 10 * kInstrSize;
#elif V8_TARGET_ARCH_RISCV64
  static constexpr int kJumpTableSlotSize = 2 * kInstrSize;
  static constexpr int kJumpTableLineSize = kJumpTableSlotSize;
  static constexpr int kFarJumpTableSlotSize = 6 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 3 * kInstrSize;
#elif V8_TARGET_ARCH_RISCV32
  static constexpr int kJumpTableSlotSize = 5 * kInstrSize;
  static constexpr int kJumpTableLineSize = kJumpTableSlotSize;
  static constexpr int kFarJumpTableSlotSize = kJumpTableSlotSize;
  static constexpr int kLazyCompileTableSlotSize = 3 * kInstrSize;
#elif V8_TARGET_ARCH_LOONG64
  static constexpr int kJumpTableLineSize = 1 * kInstrSize;
  static constexpr int kJumpTableSlotSize = 1 * kInstrSize;
  static constexpr int kFarJumpTableSlotSize = 6 * kInstrSize;
  static constexpr int kLazyCompileTableSlotSize = 3 * kInstrSize;
#else
#error Unknown architecture.
#endif

  static constexpr int kJumpTableSlotsPerLine =
      kJumpTableLineSize / kJumpTableSlotSize;
  static_assert(kJumpTableSlotsPerLine >= 1);

  void EmitLazyCompileJumpSlot(uint32_t func_index,
                               Address lazy_compile_target);

  // Returns {true} if the jump fits in the jump table slot, {false} otherwise.
  bool EmitJumpSlot(Address target);

  // Initially emit a far jump slot.
  void EmitFarJumpSlot(Address target);

  // Patch an existing far jump slot, and make sure that this updated eventually
  // becomes available to all execution units that might execute this code.
  static void PatchFarJumpSlot(WritableJitAllocation& jit_allocation,
                               Address slot, Address target);

  void SkipUntil(int offset);

  int pc_offset() const { return static_cast<int>(pc_ - buffer_start_); }

  template <typename V>
  void emit(V value);

  template <typename V>
  void emit(V value, RelaxedStoreTag);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_JUMP_TABLE_ASSEMBLER_H_
