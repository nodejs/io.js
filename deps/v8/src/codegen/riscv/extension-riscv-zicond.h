// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RISCV_EXTENSION_RISCV_ZICOND_H_
#define V8_CODEGEN_RISCV_EXTENSION_RISCV_ZICOND_H_
#include "src/codegen/assembler.h"
#include "src/codegen/riscv/base-assembler-riscv.h"
#include "src/codegen/riscv/constant-riscv-zicond.h"
#include "src/codegen/riscv/register-riscv.h"

namespace v8 {
namespace internal {

class AssemblerRISCVZicond : public AssemblerRiscvBase {
 public:
  // CSR
  void czero_eqz(Register rd, Register rs1, Register rs2);
  void czero_nez(Register rd, Register rs1, Register rs2);
};

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_RISCV_EXTENSION_RISCV_ZICOND_H_
