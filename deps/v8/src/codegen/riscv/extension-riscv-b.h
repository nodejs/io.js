// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/codegen/assembler.h"
#include "src/codegen/riscv/base-assembler-riscv.h"
#include "src/codegen/riscv/constant-riscv-b.h"
#include "src/codegen/riscv/register-riscv.h"
#ifndef V8_CODEGEN_RISCV_EXTENSION_RISCV_B_H_
#define V8_CODEGEN_RISCV_EXTENSION_RISCV_B_H_

namespace v8 {
namespace internal {
class AssemblerRISCVB : public AssemblerRiscvBase {
  // RV32B Extension
 public:
  // Zba Extension
  void sh1add(Register rd, Register rs1, Register rs2);
  void sh2add(Register rd, Register rs1, Register rs2);
  void sh3add(Register rd, Register rs1, Register rs2);
#ifdef V8_TARGET_ARCH_RISCV64
  void adduw(Register rd, Register rs1, Register rs2);
  void zextw(Register rd, Register rs1) { adduw(rd, rs1, zero_reg); }
  void sh1adduw(Register rd, Register rs1, Register rs2);
  void sh2adduw(Register rd, Register rs1, Register rs2);
  void sh3adduw(Register rd, Register rs1, Register rs2);
  void slliuw(Register rd, Register rs1, uint8_t shamt);
#endif

  // Zbb Extension
  void andn(Register rd, Register rs1, Register rs2);
  void orn(Register rd, Register rs1, Register rs2);
  void xnor(Register rd, Register rs1, Register rs2);

  void clz(Register rd, Register rs);
  void ctz(Register rd, Register rs);
  void cpop(Register rd, Register rs);
#ifdef V8_TARGET_ARCH_RISCV64
  void clzw(Register rd, Register rs);
  void ctzw(Register rd, Register rs);
  void cpopw(Register rd, Register rs);
#endif

  void max(Register rd, Register rs1, Register rs2);
  void maxu(Register rd, Register rs1, Register rs2);
  void min(Register rd, Register rs1, Register rs2);
  void minu(Register rd, Register rs1, Register rs2);

  void sextb(Register rd, Register rs);
  void sexth(Register rd, Register rs);
  void zexth(Register rd, Register rs);

  // Zbb: bitwise rotation
  void rol(Register rd, Register rs1, Register rs2);
  void ror(Register rd, Register rs1, Register rs2);
  void rori(Register rd, Register rs1, uint8_t shamt);
  void orcb(Register rd, Register rs);
  void rev8(Register rd, Register rs);
#ifdef V8_TARGET_ARCH_RISCV64
  void rolw(Register rd, Register rs1, Register rs2);
  void roriw(Register rd, Register rs1, uint8_t shamt);
  void rorw(Register rd, Register rs1, Register rs2);
#endif

  // Zbs
  void bclr(Register rd, Register rs1, Register rs2);
  void bclri(Register rd, Register rs1, uint8_t shamt);
  void bext(Register rd, Register rs1, Register rs2);
  void bexti(Register rd, Register rs1, uint8_t shamt);
  void binv(Register rd, Register rs1, Register rs2);
  void binvi(Register rd, Register rs1, uint8_t shamt);
  void bset(Register rd, Register rs1, Register rs2);
  void bseti(Register rd, Register rs1, uint8_t shamt);
};
}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_RISCV_EXTENSION_RISCV_B_H_
