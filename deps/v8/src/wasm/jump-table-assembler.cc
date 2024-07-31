// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/jump-table-assembler.h"

#include "src/codegen/macro-assembler-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

// static
void JumpTableAssembler::GenerateLazyCompileTable(
    Address base, uint32_t num_slots, uint32_t num_imported_functions,
    Address wasm_compile_lazy_target) {
  uint32_t lazy_compile_table_size = num_slots * kLazyCompileTableSlotSize;
  WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
      base, RoundUp<kCodeAlignment>(lazy_compile_table_size),
      ThreadIsolation::JitAllocationType::kWasmLazyCompileTable);
  // Assume enough space, so the Assembler does not try to grow the buffer.
  JumpTableAssembler jtasm(base, lazy_compile_table_size + 256);
  for (uint32_t slot_index = 0; slot_index < num_slots; ++slot_index) {
    DCHECK_EQ(slot_index * kLazyCompileTableSlotSize, jtasm.pc_offset());
    jtasm.EmitLazyCompileJumpSlot(slot_index + num_imported_functions,
                                  wasm_compile_lazy_target);
  }
  DCHECK_EQ(lazy_compile_table_size, jtasm.pc_offset());
  FlushInstructionCache(base, lazy_compile_table_size);
}

void JumpTableAssembler::InitializeJumpsToLazyCompileTable(
    Address base, uint32_t num_slots, Address lazy_compile_table_start) {
  uint32_t jump_table_size = SizeForNumberOfSlots(num_slots);
  WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
      base, RoundUp<kCodeAlignment>(jump_table_size),
      ThreadIsolation::JitAllocationType::kWasmJumpTable);
  JumpTableAssembler jtasm(base, jump_table_size + 256);

  for (uint32_t slot_index = 0; slot_index < num_slots; ++slot_index) {
    // Make sure we write at the correct offset.
    int slot_offset =
        static_cast<int>(JumpTableAssembler::JumpSlotIndexToOffset(slot_index));

    jtasm.SkipUntil(slot_offset);

    Address target =
        lazy_compile_table_start +
        JumpTableAssembler::LazyCompileSlotIndexToOffset(slot_index);

    int offset_before_emit = jtasm.pc_offset();
    // This function initializes the first jump table with jumps to the lazy
    // compile table. Both get allocated in the constructor of the
    // {NativeModule}, so they both should end up in the initial code space.
    // Jumps within one code space can always be near jumps, so the following
    // call to {EmitJumpSlot} should always succeed. If the call fails, then
    // either the jump table allocation was changed incorrectly so that the lazy
    // compile table was not within near-jump distance of the jump table
    // anymore (e.g. the initial code space was too small to fit both tables),
    // or the code space was allocated larger than the maximum near-jump
    // distance.
    CHECK(jtasm.EmitJumpSlot(target));
    int written_bytes = jtasm.pc_offset() - offset_before_emit;
    // We write nops here instead of skipping to avoid partial instructions in
    // the jump table. Partial instructions can cause problems for the
    // disassembler.
    jtasm.NopBytes(kJumpTableSlotSize - written_bytes);
  }
  FlushInstructionCache(base, jump_table_size);
}

// The implementation is compact enough to implement it inline here. If it gets
// much bigger, we might want to split it in a separate file per architecture.
#if V8_TARGET_ARCH_X64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  // Use a push, because mov to an extended register takes 6 bytes.
  pushq_imm32(func_index);  // 5 bytes
  intptr_t displacement =
      static_cast<intptr_t>(reinterpret_cast<uint8_t*>(lazy_compile_target) -
                            (pc_ + kNearJmpInstrSize));
  DCHECK(is_int32(displacement));
  near_jmp(displacement, RelocInfo::NO_INFO);  // 5 bytes
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  intptr_t displacement =
      static_cast<intptr_t>(reinterpret_cast<uint8_t*>(target) -
                            (pc_ + kEndbrSize + kNearJmpInstrSize));
  if (!is_int32(displacement)) return false;
  CodeEntry();                                 // kEndbrSize bytes (0 or 4)
  near_jmp(displacement, RelocInfo::NO_INFO);  // 5 bytes
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  Label data;
  [[maybe_unused]] int start_offset = pc_offset();
  jmp(Operand(&data));  // 6 bytes
  Nop(2);               // 2 bytes
  // The data must be properly aligned, so it can be patched atomically (see
  // {PatchFarJumpSlot}).
  DCHECK_EQ(start_offset + kSystemPointerSize, pc_offset());
  bind(&data);
  dq(target);  // 8 bytes
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  // The slot needs to be pointer-size aligned so we can atomically update it.
  DCHECK(IsAligned(slot, kSystemPointerSize));
  // Offset of the target is at 8 bytes, see {EmitFarJumpSlot}.
  reinterpret_cast<std::atomic<Address>*>(slot + kSystemPointerSize)
      ->store(target, std::memory_order_relaxed);
  // The update is atomic because the address is properly aligned.
  // Because of cache coherence, the data update will eventually be seen by all
  // cores. It's ok if they temporarily jump to the old target.
}

void JumpTableAssembler::NopBytes(int bytes) {
  if (bytes) Nop(bytes);
}

void JumpTableAssembler::SkipUntil(int offset) {
  DCHECK_GE(offset, pc_offset());
  pc_ += offset - pc_offset();
}

#elif V8_TARGET_ARCH_IA32
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  mov(kWasmCompileLazyFuncIndexRegister, func_index);  // 5 bytes
  jmp(lazy_compile_target, RelocInfo::NO_INFO);        // 5 bytes
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  jmp(target, RelocInfo::NO_INFO);
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  jmp(target, RelocInfo::NO_INFO);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  UNREACHABLE();
}

void JumpTableAssembler::NopBytes(int bytes) {
  if (bytes) Nop(bytes);
}

void JumpTableAssembler::SkipUntil(int offset) {
  DCHECK_GE(offset, pc_offset());
  pc_ += offset - pc_offset();
}

#elif V8_TARGET_ARCH_ARM
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  // Load function index to a register.
  // This generates [movw, movt] on ARMv7 and later, [ldr, constant pool marker,
  // constant] on ARMv6.
  Move32BitImmediate(kWasmCompileLazyFuncIndexRegister, Operand(func_index));
  // EmitJumpSlot emits either [b], [movw, movt, mov] (ARMv7+), or [ldr,
  // constant].
  // In total, this is <=5 instructions on all architectures.
  // TODO(arm): Optimize this for code size; lazy compile is not performance
  // critical, as it's only executed once per function.
  EmitJumpSlot(lazy_compile_target);
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  // Note that {Move32BitImmediate} emits [ldr, constant] for the relocation
  // mode used below, we need this to allow concurrent patching of this slot.
  Move32BitImmediate(pc, Operand(target, RelocInfo::WASM_CALL));
  CheckConstPool(true, false);  // force emit of const pool
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  // Load from [pc + kInstrSize] to pc. Note that {pc} points two instructions
  // after the currently executing one.
  ldr_pcrel(pc, -kInstrSize);  // 1 instruction
  dd(target);                  // 4 bytes (== 1 instruction)
  static_assert(kInstrSize == kInt32Size);
  static_assert(kFarJumpTableSlotSize == 2 * kInstrSize);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  UNREACHABLE();
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_ARM64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  int start = pc_offset();
  CodeEntry();                                             // 0-1 instr
  Mov(kWasmCompileLazyFuncIndexRegister.W(), func_index);  // 1-2 instr
  Jump(lazy_compile_target, RelocInfo::NO_INFO);           // 1 instr
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK(nop_bytes == 0 || nop_bytes == kInstrSize);
  if (nop_bytes) nop();
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  static constexpr ptrdiff_t kCodeEntryMarkerSize = kInstrSize;
#else
  static constexpr ptrdiff_t kCodeEntryMarkerSize = 0;
#endif

  uint8_t* jump_pc = pc_ + kCodeEntryMarkerSize;
  ptrdiff_t jump_distance = reinterpret_cast<uint8_t*>(target) - jump_pc;
  DCHECK_EQ(0, jump_distance % kInstrSize);
  int64_t instr_offset = jump_distance / kInstrSize;
  if (!MacroAssembler::IsNearCallOffset(instr_offset)) {
    return false;
  }

  CodeEntry();

  DCHECK_EQ(jump_pc, pc_);
  DCHECK_EQ(instr_offset,
            reinterpret_cast<Instr*>(target) - reinterpret_cast<Instr*>(pc_));
  DCHECK(is_int26(instr_offset));
  b(static_cast<int>(instr_offset));
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  // This code uses hard-coded registers and instructions (and avoids
  // {UseScratchRegisterScope} or {InstructionAccurateScope}) because this code
  // will only be called for the very specific runtime slot table, and we want
  // to have maximum control over the generated code.
  // Do not reuse this code without validating that the same assumptions hold.
  CodeEntry();  // 0-1 instructions
  constexpr Register kTmpReg = x16;
  DCHECK(TmpList()->IncludesAliasOf(kTmpReg));
  int kOffset = ENABLE_CONTROL_FLOW_INTEGRITY_BOOL ? 3 : 2;
  // Load from [pc + kOffset * kInstrSize] to {kTmpReg}, then branch there.
  ldr_pcrel(kTmpReg, kOffset);  // 1 instruction
  br(kTmpReg);                  // 1 instruction
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  nop();       // To keep the target below aligned to kSystemPointerSize.
#endif
  dq(target);  // 8 bytes (== 2 instructions)
  static_assert(2 * kInstrSize == kSystemPointerSize);
  const int kSlotCount = ENABLE_CONTROL_FLOW_INTEGRITY_BOOL ? 6 : 4;
  static_assert(kFarJumpTableSlotSize == kSlotCount * kInstrSize);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  // See {EmitFarJumpSlot} for the offset of the target (16 bytes with
  // CFI enabled, 8 bytes otherwise).
  int kTargetOffset =
      ENABLE_CONTROL_FLOW_INTEGRITY_BOOL ? 4 * kInstrSize : 2 * kInstrSize;
  // The slot needs to be pointer-size aligned so we can atomically update it.
  DCHECK(IsAligned(slot + kTargetOffset, kSystemPointerSize));
  reinterpret_cast<std::atomic<Address>*>(slot + kTargetOffset)
      ->store(target, std::memory_order_relaxed);
  // The data update is guaranteed to be atomic since it's a properly aligned
  // and stores a single machine word. This update will eventually be observed
  // by any concurrent [ldr] on the same address because of the data cache
  // coherence. It's ok if other cores temporarily jump to the old target.
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_S390X
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  // Load function index to r7. 6 bytes
  lgfi(kWasmCompileLazyFuncIndexRegister, Operand(func_index));
  // Jump to {lazy_compile_target}. 6 bytes or 12 bytes
  mov(r1, Operand(lazy_compile_target, RelocInfo::CODE_TARGET));
  b(r1);  // 2 bytes
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  intptr_t relative_target = reinterpret_cast<uint8_t*>(target) - pc_;

  if (!is_int32(relative_target / 2)) {
    return false;
  }

  brcl(al, Operand(relative_target / 2));
  nop(0);  // make the slot align to 8 bytes
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  Label target_addr;
  lgrl(ip, &target_addr);  // 6 bytes
  b(ip);                   // 8 bytes

  CHECK_EQ(reinterpret_cast<Address>(pc_) & 0x7, 0);  // Alignment
  bind(&target_addr);
  dp(target);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  Address target_addr = slot + 8;
  reinterpret_cast<std::atomic<Address>*>(target_addr)
      ->store(target, std::memory_order_relaxed);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % 2);
  for (; bytes > 0; bytes -= 2) {
    nop(0);
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_MIPS64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  int start = pc_offset();
  li(kWasmCompileLazyFuncIndexRegister, func_index);  // max. 2 instr
  // Jump produces max. 4 instructions for 32-bit platform
  // and max. 6 instructions for 64-bit platform.
  Jump(lazy_compile_target, RelocInfo::NO_INFO);
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK_EQ(nop_bytes % kInstrSize, 0);
  for (int i = 0; i < nop_bytes; i += kInstrSize) nop();
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  PatchAndJump(target);
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  li(t9, Operand(target, RelocInfo::OFF_HEAP_TARGET));
  Jump(t9);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  UNREACHABLE();
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_LOONG64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  DCHECK(is_int32(func_index));
  int start = pc_offset();
  li(kWasmCompileLazyFuncIndexRegister, (int32_t)func_index);  // max. 2 instr
  // EmitJumpSlot produces 1 instructions.
  CHECK(EmitJumpSlot(lazy_compile_target));
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK_EQ(nop_bytes % kInstrSize, 0);
  for (int i = 0; i < nop_bytes; i += kInstrSize) nop();
}
bool JumpTableAssembler::EmitJumpSlot(Address target) {
  intptr_t relative_target = reinterpret_cast<uint8_t*>(target) - pc_;
  DCHECK_EQ(relative_target % 4, 0);
  intptr_t instr_offset = relative_target / kInstrSize;
  if (!is_int26(instr_offset)) {
    return false;
  }

  b(instr_offset);
  return true;
}
void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  pcaddi(t7, 4);
  Ld_d(t7, MemOperand(t7, 0));
  jirl(zero_reg, t7, 0);
  nop();  // pc_ should be align.
  DCHECK_EQ(reinterpret_cast<uint64_t>(pc_) % 8, 0);
  dq(target);
}
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  Address target_addr = slot + kFarJumpTableSlotSize - 8;
  reinterpret_cast<std::atomic<Address>*>(target_addr)
      ->store(target, std::memory_order_relaxed);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_PPC64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  int start = pc_offset();
  // Load function index to register. max 5 instrs
  mov(kWasmCompileLazyFuncIndexRegister, Operand(func_index));
  // Jump to {lazy_compile_target}. max 5 instrs
  mov(r0, Operand(lazy_compile_target));
  mtctr(r0);
  bctr();
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK_EQ(nop_bytes % kInstrSize, 0);
  for (int i = 0; i < nop_bytes; i += kInstrSize) nop();
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  intptr_t relative_target = reinterpret_cast<uint8_t*>(target) - pc_;

  if (!is_int26(relative_target)) {
    return false;
  }

  b(relative_target, LeaveLK);
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  uint8_t* start = pc_;
  mov(ip, Operand(reinterpret_cast<Address>(start + kFarJumpTableSlotSize -
                                            8)));  // 5 instr
  LoadU64(ip, MemOperand(ip));
  mtctr(ip);
  bctr();
  uint8_t* end = pc_;
  int used = end - start;
  CHECK(used < kFarJumpTableSlotSize - 8);
  NopBytes(kFarJumpTableSlotSize - 8 - used);
  CHECK_EQ(reinterpret_cast<Address>(pc_) & 0x7, 0);  // Alignment
  dp(target);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  Address target_addr = slot + kFarJumpTableSlotSize - 8;
  reinterpret_cast<std::atomic<Address>*>(target_addr)
      ->store(target, std::memory_order_relaxed);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % 4);
  for (; bytes > 0; bytes -= 4) {
    nop(0);
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_RISCV64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  int start = pc_offset();
  li(kWasmCompileLazyFuncIndexRegister, func_index);  // max. 2 instr
  // Jump produces max. 8 instructions (include constant pool and j)
  Jump(lazy_compile_target, RelocInfo::NO_INFO);
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK_EQ(nop_bytes % kInstrSize, 0);
  for (int i = 0; i < nop_bytes; i += kInstrSize) nop();
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  PatchAndJump(target);
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  UseScratchRegisterScope temp(this);
  Register rd = temp.Acquire();
  auipc(rd, 0);
  ld(rd, rd, 4 * kInstrSize);
  Jump(rd);
  nop();
  dq(target);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  UNREACHABLE();
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#elif V8_TARGET_ARCH_RISCV32
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  int start = pc_offset();
  li(kWasmCompileLazyFuncIndexRegister, func_index);  // max. 2 instr
  // Jump produces max. 8 instructions (include constant pool and j)
  Jump(lazy_compile_target, RelocInfo::NO_INFO);
  int nop_bytes = start + kLazyCompileTableSlotSize - pc_offset();
  DCHECK_EQ(nop_bytes % kInstrSize, 0);
  for (int i = 0; i < nop_bytes; i += kInstrSize) nop();
}

bool JumpTableAssembler::EmitJumpSlot(Address target) {
  PatchAndJump(target);
  return true;
}

void JumpTableAssembler::EmitFarJumpSlot(Address target) {
  UseScratchRegisterScope temp(this);
  Register rd = temp.Acquire();
  auipc(rd, 0);
  lw(rd, rd, 4 * kInstrSize);
  Jump(rd);
  nop();
  dq(target);
}

// static
void JumpTableAssembler::PatchFarJumpSlot(Address slot, Address target) {
  UNREACHABLE();
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

void JumpTableAssembler::SkipUntil(int offset) {
  // On this platform the jump table is not zapped with valid instructions, so
  // skipping over bytes is not allowed.
  DCHECK_EQ(offset, pc_offset());
}

#else
#error Unknown architecture.
#endif

}  // namespace wasm
}  // namespace internal
}  // namespace v8
