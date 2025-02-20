// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_S390X

#include "src/codegen/s390/constants-s390.h"

#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

void Instruction::SetInstructionBits(Instr value,
                                     WritableJitAllocation* jit_allocation) {
  if (jit_allocation) {
    jit_allocation->WriteUnalignedValue(reinterpret_cast<Address>(this), value);
  } else {
    *reinterpret_cast<Instr*>(this) = value;
  }
}

Instruction::OpcodeFormatType Instruction::OpcodeFormatTable[] = {
    // Based on Figure B-3 in z/Architecture Principles of
    // Operation.
    TWO_BYTE_OPCODE,           // 0x00
    TWO_BYTE_OPCODE,           // 0x01
    TWO_BYTE_DISJOINT_OPCODE,  // 0x02
    TWO_BYTE_DISJOINT_OPCODE,  // 0x03
    ONE_BYTE_OPCODE,           // 0x04
    ONE_BYTE_OPCODE,           // 0x05
    ONE_BYTE_OPCODE,           // 0x06
    ONE_BYTE_OPCODE,           // 0x07
    ONE_BYTE_OPCODE,           // 0x08
    ONE_BYTE_OPCODE,           // 0x09
    ONE_BYTE_OPCODE,           // 0x0A
    ONE_BYTE_OPCODE,           // 0x0B
    ONE_BYTE_OPCODE,           // 0x0C
    ONE_BYTE_OPCODE,           // 0x0D
    ONE_BYTE_OPCODE,           // 0x0E
    ONE_BYTE_OPCODE,           // 0x0F
    ONE_BYTE_OPCODE,           // 0x10
    ONE_BYTE_OPCODE,           // 0x11
    ONE_BYTE_OPCODE,           // 0x12
    ONE_BYTE_OPCODE,           // 0x13
    ONE_BYTE_OPCODE,           // 0x14
    ONE_BYTE_OPCODE,           // 0x15
    ONE_BYTE_OPCODE,           // 0x16
    ONE_BYTE_OPCODE,           // 0x17
    ONE_BYTE_OPCODE,           // 0x18
    ONE_BYTE_OPCODE,           // 0x19
    ONE_BYTE_OPCODE,           // 0x1A
    ONE_BYTE_OPCODE,           // 0x1B
    ONE_BYTE_OPCODE,           // 0x1C
    ONE_BYTE_OPCODE,           // 0x1D
    ONE_BYTE_OPCODE,           // 0x1E
    ONE_BYTE_OPCODE,           // 0x1F
    ONE_BYTE_OPCODE,           // 0x20
    ONE_BYTE_OPCODE,           // 0x21
    ONE_BYTE_OPCODE,           // 0x22
    ONE_BYTE_OPCODE,           // 0x23
    ONE_BYTE_OPCODE,           // 0x24
    ONE_BYTE_OPCODE,           // 0x25
    ONE_BYTE_OPCODE,           // 0x26
    ONE_BYTE_OPCODE,           // 0x27
    ONE_BYTE_OPCODE,           // 0x28
    ONE_BYTE_OPCODE,           // 0x29
    ONE_BYTE_OPCODE,           // 0x2A
    ONE_BYTE_OPCODE,           // 0x2B
    ONE_BYTE_OPCODE,           // 0x2C
    ONE_BYTE_OPCODE,           // 0x2D
    ONE_BYTE_OPCODE,           // 0x2E
    ONE_BYTE_OPCODE,           // 0x2F
    ONE_BYTE_OPCODE,           // 0x30
    ONE_BYTE_OPCODE,           // 0x31
    ONE_BYTE_OPCODE,           // 0x32
    ONE_BYTE_OPCODE,           // 0x33
    ONE_BYTE_OPCODE,           // 0x34
    ONE_BYTE_OPCODE,           // 0x35
    ONE_BYTE_OPCODE,           // 0x36
    ONE_BYTE_OPCODE,           // 0x37
    ONE_BYTE_OPCODE,           // 0x38
    ONE_BYTE_OPCODE,           // 0x39
    ONE_BYTE_OPCODE,           // 0x3A
    ONE_BYTE_OPCODE,           // 0x3B
    ONE_BYTE_OPCODE,           // 0x3C
    ONE_BYTE_OPCODE,           // 0x3D
    ONE_BYTE_OPCODE,           // 0x3E
    ONE_BYTE_OPCODE,           // 0x3F
    ONE_BYTE_OPCODE,           // 0x40
    ONE_BYTE_OPCODE,           // 0x41
    ONE_BYTE_OPCODE,           // 0x42
    ONE_BYTE_OPCODE,           // 0x43
    ONE_BYTE_OPCODE,           // 0x44
    ONE_BYTE_OPCODE,           // 0x45
    ONE_BYTE_OPCODE,           // 0x46
    ONE_BYTE_OPCODE,           // 0x47
    ONE_BYTE_OPCODE,           // 0x48
    ONE_BYTE_OPCODE,           // 0x49
    ONE_BYTE_OPCODE,           // 0x4A
    ONE_BYTE_OPCODE,           // 0x4B
    ONE_BYTE_OPCODE,           // 0x4C
    ONE_BYTE_OPCODE,           // 0x4D
    ONE_BYTE_OPCODE,           // 0x4E
    ONE_BYTE_OPCODE,           // 0x4F
    ONE_BYTE_OPCODE,           // 0x50
    ONE_BYTE_OPCODE,           // 0x51
    ONE_BYTE_OPCODE,           // 0x52
    ONE_BYTE_OPCODE,           // 0x53
    ONE_BYTE_OPCODE,           // 0x54
    ONE_BYTE_OPCODE,           // 0x55
    ONE_BYTE_OPCODE,           // 0x56
    ONE_BYTE_OPCODE,           // 0x57
    ONE_BYTE_OPCODE,           // 0x58
    ONE_BYTE_OPCODE,           // 0x59
    ONE_BYTE_OPCODE,           // 0x5A
    ONE_BYTE_OPCODE,           // 0x5B
    ONE_BYTE_OPCODE,           // 0x5C
    ONE_BYTE_OPCODE,           // 0x5D
    ONE_BYTE_OPCODE,           // 0x5E
    ONE_BYTE_OPCODE,           // 0x5F
    ONE_BYTE_OPCODE,           // 0x60
    ONE_BYTE_OPCODE,           // 0x61
    ONE_BYTE_OPCODE,           // 0x62
    ONE_BYTE_OPCODE,           // 0x63
    ONE_BYTE_OPCODE,           // 0x64
    ONE_BYTE_OPCODE,           // 0x65
    ONE_BYTE_OPCODE,           // 0x66
    ONE_BYTE_OPCODE,           // 0x67
    ONE_BYTE_OPCODE,           // 0x68
    ONE_BYTE_OPCODE,           // 0x69
    ONE_BYTE_OPCODE,           // 0x6A
    ONE_BYTE_OPCODE,           // 0x6B
    ONE_BYTE_OPCODE,           // 0x6C
    ONE_BYTE_OPCODE,           // 0x6D
    ONE_BYTE_OPCODE,           // 0x6E
    ONE_BYTE_OPCODE,           // 0x6F
    ONE_BYTE_OPCODE,           // 0x70
    ONE_BYTE_OPCODE,           // 0x71
    ONE_BYTE_OPCODE,           // 0x72
    ONE_BYTE_OPCODE,           // 0x73
    ONE_BYTE_OPCODE,           // 0x74
    ONE_BYTE_OPCODE,           // 0x75
    ONE_BYTE_OPCODE,           // 0x76
    ONE_BYTE_OPCODE,           // 0x77
    ONE_BYTE_OPCODE,           // 0x78
    ONE_BYTE_OPCODE,           // 0x79
    ONE_BYTE_OPCODE,           // 0x7A
    ONE_BYTE_OPCODE,           // 0x7B
    ONE_BYTE_OPCODE,           // 0x7C
    ONE_BYTE_OPCODE,           // 0x7D
    ONE_BYTE_OPCODE,           // 0x7E
    ONE_BYTE_OPCODE,           // 0x7F
    ONE_BYTE_OPCODE,           // 0x80
    ONE_BYTE_OPCODE,           // 0x81
    ONE_BYTE_OPCODE,           // 0x82
    ONE_BYTE_OPCODE,           // 0x83
    ONE_BYTE_OPCODE,           // 0x84
    ONE_BYTE_OPCODE,           // 0x85
    ONE_BYTE_OPCODE,           // 0x86
    ONE_BYTE_OPCODE,           // 0x87
    ONE_BYTE_OPCODE,           // 0x88
    ONE_BYTE_OPCODE,           // 0x89
    ONE_BYTE_OPCODE,           // 0x8A
    ONE_BYTE_OPCODE,           // 0x8B
    ONE_BYTE_OPCODE,           // 0x8C
    ONE_BYTE_OPCODE,           // 0x8D
    ONE_BYTE_OPCODE,           // 0x8E
    ONE_BYTE_OPCODE,           // 0x8F
    ONE_BYTE_OPCODE,           // 0x90
    ONE_BYTE_OPCODE,           // 0x91
    ONE_BYTE_OPCODE,           // 0x92
    ONE_BYTE_OPCODE,           // 0x93
    ONE_BYTE_OPCODE,           // 0x94
    ONE_BYTE_OPCODE,           // 0x95
    ONE_BYTE_OPCODE,           // 0x96
    ONE_BYTE_OPCODE,           // 0x97
    ONE_BYTE_OPCODE,           // 0x98
    ONE_BYTE_OPCODE,           // 0x99
    ONE_BYTE_OPCODE,           // 0x9A
    ONE_BYTE_OPCODE,           // 0x9B
    TWO_BYTE_DISJOINT_OPCODE,  // 0x9C
    TWO_BYTE_DISJOINT_OPCODE,  // 0x9D
    TWO_BYTE_DISJOINT_OPCODE,  // 0x9E
    TWO_BYTE_DISJOINT_OPCODE,  // 0x9F
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA0
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA1
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA2
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA3
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA4
    THREE_NIBBLE_OPCODE,       // 0xA5
    TWO_BYTE_DISJOINT_OPCODE,  // 0xA6
    THREE_NIBBLE_OPCODE,       // 0xA7
    ONE_BYTE_OPCODE,           // 0xA8
    ONE_BYTE_OPCODE,           // 0xA9
    ONE_BYTE_OPCODE,           // 0xAA
    ONE_BYTE_OPCODE,           // 0xAB
    ONE_BYTE_OPCODE,           // 0xAC
    ONE_BYTE_OPCODE,           // 0xAD
    ONE_BYTE_OPCODE,           // 0xAE
    ONE_BYTE_OPCODE,           // 0xAF
    ONE_BYTE_OPCODE,           // 0xB0
    ONE_BYTE_OPCODE,           // 0xB1
    TWO_BYTE_OPCODE,           // 0xB2
    TWO_BYTE_OPCODE,           // 0xB3
    TWO_BYTE_DISJOINT_OPCODE,  // 0xB4
    TWO_BYTE_DISJOINT_OPCODE,  // 0xB5
    TWO_BYTE_DISJOINT_OPCODE,  // 0xB6
    TWO_BYTE_DISJOINT_OPCODE,  // 0xB7
    TWO_BYTE_DISJOINT_OPCODE,  // 0xB8
    TWO_BYTE_OPCODE,           // 0xB9
    ONE_BYTE_OPCODE,           // 0xBA
    ONE_BYTE_OPCODE,           // 0xBB
    ONE_BYTE_OPCODE,           // 0xBC
    ONE_BYTE_OPCODE,           // 0xBD
    ONE_BYTE_OPCODE,           // 0xBE
    ONE_BYTE_OPCODE,           // 0xBF
    THREE_NIBBLE_OPCODE,       // 0xC0
    THREE_NIBBLE_OPCODE,       // 0xC1
    THREE_NIBBLE_OPCODE,       // 0xC2
    THREE_NIBBLE_OPCODE,       // 0xC3
    THREE_NIBBLE_OPCODE,       // 0xC4
    THREE_NIBBLE_OPCODE,       // 0xC5
    THREE_NIBBLE_OPCODE,       // 0xC6
    ONE_BYTE_OPCODE,           // 0xC7
    THREE_NIBBLE_OPCODE,       // 0xC8
    THREE_NIBBLE_OPCODE,       // 0xC9
    THREE_NIBBLE_OPCODE,       // 0xCA
    THREE_NIBBLE_OPCODE,       // 0xCB
    THREE_NIBBLE_OPCODE,       // 0xCC
    TWO_BYTE_DISJOINT_OPCODE,  // 0xCD
    TWO_BYTE_DISJOINT_OPCODE,  // 0xCE
    TWO_BYTE_DISJOINT_OPCODE,  // 0xCF
    ONE_BYTE_OPCODE,           // 0xD0
    ONE_BYTE_OPCODE,           // 0xD1
    ONE_BYTE_OPCODE,           // 0xD2
    ONE_BYTE_OPCODE,           // 0xD3
    ONE_BYTE_OPCODE,           // 0xD4
    ONE_BYTE_OPCODE,           // 0xD5
    ONE_BYTE_OPCODE,           // 0xD6
    ONE_BYTE_OPCODE,           // 0xD7
    ONE_BYTE_OPCODE,           // 0xD8
    ONE_BYTE_OPCODE,           // 0xD9
    ONE_BYTE_OPCODE,           // 0xDA
    ONE_BYTE_OPCODE,           // 0xDB
    ONE_BYTE_OPCODE,           // 0xDC
    ONE_BYTE_OPCODE,           // 0xDD
    ONE_BYTE_OPCODE,           // 0xDE
    ONE_BYTE_OPCODE,           // 0xDF
    ONE_BYTE_OPCODE,           // 0xE0
    ONE_BYTE_OPCODE,           // 0xE1
    ONE_BYTE_OPCODE,           // 0xE2
    TWO_BYTE_DISJOINT_OPCODE,  // 0xE3
    TWO_BYTE_DISJOINT_OPCODE,  // 0xE4
    TWO_BYTE_OPCODE,           // 0xE5
    TWO_BYTE_DISJOINT_OPCODE,  // 0xE6
    TWO_BYTE_DISJOINT_OPCODE,  // 0xE7
    ONE_BYTE_OPCODE,           // 0xE8
    ONE_BYTE_OPCODE,           // 0xE9
    ONE_BYTE_OPCODE,           // 0xEA
    TWO_BYTE_DISJOINT_OPCODE,  // 0xEB
    TWO_BYTE_DISJOINT_OPCODE,  // 0xEC
    TWO_BYTE_DISJOINT_OPCODE,  // 0xED
    ONE_BYTE_OPCODE,           // 0xEE
    ONE_BYTE_OPCODE,           // 0xEF
    ONE_BYTE_OPCODE,           // 0xF0
    ONE_BYTE_OPCODE,           // 0xF1
    ONE_BYTE_OPCODE,           // 0xF2
    ONE_BYTE_OPCODE,           // 0xF3
    ONE_BYTE_OPCODE,           // 0xF4
    ONE_BYTE_OPCODE,           // 0xF5
    ONE_BYTE_OPCODE,           // 0xF6
    ONE_BYTE_OPCODE,           // 0xF7
    ONE_BYTE_OPCODE,           // 0xF8
    ONE_BYTE_OPCODE,           // 0xF9
    ONE_BYTE_OPCODE,           // 0xFA
    ONE_BYTE_OPCODE,           // 0xFB
    ONE_BYTE_OPCODE,           // 0xFC
    ONE_BYTE_OPCODE,           // 0xFD
    TWO_BYTE_DISJOINT_OPCODE,  // 0xFE
    TWO_BYTE_DISJOINT_OPCODE,  // 0xFF
};

// These register names are defined in a way to match the native disassembler
// formatting. See for example the command "objdump -d <binary file>".
const char* Registers::names_[kNumRegisters] = {
    "r0", "r1", "r2",  "r3", "r4", "r5",  "r6",  "r7",
    "r8", "r9", "r10", "fp", "ip", "r13", "r14", "sp"};

const char* DoubleRegisters::names_[kNumDoubleRegisters] = {
    "f0", "f1", "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15"};

int DoubleRegisters::Number(const char* name) {
  for (int i = 0; i < kNumDoubleRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
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

  // No register with the requested name found.
  return kNoRegister;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390X
