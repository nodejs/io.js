// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_CODEGEN_RISCV_CONSTANT_RISCV_ZICOND_H_
#define V8_CODEGEN_RISCV_CONSTANT_RISCV_ZICOND_H_

#include "src/codegen/riscv/base-constants-riscv.h"
namespace v8 {
namespace internal {
// RV32/RV64 Zicond Standard Extension
constexpr Opcode RO_CZERO_EQZ =
    OP | (0b101 << kFunct3Shift) | (0b0000111 << kFunct7Shift);
constexpr Opcode RO_CZERO_NEZ =
    OP | (0b111 << kFunct3Shift) | (0b0000111 << kFunct7Shift);
}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_RISCV_CONSTANT_RISCV_ZICOND_H_
