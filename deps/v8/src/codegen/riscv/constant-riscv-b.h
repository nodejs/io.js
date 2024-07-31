// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_CODEGEN_RISCV_CONSTANT_RISCV_B_H_
#define V8_CODEGEN_RISCV_CONSTANT_RISCV_B_H_

#include "src/codegen/riscv/base-constants-riscv.h"
namespace v8 {
namespace internal {

// Zba
#ifdef V8_TARGET_ARCH_RISCV64
constexpr Opcode RO_ADDUW =
    OP_32 | (0b000 << kFunct3Shift) | (0b0000100 << kFunct7Shift);
constexpr Opcode RO_SH1ADDUW =
    OP_32 | (0b010 << kFunct3Shift) | (0b0010000 << kFunct7Shift);
constexpr Opcode RO_SH2ADDUW =
    OP_32 | (0b100 << kFunct3Shift) | (0b0010000 << kFunct7Shift);
constexpr Opcode RO_SH3ADDUW =
    OP_32 | (0b110 << kFunct3Shift) | (0b0010000 << kFunct7Shift);
constexpr Opcode RO_SLLIUW =
    OP_IMM_32 | (0b001 << kFunct3Shift) | (0b000010 << kFunct6Shift);
#endif

constexpr Opcode RO_SH1ADD =
    OP | (0b010 << kFunct3Shift) | (0b0010000 << kFunct7Shift);
constexpr Opcode RO_SH2ADD =
    OP | (0b100 << kFunct3Shift) | (0b0010000 << kFunct7Shift);
constexpr Opcode RO_SH3ADD =
    OP | (0b110 << kFunct3Shift) | (0b0010000 << kFunct7Shift);

// Zbb
constexpr Opcode RO_ANDN =
    OP | (0b111 << kFunct3Shift) | (0b0100000 << kFunct7Shift);
constexpr Opcode RO_ORN =
    OP | (0b110 << kFunct3Shift) | (0b0100000 << kFunct7Shift);
constexpr Opcode RO_XNOR =
    OP | (0b100 << kFunct3Shift) | (0b0100000 << kFunct7Shift);

constexpr Opcode OP_COUNT =
    OP_IMM | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift);
constexpr Opcode RO_CLZ = OP_COUNT | (0b00000 << kShamtShift);
constexpr Opcode RO_CTZ = OP_COUNT | (0b00001 << kShamtShift);
constexpr Opcode RO_CPOP = OP_COUNT | (0b00010 << kShamtShift);
#ifdef V8_TARGET_ARCH_RISCV64
constexpr Opcode OP_COUNTW =
    OP_IMM_32 | (0b001 << kFunct3Shift) | (0b0110000 << kFunct7Shift);
constexpr Opcode RO_CLZW = OP_COUNTW | (0b00000 << kShamtShift);
constexpr Opcode RO_CTZW = OP_COUNTW | (0b00001 << kShamtShift);
constexpr Opcode RO_CPOPW = OP_COUNTW | (0b00010 << kShamtShift);
#endif

constexpr Opcode RO_MAX =
    OP | (0b110 << kFunct3Shift) | (0b0000101 << kFunct7Shift);
constexpr Opcode RO_MAXU =
    OP | (0b111 << kFunct3Shift) | (0b0000101 << kFunct7Shift);

constexpr Opcode RO_MIN =
    OP | (0b100 << kFunct3Shift) | (0b0000101 << kFunct7Shift);
constexpr Opcode RO_MINU =
    OP | (0b101 << kFunct3Shift) | (0b0000101 << kFunct7Shift);

constexpr Opcode RO_SEXTB = OP_IMM | (0b001 << kFunct3Shift) |
                            (0b0110000 << kFunct7Shift) |
                            (0b00100 << kShamtShift);
constexpr Opcode RO_SEXTH = OP_IMM | (0b001 << kFunct3Shift) |
                            (0b0110000 << kFunct7Shift) |
                            (0b00101 << kShamtShift);
#ifdef V8_TARGET_ARCH_RISCV64
constexpr Opcode RO_ZEXTH = OP_32 | (0b100 << kFunct3Shift) |
                            (0b0000100 << kFunct7Shift) |
                            (0b00000 << kShamtShift);
#elif defined(V8_TARGET_ARCH_RISCV32)
constexpr Opcode RO_ZEXTH = OP | (0b100 << kFunct3Shift) |
                            (0b0000100 << kFunct7Shift) |
                            (0b00000 << kShamtShift);
#endif

constexpr Opcode RO_REV8 =
    OP_IMM | (0b101 << kFunct3Shift) | (0b011010 << kFunct6Shift);
#ifdef V8_TARGET_ARCH_RISCV64
constexpr Opcode RO_REV8_IMM12 = 0b011010111000;
#elif defined(V8_TARGET_ARCH_RISCV32)
constexpr Opcode RO_REV8_IMM12 = 0b011010011000;
#endif
// Zbs
constexpr Opcode RO_BCLR =
    OP | (0b001 << kFunct3Shift) | (0b0100100 << kFunct7Shift);
constexpr Opcode RO_BCLRI =
    OP_IMM | (0b001 << kFunct3Shift) | (0b010010 << kFunct6Shift);

constexpr Opcode RO_BEXT =
    OP | (0b101 << kFunct3Shift) | (0b0100100 << kFunct7Shift);
constexpr Opcode RO_BEXTI =
    OP_IMM | (0b101 << kFunct3Shift) | (0b010010 << kFunct6Shift);

constexpr Opcode RO_BINV =
    OP | (0b001 << kFunct3Shift) | (0b0110100 << kFunct7Shift);
constexpr Opcode RO_BINVI =
    OP_IMM | (0b001 << kFunct3Shift) | (0b011010 << kFunct6Shift);

constexpr Opcode RO_BSET =
    OP | (0b001 << kFunct3Shift) | (0b0010100 << kFunct7Shift);
constexpr Opcode RO_BSETI =
    OP_IMM | (0b001 << kFunct3Shift) | (0b0010100 << kFunct7Shift);

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_CONSTANT_RISCV_B_H_
