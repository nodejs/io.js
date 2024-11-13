
// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/riscv/extension-riscv-zicond.h"

#include "src/codegen/assembler.h"
#include "src/codegen/riscv/constant-riscv-zicond.h"
#include "src/codegen/riscv/register-riscv.h"

namespace v8 {
namespace internal {

void AssemblerRISCVZicond::czero_eqz(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000111, 0b101, rd, rs1, rs2);
}

void AssemblerRISCVZicond::czero_nez(Register rd, Register rs1, Register rs2) {
  GenInstrALU_rr(0b0000111, 0b111, rd, rs1, rs2);
}

}  // namespace internal
}  // namespace v8
