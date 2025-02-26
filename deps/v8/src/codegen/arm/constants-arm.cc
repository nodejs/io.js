// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM

#include "src/codegen/arm/constants-arm.h"

#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

void Instruction::SetInstructionBits(Instr value,
                                     WritableJitAllocation* jit_allocation) {
  if (jit_allocation) {
    jit_allocation->WriteValue(reinterpret_cast<Address>(this), value);
  } else {
    *reinterpret_cast<Instr*>(this) = value;
  }
}

Float64 Instruction::DoubleImmedVmov() const {
  // Reconstruct a double from the immediate encoded in the vmov instruction.
  //
  //   instruction: [xxxxxxxx,xxxxabcd,xxxxxxxx,xxxxefgh]
  //   double: [aBbbbbbb,bbcdefgh,00000000,00000000,
  //            00000000,00000000,00000000,00000000]
  //
  // where B = ~b. Only the high 16 bits are affected.
  uint64_t high16;
  high16 = (Bits(17, 16) << 4) | Bits(3, 0);  // xxxxxxxx,xxcdefgh.
  high16 |= (0xFF * Bit(18)) << 6;            // xxbbbbbb,bbxxxxxx.
  high16 |= (Bit(18) ^ 1) << 14;              // xBxxxxxx,xxxxxxxx.
  high16 |= Bit(19) << 15;                    // axxxxxxx,xxxxxxxx.

  uint64_t imm = high16 << 48;
  return Float64::FromBits(imm);
}

// These register names are defined in a way to match the native disassembler
// formatting. See for example the command "objdump -d <binary file>".
const char* Registers::names_[kNumRegisters] = {
    "r0", "r1", "r2",  "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "fp", "ip", "sp", "lr", "pc",
};

// List of alias names which can be used when referring to ARM registers.
const Registers::RegisterAlias Registers::aliases_[] = {
    {10, "sl"},  {11, "r11"}, {12, "r12"},           {13, "r13"},
    {14, "r14"}, {15, "r15"}, {kNoRegister, nullptr}};

// Support for VFP registers s0 to s31 (d0 to d15) and d16-d31.
// Note that "sN:sM" is the same as "dN/2" up to d15.
// These register names are defined in a way to match the native disassembler
// formatting. See for example the command "objdump -d <binary file>".
const char* VFPRegisters::names_[kNumVFPRegisters] = {
    "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",  "s8",  "s9",  "s10",
    "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21",
    "s22", "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31", "d0",
    "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",  "d8",  "d9",  "d10", "d11",
    "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22",
    "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"};

const char* VFPRegisters::Name(int reg, bool is_double) {
  DCHECK((0 <= reg) && (reg < kNumVFPRegisters));
  return names_[reg + (is_double ? kNumVFPSingleRegisters : 0)];
}

int VFPRegisters::Number(const char* name, bool* is_double) {
  for (int i = 0; i < kNumVFPRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      if (i < kNumVFPSingleRegisters) {
        *is_double = false;
        return i;
      } else {
        *is_double = true;
        return i - kNumVFPSingleRegisters;
      }
    }
  }

  // No register with the requested name found.
  return kNoRegister;
}

int Registers::Number(const char* name) {
  // Look through the canonical names.
  for (int i = 0; i < kNumRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
    }
  }

  // Look through the alias names.
  int i = 0;
  while (aliases_[i].reg != kNoRegister) {
    if (strcmp(aliases_[i].name, name) == 0) {
      return aliases_[i].reg;
    }
    i++;
  }

  // No register with the requested name found.
  return kNoRegister;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM
