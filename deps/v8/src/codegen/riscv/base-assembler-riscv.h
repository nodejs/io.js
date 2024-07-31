// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2021 the V8 project authors. All rights reserved.

#ifndef V8_CODEGEN_RISCV_BASE_ASSEMBLER_RISCV_H_
#define V8_CODEGEN_RISCV_BASE_ASSEMBLER_RISCV_H_

#include <stdio.h>

#include <memory>
#include <set>

#include "src/codegen/assembler.h"
#include "src/codegen/constant-pool.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/riscv/constants-riscv.h"
#include "src/codegen/riscv/register-riscv.h"
#include "src/objects/contexts.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

#define DEBUG_PRINTF(...)     \
  if (v8_flags.riscv_debug) { \
    printf(__VA_ARGS__);      \
  }

class SafepointTableBuilder;

class AssemblerRiscvBase {
 protected:
  // Returns the branch offset to the given label from the current code
  // position. Links the label to the current position if it is still unbound.
  // Manages the jump elimination optimization if the second parameter is true.
  enum OffsetSize : int {
    kOffset21 = 21,  // RISCV jal
    kOffset12 = 12,  // RISCV imm12
    kOffset20 = 20,  // RISCV imm20
    kOffset13 = 13,  // RISCV branch
    kOffset32 = 32,  // RISCV auipc + instr_I
    kOffset11 = 11,  // RISCV C_J
    kOffset9 = 9     // RISCV compressed branch
  };
  virtual int32_t branch_offset_helper(Label* L, OffsetSize bits) = 0;

  virtual void emit(Instr x) = 0;
  virtual void emit(ShortInstr x) = 0;
  virtual void emit(uint64_t x) = 0;

  virtual void ClearVectorunit() = 0;
  // Instruction generation.

  // ----- Top-level instruction formats match those in the ISA manual
  // (R, I, S, B, U, J). These match the formats defined in LLVM's
  // RISCVInstrFormats.td.
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, FPURegister rs1, FPURegister rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, Register rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                 FPURegister rd, FPURegister rs1, Register rs2);
  void GenInstrR(uint8_t funct7, uint8_t funct3, BaseOpcode opcode, Register rd,
                 FPURegister rs1, FPURegister rs2);
  void GenInstrR4(uint8_t funct2, BaseOpcode opcode, Register rd, Register rs1,
                  Register rs2, Register rs3, FPURoundingMode frm);
  void GenInstrR4(uint8_t funct2, BaseOpcode opcode, FPURegister rd,
                  FPURegister rs1, FPURegister rs2, FPURegister rs3,
                  FPURoundingMode frm);
  void GenInstrRAtomic(uint8_t funct5, bool aq, bool rl, uint8_t funct3,
                       Register rd, Register rs1, Register rs2);
  void GenInstrRFrm(uint8_t funct7, BaseOpcode opcode, Register rd,
                    Register rs1, Register rs2, FPURoundingMode frm);
  void GenInstrI(uint8_t funct3, BaseOpcode opcode, Register rd, Register rs1,
                 int16_t imm12);
  void GenInstrI(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                 Register rs1, int16_t imm12);
  void GenInstrIShift(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                      Register rd, Register rs1, uint8_t shamt);
  void GenInstrIShiftW(uint8_t funct7, uint8_t funct3, BaseOpcode opcode,
                       Register rd, Register rs1, uint8_t shamt);
  void GenInstrS(uint8_t funct3, BaseOpcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrS(uint8_t funct3, BaseOpcode opcode, Register rs1,
                 FPURegister rs2, int16_t imm12);
  void GenInstrB(uint8_t funct3, BaseOpcode opcode, Register rs1, Register rs2,
                 int16_t imm12);
  void GenInstrU(BaseOpcode opcode, Register rd, int32_t imm20);
  void GenInstrJ(BaseOpcode opcode, Register rd, int32_t imm20);
  void GenInstrCR(uint8_t funct4, BaseOpcode opcode, Register rd, Register rs2);
  void GenInstrCA(uint8_t funct6, BaseOpcode opcode, Register rd, uint8_t funct,
                  Register rs2);
  void GenInstrCI(uint8_t funct3, BaseOpcode opcode, Register rd, int8_t imm6);
  void GenInstrCIU(uint8_t funct3, BaseOpcode opcode, Register rd,
                   uint8_t uimm6);
  void GenInstrCIU(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                   uint8_t uimm6);
  void GenInstrCIW(uint8_t funct3, BaseOpcode opcode, Register rd,
                   uint8_t uimm8);
  void GenInstrCSS(uint8_t funct3, BaseOpcode opcode, FPURegister rs2,
                   uint8_t uimm6);
  void GenInstrCSS(uint8_t funct3, BaseOpcode opcode, Register rs2,
                   uint8_t uimm6);
  void GenInstrCL(uint8_t funct3, BaseOpcode opcode, Register rd, Register rs1,
                  uint8_t uimm5);
  void GenInstrCL(uint8_t funct3, BaseOpcode opcode, FPURegister rd,
                  Register rs1, uint8_t uimm5);
  void GenInstrCS(uint8_t funct3, BaseOpcode opcode, Register rs2, Register rs1,
                  uint8_t uimm5);
  void GenInstrCS(uint8_t funct3, BaseOpcode opcode, FPURegister rs2,
                  Register rs1, uint8_t uimm5);
  void GenInstrCJ(uint8_t funct3, BaseOpcode opcode, uint16_t uint11);
  void GenInstrCB(uint8_t funct3, BaseOpcode opcode, Register rs1,
                  uint8_t uimm8);
  void GenInstrCBA(uint8_t funct3, uint8_t funct2, BaseOpcode opcode,
                   Register rs1, int8_t imm6);

  // ----- Instruction class templates match those in LLVM's RISCVInstrInfo.td
  void GenInstrBranchCC_rri(uint8_t funct3, Register rs1, Register rs2,
                            int16_t imm12);
  void GenInstrLoad_ri(uint8_t funct3, Register rd, Register rs1,
                       int16_t imm12);
  void GenInstrStore_rri(uint8_t funct3, Register rs1, Register rs2,
                         int16_t imm12);
  void GenInstrALU_ri(uint8_t funct3, Register rd, Register rs1, int16_t imm12);
  void GenInstrShift_ri(bool arithshift, uint8_t funct3, Register rd,
                        Register rs1, uint8_t shamt);
  void GenInstrALU_rr(uint8_t funct7, uint8_t funct3, Register rd, Register rs1,
                      Register rs2);
  void GenInstrCSR_ir(uint8_t funct3, Register rd, ControlStatusReg csr,
                      Register rs1);
  void GenInstrCSR_ii(uint8_t funct3, Register rd, ControlStatusReg csr,
                      uint8_t rs1);
  void GenInstrShiftW_ri(bool arithshift, uint8_t funct3, Register rd,
                         Register rs1, uint8_t shamt);
  void GenInstrALUW_rr(uint8_t funct7, uint8_t funct3, Register rd,
                       Register rs1, Register rs2);
  void GenInstrPriv(uint8_t funct7, Register rs1, Register rs2);
  void GenInstrLoadFP_ri(uint8_t funct3, FPURegister rd, Register rs1,
                         int16_t imm12);
  void GenInstrStoreFP_rri(uint8_t funct3, Register rs1, FPURegister rs2,
                           int16_t imm12);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, FPURegister rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        Register rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, FPURegister rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, Register rs2);
  void GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3, Register rd,
                        FPURegister rs1, FPURegister rs2);
  virtual void BlockTrampolinePoolFor(int instructions) = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_BASE_ASSEMBLER_RISCV_H_
