// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_LINKAGE_H_
#define V8_WASM_WASM_LINKAGE_H_

#include "src/codegen/aligned-slot-allocator.h"
#include "src/codegen/assembler-arch.h"
#include "src/codegen/linkage-location.h"
#include "src/codegen/machine-type.h"

namespace v8 {
namespace internal {
namespace wasm {

// TODO(wasm): optimize calling conventions to be both closer to C++ (to
// reduce adapter costs for fast Wasm <-> C++ calls) and to be more efficient
// in general.

#if V8_TARGET_ARCH_IA32
// ===========================================================================
// == ia32 ===================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {esi, eax, edx, ecx};
constexpr Register kGpReturnRegisters[] = {eax, edx};
constexpr DoubleRegister kFpParamRegisters[] = {xmm1, xmm2, xmm3,
                                                xmm4, xmm5, xmm6};
constexpr DoubleRegister kFpReturnRegisters[] = {xmm1, xmm2};

#elif V8_TARGET_ARCH_X64
// ===========================================================================
// == x64 ====================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {rsi, rax, rdx, rcx, rbx, r9};
constexpr Register kGpReturnRegisters[] = {rax, rdx};
constexpr DoubleRegister kFpParamRegisters[] = {xmm1, xmm2, xmm3,
                                                xmm4, xmm5, xmm6};
constexpr DoubleRegister kFpReturnRegisters[] = {xmm1, xmm2};

#elif V8_TARGET_ARCH_ARM
// ===========================================================================
// == arm ====================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {r3, r0, r2, r6};
constexpr Register kGpReturnRegisters[] = {r0, r1};
// ARM d-registers must be in even/odd D-register pairs for correct allocation.
constexpr DoubleRegister kFpParamRegisters[] = {d0, d1, d2, d3, d4, d5, d6, d7};
constexpr DoubleRegister kFpReturnRegisters[] = {d0, d1};

#elif V8_TARGET_ARCH_ARM64
// ===========================================================================
// == arm64 ====================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {x7, x0, x2, x3, x4, x5, x6};
constexpr Register kGpReturnRegisters[] = {x0, x1};
constexpr DoubleRegister kFpParamRegisters[] = {d0, d1, d2, d3, d4, d5, d6, d7};
constexpr DoubleRegister kFpReturnRegisters[] = {d0, d1};

#elif V8_TARGET_ARCH_MIPS64
// ===========================================================================
// == mips64 =================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {a0, a2, a3, a4, a5, a6, a7};
constexpr Register kGpReturnRegisters[] = {v0, v1};
constexpr DoubleRegister kFpParamRegisters[] = {f2, f4, f6, f8, f10, f12, f14};
constexpr DoubleRegister kFpReturnRegisters[] = {f2, f4};

#elif V8_TARGET_ARCH_LOONG64
// ===========================================================================
// == LOONG64 ================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {a7, a0, a2, a3, a4, a5, a6};
constexpr Register kGpReturnRegisters[] = {a0, a1};
constexpr DoubleRegister kFpParamRegisters[] = {f0, f1, f2, f3, f4, f5, f6, f7};
constexpr DoubleRegister kFpReturnRegisters[] = {f0, f1};

#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
// ===========================================================================
// == ppc & ppc64 ============================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {r10, r3, r5, r6, r7, r8, r9};
constexpr Register kGpReturnRegisters[] = {r3, r4};
constexpr DoubleRegister kFpParamRegisters[] = {d1, d2, d3, d4, d5, d6, d7, d8};
constexpr DoubleRegister kFpReturnRegisters[] = {d1, d2};

#elif V8_TARGET_ARCH_S390X
// ===========================================================================
// == s390x ==================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {r6, r2, r4, r5};
constexpr Register kGpReturnRegisters[] = {r2, r3};
constexpr DoubleRegister kFpParamRegisters[] = {d0, d2, d4, d6};
constexpr DoubleRegister kFpReturnRegisters[] = {d0, d2};

#elif V8_TARGET_ARCH_S390
// ===========================================================================
// == s390 ===================================================================
// ===========================================================================
constexpr Register kGpParamRegisters[] = {r6, r2, r4, r5};
constexpr Register kGpReturnRegisters[] = {r2, r3};
constexpr DoubleRegister kFpParamRegisters[] = {d0, d2};
constexpr DoubleRegister kFpReturnRegisters[] = {d0, d2};

#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
// ===========================================================================
// == riscv64 =================================================================
// ===========================================================================
// Note that kGpParamRegisters and kFpParamRegisters are used in
// Builtins::Generate_WasmCompileLazy (builtins-riscv.cc)
constexpr Register kGpParamRegisters[] = {a7, a0, a2, a3, a4, a5, a6};
constexpr Register kGpReturnRegisters[] = {a0, a1};
constexpr DoubleRegister kFpParamRegisters[] = {fa0, fa1, fa2, fa3,
                                                fa4, fa5, fa6, fa7};
constexpr DoubleRegister kFpReturnRegisters[] = {fa0, fa1};

#else
// ===========================================================================
// == unknown ================================================================
// ===========================================================================
// Do not use any registers, we will just always use the stack.
constexpr Register kGpParamRegisters[] = {};
constexpr Register kGpReturnRegisters[] = {};
constexpr DoubleRegister kFpParamRegisters[] = {};
constexpr DoubleRegister kFpReturnRegisters[] = {};

#endif

#if V8_TARGET_ARCH_PPC64
// Platforms where a Floating Point value is represented in Double Precision
// format in a FP register.
constexpr bool kIsFpAlwaysDouble = true;
#else
constexpr bool kIsFpAlwaysDouble = false;
#endif
#if V8_TARGET_BIG_ENDIAN
constexpr bool kIsBigEndian = true;
#else
constexpr bool kIsBigEndian = false;
#endif
#if V8_TARGET_ARCH_S390_LE_SIM
constexpr bool kIsBigEndianOnSim = true;
#else
constexpr bool kIsBigEndianOnSim = false;
#endif

// The parameter index where the instance parameter should be placed in wasm
// call descriptors. This is used by the Int64Lowering::LowerNode method.
// TODO(14499): Rename to kWasmInstanceDataParameterIndex.
constexpr int kWasmInstanceParameterIndex = 0;
static_assert(kWasmInstanceRegister ==
              kGpParamRegisters[kWasmInstanceParameterIndex]);

class LinkageAllocator {
 public:
  template <size_t kNumGpRegs, size_t kNumFpRegs>
  constexpr LinkageAllocator(const Register (&gp)[kNumGpRegs],
                             const DoubleRegister (&fp)[kNumFpRegs])
      : LinkageAllocator(gp, kNumGpRegs, fp, kNumFpRegs) {}

  constexpr LinkageAllocator(const Register* gp, int gpc,
                             const DoubleRegister* fp, int fpc)
      : gp_count_(gpc), gp_regs_(gp), fp_count_(fpc), fp_regs_(fp) {}

  bool CanAllocateGP() const { return gp_offset_ < gp_count_; }
  bool CanAllocateFP(MachineRepresentation rep) const {
#if V8_TARGET_ARCH_ARM
    switch (rep) {
      case MachineRepresentation::kFloat32: {
        // Get the next D-register (Liftoff only uses the even S-registers).
        int next = fp_allocator_.NextSlot(2) / 2;
        // Only the lower 16 D-registers alias S-registers.
        return next < fp_count_ && fp_regs_[next].code() < 16;
      }
      case MachineRepresentation::kFloat64: {
        int next = fp_allocator_.NextSlot(2) / 2;
        return next < fp_count_;
      }
      case MachineRepresentation::kSimd128: {
        int next = fp_allocator_.NextSlot(4) / 2;
        return next < fp_count_ - 1;  // 2 D-registers are required.
      }
      default:
        UNREACHABLE();
        return false;
    }
#else
    return fp_offset_ < fp_count_;
#endif
  }

  int NextGpReg() {
    DCHECK_LT(gp_offset_, gp_count_);
    return gp_regs_[gp_offset_++].code();
  }

  int NextFpReg(MachineRepresentation rep) {
    DCHECK(CanAllocateFP(rep));
#if V8_TARGET_ARCH_ARM
    switch (rep) {
      case MachineRepresentation::kFloat32: {
        // Liftoff uses only even-numbered S-registers, and encodes them using
        // the code of the corresponding D-register. This limits the calling
        // interface to only using the even-numbered S-registers.
        int d_reg_code = NextFpReg(MachineRepresentation::kFloat64);
        DCHECK_GT(16, d_reg_code);  // D16 - D31 don't alias S-registers.
        return d_reg_code * 2;
      }
      case MachineRepresentation::kFloat64: {
        int next = fp_allocator_.Allocate(2) / 2;
        return fp_regs_[next].code();
      }
      case MachineRepresentation::kSimd128: {
        int next = fp_allocator_.Allocate(4) / 2;
        int d_reg_code = fp_regs_[next].code();
        // Check that result and the next D-register pair.
        DCHECK_EQ(0, d_reg_code % 2);
        DCHECK_EQ(d_reg_code + 1, fp_regs_[next + 1].code());
        return d_reg_code / 2;
      }
      default:
        UNREACHABLE();
    }
#else
    return fp_regs_[fp_offset_++].code();
#endif
  }

  // Stackslots are counted upwards starting from 0 (or the offset set by
  // {SetStackOffset}. If {type} needs more than one stack slot, the lowest
  // used stack slot is returned.
  int NextStackSlot(MachineRepresentation type) {
    int num_slots =
        AlignedSlotAllocator::NumSlotsForWidth(ElementSizeInBytes(type));
    int slot = slot_allocator_.Allocate(num_slots);
    return slot;
  }

  // Set an offset for the stack slots returned by {NextStackSlot} and
  // {NumStackSlots}. Can only be called before any call to {NextStackSlot}.
  void SetStackOffset(int offset) {
    DCHECK_LE(0, offset);
    DCHECK_EQ(0, slot_allocator_.Size());
    slot_allocator_.AllocateUnaligned(offset);
  }

  int NumStackSlots() const { return slot_allocator_.Size(); }

  void EndSlotArea() { slot_allocator_.AllocateUnaligned(0); }

 private:
  const int gp_count_;
  int gp_offset_ = 0;
  const Register* const gp_regs_;

  const int fp_count_;
#if V8_TARGET_ARCH_ARM
  // Use an aligned slot allocator to model ARM FP register aliasing. The slots
  // are 32 bits, so 2 slots are required for a D-register, 4 for a Q-register.
  AlignedSlotAllocator fp_allocator_;
#else
  int fp_offset_ = 0;
#endif
  const DoubleRegister* const fp_regs_;

  AlignedSlotAllocator slot_allocator_;
};

// Helper for allocating either an GP or FP reg, or the next stack slot.
class LinkageLocationAllocator {
 public:
  template <size_t kNumGpRegs, size_t kNumFpRegs>
  constexpr LinkageLocationAllocator(const Register (&gp)[kNumGpRegs],
                                     const DoubleRegister (&fp)[kNumFpRegs],
                                     int slot_offset)
      : allocator_(LinkageAllocator(gp, fp)), slot_offset_(slot_offset) {}

  LinkageLocation Next(MachineRepresentation rep) {
    MachineType type = MachineType::TypeForRepresentation(rep);
    if (IsFloatingPoint(rep)) {
      if (allocator_.CanAllocateFP(rep)) {
        int reg_code = allocator_.NextFpReg(rep);
        return LinkageLocation::ForRegister(reg_code, type);
      }
    } else if (allocator_.CanAllocateGP()) {
      int reg_code = allocator_.NextGpReg();
      return LinkageLocation::ForRegister(reg_code, type);
    }
    // Cannot use register; use stack slot.
    int index = -1 - (slot_offset_ + allocator_.NextStackSlot(rep));
    return LinkageLocation::ForCallerFrameSlot(index, type);
  }

  int NumStackSlots() const { return allocator_.NumStackSlots(); }
  void EndSlotArea() { allocator_.EndSlotArea(); }

 private:
  LinkageAllocator allocator_;
  // Since params and returns are in different stack frames, we must allocate
  // them separately. Parameter slots don't need an offset, but return slots
  // must be offset to just before the param slots, using this |slot_offset_|.
  int slot_offset_;
};
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_LINKAGE_H_
