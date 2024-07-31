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

#if V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64

#include "src/base/platform/platform.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/ppc/constants-ppc.h"
#include "src/codegen/register-configuration.h"
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

  // Prefixed instructions.
  enum PrefixType { not_prefixed, is_prefixed };
  // static is used to retain values even with new instances.
  static PrefixType PrefixStatus;
  static uint64_t PrefixValue;
  uint64_t GetPrefixValue();
  void SetAsPrefixed(uint64_t v);
  void ResetPrefix();
  bool IsPrefixed();

 private:
  // Bottleneck functions to print into the out_buffer.
  void PrintChar(const char ch);
  void Print(const char* str);

  // Printing of common values.
  void PrintRegister(int reg);
  void PrintDRegister(int reg);
  void PrintVectorRegister(int reg);
  int FormatFPRegister(Instruction* instr, const char* format);
  int FormatVectorRegister(Instruction* instr, const char* format);
  void PrintSoftwareInterrupt(SoftwareInterruptCodes svc);
  const char* NameOfVectorRegister(int reg) const;

  // Handle formatting of instructions and their options.
  int FormatRegister(Instruction* instr, const char* option);
  int FormatOption(Instruction* instr, const char* option);
  void Format(Instruction* instr, const char* format);
  void Unknown(Instruction* instr);
  void UnknownFormat(Instruction* instr, const char* opcname);

  void DecodeExtP(Instruction* instr);
  void DecodeExt0(Instruction* instr);
  void DecodeExt1(Instruction* instr);
  void DecodeExt2(Instruction* instr);
  void DecodeExt3(Instruction* instr);
  void DecodeExt4(Instruction* instr);
  void DecodeExt5(Instruction* instr);
  void DecodeExt6(Instruction* instr);

  const disasm::NameConverter& converter_;
  base::Vector<char> out_buffer_;
  int out_buffer_pos_;
};

// Define Prefix functions and values.
// static
Decoder::PrefixType Decoder::PrefixStatus = not_prefixed;
uint64_t Decoder::PrefixValue = 0;

uint64_t Decoder::GetPrefixValue() { return PrefixValue; }

void Decoder::SetAsPrefixed(uint64_t v) {
  PrefixStatus = is_prefixed;
  PrefixValue = v;
}

void Decoder::ResetPrefix() {
  PrefixStatus = not_prefixed;
  PrefixValue = 0;
}

bool Decoder::IsPrefixed() { return PrefixStatus == is_prefixed; }

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

// Print the Simd128 register name according to the active name converter.
void Decoder::PrintVectorRegister(int reg) {
  Print(RegisterName(Simd128Register::from_code(reg)));
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

  if ((format[1] == 't') || (format[1] == 's')) {  // 'rt & 'rs register
    int reg = instr->RTValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'a') {  // 'ra: RA register
    int reg = instr->RAValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'b') {  // 'rb: RB register
    int reg = instr->RBValue();
    PrintRegister(reg);
    return 2;
  }

  UNREACHABLE();
}

// Handle all FP register based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatFPRegister(Instruction* instr, const char* format) {
  DCHECK(format[0] == 'D' || format[0] == 'X');

  int retval = 2;
  int reg = -1;
  if (format[1] == 't' || format[1] == 's') {
    reg = instr->RTValue();
  } else if (format[1] == 'a') {
    reg = instr->RAValue();
  } else if (format[1] == 'b') {
    reg = instr->RBValue();
  } else if (format[1] == 'c') {
    reg = instr->RCValue();
  } else {
    UNREACHABLE();
  }

  PrintDRegister(reg);

  return retval;
}

int Decoder::FormatVectorRegister(Instruction* instr, const char* format) {
  int retval = 2;
  int reg = -1;
  if (format[1] == 't' || format[1] == 's') {
    reg = instr->RTValue();
  } else if (format[1] == 'a') {
    reg = instr->RAValue();
  } else if (format[1] == 'b') {
    reg = instr->RBValue();
  } else if (format[1] == 'c') {
    reg = instr->RCValue();
  } else {
    UNREACHABLE();
  }

  PrintVectorRegister(reg);

  return retval;
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
    case 'D': {
      return FormatFPRegister(instr, format);
    }
    case 'X': {
      // Check the TX/SX value, if set then it's a Vector register.
      if (instr->Bit(0) == 1) {
        return FormatVectorRegister(instr, format);
      }
      // Double (VSX) register.
      return FormatFPRegister(instr, format);
    }
    case 'V': {
      return FormatVectorRegister(instr, format);
    }
    case 'i': {  // int16
      int64_t value;
      uint32_t imm_value = instr->Bits(15, 0);
      if (IsPrefixed()) {
        uint64_t prefix_value = GetPrefixValue();
        value = SIGN_EXT_IMM34((prefix_value << 16) | imm_value);
      } else {
        value = (static_cast<int64_t>(imm_value) << 48) >> 48;
      }
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%ld", value);
      return 5;
    }
    case 'I': {  // IMM8
      int8_t value = instr->Bits(18, 11);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 4;
    }
    case 'u': {  // uint16
      int32_t value = instr->Bits(15, 0);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 6;
    }
    case 'F': {  // FXM
      uint8_t value = instr->Bits(19, 12);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 3;
    }
    case 'S': {  // SIM
      int32_t value = static_cast<int32_t>(SIGN_EXT_IMM5(instr->Bits(20, 16)));
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 3;
    }
    case 'U': {  // UIM
      uint8_t value = instr->Bits(19, 16);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 3;
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
    case 'c': {  // 'cr: condition register of branch instruction
      int code = instr->Bits(20, 18);
      if (code != 7) {
        out_buffer_pos_ +=
            base::SNPrintF(out_buffer_ + out_buffer_pos_, " cr%d", code);
      }
      return 2;
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
      case 's': {
        DCHECK_EQ(format[1], 'h');
        int32_t value = 0;
        int32_t opcode = instr->OpcodeValue() << 26;
        int32_t sh = instr->Bits(15, 11);
        if (opcode == EXT5 ||
            (opcode == EXT2 && instr->Bits(10, 2) << 2 == SRADIX)) {
          // SH Bits 1 and 15-11 (split field)
          value = (sh | (instr->Bit(1) << 5));
        } else {
          // SH Bits 15-11
          value = (sh << 26) >> 26;
        }
        out_buffer_pos_ +=
            base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
        return 2;
      }
      case 'm': {
        int32_t value = 0;
        if (format[1] == 'e') {
          if (instr->OpcodeValue() << 26 != EXT5) {
            // ME Bits 10-6
            value = (instr->Bits(10, 6) << 26) >> 26;
          } else {
            // ME Bits 5 and 10-6 (split field)
            value = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
          }
        } else if (format[1] == 'b') {
          if (instr->OpcodeValue() << 26 != EXT5) {
            // MB Bits 5-1
            value = (instr->Bits(5, 1) << 26) >> 26;
          } else {
            // MB Bits 5 and 10-6 (split field)
            value = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
          }
        } else {
          UNREACHABLE();  // bad format
        }
        out_buffer_pos_ +=
            base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
        return 2;
      }
    }
#if V8_TARGET_ARCH_PPC64
    case 'd': {  // ds value for offset
      int32_t value = SIGN_EXT_IMM16(instr->Bits(15, 0) & ~3);
      out_buffer_pos_ +=
          base::SNPrintF(out_buffer_ + out_buffer_pos_, "%d", value);
      return 1;
    }
#endif
    default: {
      UNREACHABLE();
    }
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

void Decoder::DecodeExtP(Instruction* instr) {
  switch (EXTP | (instr->BitField(25, 25))) {
    case PLOAD_STORE_8LS:
    case PLOAD_STORE_MLS: {
      // TODO(miladfarca): Decode the R bit.
      DCHECK_NE(instr->Bit(20), 1);
      // Read prefix.
      SetAsPrefixed(instr->Bits(17, 0));
      // Read suffix (next instruction).
      Instruction* next_instr = reinterpret_cast<Instruction*>(
          reinterpret_cast<intptr_t>(instr) + kInstrSize);
      switch (next_instr->OpcodeBase()) {
          // Prefixed ADDI.
        case (ADDI): {
          if (next_instr->RAValue() == 0) {
            // This is load immediate prefixed.
            Format(instr, "pli");
            Format(next_instr, "     'rt, ");
          } else {
            Format(instr, "paddi");
            Format(next_instr, "   'rt, 'ra, ");
          }
          Format(next_instr, "'int34");
          break;
        }
        // Prefixed LBZ.
        case LBZ: {
          Format(next_instr, "plbz    'rt, 'int34('ra)");
          break;
        }
          // Prefixed LHZ.
        case LHZ: {
          Format(next_instr, "plhz    'rt, 'int34('ra)");
          break;
        }
          // Prefixed LHA.
        case LHA: {
          Format(next_instr, "plha    'rt, 'int34('ra)");
          break;
        }
          // Prefixed LWZ.
        case LWZ: {
          Format(next_instr, "plwz    'rt, 'int34('ra)");
          break;
        }
          // Prefixed LWA.
        case PPLWA: {
          Format(next_instr, "plwa    'rt, 'int34('ra)");
          break;
        }
          // Prefixed LD.
        case PPLD: {
          Format(next_instr, "pld     'rt, 'int34('ra)");
          break;
        }
          // Prefixed LFS.
        case LFS: {
          Format(next_instr, "plfs    'Dt, 'int34('ra)");
          break;
        }
        // Prefixed LFD.
        case LFD: {
          Format(next_instr, "plfd    'Dt, 'int34('ra)");
          break;
        }
          // Prefixed STB.
        case STB: {
          Format(next_instr, "pstb    'rs, 'int34('ra)");
          break;
        }
        // Prefixed STH.
        case STH: {
          Format(next_instr, "psth    'rs, 'int34('ra)");
          break;
        }
        // Prefixed STW.
        case STW: {
          Format(next_instr, "pstw    'rs, 'int34('ra)");
          break;
        }
        // Prefixed STD.
        case PPSTD: {
          Format(next_instr, "pstd    'rs, 'int34('ra)");
          break;
        }
        // Prefixed STFS.
        case STFS: {
          Format(next_instr, "pstfs   'Dt, 'int34('ra)");
          break;
        }
        // Prefixed STFD.
        case STFD: {
          Format(next_instr, "pstfd   'Dt, 'int34('ra)");
          break;
        }
        default: {
          Unknown(instr);
        }
      }
      break;
    }
    default: {
      Unknown(instr);
    }
  }
}

void Decoder::DecodeExt0(Instruction* instr) {
  // Some encodings have integers hard coded in the middle, handle those first.
  switch (EXT0 | (instr->BitField(20, 16)) | (instr->BitField(10, 0))) {
#define DECODE_VX_D_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'Vb");                                   \
    return;                                                             \
  }
    PPC_VX_OPCODE_D_FORM_LIST(DECODE_VX_D_FORM__INSTRUCTIONS)
#undef DECODE_VX_D_FORM__INSTRUCTIONS
#define DECODE_VX_F_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'rt, 'Vb");                                   \
    return;                                                             \
  }
    PPC_VX_OPCODE_F_FORM_LIST(DECODE_VX_F_FORM__INSTRUCTIONS)
#undef DECODE_VX_F_FORM__INSTRUCTIONS
  }
  // Some encodings are 5-0 bits, handle those first
  switch (EXT0 | (instr->BitField(5, 0))) {
#define DECODE_VA_A_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'Va, 'Vb, 'Vc");                         \
    return;                                                             \
  }
    PPC_VA_OPCODE_A_FORM_LIST(DECODE_VA_A_FORM__INSTRUCTIONS)
#undef DECODE_VA_A_FORM__INSTRUCTIONS
  }
  switch (EXT0 | (instr->BitField(9, 0))) {
// TODO(miladfarca): Fix RC indicator.
#define DECODE_VC_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                 \
    Format(instr, #name " 'Vt, 'Va, 'Vb");                            \
    return;                                                           \
  }
    PPC_VC_OPCODE_LIST(DECODE_VC_FORM__INSTRUCTIONS)
#undef DECODE_VC_FORM__INSTRUCTIONS
  }
  switch (EXT0 | (instr->BitField(10, 0))) {
#define DECODE_VX_A_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'Vb, 'UIM");                             \
    return;                                                             \
  }
    PPC_VX_OPCODE_A_FORM_LIST(DECODE_VX_A_FORM__INSTRUCTIONS)
#undef DECODE_VX_A_FORM__INSTRUCTIONS
#define DECODE_VX_B_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'Va, 'Vb");                              \
    return;                                                             \
  }
    PPC_VX_OPCODE_B_FORM_LIST(DECODE_VX_B_FORM__INSTRUCTIONS)
#undef DECODE_VX_B_FORM__INSTRUCTIONS
#define DECODE_VX_C_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'Vb");                                   \
    return;                                                             \
  }
    PPC_VX_OPCODE_C_FORM_LIST(DECODE_VX_C_FORM__INSTRUCTIONS)
#undef DECODE_VX_C_FORM__INSTRUCTIONS
#define DECODE_VX_E_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'SIM");                                  \
    return;                                                             \
  }
    PPC_VX_OPCODE_E_FORM_LIST(DECODE_VX_E_FORM__INSTRUCTIONS)
#undef DECODE_VX_E_FORM__INSTRUCTIONS
#define DECODE_VX_G_FORM__INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Vt, 'rb, 'UIM");                             \
    return;                                                             \
  }
    PPC_VX_OPCODE_G_FORM_LIST(DECODE_VX_G_FORM__INSTRUCTIONS)
#undef DECODE_VX_G_FORM__INSTRUCTIONS
  }
}

void Decoder::DecodeExt1(Instruction* instr) {
  switch (EXT1 | (instr->BitField(10, 1))) {
    case MCRF: {
      UnknownFormat(instr, "mcrf");  // not used by V8
      break;
    }
    case BCLRX: {
      int bo = instr->BitField(25, 21);
      int bi = instr->Bits(20, 16);
      CRBit cond = static_cast<CRBit>(bi & (CRWIDTH - 1));
      switch (bo) {
        case DCBNZF: {
          UnknownFormat(instr, "bclrx-dcbnzf");
          break;
        }
        case DCBEZF: {
          UnknownFormat(instr, "bclrx-dcbezf");
          break;
        }
        case BF: {
          switch (cond) {
            case CR_EQ:
              Format(instr, "bnelr'l'cr");
              break;
            case CR_GT:
              Format(instr, "blelr'l'cr");
              break;
            case CR_LT:
              Format(instr, "bgelr'l'cr");
              break;
            case CR_SO:
              Format(instr, "bnsolr'l'cr");
              break;
          }
          break;
        }
        case DCBNZT: {
          UnknownFormat(instr, "bclrx-dcbbzt");
          break;
        }
        case DCBEZT: {
          UnknownFormat(instr, "bclrx-dcbnezt");
          break;
        }
        case BT: {
          switch (cond) {
            case CR_EQ:
              Format(instr, "beqlr'l'cr");
              break;
            case CR_GT:
              Format(instr, "bgtlr'l'cr");
              break;
            case CR_LT:
              Format(instr, "bltlr'l'cr");
              break;
            case CR_SO:
              Format(instr, "bsolr'l'cr");
              break;
          }
          break;
        }
        case DCBNZ: {
          UnknownFormat(instr, "bclrx-dcbnz");
          break;
        }
        case DCBEZ: {
          UnknownFormat(instr, "bclrx-dcbez");  // not used by V8
          break;
        }
        case BA: {
          Format(instr, "blr'l");
          break;
        }
      }
      break;
    }
    case BCCTRX: {
      switch (instr->BitField(25, 21)) {
        case DCBNZF: {
          UnknownFormat(instr, "bcctrx-dcbnzf");
          break;
        }
        case DCBEZF: {
          UnknownFormat(instr, "bcctrx-dcbezf");
          break;
        }
        case BF: {
          UnknownFormat(instr, "bcctrx-bf");
          break;
        }
        case DCBNZT: {
          UnknownFormat(instr, "bcctrx-dcbnzt");
          break;
        }
        case DCBEZT: {
          UnknownFormat(instr, "bcctrx-dcbezf");
          break;
        }
        case BT: {
          UnknownFormat(instr, "bcctrx-bt");
          break;
        }
        case DCBNZ: {
          UnknownFormat(instr, "bcctrx-dcbnz");
          break;
        }
        case DCBEZ: {
          UnknownFormat(instr, "bcctrx-dcbez");
          break;
        }
        case BA: {
          if (instr->Bit(0) == 1) {
            Format(instr, "bctrl");
          } else {
            Format(instr, "bctr");
          }
          break;
        }
        default: {
          UNREACHABLE();
        }
      }
      break;
    }
    case CRNOR: {
      Format(instr, "crnor (stuff)");
      break;
    }
    case RFI: {
      Format(instr, "rfi (stuff)");
      break;
    }
    case CRANDC: {
      Format(instr, "crandc (stuff)");
      break;
    }
    case ISYNC: {
      Format(instr, "isync (stuff)");
      break;
    }
    case CRXOR: {
      Format(instr, "crxor (stuff)");
      break;
    }
    case CRNAND: {
      UnknownFormat(instr, "crnand");
      break;
    }
    case CRAND: {
      UnknownFormat(instr, "crand");
      break;
    }
    case CREQV: {
      UnknownFormat(instr, "creqv");
      break;
    }
    case CRORC: {
      UnknownFormat(instr, "crorc");
      break;
    }
    case CROR: {
      UnknownFormat(instr, "cror");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt2(Instruction* instr) {
  // Some encodings are 10-1 bits, handle those first
  switch (EXT2 | (instr->BitField(10, 1))) {
    case LVX: {
      Format(instr, "lvx     'Vt, 'ra, 'rb");
      return;
    }
    case STVX: {
      Format(instr, "stvx    'Vs, 'ra, 'rb");
      return;
    }
    case LXVD: {
      Format(instr, "lxvd    'Xt, 'ra, 'rb");
      return;
    }
    case LXVX: {
      Format(instr, "lxvx    'Xt, 'ra, 'rb");
      return;
    }
    case LXSDX: {
      Format(instr, "lxsdx    'Xt, 'ra, 'rb");
      return;
    }
    case LXSIBZX: {
      Format(instr, "lxsibzx  'Xt, 'ra, 'rb");
      return;
    }
    case LXSIHZX: {
      Format(instr, "lxsihzx  'Xt, 'ra, 'rb");
      return;
    }
    case LXSIWZX: {
      Format(instr, "lxsiwzx  'Xt, 'ra, 'rb");
      return;
    }
    case STXVD: {
      Format(instr, "stxvd   'Xs, 'ra, 'rb");
      return;
    }
    case STXVX: {
      Format(instr, "stxvx   'Xs, 'ra, 'rb");
      return;
    }
    case STXSDX: {
      Format(instr, "stxsdx  'Xs, 'ra, 'rb");
      return;
    }
    case STXSIBX: {
      Format(instr, "stxsibx 'Xs, 'ra, 'rb");
      return;
    }
    case STXSIHX: {
      Format(instr, "stxsihx 'Xs, 'ra, 'rb");
      return;
    }
    case STXSIWX: {
      Format(instr, "stxsiwx 'Xs, 'ra, 'rb");
      return;
    }
    case SRWX: {
      Format(instr, "srw'.    'ra, 'rs, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case SRDX: {
      Format(instr, "srd'.    'ra, 'rs, 'rb");
      return;
    }
#endif
    case SRAW: {
      Format(instr, "sraw'.   'ra, 'rs, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case SRAD: {
      Format(instr, "srad'.   'ra, 'rs, 'rb");
      return;
    }
#endif
    case SYNC: {
      Format(instr, "sync");
      return;
    }
    case MODSW: {
      Format(instr, "modsw  'rt, 'ra, 'rb");
      return;
    }
    case MODUW: {
      Format(instr, "moduw  'rt, 'ra, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case MODSD: {
      Format(instr, "modsd  'rt, 'ra, 'rb");
      return;
    }
    case MODUD: {
      Format(instr, "modud  'rt, 'ra, 'rb");
      return;
    }
#endif
    case SRAWIX: {
      Format(instr, "srawi'.  'ra,'rs,'sh");
      return;
    }
    case EXTSH: {
      Format(instr, "extsh'.  'ra, 'rs");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case EXTSW: {
      Format(instr, "extsw'.  'ra, 'rs");
      return;
    }
#endif
    case EXTSB: {
      Format(instr, "extsb'.  'ra, 'rs");
      return;
    }
    case LFSX: {
      Format(instr, "lfsx    'Dt, 'ra, 'rb");
      return;
    }
    case LFSUX: {
      Format(instr, "lfsux   'Dt, 'ra, 'rb");
      return;
    }
    case LFDX: {
      Format(instr, "lfdx    'Dt, 'ra, 'rb");
      return;
    }
    case LFDUX: {
      Format(instr, "lfdux   'Dt, 'ra, 'rb");
      return;
    }
    case STFSX: {
      Format(instr, "stfsx    'rs, 'ra, 'rb");
      return;
    }
    case STFSUX: {
      Format(instr, "stfsux   'rs, 'ra, 'rb");
      return;
    }
    case STFDX: {
      Format(instr, "stfdx    'rs, 'ra, 'rb");
      return;
    }
    case STFDUX: {
      Format(instr, "stfdux   'rs, 'ra, 'rb");
      return;
    }
    case POPCNTW: {
      Format(instr, "popcntw  'ra, 'rs");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case POPCNTD: {
      Format(instr, "popcntd  'ra, 'rs");
      return;
    }
#endif
  }

  switch (EXT2 | (instr->BitField(10, 2))) {
    case SRADIX: {
      Format(instr, "sradi'.  'ra,'rs,'sh");
      return;
    }
  }

  switch (EXT2 | (instr->BitField(10, 0))) {
    case STBCX: {
      Format(instr, "stbcx   'rs, 'ra, 'rb");
      return;
    }
    case STHCX: {
      Format(instr, "sthcx   'rs, 'ra, 'rb");
      return;
    }
    case STWCX: {
      Format(instr, "stwcx   'rs, 'ra, 'rb");
      return;
    }
    case STDCX: {
      Format(instr, "stdcx   'rs, 'ra, 'rb");
      return;
    }
  }

  // ?? are all of these xo_form?
  switch (EXT2 | (instr->BitField(10, 1))) {
    case CMP: {
#if V8_TARGET_ARCH_PPC64
      if (instr->Bit(21)) {
#endif
        Format(instr, "cmp     'ra, 'rb");
#if V8_TARGET_ARCH_PPC64
      } else {
        Format(instr, "cmpw    'ra, 'rb");
      }
#endif
      return;
    }
    case SLWX: {
      Format(instr, "slw'.   'ra, 'rs, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case SLDX: {
      Format(instr, "sld'.   'ra, 'rs, 'rb");
      return;
    }
#endif
    case SUBFCX: {
      Format(instr, "subfc'. 'rt, 'ra, 'rb");
      return;
    }
    case SUBFEX: {
      Format(instr, "subfe'. 'rt, 'ra, 'rb");
      return;
    }
    case ADDCX: {
      Format(instr, "addc'.   'rt, 'ra, 'rb");
      return;
    }
    case ADDEX: {
      Format(instr, "adde'.   'rt, 'ra, 'rb");
      return;
    }
    case CNTLZWX: {
      Format(instr, "cntlzw'. 'ra, 'rs");
      return;
    }
    case CNTLZDX: {
      Format(instr, "cntlzd'. 'ra, 'rs");
      return;
    }
    case CNTTZWX: {
      Format(instr, "cnttzw'. 'ra, 'rs");
      return;
    }
    case CNTTZDX: {
      Format(instr, "cnttzd'. 'ra, 'rs");
      return;
    }
    case BRH: {
      Format(instr, "brh     'ra, 'rs");
      return;
    }
    case BRW: {
      Format(instr, "brw     'ra, 'rs");
      return;
    }
    case BRD: {
      Format(instr, "brd     'ra, 'rs");
      return;
    }
    case ANDX: {
      Format(instr, "and'.    'ra, 'rs, 'rb");
      return;
    }
    case ANDCX: {
      Format(instr, "andc'.   'ra, 'rs, 'rb");
      return;
    }
    case CMPL: {
#if V8_TARGET_ARCH_PPC64
      if (instr->Bit(21)) {
#endif
        Format(instr, "cmpl    'ra, 'rb");
#if V8_TARGET_ARCH_PPC64
      } else {
        Format(instr, "cmplw   'ra, 'rb");
      }
#endif
      return;
    }
    case NEGX: {
      Format(instr, "neg'.    'rt, 'ra");
      return;
    }
    case NORX: {
      Format(instr, "nor'.    'rt, 'ra, 'rb");
      return;
    }
    case SUBFX: {
      Format(instr, "subf'.   'rt, 'ra, 'rb");
      return;
    }
    case MULHWX: {
      Format(instr, "mulhw'o'.  'rt, 'ra, 'rb");
      return;
    }
    case ADDZEX: {
      Format(instr, "addze'.   'rt, 'ra");
      return;
    }
    case MULLW: {
      Format(instr, "mullw'o'.  'rt, 'ra, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case MULLD: {
      Format(instr, "mulld'o'.  'rt, 'ra, 'rb");
      return;
    }
#endif
    case DIVW: {
      Format(instr, "divw'o'.   'rt, 'ra, 'rb");
      return;
    }
    case DIVWU: {
      Format(instr, "divwu'o'.  'rt, 'ra, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case DIVD: {
      Format(instr, "divd'o'.   'rt, 'ra, 'rb");
      return;
    }
#endif
    case ADDX: {
      Format(instr, "add'o     'rt, 'ra, 'rb");
      return;
    }
    case XORX: {
      Format(instr, "xor'.    'ra, 'rs, 'rb");
      return;
    }
    case ORX: {
      if (instr->RTValue() == instr->RBValue()) {
        Format(instr, "mr      'ra, 'rb");
      } else {
        Format(instr, "or      'ra, 'rs, 'rb");
      }
      return;
    }
    case MFSPR: {
      int spr = instr->Bits(20, 11);
      if (256 == spr) {
        Format(instr, "mflr    'rt");
      } else {
        Format(instr, "mfspr   'rt ??");
      }
      return;
    }
    case MTSPR: {
      int spr = instr->Bits(20, 11);
      if (256 == spr) {
        Format(instr, "mtlr    'rt");
      } else if (288 == spr) {
        Format(instr, "mtctr   'rt");
      } else {
        Format(instr, "mtspr   'rt ??");
      }
      return;
    }
    case MFCR: {
      Format(instr, "mfcr    'rt");
      return;
    }
    case STWX: {
      Format(instr, "stwx    'rs, 'ra, 'rb");
      return;
    }
    case STWUX: {
      Format(instr, "stwux   'rs, 'ra, 'rb");
      return;
    }
    case STBX: {
      Format(instr, "stbx    'rs, 'ra, 'rb");
      return;
    }
    case STBUX: {
      Format(instr, "stbux   'rs, 'ra, 'rb");
      return;
    }
    case STHX: {
      Format(instr, "sthx    'rs, 'ra, 'rb");
      return;
    }
    case STHUX: {
      Format(instr, "sthux   'rs, 'ra, 'rb");
      return;
    }
    case LWZX: {
      Format(instr, "lwzx    'rt, 'ra, 'rb");
      return;
    }
    case LWZUX: {
      Format(instr, "lwzux   'rt, 'ra, 'rb");
      return;
    }
    case LWAX: {
      Format(instr, "lwax    'rt, 'ra, 'rb");
      return;
    }
    case LBZX: {
      Format(instr, "lbzx    'rt, 'ra, 'rb");
      return;
    }
    case LBZUX: {
      Format(instr, "lbzux   'rt, 'ra, 'rb");
      return;
    }
    case LHZX: {
      Format(instr, "lhzx    'rt, 'ra, 'rb");
      return;
    }
    case LHZUX: {
      Format(instr, "lhzux   'rt, 'ra, 'rb");
      return;
    }
    case LHAX: {
      Format(instr, "lhax    'rt, 'ra, 'rb");
      return;
    }
    case LBARX: {
      Format(instr, "lbarx   'rt, 'ra, 'rb");
      return;
    }
    case LHARX: {
      Format(instr, "lharx   'rt, 'ra, 'rb");
      return;
    }
    case LWARX: {
      Format(instr, "lwarx   'rt, 'ra, 'rb");
      return;
    }
#if V8_TARGET_ARCH_PPC64
    case LDX: {
      Format(instr, "ldx     'rt, 'ra, 'rb");
      return;
    }
    case LDUX: {
      Format(instr, "ldux    'rt, 'ra, 'rb");
      return;
    }
    case LDARX: {
      Format(instr, "ldarx   'rt, 'ra, 'rb");
      return;
    }
    case STDX: {
      Format(instr, "stdx    'rt, 'ra, 'rb");
      return;
    }
    case STDUX: {
      Format(instr, "stdux   'rt, 'ra, 'rb");
      return;
    }
    case MFVSRD: {
      Format(instr, "mfvsrd  'ra, 'Xs");
      return;
    }
    case MFVSRWZ: {
      Format(instr, "mffprwz 'ra, 'Dt");
      return;
    }
    case MTVSRD: {
      Format(instr, "mtvsrd  'Xt, 'ra");
      return;
    }
    case MTVSRWA: {
      Format(instr, "mtfprwa 'Dt, 'ra");
      return;
    }
    case MTVSRWZ: {
      Format(instr, "mtfprwz 'Dt, 'ra");
      return;
    }
    case MTVSRDD: {
      Format(instr, "mtvsrdd 'Xt, 'ra, 'rb");
      return;
    }
    case LDBRX: {
      Format(instr, "ldbrx   'rt, 'ra, 'rb");
      return;
    }
    case LHBRX: {
      Format(instr, "lhbrx   'rt, 'ra, 'rb");
      return;
    }
    case LWBRX: {
      Format(instr, "lwbrx   'rt, 'ra, 'rb");
      return;
    }
    case STDBRX: {
      Format(instr, "stdbrx  'rs, 'ra, 'rb");
      return;
    }
    case STWBRX: {
      Format(instr, "stwbrx  'rs, 'ra, 'rb");
      return;
    }
    case STHBRX: {
      Format(instr, "sthbrx  'rs, 'ra, 'rb");
      return;
    }
    case MTCRF: {
      Format(instr, "mtcrf   'FXM, 'rs");
      return;
    }
#endif
  }

  switch (EXT2 | (instr->BitField(5, 1))) {
    case ISEL: {
      Format(instr, "isel    'rt, 'ra, 'rb");
      return;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt3(Instruction* instr) {
  switch (EXT3 | (instr->BitField(10, 1))) {
    case FCFID: {
      Format(instr, "fcfid'.  'Dt, 'Db");
      break;
    }
    case FCFIDS: {
      Format(instr, "fcfids'. 'Dt, 'Db");
      break;
    }
    case FCFIDU: {
      Format(instr, "fcfidu'. 'Dt, 'Db");
      break;
    }
    case FCFIDUS: {
      Format(instr, "fcfidus'.'Dt, 'Db");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt4(Instruction* instr) {
  switch (EXT4 | (instr->BitField(5, 1))) {
    case FDIV: {
      Format(instr, "fdiv'.   'Dt, 'Da, 'Db");
      return;
    }
    case FSUB: {
      Format(instr, "fsub'.   'Dt, 'Da, 'Db");
      return;
    }
    case FADD: {
      Format(instr, "fadd'.   'Dt, 'Da, 'Db");
      return;
    }
    case FSQRT: {
      Format(instr, "fsqrt'.  'Dt, 'Db");
      return;
    }
    case FSEL: {
      Format(instr, "fsel'.   'Dt, 'Da, 'Dc, 'Db");
      return;
    }
    case FMUL: {
      Format(instr, "fmul'.   'Dt, 'Da, 'Dc");
      return;
    }
    case FMSUB: {
      Format(instr, "fmsub'.  'Dt, 'Da, 'Dc, 'Db");
      return;
    }
    case FMADD: {
      Format(instr, "fmadd'.  'Dt, 'Da, 'Dc, 'Db");
      return;
    }
  }

  switch (EXT4 | (instr->BitField(10, 1))) {
    case FCMPU: {
      Format(instr, "fcmpu   'Da, 'Db");
      break;
    }
    case FRSP: {
      Format(instr, "frsp'.   'Dt, 'Db");
      break;
    }
    case FCFID: {
      Format(instr, "fcfid'.  'Dt, 'Db");
      break;
    }
    case FCFIDU: {
      Format(instr, "fcfidu'. 'Dt, 'Db");
      break;
    }
    case FCTID: {
      Format(instr, "fctid   'Dt, 'Db");
      break;
    }
    case FCTIDZ: {
      Format(instr, "fctidz  'Dt, 'Db");
      break;
    }
    case FCTIDU: {
      Format(instr, "fctidu  'Dt, 'Db");
      break;
    }
    case FCTIDUZ: {
      Format(instr, "fctiduz 'Dt, 'Db");
      break;
    }
    case FCTIW: {
      Format(instr, "fctiw'. 'Dt, 'Db");
      break;
    }
    case FCTIWZ: {
      Format(instr, "fctiwz'. 'Dt, 'Db");
      break;
    }
    case FCTIWUZ: {
      Format(instr, "fctiwuz 'Dt, 'Db");
      break;
    }
    case FMR: {
      Format(instr, "fmr'.    'Dt, 'Db");
      break;
    }
    case MTFSFI: {
      Format(instr, "mtfsfi'.  ?,?");
      break;
    }
    case MFFS: {
      Format(instr, "mffs'.   'Dt");
      break;
    }
    case MTFSF: {
      Format(instr, "mtfsf'.  'Db ?,?,?");
      break;
    }
    case FABS: {
      Format(instr, "fabs'.   'Dt, 'Db");
      break;
    }
    case FRIN: {
      Format(instr, "frin.   'Dt, 'Db");
      break;
    }
    case FRIZ: {
      Format(instr, "friz.   'Dt, 'Db");
      break;
    }
    case FRIP: {
      Format(instr, "frip.   'Dt, 'Db");
      break;
    }
    case FRIM: {
      Format(instr, "frim.   'Dt, 'Db");
      break;
    }
    case FNEG: {
      Format(instr, "fneg'.   'Dt, 'Db");
      break;
    }
    case FCPSGN: {
      Format(instr, "fcpsgn'.   'Dt, 'Da, 'Db");
      break;
    }
    case MCRFS: {
      Format(instr, "mcrfs   ?,?");
      break;
    }
    case MTFSB0: {
      Format(instr, "mtfsb0'. ?");
      break;
    }
    case MTFSB1: {
      Format(instr, "mtfsb1'. ?");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt5(Instruction* instr) {
  switch (EXT5 | (instr->BitField(4, 2))) {
    case RLDICL: {
      Format(instr, "rldicl'. 'ra, 'rs, 'sh, 'mb");
      return;
    }
    case RLDICR: {
      Format(instr, "rldicr'. 'ra, 'rs, 'sh, 'me");
      return;
    }
    case RLDIC: {
      Format(instr, "rldic'.  'ra, 'rs, 'sh, 'mb");
      return;
    }
    case RLDIMI: {
      Format(instr, "rldimi'. 'ra, 'rs, 'sh, 'mb");
      return;
    }
  }
  switch (EXT5 | (instr->BitField(4, 1))) {
    case RLDCL: {
      Format(instr, "rldcl'.  'ra, 'rs, 'sb, 'mb");
      return;
    }
  }
  Unknown(instr);  // not used by V8
}

void Decoder::DecodeExt6(Instruction* instr) {
  switch (EXT6 | (instr->BitField(10, 1))) {
    case XXSPLTIB: {
      Format(instr, "xxspltib  'Xt, 'IMM8");
      return;
    }
  }
  switch (EXT6 | (instr->BitField(10, 3))) {
#define DECODE_XX3_VECTOR_B_FORM_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                          \
    Format(instr, #name " 'Xt, 'Xa, 'Xb");                                     \
    return;                                                                    \
  }
    PPC_XX3_OPCODE_VECTOR_B_FORM_LIST(DECODE_XX3_VECTOR_B_FORM_INSTRUCTIONS)
#undef DECODE_XX3_VECTOR_B_FORM_INSTRUCTIONS
#define DECODE_XX3_SCALAR_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                   \
    Format(instr, #name " 'Dt, 'Da, 'Db");                              \
    return;                                                             \
  }
    PPC_XX3_OPCODE_SCALAR_LIST(DECODE_XX3_SCALAR_INSTRUCTIONS)
#undef DECODE_XX3_SCALAR_INSTRUCTIONS
  }
  // Some encodings have integers hard coded in the middle, handle those first.
  switch (EXT6 | (instr->BitField(20, 16)) | (instr->BitField(10, 2))) {
#define DECODE_XX2_B_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                              \
    Format(instr, #name " 'Xt, 'Xb");                              \
    return;                                                        \
  }
    PPC_XX2_OPCODE_B_FORM_LIST(DECODE_XX2_B_INSTRUCTIONS)
#undef DECODE_XX2_B_INSTRUCTIONS
  }
  switch (EXT6 | (instr->BitField(10, 2))) {
#define DECODE_XX2_VECTOR_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                     \
    Format(instr, #name " 'Xt, 'Xb");                                     \
    return;                                                               \
  }
    PPC_XX2_OPCODE_VECTOR_A_FORM_LIST(DECODE_XX2_VECTOR_A_INSTRUCTIONS)
#undef DECODE_XX2_VECTOR_A_INSTRUCTIONS
#define DECODE_XX2_SCALAR_A_INSTRUCTIONS(name, opcode_name, opcode_value) \
  case opcode_name: {                                                     \
    Format(instr, #name " 'Dt, 'Db");                                     \
    return;                                                               \
  }
    PPC_XX2_OPCODE_SCALAR_A_FORM_LIST(DECODE_XX2_SCALAR_A_INSTRUCTIONS)
#undef DECODE_XX2_SCALAR_A_INSTRUCTIONS
  }
  Unknown(instr);  // not used by V8
}

#undef VERIFY

// Disassemble the instruction at *instr_ptr into the output buffer.
int Decoder::InstructionDecode(uint8_t* instr_ptr) {
  Instruction* instr = Instruction::At(instr_ptr);

  uint32_t opcode = instr->OpcodeValue() << 26;
  // Print raw instruction bytes.
  if (opcode != EXTP) {
    out_buffer_pos_ += base::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%08x       ", instr->InstructionBits());
  } else {
    // Prefixed instructions have a 4-byte prefix and a 4-byte suffix. Print
    // both on the same line.
    Instruction* next_instr = reinterpret_cast<Instruction*>(
        reinterpret_cast<intptr_t>(instr) + kInstrSize);
    out_buffer_pos_ +=
        base::SNPrintF(out_buffer_ + out_buffer_pos_, "%08x|%08x ",
                       instr->InstructionBits(), next_instr->InstructionBits());
  }

  if (ABI_USES_FUNCTION_DESCRIPTORS && instr->InstructionBits() == 0) {
    // The first field will be identified as a jump table entry.  We
    // emit the rest of the structure as zero, so just skip past them.
    Format(instr, "constant");
    return kInstrSize;
  }

  switch (opcode) {
    case TWI: {
      PrintSoftwareInterrupt(instr->SvcValue());
      break;
    }
    case MULLI: {
      UnknownFormat(instr, "mulli");
      break;
    }
    case SUBFIC: {
      Format(instr, "subfic  'rt, 'ra, 'int16");
      break;
    }
    case CMPLI: {
#if V8_TARGET_ARCH_PPC64
      if (instr->Bit(21)) {
#endif
        Format(instr, "cmpli   'ra, 'uint16");
#if V8_TARGET_ARCH_PPC64
      } else {
        Format(instr, "cmplwi  'ra, 'uint16");
      }
#endif
      break;
    }
    case CMPI: {
#if V8_TARGET_ARCH_PPC64
      if (instr->Bit(21)) {
#endif
        Format(instr, "cmpi    'ra, 'int16");
#if V8_TARGET_ARCH_PPC64
      } else {
        Format(instr, "cmpwi   'ra, 'int16");
      }
#endif
      break;
    }
    case ADDIC: {
      Format(instr, "addic   'rt, 'ra, 'int16");
      break;
    }
    case ADDICx: {
      UnknownFormat(instr, "addicx");
      break;
    }
    case ADDI: {
      if (instr->RAValue() == 0) {
        // this is load immediate
        Format(instr, "li      'rt, 'int16");
      } else {
        Format(instr, "addi    'rt, 'ra, 'int16");
      }
      break;
    }
    case ADDIS: {
      if (instr->RAValue() == 0) {
        Format(instr, "lis     'rt, 'int16");
      } else {
        Format(instr, "addis   'rt, 'ra, 'int16");
      }
      break;
    }
    case BCX: {
      int bo = instr->Bits(25, 21) << 21;
      int bi = instr->Bits(20, 16);
      CRBit cond = static_cast<CRBit>(bi & (CRWIDTH - 1));
      switch (bo) {
        case BT: {  // Branch if condition true
          switch (cond) {
            case CR_EQ:
              Format(instr, "beq'l'a'cr 'target16");
              break;
            case CR_GT:
              Format(instr, "bgt'l'a'cr 'target16");
              break;
            case CR_LT:
              Format(instr, "blt'l'a'cr 'target16");
              break;
            case CR_SO:
              Format(instr, "bso'l'a'cr 'target16");
              break;
          }
          break;
        }
        case BF: {  // Branch if condition false
          switch (cond) {
            case CR_EQ:
              Format(instr, "bne'l'a'cr 'target16");
              break;
            case CR_GT:
              Format(instr, "ble'l'a'cr 'target16");
              break;
            case CR_LT:
              Format(instr, "bge'l'a'cr 'target16");
              break;
            case CR_SO:
              Format(instr, "bnso'l'a'cr 'target16");
              break;
          }
          break;
        }
        case DCBNZ: {  // Decrement CTR; branch if CTR != 0
          Format(instr, "bdnz'l'a 'target16");
          break;
        }
        default:
          Format(instr, "bc'l'a'cr 'target16");
          break;
      }
      break;
    }
    case SC: {
      UnknownFormat(instr, "sc");
      break;
    }
    case BX: {
      Format(instr, "b'l'a 'target26");
      break;
    }
    case EXTP: {
      DecodeExtP(instr);
      break;
    }
    case EXT0: {
      DecodeExt0(instr);
      break;
    }
    case EXT1: {
      DecodeExt1(instr);
      break;
    }
    case RLWIMIX: {
      Format(instr, "rlwimi'. 'ra, 'rs, 'sh, 'me, 'mb");
      break;
    }
    case RLWINMX: {
      Format(instr, "rlwinm'. 'ra, 'rs, 'sh, 'me, 'mb");
      break;
    }
    case RLWNMX: {
      Format(instr, "rlwnm'.  'ra, 'rs, 'rb, 'me, 'mb");
      break;
    }
    case ORI: {
      Format(instr, "ori     'ra, 'rs, 'uint16");
      break;
    }
    case ORIS: {
      Format(instr, "oris    'ra, 'rs, 'uint16");
      break;
    }
    case XORI: {
      Format(instr, "xori    'ra, 'rs, 'uint16");
      break;
    }
    case XORIS: {
      Format(instr, "xoris   'ra, 'rs, 'uint16");
      break;
    }
    case ANDIx: {
      Format(instr, "andi.   'ra, 'rs, 'uint16");
      break;
    }
    case ANDISx: {
      Format(instr, "andis.  'ra, 'rs, 'uint16");
      break;
    }
    case EXT2: {
      DecodeExt2(instr);
      break;
    }
    case LWZ: {
      Format(instr, "lwz     'rt, 'int16('ra)");
      break;
    }
    case LWZU: {
      Format(instr, "lwzu    'rt, 'int16('ra)");
      break;
    }
    case LBZ: {
      Format(instr, "lbz     'rt, 'int16('ra)");
      break;
    }
    case LBZU: {
      Format(instr, "lbzu    'rt, 'int16('ra)");
      break;
    }
    case STW: {
      Format(instr, "stw     'rs, 'int16('ra)");
      break;
    }
    case STWU: {
      Format(instr, "stwu    'rs, 'int16('ra)");
      break;
    }
    case STB: {
      Format(instr, "stb     'rs, 'int16('ra)");
      break;
    }
    case STBU: {
      Format(instr, "stbu    'rs, 'int16('ra)");
      break;
    }
    case LHZ: {
      Format(instr, "lhz     'rt, 'int16('ra)");
      break;
    }
    case LHZU: {
      Format(instr, "lhzu    'rt, 'int16('ra)");
      break;
    }
    case LHA: {
      Format(instr, "lha     'rt, 'int16('ra)");
      break;
    }
    case LHAU: {
      Format(instr, "lhau    'rt, 'int16('ra)");
      break;
    }
    case STH: {
      Format(instr, "sth 'rs, 'int16('ra)");
      break;
    }
    case STHU: {
      Format(instr, "sthu 'rs, 'int16('ra)");
      break;
    }
    case LMW: {
      UnknownFormat(instr, "lmw");
      break;
    }
    case STMW: {
      UnknownFormat(instr, "stmw");
      break;
    }
    case LFS: {
      Format(instr, "lfs     'Dt, 'int16('ra)");
      break;
    }
    case LFSU: {
      Format(instr, "lfsu    'Dt, 'int16('ra)");
      break;
    }
    case LFD: {
      Format(instr, "lfd     'Dt, 'int16('ra)");
      break;
    }
    case LFDU: {
      Format(instr, "lfdu    'Dt, 'int16('ra)");
      break;
    }
    case STFS: {
      Format(instr, "stfs    'Dt, 'int16('ra)");
      break;
    }
    case STFSU: {
      Format(instr, "stfsu   'Dt, 'int16('ra)");
      break;
    }
    case STFD: {
      Format(instr, "stfd    'Dt, 'int16('ra)");
      break;
    }
    case STFDU: {
      Format(instr, "stfdu   'Dt, 'int16('ra)");
      break;
    }
    case EXT3: {
      DecodeExt3(instr);
      break;
    }
    case EXT4: {
      DecodeExt4(instr);
      break;
    }
    case EXT5: {
      DecodeExt5(instr);
      break;
    }
    case EXT6: {
      DecodeExt6(instr);
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case LD: {
      switch (instr->Bits(1, 0)) {
        case 0:
          Format(instr, "ld      'rt, 'd('ra)");
          break;
        case 1:
          Format(instr, "ldu     'rt, 'd('ra)");
          break;
        case 2:
          Format(instr, "lwa     'rt, 'd('ra)");
          break;
      }
      break;
    }
    case STD: {  // could be STD or STDU
      if (instr->Bit(0) == 0) {
        Format(instr, "std     'rs, 'd('ra)");
      } else {
        Format(instr, "stdu    'rs, 'd('ra)");
      }
      break;
    }
#endif
    default: {
      Unknown(instr);
      break;
    }
  }

  if (IsPrefixed()) {
    // The next instruction (suffix) should have already been decoded as part of
    // prefix decoding.
    ResetPrefix();
    return 2 * kInstrSize;
  }

  return kInstrSize;
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
  UNREACHABLE();  // PPC does not have the concept of a byte register
}

const char* NameConverter::NameOfXMMRegister(int reg) const {
  UNREACHABLE();  // PPC does not have any XMM registers
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

// The PPC assembler does not currently use constant pools.
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

#undef STRING_STARTS_WITH

}  // namespace disasm

#endif  // V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
