// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_LOONG64

#include "src/codegen/loong64/constants-loong64.h"

#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

void InstructionBase::SetInstructionBits(
    Instr new_instr, WritableJitAllocation* jit_allocation) {
  // Usually this is aligned, but when de/serializing that's not guaranteed.
  if (jit_allocation) {
    jit_allocation->WriteUnalignedValue(reinterpret_cast<Address>(this),
                                        new_instr);
  } else {
    base::WriteUnalignedValue(reinterpret_cast<Address>(this), new_instr);
  }
}

// -----------------------------------------------------------------------------
// Registers.

// These register names are defined in a way to match the native disassembler
// formatting. See for example the command "objdump -d <binary file>".
const char* Registers::names_[kNumSimuRegisters] = {
    "zero_reg", "ra", "tp", "sp", "a0", "a1", "a2", "a3", "a4", "a5", "a6",
    "a7",       "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "x_reg",
    "fp",       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "pc"};

// List of alias names which can be used when referring to registers.
const Registers::RegisterAlias Registers::aliases_[] = {
    {0, "zero"}, {30, "cp"}, {kInvalidRegister, nullptr}};

const char* Registers::Name(int reg) {
  const char* result;
  if ((0 <= reg) && (reg < kNumSimuRegisters)) {
    result = names_[reg];
  } else {
    result = "noreg";
  }
  return result;
}

int Registers::Number(const char* name) {
  // Look through the canonical names.
  for (int i = 0; i < kNumSimuRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
    }
  }

  // Look through the alias names.
  int i = 0;
  while (aliases_[i].reg != kInvalidRegister) {
    if (strcmp(aliases_[i].name, name) == 0) {
      return aliases_[i].reg;
    }
    i++;
  }

  // No register with the reguested name found.
  return kInvalidRegister;
}

const char* FPURegisters::names_[kNumFPURegisters] = {
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",  "f8",  "f9",  "f10",
    "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21",
    "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"};

// List of alias names which can be used when referring to LoongArch registers.
const FPURegisters::RegisterAlias FPURegisters::aliases_[] = {
    {kInvalidRegister, nullptr}};

const char* FPURegisters::Name(int creg) {
  const char* result;
  if ((0 <= creg) && (creg < kNumFPURegisters)) {
    result = names_[creg];
  } else {
    result = "nocreg";
  }
  return result;
}

int FPURegisters::Number(const char* name) {
  // Look through the canonical names.
  for (int i = 0; i < kNumFPURegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
    }
  }

  // Look through the alias names.
  int i = 0;
  while (aliases_[i].creg != kInvalidRegister) {
    if (strcmp(aliases_[i].name, name) == 0) {
      return aliases_[i].creg;
    }
    i++;
  }

  // No Cregister with the reguested name found.
  return kInvalidFPURegister;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_LOONG64
