// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Disassembler object is used to disassemble a block of code instruction by
// instruction. The default implementation of the NameConverter object can be
// overriden to modify register names or to do symbol lookup on addresses.
//
// The example below will disassemble a block of code and print it to stdout.
//
//   NameConverter converter;
//   Disassembler d(converter);
//   for (uint8_t* pc = begin; pc < end;) {
//     v8::base::EmbeddedVector<char, 256> buffer;
//     uint8_t* prev_pc = pc;
//     pc += d.InstructionDecode(buffer, pc);
//     printf("%p    %08x      %s\n",
//            prev_pc, *reinterpret_cast<int32_t*>(prev_pc), buffer);
//   }
//
// The Disassembler class also has a convenience method to disassemble a block
// of code into a FILE*, meaning that the above functionality could also be
// achieved by just calling Disassembler::Disassemble(stdout, begin, end);

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if V8_TARGET_ARCH_S390X

#include "src/base/platform/platform.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/s390/constants-s390.h"
#include "src/diagnostics/disasm.h"

namespace v8 {
namespace internal {

//------------------------------------------------------------------------------

// Decoder decodes and disassembles instructions into an output buffer.
// It uses the converter to convert register names and call destinations into
// more informative description.
class Decoder {
 public:
  Decoder(const disasm::NameConverter& converter, base::Vector<char> out_buffer)
      : converter_(converter), out_buffer_(out_buffer), out_buffer_pos_(0) {
    out_buffer_[out_buffer_pos_] = '\0';
  }

  ~Decoder() {}

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(uint8_t* instruction);

 private:
  // Bottleneck functions to print into the out_buffer.
  void PrintChar(const char ch);
  void Print(const char* str);

  // Printing of common values.
  void PrintRegister(int reg);
  void PrintDRegister(int reg);
  void PrintSoftwareInterrupt(SoftwareInterruptCodes svc);

  // Handle formatting of instructions and their options.
  int FormatRegister(Instruction* instr, const char* option);
  int FormatFloatingRegister(Instruction* instr, const char* option);
  int FormatMask(Instruction* instr, const char* option);
  int FormatDisplacement(Instruction* instr, const char* option);
  int FormatImmediate(Instruction* instr, const char* option);
  int FormatOption(Instruction* instr, const char* option);
  void Format(Instruction* instr, const char* format);
  void Unknown(Instruction* instr);
  void UnknownFormat(Instruction* instr, const char* opcname);

  bool DecodeSpecial(Instruction* instr);
  bool DecodeGeneric(Instruction* instr);

  const disasm::NameConverter& converter_;
  base::Vector<char> out_buffer_;
  int out_buffer_pos_;
};

// Support for assertions in the Decoder formatting functions.
#define STRING_STARTS_WITH(string, compare_string) \
  (strncmp(string, compare_string, strlen(compare_string)) == 0)

// Append the ch to the output buffer.
void Decoder::PrintChar(const char ch) { out_buffer_[out_buffer_pos_++] = ch; }

// Append the str to the output buffer.
void Decoder::Print(const char* str) {
  char cur = *str++;
  while (cur != '\0' && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    PrintChar(cur);
    cur = *str++;
  }
  out_buffer_[out_buffer_pos_] = 0;
}

// Print the register name according to the active name converter.
void Decoder::PrintRegister(int reg) {
  Print(converter_.NameOfCPURegister(reg));
}

// Print the double FP register name according to the active name converter.
void Decoder::PrintDRegister(int reg) {
  Print(RegisterName(DoubleRegister::from_code(reg)));
}

// Print SoftwareInterrupt codes. Factoring this out reduces the complexity of
// the FormatOption method.
void Decoder::PrintSoftwareInterrupt(SoftwareInterruptCodes svc) {
  switch (svc) {
    case kCallRtRedirected:
      Print("call rt redirected");
      return;
    case kBreakpoint:
      Print("breakpoint");
      return;
    default:
      if (svc >= kStopCode) {
        out_buffer_pos_ +=
            base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d - 0x%x",
                           svc & kStopCodeMask, svc & kStopCodeMask);
      } else {
        out_buffer_pos_ +=
            base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", svc);
      }
      return;
  }
}

// Handle all register based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatRegister(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'r');

  if (format[1] == '1') {  // 'r1: register resides in bit 8-11
    int reg = instr->Bits<SixByteInstr, int>(39, 36);
    PrintRegister(reg);
    return 2;
  } else if (format[1] == '2') {  // 'r2: register resides in bit 12-15
    int reg = instr->Bits<SixByteInstr, int>(35, 32);
    // indicating it is a r0 for displacement, in which case the offset
    // should be 0.
    if (format[2] == 'd') {
      if (reg == 0) return 4;
      PrintRegister(reg);
      return 3;
    } else {
      PrintRegister(reg);
      return 2;
    }
  } else if (format[1] == '3') {  // 'r3: register resides in bit 16-19
    int reg = instr->Bits<SixByteInstr, int>(31, 28);
    PrintRegister(reg);
    return 2;
  } else if (format[1] == '4') {  // 'r4: register resides in bit 20-23
    int reg = instr->Bits<SixByteInstr, int>(27, 24);
    PrintRegister(reg);
    return 2;
  } else if (format[1] == '5') {  // 'r5: register resides in bit 24-27
    int reg = instr->Bits<SixByteInstr, int>(23, 20);
    PrintRegister(reg);
    return 2;
  } else if (format[1] == '6') {  // 'r6: register resides in bit 28-31
    int reg = instr->Bits<SixByteInstr, int>(19, 16);
    PrintRegister(reg);
    return 2;
  } else if (format[1] == '7') {  // 'r6: register resides in bit 32-35
    int reg = instr->Bits<SixByteInstr, int>(15, 12);
    PrintRegister(reg);
    return 2;
  }

  UNREACHABLE();
}

int Decoder::FormatFloatingRegister(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'f');

  // reuse 1, 5 and 6 because it is coresponding
  if (format[1] == '1') {  // 'r1: register resides in bit 8-11
    RRInstruction* rrinstr = reinterpret_cast<RRInstruction*>(instr);
    int reg = rrinstr->R1Value();
    PrintDRegister(reg);
    return 2;
  } else if (format[1] == '2') {  // 'f2: register resides in bit 12-15
    RRInstruction* rrinstr = reinterpret_cast<RRInstruction*>(instr);
    int reg = rrinstr->R2Value();
    PrintDRegister(reg);
    return 2;
  } else if (format[1] == '3') {  // 'f3: register resides in bit 16-19
    RRDInstruction* rrdinstr = reinterpret_cast<RRDInstruction*>(instr);
    int reg = rrdinstr->R1Value();
    PrintDRegister(reg);
    return 2;
  } else if (format[1] == '5') {  // 'f5: register resides in bit 24-28
    RREInstruction* rreinstr = reinterpret_cast<RREInstruction*>(instr);
    int reg = rreinstr->R1Value();
    PrintDRegister(reg);
    return 2;
  } else if (format[1] == '6') {  // 'f6: register resides in bit 29-32
    RREInstruction* rreinstr = reinterpret_cast<RREInstruction*>(instr);
    int reg = rreinstr->R2Value();
    PrintDRegister(reg);
    return 2;
  } else if (format[1] == '4') {
    VRR_E_Instruction* vrreinstr = reinterpret_cast<VRR_E_Instruction*>(instr);
    int reg = vrreinstr->R4Value();
    PrintDRegister(reg);
    return 2;
  }
  UNREACHABLE();
}

// FormatOption takes a formatting string and interprets it based on
// the current instructions. The format string points to the first
// character of the option string (the option escape has already been
// consumed by the caller.)  FormatOption returns the number of
// characters that were consumed from the formatting string.
int Decoder::FormatOption(Instruction* instr, const char* format) {
  switch (format[0]) {
    case 'o': {
      if (instr->Bit(10) == 1) {
        Print("o");
      }
      return 1;
    }
    case '.': {
      if (instr->Bit(0) == 1) {
        Print(".");
      } else {
        Print(" ");  // ensure consistent spacing
      }
      return 1;
    }
    case 'r': {
      return FormatRegister(instr, format);
    }
    case 'f': {
      return FormatFloatingRegister(instr, format);
    }
    case 'i': {  // int16
      return FormatImmediate(instr, format);
    }
    case 'u': {  // uint16
      int32_t value = instr->Bits(15, 0);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 6;
    }
    case 'l': {
      // Link (LK) Bit 0
      if (instr->Bit(0) == 1) {
        Print("l");
      }
      return 1;
    }
    case 'a': {
      // Absolute Address Bit 1
      if (instr->Bit(1) == 1) {
        Print("a");
      }
      return 1;
    }
    case 't': {  // 'target: target of branch instructions
      // target26 or target16
      DCHECK(STRING_STARTS_WITH(format, "target"));
      if ((format[6] == '2') && (format[7] == '6')) {
        int off = ((instr->Bits(25, 2)) << 8) >> 6;
        out_buffer_pos_ += base::SNPrintF(
            out_buffer_ + out_buffer_pos_, "%+d -> %s", off,
            converter_.NameOfAddress(reinterpret_cast<uint8_t*>(instr) + off));
        return 8;
      } else if ((format[6] == '1') && (format[7] == '6')) {
        int off = ((instr->Bits(15, 2)) << 18) >> 16;
        out_buffer_pos_ += base::SNPrintF(
            out_buffer_ + out_buffer_pos_, "%+d -> %s", off,
            converter_.NameOfAddress(reinterpret_cast<uint8_t*>(instr) + off));
        return 8;
      }
      break;
      case 'm': {
        return FormatMask(instr, format);
      }
    }
    case 'd': {  // ds value for offset
      return FormatDisplacement(instr, format);
    }
    default: {
      UNREACHABLE();
    }
  }

  UNREACHABLE();
}

int Decoder::FormatMask(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'm');
  int32_t value = 0;
  if ((format[1] == '1')) {  // prints the mask format in bits 8-12
    value = reinterpret_cast<RRInstruction*>(instr)->R1Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  } else if (format[1] == '2') {  // mask format in bits 16-19
    value = reinterpret_cast<RXInstruction*>(instr)->B2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  } else if (format[1] == '3') {  // mask format in bits 20-23
    value = reinterpret_cast<RRFInstruction*>(instr)->M4Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  } else if (format[1] == '4') {  // mask format in bits 32-35
    value = reinterpret_cast<VRR_C_Instruction*>(instr)->M4Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  } else if (format[1] == '5') {  // mask format in bits 28-31
    value = reinterpret_cast<VRR_C_Instruction*>(instr)->M5Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  } else if (format[1] == '6') {  // mask format in bits 24-27
    value = reinterpret_cast<VRR_C_Instruction*>(instr)->M6Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "0x%x", value);
    return 2;
  }
  out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
  return 2;
}

int Decoder::FormatDisplacement(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'd');

  if (format[1] == '1') {  // displacement in 20-31
    RSInstruction* rsinstr = reinterpret_cast<RSInstruction*>(instr);
    uint16_t value = rsinstr->D2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);

    return 2;
  } else if (format[1] == '2') {  // displacement in 20-39
    RXYInstruction* rxyinstr = reinterpret_cast<RXYInstruction*>(instr);
    int32_t value = rxyinstr->D2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '4') {  // SS displacement 2 36-47
    SSInstruction* ssInstr = reinterpret_cast<SSInstruction*>(instr);
    uint16_t value = ssInstr->D2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '3') {  // SS displacement 1 20 - 32
    SSInstruction* ssInstr = reinterpret_cast<SSInstruction*>(instr);
    uint16_t value = ssInstr->D1Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else {  // s390 specific
    int32_t value = SIGN_EXT_IMM16(instr->Bits(15, 0) & ~3);
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 1;
  }
}

int Decoder::FormatImmediate(Instruction* instr, const char* format) {
  DCHECK_EQ(format[0], 'i');

  if (format[1] == '1') {  // immediate in 16-31
    RIInstruction* riinstr = reinterpret_cast<RIInstruction*>(instr);
    int16_t value = riinstr->I2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '2') {  // immediate in 16-48
    RILInstruction* rilinstr = reinterpret_cast<RILInstruction*>(instr);
    int32_t value = rilinstr->I2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '3') {  // immediate in I format
    IInstruction* iinstr = reinterpret_cast<IInstruction*>(instr);
    int8_t value = iinstr->IValue();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '4') {  // immediate in 16-31, but outputs as offset
    RIInstruction* riinstr = reinterpret_cast<RIInstruction*>(instr);
    int16_t value = riinstr->I2Value() * 2;
    if (value >= 0)
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*+");
    else
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*");

    out_buffer_pos_ += base::SNPrintF(
        out_buffer_ + out_buffer_pos_, "%d -> %s", value,
        converter_.NameOfAddress(reinterpret_cast<uint8_t*>(instr) + value));
    return 2;
  } else if (format[1] == '5') {  // immediate in 16-31, but outputs as offset
    RILInstruction* rilinstr = reinterpret_cast<RILInstruction*>(instr);
    int32_t value = rilinstr->I2Value() * 2;
    if (value >= 0)
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*+");
    else
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*");

    out_buffer_pos_ += base::SNPrintF(
        out_buffer_ + out_buffer_pos_, "%d -> %s", value,
        converter_.NameOfAddress(reinterpret_cast<uint8_t*>(instr) + value));
    return 2;
  } else if (format[1] == '6') {  // unsigned immediate in 16-31
    RIInstruction* riinstr = reinterpret_cast<RIInstruction*>(instr);
    uint16_t value = riinstr->I2UnsignedValue();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '7') {  // unsigned immediate in 16-47
    RILInstruction* rilinstr = reinterpret_cast<RILInstruction*>(instr);
    uint32_t value = rilinstr->I2UnsignedValue();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '8') {  // unsigned immediate in 8-15
    SSInstruction* ssinstr = reinterpret_cast<SSInstruction*>(instr);
    uint8_t value = ssinstr->Length();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == '9') {  // unsigned immediate in 16-23
    RIEInstruction* rie_instr = reinterpret_cast<RIEInstruction*>(instr);
    uint8_t value = rie_instr->I3Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == 'a') {  // unsigned immediate in 24-31
    RIEInstruction* rie_instr = reinterpret_cast<RIEInstruction*>(instr);
    uint8_t value = rie_instr->I4Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == 'b') {  // unsigned immediate in 32-39
    RIEInstruction* rie_instr = reinterpret_cast<RIEInstruction*>(instr);
    uint8_t value = rie_instr->I5Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == 'c') {  // signed immediate in 8-15
    SSInstruction* ssinstr = reinterpret_cast<SSInstruction*>(instr);
    int8_t value = ssinstr->Length();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == 'd') {  // signed immediate in 32-47
    SILInstruction* silinstr = reinterpret_cast<SILInstruction*>(instr);
    int16_t value = silinstr->I2Value();
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
    return 2;
  } else if (format[1] == 'e') {  // immediate in 16-47, but outputs as offset
    RILInstruction* rilinstr = reinterpret_cast<RILInstruction*>(instr);
    int32_t value = rilinstr->I2Value() * 2;
    if (value >= 0)
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*+");
    else
      out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_, "*");

    out_buffer_pos_ += base::SNPrintF(
        out_buffer_ + out_buffer_pos_, "%d -> %s", value,
        converter_.NameOfAddress(reinterpret_cast<uint8_t*>(instr) + value));
    return 2;
  }

  UNREACHABLE();
}

// Format takes a formatting string for a whole instruction and prints it into
// the output buffer. All escaped options are handed to FormatOption to be
// parsed further.
void Decoder::Format(Instruction* instr, const char* format) {
  char cur = *format++;
  while ((cur != 0) && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    if (cur == '\'') {  // Single quote is used as the formatting escape.
      format += FormatOption(instr, format);
    } else {
      out_buffer_[out_buffer_pos_++] = cur;
    }
    cur = *format++;
  }
  out_buffer_[out_buffer_pos_] = '\0';
}

// The disassembler may end up decoding data inlined in the code. We do not want
// it to crash if the data does not resemble any known instruction.
#define VERIFY(condition) \
  if (!(condition)) {     \
    Unknown(instr);       \
    return;               \
  }

// For currently unimplemented decodings the disassembler calls Unknown(instr)
// which will just print "unknown" of the instruction bits.
void Decoder::Unknown(Instruction* instr) { Format(instr, "unknown"); }

// For currently unimplemented decodings the disassembler calls
// UnknownFormat(instr) which will just print opcode name of the
// instruction bits.
void Decoder::UnknownFormat(Instruction* instr, const char* name) {
  char buffer[100];
  snprintf(buffer, sizeof(buffer), "%s (unknown-format)", name);
  Format(instr, buffer);
}

#undef VERIFY
#undef STRING_STARTS_WITH

// Handles special cases of instructions;
// @return true if successfully decoded
bool Decoder::DecodeSpecial(Instruction* instr) {
  Opcode opcode = instr->S390OpcodeValue();
  switch (opcode) {
    case BKPT:
      Format(instr, "bkpt");
      break;
    case DUMY:
      Format(instr, "dumy\t'r1, 'd2 ( 'r2d, 'r3 )");
      break;
    /* RR format */
    case LDR:
      Format(instr, "ldr\t'f1,'f2");
      break;
    case BCR:
      Format(instr, "bcr\t'm1,'r2");
      break;
    case OR:
      Format(instr, "or\t'r1,'r2");
      break;
    case CR:
      Format(instr, "cr\t'r1,'r2");
      break;
    case MR:
      Format(instr, "mr\t'r1,'r2");
      break;
    case HER_Z:
      Format(instr, "her\t'r1,'r2");
      break;
    /* RI-b format */
    case BRAS:
      Format(instr, "bras\t'r1,'i1");
      break;
    /* RRE format */
    case MDBR:
      Format(instr, "mdbr\t'f5,'f6");
      break;
    case SDBR:
      Format(instr, "sdbr\t'f5,'f6");
      break;
    case ADBR:
      Format(instr, "adbr\t'f5,'f6");
      break;
    case CDBR:
      Format(instr, "cdbr\t'f5,'f6");
      break;
    case MEEBR:
      Format(instr, "meebr\t'f5,'f6");
      break;
    case SQDBR:
      Format(instr, "sqdbr\t'f5,'f6");
      break;
    case SQEBR:
      Format(instr, "sqebr\t'f5,'f6");
      break;
    case LCDBR:
      Format(instr, "lcdbr\t'f5,'f6");
      break;
    case LCEBR:
      Format(instr, "lcebr\t'f5,'f6");
      break;
    case LTEBR:
      Format(instr, "ltebr\t'f5,'f6");
      break;
    case LDEBR:
      Format(instr, "ldebr\t'f5,'f6");
      break;
    case CEBR:
      Format(instr, "cebr\t'f5,'f6");
      break;
    case AEBR:
      Format(instr, "aebr\t'f5,'f6");
      break;
    case SEBR:
      Format(instr, "sebr\t'f5,'f6");
      break;
    case DEBR:
      Format(instr, "debr\t'f5,'f6");
      break;
    case LTDBR:
      Format(instr, "ltdbr\t'f5,'f6");
      break;
    case LDGR:
      Format(instr, "ldgr\t'f5,'f6");
      break;
    case DDBR:
      Format(instr, "ddbr\t'f5,'f6");
      break;
    case LZDR:
      Format(instr, "lzdr\t'f5");
      break;
    /* RRF-e format */
    case FIEBRA:
      Format(instr, "fiebra\t'f5,'m2,'f6,'m3");
      break;
    case FIDBRA:
      Format(instr, "fidbra\t'f5,'m2,'f6,'m3");
      break;
    /* RX-a format */
    case IC_z:
      Format(instr, "ic\t'r1,'d1('r2d,'r3)");
      break;
    case AL:
      Format(instr, "al\t'r1,'d1('r2d,'r3)");
      break;
    case LE:
      Format(instr, "le\t'f1,'d1('r2d,'r3)");
      break;
    case LD:
      Format(instr, "ld\t'f1,'d1('r2d,'r3)");
      break;
    case STE:
      Format(instr, "ste\t'f1,'d1('r2d,'r3)");
      break;
    case STD:
      Format(instr, "std\t'f1,'d1('r2d,'r3)");
      break;
    /* S format */
    // TRAP4 is used in calling to native function. it will not be generated
    // in native code.
    case TRAP4:
      Format(instr, "trap4");
      break;
    /* RIL-a format */
    case CFI:
      Format(instr, "cfi\t'r1,'i2");
      break;
    case CGFI:
      Format(instr, "cgfi\t'r1,'i2");
      break;
    case AFI:
      Format(instr, "afi\t'r1,'i2");
      break;
    case AGFI:
      Format(instr, "agfi\t'r1,'i2");
      break;
    case MSFI:
      Format(instr, "msfi\t'r1,'i2");
      break;
    case MSGFI:
      Format(instr, "msgfi\t'r1,'i2");
      break;
    case ALSIH:
      Format(instr, "alsih\t'r1,'i2");
      break;
    case ALSIHN:
      Format(instr, "alsihn\t'r1,'i2");
      break;
    case CIH:
      Format(instr, "cih\t'r1,'i2");
      break;
    case AIH:
      Format(instr, "aih\t'r1,'i2");
      break;
    case LGFI:
      Format(instr, "lgfi\t'r1,'i2");
      break;
    /* SIY format */
    case ASI:
      Format(instr, "asi\t'd2('r3),'ic");
      break;
    case AGSI:
      Format(instr, "agsi\t'd2('r3),'ic");
      break;
    /* RXY-a format */
    case LT:
      Format(instr, "lt\t'r1,'d2('r2d,'r3)");
      break;
    case LDY:
      Format(instr, "ldy\t'f1,'d2('r2d,'r3)");
      break;
    case LEY:
      Format(instr, "ley\t'f1,'d2('r2d,'r3)");
      break;
    case STDY:
      Format(instr, "stdy\t'f1,'d2('r2d,'r3)");
      break;
    case STEY:
      Format(instr, "stey\t'f1,'d2('r2d,'r3)");
      break;
    /* RXE format */
    case LDEB:
      Format(instr, "ldeb\t'f1,'d2('r2d,'r3)");
      break;
    default:
      return false;
  }
  return true;
}

// Handles common cases of instructions;
// @return true if successfully decoded
bool Decoder::DecodeGeneric(Instruction* instr) {
  Opcode opcode = instr->S390OpcodeValue();
  switch (opcode) {
    /* 2 bytes */
#define DECODE_RR_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                             \
    Format(instr, #name "\t'r1,'r2");                           \
    break;
    S390_RR_OPCODE_LIST(DECODE_RR_INSTRUCTIONS)
#undef DECODE_RR_INSTRUCTIONS

    /* 4 bytes */
#define DECODE_RS_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'r1,'r2,'d1('r3)");                    \
    break;
    S390_RS_A_OPCODE_LIST(DECODE_RS_A_INSTRUCTIONS)
#undef DECODE_RS_A_INSTRUCTIONS

#define DECODE_RSI_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'r1,'r2,'i4");                        \
    break;
    S390_RSI_OPCODE_LIST(DECODE_RSI_INSTRUCTIONS)
#undef DECODE_RSI_INSTRUCTIONS

#define DECODE_RI_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'r1,'i1");                             \
    break;
    S390_RI_A_OPCODE_LIST(DECODE_RI_A_INSTRUCTIONS)
#undef DECODE_RI_A_INSTRUCTIONS

#define DECODE_RI_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'r1,'i4");                             \
    break;
    S390_RI_B_OPCODE_LIST(DECODE_RI_B_INSTRUCTIONS)
#undef DECODE_RI_B_INSTRUCTIONS

#define DECODE_RI_C_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'm1,'i4");                             \
    break;
    S390_RI_C_OPCODE_LIST(DECODE_RI_C_INSTRUCTIONS)
#undef DECODE_RI_C_INSTRUCTIONS

#define DECODE_RRE_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'r5,'r6");                            \
    break;
    S390_RRE_OPCODE_LIST(DECODE_RRE_INSTRUCTIONS)
#undef DECODE_RRE_INSTRUCTIONS

#define DECODE_RRF_A_INSTRUCTIONS(name, opcode_name, opcode_val) \
  case opcode_name:                                              \
    Format(instr, #name "\t'r5,'r6,'r3");                        \
    break;
    S390_RRF_A_OPCODE_LIST(DECODE_RRF_A_INSTRUCTIONS)
#undef DECODE_RRF_A_INSTRUCTIONS

#define DECODE_RRF_C_INSTRUCTIONS(name, opcode_name, opcode_val) \
  case opcode_name:                                              \
    Format(instr, #name "\t'r5,'r6,'m2");                        \
    break;
    S390_RRF_C_OPCODE_LIST(DECODE_RRF_C_INSTRUCTIONS)
#undef DECODE_RRF_C_INSTRUCTIONS

#define DECODE_RRF_E_INSTRUCTIONS(name, opcode_name, opcode_val) \
  case opcode_name:                                              \
    Format(instr, #name "\t'r5,'m2,'f6");                        \
    break;
    S390_RRF_E_OPCODE_LIST(DECODE_RRF_E_INSTRUCTIONS)
#undef DECODE_RRF_E_INSTRUCTIONS

#define DECODE_RX_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'r1,'d1('r2d,'r3)");                   \
    break;
    S390_RX_A_OPCODE_LIST(DECODE_RX_A_INSTRUCTIONS)
#undef DECODE_RX_A_INSTRUCTIONS

#define DECODE_RX_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'm1,'d1('r2d,'r3)");                   \
    break;
    S390_RX_B_OPCODE_LIST(DECODE_RX_B_INSTRUCTIONS)
#undef DECODE_RX_B_INSTRUCTIONS

#define DECODE_RRD_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'f3,'f5,'f6");                        \
    break;
    S390_RRD_OPCODE_LIST(DECODE_RRD_INSTRUCTIONS)
#undef DECODE_RRD_INSTRUCTIONS

#define DECODE_SI_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                             \
    Format(instr, #name "\t'd1('r3),'i8");                      \
    break;
    S390_SI_OPCODE_LIST(DECODE_SI_INSTRUCTIONS)
#undef DECODE_SI_INSTRUCTIONS

    /* 6 bytes */
#define DECODE_VRR_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'m4,'m5,'m6");                  \
    break;
    S390_VRR_A_OPCODE_LIST(DECODE_VRR_A_INSTRUCTIONS)
#undef DECODE_VRR_A_INSTRUCTIONS

#define DECODE_VRR_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'f3,'m4,'m6");                  \
    break;
    S390_VRR_B_OPCODE_LIST(DECODE_VRR_B_INSTRUCTIONS)
#undef DECODE_VRR_B_INSTRUCTIONS

#define DECODE_VRR_C_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'f3,'m4");                      \
    break;
    S390_VRR_C_OPCODE_LIST(DECODE_VRR_C_INSTRUCTIONS)
#undef DECODE_VRR_C_INSTRUCTIONS

#define DECODE_VRR_E_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'f3,'f4,'m5,'m3");              \
    break;
    S390_VRR_E_OPCODE_LIST(DECODE_VRR_E_INSTRUCTIONS)
#undef DECODE_VRR_E_INSTRUCTIONS

#define DECODE_VRR_F_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'r1,'r2");                          \
    break;
    S390_VRR_F_OPCODE_LIST(DECODE_VRR_F_INSTRUCTIONS)
#undef DECODE_VRR_F_INSTRUCTIONS

#define DECODE_VRX_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'f1,'d1('r2d,'r3),'m4");              \
    break;
    S390_VRX_OPCODE_LIST(DECODE_VRX_INSTRUCTIONS)
#undef DECODE_VRX_INSTRUCTIONS

#define DECODE_VRS_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'d1('r3),'m4");                 \
    break;
    S390_VRS_A_OPCODE_LIST(DECODE_VRS_A_INSTRUCTIONS)
#undef DECODE_VRS_A_INSTRUCTIONS

#define DECODE_VRS_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'r2,'d1('r3),'m4");                 \
    break;
    S390_VRS_B_OPCODE_LIST(DECODE_VRS_B_INSTRUCTIONS)
#undef DECODE_VRS_B_INSTRUCTIONS

#define DECODE_VRS_C_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'f2,'d1('r3),'m4");                 \
    break;
    S390_VRS_C_OPCODE_LIST(DECODE_VRS_C_INSTRUCTIONS)
#undef DECODE_VRS_C_INSTRUCTIONS

#define DECODE_VRI_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'i1,'m4");                          \
    break;
    S390_VRI_A_OPCODE_LIST(DECODE_VRI_A_INSTRUCTIONS)
#undef DECODE_VRI_A_INSTRUCTIONS

#define DECODE_VRI_C_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'f1,'f2,'i1,'m4");                      \
    break;
    S390_VRI_C_OPCODE_LIST(DECODE_VRI_C_INSTRUCTIONS)
#undef DECODE_VRI_C_INSTRUCTIONS

#define DECODE_RIL_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'i7");                              \
    break;
    S390_RIL_A_OPCODE_LIST(DECODE_RIL_A_INSTRUCTIONS)
#undef DECODE_RIL_A_INSTRUCTIONS

#define DECODE_RIL_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'ie");                              \
    break;
    S390_RIL_B_OPCODE_LIST(DECODE_RIL_B_INSTRUCTIONS)
#undef DECODE_RIL_B_INSTRUCTIONS

#define DECODE_RIL_C_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'm1,'ie");                              \
    break;
    S390_RIL_C_OPCODE_LIST(DECODE_RIL_C_INSTRUCTIONS)
#undef DECODE_RIL_C_INSTRUCTIONS

#define DECODE_SIY_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'd2('r3),'i8");                       \
    break;
    S390_SIY_OPCODE_LIST(DECODE_SIY_INSTRUCTIONS)
#undef DECODE_SIY_INSTRUCTIONS

#define DECODE_RIE_D_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'r2,'i1");                          \
    break;
    S390_RIE_D_OPCODE_LIST(DECODE_RIE_D_INSTRUCTIONS)
#undef DECODE_RIE_D_INSTRUCTIONS

#define DECODE_RIE_E_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'r2,'i4");                          \
    break;
    S390_RIE_E_OPCODE_LIST(DECODE_RIE_E_INSTRUCTIONS)
#undef DECODE_RIE_E_INSTRUCTIONS

#define DECODE_RIE_F_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'r2,'i9,'ia,'ib");                  \
    break;
    S390_RIE_F_OPCODE_LIST(DECODE_RIE_F_INSTRUCTIONS)
#undef DECODE_RIE_F_INSTRUCTIONS

#define DECODE_RSY_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'r2,'d2('r3)");                     \
    break;
    S390_RSY_A_OPCODE_LIST(DECODE_RSY_A_INSTRUCTIONS)
#undef DECODE_RSY_A_INSTRUCTIONS

#define DECODE_RSY_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'm2,'r1,'d2('r3)");                     \
    break;
    S390_RSY_B_OPCODE_LIST(DECODE_RSY_B_INSTRUCTIONS)
#undef DECODE_RSY_B_INSTRUCTIONS

#define DECODE_RXY_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'r1,'d2('r2d,'r3)");                    \
    break;
    S390_RXY_A_OPCODE_LIST(DECODE_RXY_A_INSTRUCTIONS)
#undef DECODE_RXY_A_INSTRUCTIONS

#define DECODE_RXY_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                                \
    Format(instr, #name "\t'm1,'d2('r2d,'r3)");                    \
    break;
    S390_RXY_B_OPCODE_LIST(DECODE_RXY_B_INSTRUCTIONS)
#undef DECODE_RXY_B_INSTRUCTIONS

#define DECODE_RXE_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'f1,'d1('r2d, 'r3)");                 \
    break;
    S390_RXE_OPCODE_LIST(DECODE_RXE_INSTRUCTIONS)
#undef DECODE_RXE_INSTRUCTIONS

#define DECODE_SIL_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                              \
    Format(instr, #name "\t'd3('r3),'id");                       \
    break;
    S390_SIL_OPCODE_LIST(DECODE_SIL_INSTRUCTIONS)
#undef DECODE_SIL_INSTRUCTIONS

#define DECODE_SS_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name:                                               \
    Format(instr, #name "\t'd3('i8,'r3),'d4('r7)");               \
    break;
    S390_SS_A_OPCODE_LIST(DECODE_SS_A_INSTRUCTIONS)
#undef DECODE_SS_A_INSTRUCTIONS

    default:
      return false;
  }
  return true;
}

// Disassemble the instruction at *instr_ptr into the output buffer.
int Decoder::InstructionDecode(uint8_t* instr_ptr) {
  Instruction* instr = Instruction::At(instr_ptr);
  int instrLength = instr->InstructionLength();

  // Print the Instruction bits.
  if (instrLength == 2) {
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%04x           ",
                       instr->InstructionBits<TwoByteInstr>());
  } else if (instrLength == 4) {
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%08x       ",
                       instr->InstructionBits<FourByteInstr>());
  } else {
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%012" PRIx64 "   ",
                       instr->InstructionBits<SixByteInstr>());
  }

  bool decoded = DecodeSpecial(instr);
  if (!decoded) decoded = DecodeGeneric(instr);
  if (!decoded) Unknown(instr);
  return instrLength;
}

}  // namespace internal
}  // namespace v8

//------------------------------------------------------------------------------

namespace disasm {

const char* NameConverter::NameOfAddress(uint8_t* addr) const {
  v8::base::SNPrintF(tmp_buffer_, "%p", static_cast<void*>(addr));
  return tmp_buffer_.begin();
}

const char* NameConverter::NameOfConstant(uint8_t* addr) const {
  return NameOfAddress(addr);
}

const char* NameConverter::NameOfCPURegister(int reg) const {
  return RegisterName(i::Register::from_code(reg));
}

const char* NameConverter::NameOfByteCPURegister(int reg) const {
  UNREACHABLE();  // S390 does not have the concept of a byte register
}

const char* NameConverter::NameOfXMMRegister(int reg) const {
  // S390 does not have XMM register
  // TODO(joransiu): Consider update this for Vector Regs
  UNREACHABLE();
}

const char* NameConverter::NameInCode(uint8_t* addr) const {
  // The default name converter is called for unknown code. So we will not try
  // to access any memory.
  return "";
}

//------------------------------------------------------------------------------

int Disassembler::InstructionDecode(v8::base::Vector<char> buffer,
                                    uint8_t* instruction) {
  v8::internal::Decoder d(converter_, buffer);
  return d.InstructionDecode(instruction);
}

// The S390 assembler does not currently use constant pools.
int Disassembler::ConstantPoolSizeAt(uint8_t* instruction) { return -1; }

void Disassembler::Disassemble(FILE* f, uint8_t* begin, uint8_t* end,
                               UnimplementedOpcodeAction unimplemented_action) {
  NameConverter converter;
  Disassembler d(converter, unimplemented_action);
  for (uint8_t* pc = begin; pc < end;) {
    v8::base::EmbeddedVector<char, 128> buffer;
    buffer[0] = '\0';
    uint8_t* prev_pc = pc;
    pc += d.InstructionDecode(buffer, pc);
    v8::internal::PrintF(f, "%p    %08x      %s\n", static_cast<void*>(prev_pc),
                         *reinterpret_cast<int32_t*>(prev_pc), buffer.begin());
  }
}

}  // namespace disasm

#endif  // V8_TARGET_ARCH_S390X
