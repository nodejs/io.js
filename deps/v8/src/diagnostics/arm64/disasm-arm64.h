// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ARM64_DISASM_ARM64_H_
#define V8_DIAGNOSTICS_ARM64_DISASM_ARM64_H_

#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/arm64/decoder-arm64.h"
#include "src/codegen/arm64/instructions-arm64.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE DisassemblingDecoder : public DecoderVisitor {
 public:
  DisassemblingDecoder();
  DisassemblingDecoder(char* text_buffer, int buffer_size);
  virtual ~DisassemblingDecoder();
  char* GetOutput();

// Declare all Visitor functions.
#define DECLARE(A) void Visit##A(Instruction* instr);
  VISITOR_LIST(DECLARE)
#undef DECLARE

 protected:
  virtual void ProcessOutput(Instruction* instr);

  // Default output functions.  The functions below implement a default way of
  // printing elements in the disassembly. A sub-class can override these to
  // customize the disassembly output.

  // Prints the name of a register.
  virtual void AppendRegisterNameToOutput(const CPURegister& reg);

  void Format(Instruction* instr, const char* mnemonic, const char* format);
  void Substitute(Instruction* instr, const char* string);
  int SubstituteField(Instruction* instr, const char* format);
  int SubstituteRegisterField(Instruction* instr, const char* format);
  int SubstituteImmediateField(Instruction* instr, const char* format);
  int SubstituteLiteralField(Instruction* instr, const char* format);
  int SubstituteBitfieldImmediateField(Instruction* instr, const char* format);
  int SubstituteShiftField(Instruction* instr, const char* format);
  int SubstituteExtendField(Instruction* instr, const char* format);
  int SubstituteConditionField(Instruction* instr, const char* format);
  int SubstitutePCRelAddressField(Instruction* instr, const char* format);
  int SubstituteBranchTargetField(Instruction* instr, const char* format);
  int SubstituteLSRegOffsetField(Instruction* instr, const char* format);
  int SubstitutePrefetchField(Instruction* instr, const char* format);
  int SubstituteBarrierField(Instruction* instr, const char* format);

  bool RdIsZROrSP(Instruction* instr) const {
    return (instr->Rd() == kZeroRegCode);
  }

  bool RnIsZROrSP(Instruction* instr) const {
    return (instr->Rn() == kZeroRegCode);
  }

  bool RmIsZROrSP(Instruction* instr) const {
    return (instr->Rm() == kZeroRegCode);
  }

  bool RaIsZROrSP(Instruction* instr) const {
    return (instr->Ra() == kZeroRegCode);
  }

  bool IsMovzMovnImm(unsigned reg_size, uint64_t value);

  void ResetOutput();
  void AppendToOutput(const char* string, ...);
  void DisassembleNEONPolynomialMul(Instruction* instr);

  char* buffer_;
  uint32_t buffer_pos_;
  uint32_t buffer_size_;
  bool own_buffer_;
};

class V8_EXPORT_PRIVATE PrintDisassembler : public DisassemblingDecoder {
 public:
  explicit PrintDisassembler(FILE* stream) : stream_(stream) {}
  ~PrintDisassembler() {}

  virtual void ProcessOutput(Instruction* instr);

 private:
  FILE* stream_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ARM64_DISASM_ARM64_H_
