// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cassert>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#if V8_TARGET_ARCH_X64

#include "src/base/compiler-specific.h"
#include "src/base/lazy-instance.h"
#include "src/base/memory.h"
#include "src/base/strings.h"
#include "src/codegen/x64/fma-instr.h"
#include "src/codegen/x64/register-x64.h"
#include "src/codegen/x64/sse-instr.h"
#include "src/common/globals.h"
#include "src/diagnostics/disasm.h"

namespace disasm {

enum OperandType {
  UNSET_OP_ORDER = 0,
  // Operand size decides between 16, 32 and 64 bit operands.
  REG_OPER_OP_ORDER = 1,  // Register destination, operand source.
  OPER_REG_OP_ORDER = 2,  // Operand destination, register source.
  // Fixed 8-bit operands.
  BYTE_SIZE_OPERAND_FLAG = 4,
  BYTE_REG_OPER_OP_ORDER = REG_OPER_OP_ORDER | BYTE_SIZE_OPERAND_FLAG,
  BYTE_OPER_REG_OP_ORDER = OPER_REG_OP_ORDER | BYTE_SIZE_OPERAND_FLAG,
  // XMM registers/operands can be mixed with normal operands.
  OPER_XMMREG_OP_ORDER,
  XMMREG_OPER_OP_ORDER,
  XMMREG_XMMOPER_OP_ORDER,
  XMMOPER_XMMREG_OP_ORDER,
};

//------------------------------------------------------------------
// Tables
//------------------------------------------------------------------
struct ByteMnemonic {
  int b;  // -1 terminates, otherwise must be in range (0..255)
  OperandType op_order_;
  const char* mnem;
};

static const ByteMnemonic two_operands_instr[] = {
    {0x00, BYTE_OPER_REG_OP_ORDER, "add"},
    {0x01, OPER_REG_OP_ORDER, "add"},
    {0x02, BYTE_REG_OPER_OP_ORDER, "add"},
    {0x03, REG_OPER_OP_ORDER, "add"},
    {0x08, BYTE_OPER_REG_OP_ORDER, "or"},
    {0x09, OPER_REG_OP_ORDER, "or"},
    {0x0A, BYTE_REG_OPER_OP_ORDER, "or"},
    {0x0B, REG_OPER_OP_ORDER, "or"},
    {0x10, BYTE_OPER_REG_OP_ORDER, "adc"},
    {0x11, OPER_REG_OP_ORDER, "adc"},
    {0x12, BYTE_REG_OPER_OP_ORDER, "adc"},
    {0x13, REG_OPER_OP_ORDER, "adc"},
    {0x18, BYTE_OPER_REG_OP_ORDER, "sbb"},
    {0x19, OPER_REG_OP_ORDER, "sbb"},
    {0x1A, BYTE_REG_OPER_OP_ORDER, "sbb"},
    {0x1B, REG_OPER_OP_ORDER, "sbb"},
    {0x20, BYTE_OPER_REG_OP_ORDER, "and"},
    {0x21, OPER_REG_OP_ORDER, "and"},
    {0x22, BYTE_REG_OPER_OP_ORDER, "and"},
    {0x23, REG_OPER_OP_ORDER, "and"},
    {0x28, BYTE_OPER_REG_OP_ORDER, "sub"},
    {0x29, OPER_REG_OP_ORDER, "sub"},
    {0x2A, BYTE_REG_OPER_OP_ORDER, "sub"},
    {0x2B, REG_OPER_OP_ORDER, "sub"},
    {0x30, BYTE_OPER_REG_OP_ORDER, "xor"},
    {0x31, OPER_REG_OP_ORDER, "xor"},
    {0x32, BYTE_REG_OPER_OP_ORDER, "xor"},
    {0x33, REG_OPER_OP_ORDER, "xor"},
    {0x38, BYTE_OPER_REG_OP_ORDER, "cmp"},
    {0x39, OPER_REG_OP_ORDER, "cmp"},
    {0x3A, BYTE_REG_OPER_OP_ORDER, "cmp"},
    {0x3B, REG_OPER_OP_ORDER, "cmp"},
    {0x63, REG_OPER_OP_ORDER, "movsxl"},
    {0x84, BYTE_REG_OPER_OP_ORDER, "test"},
    {0x85, REG_OPER_OP_ORDER, "test"},
    {0x86, BYTE_REG_OPER_OP_ORDER, "xchg"},
    {0x87, REG_OPER_OP_ORDER, "xchg"},
    {0x88, BYTE_OPER_REG_OP_ORDER, "mov"},
    {0x89, OPER_REG_OP_ORDER, "mov"},
    {0x8A, BYTE_REG_OPER_OP_ORDER, "mov"},
    {0x8B, REG_OPER_OP_ORDER, "mov"},
    {0x8D, REG_OPER_OP_ORDER, "lea"},
    {-1, UNSET_OP_ORDER, ""}};

static const ByteMnemonic zero_operands_instr[] = {
    {0xC3, UNSET_OP_ORDER, "ret"},   {0xC9, UNSET_OP_ORDER, "leave"},
    {0xF4, UNSET_OP_ORDER, "hlt"},   {0xFC, UNSET_OP_ORDER, "cld"},
    {0xCC, UNSET_OP_ORDER, "int3"},  {0x60, UNSET_OP_ORDER, "pushad"},
    {0x61, UNSET_OP_ORDER, "popad"}, {0x9C, UNSET_OP_ORDER, "pushfd"},
    {0x9D, UNSET_OP_ORDER, "popfd"}, {0x9E, UNSET_OP_ORDER, "sahf"},
    {0x99, UNSET_OP_ORDER, "cdq"},   {0x9B, UNSET_OP_ORDER, "fwait"},
    {0xAB, UNSET_OP_ORDER, "stos"},  {0xA4, UNSET_OP_ORDER, "movs"},
    {0xA5, UNSET_OP_ORDER, "movs"},  {0xA6, UNSET_OP_ORDER, "cmps"},
    {0xA7, UNSET_OP_ORDER, "cmps"},  {-1, UNSET_OP_ORDER, ""}};

static const ByteMnemonic call_jump_instr[] = {{0xE8, UNSET_OP_ORDER, "call"},
                                               {0xE9, UNSET_OP_ORDER, "jmp"},
                                               {-1, UNSET_OP_ORDER, ""}};

static const ByteMnemonic short_immediate_instr[] = {
    {0x05, UNSET_OP_ORDER, "add"}, {0x0D, UNSET_OP_ORDER, "or"},
    {0x15, UNSET_OP_ORDER, "adc"}, {0x1D, UNSET_OP_ORDER, "sbb"},
    {0x25, UNSET_OP_ORDER, "and"}, {0x2D, UNSET_OP_ORDER, "sub"},
    {0x35, UNSET_OP_ORDER, "xor"}, {0x3D, UNSET_OP_ORDER, "cmp"},
    {-1, UNSET_OP_ORDER, ""}};

static const char* const conditional_code_suffix[] = {
    "o", "no", "c",  "nc", "z", "nz", "na", "a",
    "s", "ns", "pe", "po", "l", "ge", "le", "g"};

enum InstructionType {
  NO_INSTR,
  ZERO_OPERANDS_INSTR,
  TWO_OPERANDS_INSTR,
  JUMP_CONDITIONAL_SHORT_INSTR,
  REGISTER_INSTR,
  PUSHPOP_INSTR,  // Has implicit 64-bit operand size.
  MOVE_REG_INSTR,
  CALL_JUMP_INSTR,
  SHORT_IMMEDIATE_INSTR
};

enum Prefixes {
  ESCAPE_PREFIX = 0x0F,
  SEGMENT_FS_OVERRIDE_PREFIX = 0x64,
  OPERAND_SIZE_OVERRIDE_PREFIX = 0x66,
  ADDRESS_SIZE_OVERRIDE_PREFIX = 0x67,
  VEX3_PREFIX = 0xC4,
  VEX2_PREFIX = 0xC5,
  LOCK_PREFIX = 0xF0,
  REPNE_PREFIX = 0xF2,
  REP_PREFIX = 0xF3,
  REPEQ_PREFIX = REP_PREFIX
};

struct InstructionDesc {
  const char* mnem;
  InstructionType type;
  OperandType op_order_;
  bool byte_size_operation;  // Fixed 8-bit operation.
};

class InstructionTable {
 public:
  InstructionTable();
  const InstructionDesc& Get(uint8_t x) const { return instructions_[x]; }

 private:
  InstructionDesc instructions_[256];
  void Clear();
  void Init();
  void CopyTable(const ByteMnemonic bm[], InstructionType type);
  void SetTableRange(InstructionType type, uint8_t start, uint8_t end,
                     bool byte_size, const char* mnem);
  void AddJumpConditionalShort();
};

InstructionTable::InstructionTable() {
  Clear();
  Init();
}

void InstructionTable::Clear() {
  for (int i = 0; i < 256; i++) {
    instructions_[i].mnem = "(bad)";
    instructions_[i].type = NO_INSTR;
    instructions_[i].op_order_ = UNSET_OP_ORDER;
    instructions_[i].byte_size_operation = false;
  }
}

void InstructionTable::Init() {
  CopyTable(two_operands_instr, TWO_OPERANDS_INSTR);
  CopyTable(zero_operands_instr, ZERO_OPERANDS_INSTR);
  CopyTable(call_jump_instr, CALL_JUMP_INSTR);
  CopyTable(short_immediate_instr, SHORT_IMMEDIATE_INSTR);
  AddJumpConditionalShort();
  SetTableRange(PUSHPOP_INSTR, 0x50, 0x57, false, "push");
  SetTableRange(PUSHPOP_INSTR, 0x58, 0x5F, false, "pop");
  SetTableRange(MOVE_REG_INSTR, 0xB8, 0xBF, false, "mov");
}

void InstructionTable::CopyTable(const ByteMnemonic bm[],
                                 InstructionType type) {
  for (int i = 0; bm[i].b >= 0; i++) {
    InstructionDesc* id = &instructions_[bm[i].b];
    id->mnem = bm[i].mnem;
    OperandType op_order = bm[i].op_order_;
    id->op_order_ =
        static_cast<OperandType>(op_order & ~BYTE_SIZE_OPERAND_FLAG);
    DCHECK_EQ(NO_INSTR, id->type);  // Information not already entered
    id->type = type;
    id->byte_size_operation = ((op_order & BYTE_SIZE_OPERAND_FLAG) != 0);
  }
}

void InstructionTable::SetTableRange(InstructionType type, uint8_t start,
                                     uint8_t end, bool byte_size,
                                     const char* mnem) {
  for (uint8_t b = start; b <= end; b++) {
    InstructionDesc* id = &instructions_[b];
    DCHECK_EQ(NO_INSTR, id->type);  // Information not already entered
    id->mnem = mnem;
    id->type = type;
    id->byte_size_operation = byte_size;
  }
}

void InstructionTable::AddJumpConditionalShort() {
  for (uint8_t b = 0x70; b <= 0x7F; b++) {
    InstructionDesc* id = &instructions_[b];
    DCHECK_EQ(NO_INSTR, id->type);  // Information not already entered
    id->mnem = nullptr;             // Computed depending on condition code.
    id->type = JUMP_CONDITIONAL_SHORT_INSTR;
  }
}

namespace {
DEFINE_LAZY_LEAKY_OBJECT_GETTER(InstructionTable, GetInstructionTable)
}  // namespace

static const InstructionDesc cmov_instructions[16] = {
    {"cmovo", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovno", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovc", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovnc", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovz", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovnz", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovna", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmova", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovs", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovns", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovpe", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovpo", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovl", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovge", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovle", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false},
    {"cmovg", TWO_OPERANDS_INSTR, REG_OPER_OP_ORDER, false}};

static const char* const cmp_pseudo_op[16] = {
    "eq",    "lt",  "le",  "unord", "neq",    "nlt", "nle", "ord",
    "eq_uq", "nge", "ngt", "false", "neq_oq", "ge",  "gt",  "true"};

namespace {
int8_t Imm8(const uint8_t* data) {
  return *reinterpret_cast<const int8_t*>(data);
}
uint8_t Imm8_U(const uint8_t* data) {
  return *reinterpret_cast<const uint8_t*>(data);
}
int16_t Imm16(const uint8_t* data) {
  return v8::base::ReadUnalignedValue<int16_t>(
      reinterpret_cast<v8::internal::Address>(data));
}
uint16_t Imm16_U(const uint8_t* data) {
  return v8::base::ReadUnalignedValue<uint16_t>(
      reinterpret_cast<v8::internal::Address>(data));
}
int32_t Imm32(const uint8_t* data) {
  return v8::base::ReadUnalignedValue<int32_t>(
      reinterpret_cast<v8::internal::Address>(data));
}
uint32_t Imm32_U(const uint8_t* data) {
  return v8::base::ReadUnalignedValue<uint32_t>(
      reinterpret_cast<v8::internal::Address>(data));
}
int64_t Imm64(const uint8_t* data) {
  return v8::base::ReadUnalignedValue<int64_t>(
      reinterpret_cast<v8::internal::Address>(data));
}
}  // namespace

//------------------------------------------------------------------------------
// DisassemblerX64 implementation.

// Forward-declare NameOfYMMRegister to keep its implementation with the
// NameConverter methods and register name arrays at bottom.
const char* NameOfYMMRegister(int reg);

// A new DisassemblerX64 object is created to disassemble each instruction.
// The object can only disassemble a single instruction.
class DisassemblerX64 {
 public:
  DisassemblerX64(const NameConverter& converter,
                  Disassembler::UnimplementedOpcodeAction unimplemented_action)
      : converter_(converter),
        tmp_buffer_pos_(0),
        abort_on_unimplemented_(unimplemented_action ==
                                Disassembler::kAbortOnUnimplementedOpcode),
        rex_(0),
        operand_size_(0),
        group_1_prefix_(0),
        segment_prefix_(0),
        address_size_prefix_(0),
        vex_byte0_(0),
        vex_byte1_(0),
        vex_byte2_(0),
        byte_size_operand_(false),
        instruction_table_(GetInstructionTable()) {
    tmp_buffer_[0] = '\0';
  }

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(v8::base::Vector<char> buffer, uint8_t* instruction);

 private:
  enum OperandSize {
    OPERAND_BYTE_SIZE = 0,
    OPERAND_WORD_SIZE = 1,
    OPERAND_DOUBLEWORD_SIZE = 2,
    OPERAND_QUADWORD_SIZE = 3
  };

  const NameConverter& converter_;
  v8::base::EmbeddedVector<char, 128> tmp_buffer_;
  unsigned int tmp_buffer_pos_;
  bool abort_on_unimplemented_;
  // Prefixes parsed.
  uint8_t rex_;
  uint8_t operand_size_;         // 0x66 or (without group 3 prefix) 0x0.
  uint8_t group_1_prefix_;       // 0xF2, 0xF3, or (without group 1 prefix) 0.
  uint8_t segment_prefix_;       // 0x64 or (without group 2 prefix) 0.
  uint8_t address_size_prefix_;  // 0x67 or (without group 4 prefix) 0.
  uint8_t vex_byte0_;            // 0xC4 or 0xC5.
  uint8_t vex_byte1_;
  uint8_t vex_byte2_;  // only for 3 bytes vex prefix.
  // Byte size operand override.
  bool byte_size_operand_;
  const InstructionTable* const instruction_table_;

  void setRex(uint8_t rex) {
    DCHECK_EQ(0x40, rex & 0xF0);
    rex_ = rex;
  }

  bool rex() { return rex_ != 0; }

  bool rex_b() { return (rex_ & 0x01) != 0; }

  // Actual number of base register given the low bits and the rex.b state.
  int base_reg(int low_bits) { return low_bits | ((rex_ & 0x01) << 3); }

  bool rex_x() { return (rex_ & 0x02) != 0; }

  bool rex_r() { return (rex_ & 0x04) != 0; }

  bool rex_w() { return (rex_ & 0x08) != 0; }

  bool vex_w() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    return vex_byte0_ == VEX3_PREFIX ? (vex_byte2_ & 0x80) != 0 : false;
  }

  bool vex_128() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 4) == 0;
  }

  bool vex_256() const {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 4) != 0;
  }

  bool vex_none() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 3) == 0;
  }

  bool vex_66() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 3) == 1;
  }

  bool vex_f3() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 3) == 2;
  }

  bool vex_f2() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return (checked & 3) == 3;
  }

  bool vex_0f() {
    if (vex_byte0_ == VEX2_PREFIX) return true;
    return (vex_byte1_ & 3) == 1;
  }

  bool vex_0f38() {
    if (vex_byte0_ == VEX2_PREFIX) return false;
    return (vex_byte1_ & 3) == 2;
  }

  bool vex_0f3a() {
    if (vex_byte0_ == VEX2_PREFIX) return false;
    return (vex_byte1_ & 3) == 3;
  }

  int vex_vreg() {
    DCHECK(vex_byte0_ == VEX3_PREFIX || vex_byte0_ == VEX2_PREFIX);
    uint8_t checked = vex_byte0_ == VEX3_PREFIX ? vex_byte2_ : vex_byte1_;
    return ~(checked >> 3) & 0xF;
  }

  OperandSize operand_size() {
    if (byte_size_operand_) return OPERAND_BYTE_SIZE;
    if (rex_w()) return OPERAND_QUADWORD_SIZE;
    if (operand_size_ != 0) return OPERAND_WORD_SIZE;
    return OPERAND_DOUBLEWORD_SIZE;
  }

  char operand_size_code() { return "bwlq"[operand_size()]; }

  char float_size_code() { return "sd"[rex_w()]; }

  const char* NameOfCPURegister(int reg) const {
    return converter_.NameOfCPURegister(reg);
  }

  const char* NameOfByteCPURegister(int reg) const {
    return converter_.NameOfByteCPURegister(reg);
  }

  const char* NameOfXMMRegister(int reg) const {
    return converter_.NameOfXMMRegister(reg);
  }

  const char* NameOfAVXRegister(int reg) const {
    if (vex_256()) {
      return NameOfYMMRegister(reg);
    } else {
      return converter_.NameOfXMMRegister(reg);
    }
  }

  const char* NameOfAddress(uint8_t* addr) const {
    return converter_.NameOfAddress(addr);
  }

  // Disassembler helper functions.
  void get_modrm(uint8_t data, int* mod, int* regop, int* rm) {
    *mod = (data >> 6) & 3;
    *regop = ((data & 0x38) >> 3) | (rex_r() ? 8 : 0);
    *rm = (data & 7) | (rex_b() ? 8 : 0);
  }

  void get_sib(uint8_t data, int* scale, int* index, int* base) {
    *scale = (data >> 6) & 3;
    *index = ((data >> 3) & 7) | (rex_x() ? 8 : 0);
    *base = (data & 7) | (rex_b() ? 8 : 0);
  }

  using RegisterNameMapping = const char* (DisassemblerX64::*)(int reg) const;

  void TryAppendRootRelativeName(int offset);
  int PrintRightOperandHelper(uint8_t* modrmp, RegisterNameMapping);
  int PrintRightOperand(uint8_t* modrmp);
  int PrintRightByteOperand(uint8_t* modrmp);
  int PrintRightXMMOperand(uint8_t* modrmp);
  int PrintRightAVXOperand(uint8_t* modrmp);
  int PrintOperands(const char* mnem, OperandType op_order, uint8_t* data);
  int PrintImmediate(uint8_t* data, OperandSize size);
  int PrintImmediateOp(uint8_t* data);
  const char* TwoByteMnemonic(uint8_t opcode);
  int TwoByteOpcodeInstruction(uint8_t* data);
  int ThreeByteOpcodeInstruction(uint8_t* data);
  int F6F7Instruction(uint8_t* data);
  int ShiftInstruction(uint8_t* data);
  int JumpShort(uint8_t* data);
  int JumpConditional(uint8_t* data);
  int JumpConditionalShort(uint8_t* data);
  int SetCC(uint8_t* data);
  int FPUInstruction(uint8_t* data);
  int MemoryFPUInstruction(int escape_opcode, int regop, uint8_t* modrm_start);
  int RegisterFPUInstruction(int escape_opcode, uint8_t modrm_byte);
  int AVXInstruction(uint8_t* data);
  PRINTF_FORMAT(2, 3) void AppendToBuffer(const char* format, ...);

  void UnimplementedInstruction() {
    if (abort_on_unimplemented_) {
      FATAL("'Unimplemented Instruction'");
    } else {
      AppendToBuffer("'Unimplemented Instruction'");
    }
  }
};

void DisassemblerX64::AppendToBuffer(const char* format, ...) {
  v8::base::Vector<char> buf = tmp_buffer_ + tmp_buffer_pos_;
  va_list args;
  va_start(args, format);
  int result = v8::base::VSNPrintF(buf, format, args);
  va_end(args);
  tmp_buffer_pos_ += result;
}

void DisassemblerX64::TryAppendRootRelativeName(int offset) {
  const char* maybe_name = converter_.RootRelativeName(offset);
  if (maybe_name != nullptr) AppendToBuffer(" (%s)", maybe_name);
}

int DisassemblerX64::PrintRightOperandHelper(
    uint8_t* modrmp, RegisterNameMapping direct_register_name) {
  int mod, regop, rm;
  get_modrm(*modrmp, &mod, &regop, &rm);
  RegisterNameMapping register_name =
      (mod == 3) ? direct_register_name : &DisassemblerX64::NameOfCPURegister;
  switch (mod) {
    case 0:
      if ((rm & 7) == 5) {
        AppendToBuffer("[rip+0x%x]", Imm32(modrmp + 1));
        return 5;
      } else if ((rm & 7) == 4) {
        // Codes for SIB byte.
        uint8_t sib = *(modrmp + 1);
        int scale, index, base;
        get_sib(sib, &scale, &index, &base);
        if (index == 4 && (base & 7) == 4 && scale == 0 /*times_1*/) {
          // index == rsp means no index. Only use sib byte with no index for
          // rsp and r12 base.
          AppendToBuffer("[%s]", NameOfCPURegister(base));
          return 2;
        } else if (base == 5) {
          // base == rbp means no base register (when mod == 0).
          int32_t disp = Imm32(modrmp + 2);
          AppendToBuffer("[%s*%d%s0x%x]", NameOfCPURegister(index), 1 << scale,
                         disp < 0 ? "-" : "+", disp < 0 ? -disp : disp);
          return 6;
        } else if (index != 4 && base != 5) {
          // [base+index*scale]
          AppendToBuffer("[%s+%s*%d]", NameOfCPURegister(base),
                         NameOfCPURegister(index), 1 << scale);
          return 2;
        } else {
          UnimplementedInstruction();
          return 1;
        }
      } else {
        AppendToBuffer("[%s]", NameOfCPURegister(rm));
        return 1;
      }
    case 1:  // fall through
    case 2:
      if ((rm & 7) == 4) {
        uint8_t sib = *(modrmp + 1);
        int scale, index, base;
        get_sib(sib, &scale, &index, &base);
        int disp = (mod == 2) ? Imm32(modrmp + 2) : Imm8(modrmp + 2);
        if (index == 4 && (base & 7) == 4 && scale == 0 /*times_1*/) {
          AppendToBuffer("[%s%s0x%x]", NameOfCPURegister(base),
                         disp < 0 ? "-" : "+", disp < 0 ? -disp : disp);
        } else {
          AppendToBuffer("[%s+%s*%d%s0x%x]", NameOfCPURegister(base),
                         NameOfCPURegister(index), 1 << scale,
                         disp < 0 ? "-" : "+", disp < 0 ? -disp : disp);
        }
        return mod == 2 ? 6 : 3;
      } else {
        // No sib.
        int disp = (mod == 2) ? Imm32(modrmp + 1) : Imm8(modrmp + 1);
        AppendToBuffer("[%s%s0x%x]", NameOfCPURegister(rm),
                       disp < 0 ? "-" : "+", disp < 0 ? -disp : disp);
        if (rm == i::kRootRegister.code()) {
          // For root-relative accesses, try to append a description.
          TryAppendRootRelativeName(disp);
        }
        return (mod == 2) ? 5 : 2;
      }
    case 3:
      AppendToBuffer("%s", (this->*register_name)(rm));
      return 1;
    default:
      UnimplementedInstruction();
      return 1;
  }
  UNREACHABLE();
}

int DisassemblerX64::PrintImmediate(uint8_t* data, OperandSize size) {
  int64_t value;
  int count;
  switch (size) {
    case OPERAND_BYTE_SIZE:
      value = *data;
      count = 1;
      break;
    case OPERAND_WORD_SIZE:
      value = Imm16(data);
      count = 2;
      break;
    case OPERAND_DOUBLEWORD_SIZE:
      value = Imm32_U(data);
      count = 4;
      break;
    case OPERAND_QUADWORD_SIZE:
      value = Imm32(data);
      count = 4;
      break;
    default:
      UNREACHABLE();
  }
  AppendToBuffer("%" PRIx64, value);
  return count;
}

int DisassemblerX64::PrintRightOperand(uint8_t* modrmp) {
  return PrintRightOperandHelper(modrmp, &DisassemblerX64::NameOfCPURegister);
}

int DisassemblerX64::PrintRightByteOperand(uint8_t* modrmp) {
  return PrintRightOperandHelper(modrmp,
                                 &DisassemblerX64::NameOfByteCPURegister);
}

int DisassemblerX64::PrintRightXMMOperand(uint8_t* modrmp) {
  return PrintRightOperandHelper(modrmp, &DisassemblerX64::NameOfXMMRegister);
}

int DisassemblerX64::PrintRightAVXOperand(uint8_t* modrmp) {
  return PrintRightOperandHelper(modrmp, &DisassemblerX64::NameOfAVXRegister);
}

// Returns number of bytes used including the current *data.
// Writes instruction's mnemonic, left and right operands to 'tmp_buffer_'.
int DisassemblerX64::PrintOperands(const char* mnem, OperandType op_order,
                                   uint8_t* data) {
  uint8_t modrm = *data;
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  int advance = 0;
  const char* register_name = byte_size_operand_ ? NameOfByteCPURegister(regop)
                                                 : NameOfCPURegister(regop);
  switch (op_order) {
    case REG_OPER_OP_ORDER: {
      AppendToBuffer("%s%c %s,", mnem, operand_size_code(), register_name);
      advance = byte_size_operand_ ? PrintRightByteOperand(data)
                                   : PrintRightOperand(data);
      break;
    }
    case OPER_REG_OP_ORDER: {
      AppendToBuffer("%s%c ", mnem, operand_size_code());
      advance = byte_size_operand_ ? PrintRightByteOperand(data)
                                   : PrintRightOperand(data);
      AppendToBuffer(",%s", register_name);
      break;
    }
    case XMMREG_XMMOPER_OP_ORDER: {
      AppendToBuffer("%s %s,", mnem, NameOfXMMRegister(regop));
      advance = PrintRightXMMOperand(data);
      break;
    }
    case XMMOPER_XMMREG_OP_ORDER: {
      AppendToBuffer("%s ", mnem);
      advance = PrintRightXMMOperand(data);
      AppendToBuffer(",%s", NameOfXMMRegister(regop));
      break;
    }
    case OPER_XMMREG_OP_ORDER: {
      AppendToBuffer("%s ", mnem);
      advance = PrintRightOperand(data);
      AppendToBuffer(",%s", NameOfXMMRegister(regop));
      break;
    }
    case XMMREG_OPER_OP_ORDER: {
      AppendToBuffer("%s %s,", mnem, NameOfXMMRegister(regop));
      advance = PrintRightOperand(data);
      break;
    }
    default:
      UNREACHABLE();
  }
  return advance;
}

// Returns number of bytes used by machine instruction, including *data byte.
// Writes immediate instructions to 'tmp_buffer_'.
int DisassemblerX64::PrintImmediateOp(uint8_t* data) {
  DCHECK(*data == 0x80 || *data == 0x81 || *data == 0x83);
  bool byte_size_immediate = *data != 0x81;
  uint8_t modrm = *(data + 1);
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  const char* mnem = "Imm???";
  switch (regop) {
    case 0:
      mnem = "add";
      break;
    case 1:
      mnem = "or";
      break;
    case 2:
      mnem = "adc";
      break;
    case 3:
      mnem = "sbb";
      break;
    case 4:
      mnem = "and";
      break;
    case 5:
      mnem = "sub";
      break;
    case 6:
      mnem = "xor";
      break;
    case 7:
      mnem = "cmp";
      break;
    default:
      UnimplementedInstruction();
  }
  AppendToBuffer("%s%c ", mnem, operand_size_code());
  int count = byte_size_operand_ ? PrintRightByteOperand(data + 1)
                                 : PrintRightOperand(data + 1);
  AppendToBuffer(",0x");
  OperandSize immediate_size =
      byte_size_immediate ? OPERAND_BYTE_SIZE : operand_size();
  count += PrintImmediate(data + 1 + count, immediate_size);
  return 1 + count;
}

// Returns number of bytes used, including *data.
int DisassemblerX64::F6F7Instruction(uint8_t* data) {
  DCHECK(*data == 0xF7 || *data == 0xF6);
  uint8_t modrm = *(data + 1);
  int mod, regop, rm;
  get_modrm(modrm, &mod, &regop, &rm);
  if (regop != 0) {
    const char* mnem = nullptr;
    switch (regop) {
      case 2:
        mnem = "not";
        break;
      case 3:
        mnem = "neg";
        break;
      case 4:
        mnem = "mul";
        break;
      case 5:
        mnem = "imul";
        break;
      case 6:
        mnem = "div";
        break;
      case 7:
        mnem = "idiv";
        break;
      default:
        UnimplementedInstruction();
    }
    if (mod == 3) {
      AppendToBuffer("%s%c %s", mnem, operand_size_code(),
                     NameOfCPURegister(rm));
      return 2;
    } else if (mod == 1 ||
               mod == 2) {  // Byte displacement or 32-bit displacement
      AppendToBuffer("%s%c ", mnem, operand_size_code());
      int count = PrintRightOperand(data + 1);  // Use name of 64-bit register.
      return 1 + count;
    } else {
      UnimplementedInstruction();
      return 2;
    }
  } else if (regop == 0) {
    AppendToBuffer("test%c ", operand_size_code());
    int count = PrintRightOperand(data + 1);  // Use name of 64-bit register.
    AppendToBuffer(",0x");
    count += PrintImmediate(data + 1 + count, operand_size());
    return 1 + count;
  } else {
    UnimplementedInstruction();
    return 2;
  }
}

int DisassemblerX64::ShiftInstruction(uint8_t* data) {
  uint8_t op = *data & (~1);
  int count = 1;
  if (op != 0xD0 && op != 0xD2 && op != 0xC0) {
    UnimplementedInstruction();
    return count;
  }
  // Print mneumonic.
  {
    uint8_t modrm = *(data + count);
    int mod, regop, rm;
    get_modrm(modrm, &mod, &regop, &rm);
    regop &= 0x7;  // The REX.R bit does not affect the operation.
    const char* mnem = nullptr;
    switch (regop) {
      case 0:
        mnem = "rol";
        break;
      case 1:
        mnem = "ror";
        break;
      case 2:
        mnem = "rcl";
        break;
      case 3:
        mnem = "rcr";
        break;
      case 4:
        mnem = "shl";
        break;
      case 5:
        mnem = "shr";
        break;
      case 7:
        mnem = "sar";
        break;
      default:
        UnimplementedInstruction();
        return count + 1;
    }
    DCHECK_NOT_NULL(mnem);
    AppendToBuffer("%s%c ", mnem, operand_size_code());
  }
  count += PrintRightOperand(data + count);
  if (op == 0xD2) {
    AppendToBuffer(", cl");
  } else {
    int imm8 = -1;
    if (op == 0xD0) {
      imm8 = 1;
    } else {
      DCHECK_EQ(0xC0, op);
      imm8 = *(data + count);
      count++;
    }
    AppendToBuffer(", %d", imm8);
  }
  return count;
}

// Returns number of bytes used, including *data.
int DisassemblerX64::JumpShort(uint8_t* data) {
  DCHECK_EQ(0xEB, *data);
  uint8_t b = *(data + 1);
  uint8_t* dest = data + static_cast<int8_t>(b) + 2;
  AppendToBuffer("jmp %s", NameOfAddress(dest));
  return 2;
}

// Returns number of bytes used, including *data.
int DisassemblerX64::JumpConditional(uint8_t* data) {
  DCHECK_EQ(0x0F, *data);
  uint8_t cond = *(data + 1) & 0x0F;
  uint8_t* dest = data + Imm32(data + 2) + 6;
  const char* mnem = conditional_code_suffix[cond];
  AppendToBuffer("j%s %s", mnem, NameOfAddress(dest));
  return 6;  // includes 0x0F
}

// Returns number of bytes used, including *data.
int DisassemblerX64::JumpConditionalShort(uint8_t* data) {
  uint8_t cond = *data & 0x0F;
  uint8_t b = *(data + 1);
  uint8_t* dest = data + static_cast<int8_t>(b) + 2;
  const char* mnem = conditional_code_suffix[cond];
  AppendToBuffer("j%s %s", mnem, NameOfAddress(dest));
  return 2;
}

// Returns number of bytes used, including *data.
int DisassemblerX64::SetCC(uint8_t* data) {
  DCHECK_EQ(0x0F, *data);
  uint8_t cond = *(data + 1) & 0x0F;
  const char* mnem = conditional_code_suffix[cond];
  AppendToBuffer("set%s%c ", mnem, operand_size_code());
  PrintRightByteOperand(data + 2);
  return 3;  // includes 0x0F
}

const char* sf_str[4] = {"", "rl", "ra", "ll"};

int DisassemblerX64::AVXInstruction(uint8_t* data) {
  uint8_t opcode = *data;
  uint8_t* current = data + 1;
  if (vex_66() && vex_0f38()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x13:
        AppendToBuffer("vcvtph2ps %s,", NameOfAVXRegister(regop));
        current += PrintRightXMMOperand(current);
        break;
      case 0x18:
        AppendToBuffer("vbroadcastss %s,", NameOfAVXRegister(regop));
        current += PrintRightXMMOperand(current);
        break;
      case 0x19:
        AppendToBuffer("vbroadcastsd %s,", NameOfAVXRegister(regop));
        current += PrintRightXMMOperand(current);
        break;
      case 0xF7:
        AppendToBuffer("shlx%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfCPURegister(vvvv));
        break;
      case 0x50:
        AppendToBuffer("vpdpbusd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
#define DECLARE_SSE_AVX_DIS_CASE(instruction, notUsed1, notUsed2, notUsed3, \
                                 opcode)                                    \
  case 0x##opcode: {                                                        \
    AppendToBuffer("v" #instruction " %s,%s,", NameOfAVXRegister(regop),    \
                   NameOfAVXRegister(vvvv));                                \
    current += PrintRightAVXOperand(current);                               \
    break;                                                                  \
  }

        SSSE3_INSTRUCTION_LIST(DECLARE_SSE_AVX_DIS_CASE)
        SSE4_INSTRUCTION_LIST(DECLARE_SSE_AVX_DIS_CASE)
        SSE4_2_INSTRUCTION_LIST(DECLARE_SSE_AVX_DIS_CASE)
#undef DECLARE_SSE_AVX_DIS_CASE

#define DECLARE_SSE_UNOP_AVX_DIS_CASE(instruction, notUsed1, notUsed2, \
                                      notUsed3, opcode)                \
  case 0x##opcode: {                                                   \
    AppendToBuffer("v" #instruction " %s,", NameOfAVXRegister(regop)); \
    current += PrintRightAVXOperand(current);                          \
    break;                                                             \
  }
        SSSE3_UNOP_INSTRUCTION_LIST(DECLARE_SSE_UNOP_AVX_DIS_CASE)
        SSE4_UNOP_INSTRUCTION_LIST(DECLARE_SSE_UNOP_AVX_DIS_CASE)
#undef DECLARE_SSE_UNOP_AVX_DIS_CASE

#define DISASSEMBLE_AVX2_BROADCAST(instruction, _1, _2, _3, code)     \
  case 0x##code:                                                      \
    AppendToBuffer("" #instruction " %s,", NameOfAVXRegister(regop)); \
    current += PrintRightXMMOperand(current);                         \
    break;
        AVX2_BROADCAST_LIST(DISASSEMBLE_AVX2_BROADCAST)
#undef DISASSEMBLE_AVX2_BROADCAST

      default: {
#define DECLARE_FMA_DISASM(instruction, _1, _2, _3, _4, code)        \
  case 0x##code: {                                                   \
    AppendToBuffer(#instruction " %s,%s,", NameOfAVXRegister(regop), \
                   NameOfAVXRegister(vvvv));                         \
    current += PrintRightAVXOperand(current);                        \
    break;                                                           \
  }
        // Handle all the fma instructions here in the default branch since they
        // have the same opcodes but differ by rex_w.
        if (rex_w()) {
          switch (opcode) {
            FMA_SD_INSTRUCTION_LIST(DECLARE_FMA_DISASM)
            FMA_PD_INSTRUCTION_LIST(DECLARE_FMA_DISASM)
            default: {
              UnimplementedInstruction();
            }
          }
        } else {
          switch (opcode) {
            FMA_SS_INSTRUCTION_LIST(DECLARE_FMA_DISASM)
            FMA_PS_INSTRUCTION_LIST(DECLARE_FMA_DISASM)
            default: {
              UnimplementedInstruction();
            }
          }
        }
#undef DECLARE_FMA_DISASM
      }
    }
  } else if (vex_66() && vex_0f3a()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x00:
        AppendToBuffer("vpermq %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x06:
        AppendToBuffer("vperm2f128 %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x08:
        AppendToBuffer("vroundps %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x09:
        AppendToBuffer("vroundpd %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x0A:
        AppendToBuffer("vroundss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x0B:
        AppendToBuffer("vroundsd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x0E:
        AppendToBuffer("vpblendw %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x0F:
        AppendToBuffer("vpalignr %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x14:
        AppendToBuffer("vpextrb ");
        current += PrintRightByteOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x15:
        AppendToBuffer("vpextrw ");
        current += PrintRightOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x16:
        AppendToBuffer("vpextr%c ", rex_w() ? 'q' : 'd');
        current += PrintRightOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x17:
        AppendToBuffer("vextractps ");
        current += PrintRightOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x19:
        AppendToBuffer("vextractf128 ");
        current += PrintRightXMMOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x1D:
        AppendToBuffer("vcvtps2ph ");
        current += PrintRightXMMOperand(current);
        AppendToBuffer(",%s,0x%x", NameOfAVXRegister(regop), *current++);
        break;
      case 0x20:
        AppendToBuffer("vpinsrb %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightByteOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x21:
        AppendToBuffer("vinsertps %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x22:
        AppendToBuffer("vpinsr%c %s,%s,", rex_w() ? 'q' : 'd',
                       NameOfAVXRegister(regop), NameOfAVXRegister(vvvv));
        current += PrintRightOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x38:
        AppendToBuffer("vinserti128 %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightXMMOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x4A: {
        AppendToBuffer("vblendvps %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister((*current++) >> 4));
        break;
      }
      case 0x4B: {
        AppendToBuffer("vblendvpd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister((*current++) >> 4));
        break;
      }
      case 0x4C: {
        AppendToBuffer("vpblendvb %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister((*current++) >> 4));
        break;
      }
      default:
        UnimplementedInstruction();
    }
  } else if (vex_f3() && vex_0f()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x10:
        AppendToBuffer("vmovss %s,", NameOfAVXRegister(regop));
        if (mod == 3) {
          AppendToBuffer("%s,", NameOfAVXRegister(vvvv));
        }
        current += PrintRightAVXOperand(current);
        break;
      case 0x11:
        AppendToBuffer("vmovss ");
        current += PrintRightAVXOperand(current);
        if (mod == 3) {
          AppendToBuffer(",%s", NameOfAVXRegister(vvvv));
        }
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x16:
        AppendToBuffer("vmovshdup %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x2A:
        AppendToBuffer("%s %s,%s,", vex_w() ? "vcvtqsi2ss" : "vcvtlsi2ss",
                       NameOfAVXRegister(regop), NameOfAVXRegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0x2C:
        AppendToBuffer("vcvttss2si%s %s,", vex_w() ? "q" : "",
                       NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x51:
        AppendToBuffer("vsqrtss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x58:
        AppendToBuffer("vaddss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x59:
        AppendToBuffer("vmulss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5A:
        AppendToBuffer("vcvtss2sd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5B:
        AppendToBuffer("vcvttps2dq %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5C:
        AppendToBuffer("vsubss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5D:
        AppendToBuffer("vminss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5E:
        AppendToBuffer("vdivss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x5F:
        AppendToBuffer("vmaxss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0x6F:
        AppendToBuffer("vmovdqu %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x70:
        AppendToBuffer("vpshufhw %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x7F:
        AppendToBuffer("vmovdqu ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0xE6:
        AppendToBuffer("vcvtdq2pd %s,", NameOfAVXRegister(regop));
        current += PrintRightXMMOperand(current);
        break;
      case 0xC2:
        AppendToBuffer("vcmpss %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(", (%s)", cmp_pseudo_op[*current]);
        current += 1;
        break;
      default:
        UnimplementedInstruction();
    }
  } else if (vex_f2() && vex_0f()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x10:
        AppendToBuffer("vmovsd %s,", NameOfAVXRegister(regop));
        if (mod == 3) {
          AppendToBuffer("%s,", NameOfAVXRegister(vvvv));
        }
        current += PrintRightAVXOperand(current);
        break;
      case 0x11:
        AppendToBuffer("vmovsd ");
        current += PrintRightAVXOperand(current);
        if (mod == 3) {
          AppendToBuffer(",%s", NameOfAVXRegister(vvvv));
        }
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x12:
        AppendToBuffer("vmovddup %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x2A:
        AppendToBuffer("%s %s,%s,", vex_w() ? "vcvtqsi2sd" : "vcvtlsi2sd",
                       NameOfAVXRegister(regop), NameOfAVXRegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0x2C:
        AppendToBuffer("vcvttsd2si%s %s,", vex_w() ? "q" : "",
                       NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x2D:
        AppendToBuffer("vcvtsd2si%s %s,", vex_w() ? "q" : "",
                       NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0xF0:
        AppendToBuffer("vlddqu %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x70:
        AppendToBuffer("vpshuflw %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x7C:
        AppendToBuffer("vhaddps %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      case 0xC2:
        AppendToBuffer("vcmpsd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(", (%s)", cmp_pseudo_op[*current]);
        current += 1;
        break;
#define DISASM_SSE2_INSTRUCTION_LIST_SD(instruction, _1, _2, opcode)     \
  case 0x##opcode:                                                       \
    AppendToBuffer("v" #instruction " %s,%s,", NameOfAVXRegister(regop), \
                   NameOfAVXRegister(vvvv));                             \
    current += PrintRightAVXOperand(current);                            \
    break;
        SSE2_INSTRUCTION_LIST_SD(DISASM_SSE2_INSTRUCTION_LIST_SD)
#undef DISASM_SSE2_INSTRUCTION_LIST_SD
      default:
        UnimplementedInstruction();
    }
  } else if (vex_none() && vex_0f38()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    const char* mnem = "?";
    switch (opcode) {
      case 0xF2:
        AppendToBuffer("andn%c %s,%s,", operand_size_code(),
                       NameOfCPURegister(regop), NameOfCPURegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0xF5:
        AppendToBuffer("bzhi%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfCPURegister(vvvv));
        break;
      case 0xF7:
        AppendToBuffer("bextr%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfCPURegister(vvvv));
        break;
      case 0xF3:
        switch (regop) {
          case 1:
            mnem = "blsr";
            break;
          case 2:
            mnem = "blsmsk";
            break;
          case 3:
            mnem = "blsi";
            break;
          default:
            UnimplementedInstruction();
        }
        AppendToBuffer("%s%c %s,", mnem, operand_size_code(),
                       NameOfCPURegister(vvvv));
        current += PrintRightOperand(current);
        mnem = "?";
        break;
      default:
        UnimplementedInstruction();
    }
  } else if (vex_f2() && vex_0f38()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0xF5:
        AppendToBuffer("pdep%c %s,%s,", operand_size_code(),
                       NameOfCPURegister(regop), NameOfCPURegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0xF6:
        AppendToBuffer("mulx%c %s,%s,", operand_size_code(),
                       NameOfCPURegister(regop), NameOfCPURegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0xF7:
        AppendToBuffer("shrx%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfCPURegister(vvvv));
        break;
      case 0x50:
        AppendToBuffer("vpdpbssd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        break;
      default:
        UnimplementedInstruction();
    }
  } else if (vex_f3() && vex_0f38()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0xF5:
        AppendToBuffer("pext%c %s,%s,", operand_size_code(),
                       NameOfCPURegister(regop), NameOfCPURegister(vvvv));
        current += PrintRightOperand(current);
        break;
      case 0xF7:
        AppendToBuffer("sarx%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfCPURegister(vvvv));
        break;
      default:
        UnimplementedInstruction();
    }
  } else if (vex_f2() && vex_0f3a()) {
    int mod, regop, rm;
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0xF0:
        AppendToBuffer("rorx%c %s,", operand_size_code(),
                       NameOfCPURegister(regop));
        current += PrintRightOperand(current);
        switch (operand_size()) {
          case OPERAND_DOUBLEWORD_SIZE:
            AppendToBuffer(",%d", *current & 0x1F);
            break;
          case OPERAND_QUADWORD_SIZE:
            AppendToBuffer(",%d", *current & 0x3F);
            break;
          default:
            UnimplementedInstruction();
        }
        current += 1;
        break;
      default:
        UnimplementedInstruction();
    }
  } else if (vex_none() && vex_0f()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x10:
        AppendToBuffer("vmovups %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x11:
        AppendToBuffer("vmovups ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x12:
        if (mod == 0b11) {
          AppendToBuffer("vmovhlps %s,%s,", NameOfAVXRegister(regop),
                         NameOfAVXRegister(vvvv));
          current += PrintRightAVXOperand(current);
        } else {
          AppendToBuffer("vmovlps %s,%s,", NameOfAVXRegister(regop),
                         NameOfAVXRegister(vvvv));
          current += PrintRightAVXOperand(current);
        }
        break;
      case 0x13:
        AppendToBuffer("vmovlps ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x16:
        if (mod == 0b11) {
          AppendToBuffer("vmovlhps %s,%s,", NameOfAVXRegister(regop),
                         NameOfAVXRegister(vvvv));
          current += PrintRightAVXOperand(current);
        } else {
          AppendToBuffer("vmovhps %s,%s,", NameOfAVXRegister(regop),
                         NameOfAVXRegister(vvvv));
          current += PrintRightAVXOperand(current);
        }
        break;
      case 0x17:
        AppendToBuffer("vmovhps ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x28:
        AppendToBuffer("vmovaps %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x29:
        AppendToBuffer("vmovaps ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x2E:
        AppendToBuffer("vucomiss %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x50:
        AppendToBuffer("vmovmskps %s,", NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0xC2: {
        AppendToBuffer("vcmpps %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(", (%s)", cmp_pseudo_op[*current]);
        current += 1;
        break;
      }
      case 0xC6: {
        AppendToBuffer("vshufps %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      }
#define SSE_UNOP_CASE(instruction, unused, code)                       \
  case 0x##code:                                                       \
    AppendToBuffer("v" #instruction " %s,", NameOfAVXRegister(regop)); \
    current += PrintRightAVXOperand(current);                          \
    break;
        SSE_UNOP_INSTRUCTION_LIST(SSE_UNOP_CASE)
#undef SSE_UNOP_CASE
#define SSE_BINOP_CASE(instruction, unused, code)                        \
  case 0x##code:                                                         \
    AppendToBuffer("v" #instruction " %s,%s,", NameOfAVXRegister(regop), \
                   NameOfAVXRegister(vvvv));                             \
    current += PrintRightAVXOperand(current);                            \
    break;
        SSE_BINOP_INSTRUCTION_LIST(SSE_BINOP_CASE)
#undef SSE_BINOP_CASE
      default:
        UnimplementedInstruction();
    }
  } else if (vex_66() && vex_0f()) {
    int mod, regop, rm, vvvv = vex_vreg();
    get_modrm(*current, &mod, &regop, &rm);
    switch (opcode) {
      case 0x10:
        AppendToBuffer("vmovupd %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x11:
        AppendToBuffer("vmovupd ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x28:
        AppendToBuffer("vmovapd %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x29:
        AppendToBuffer("vmovapd ");
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0x50:
        AppendToBuffer("vmovmskpd %s,", NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x6E:
        AppendToBuffer("vmov%c %s,", vex_w() ? 'q' : 'd',
                       NameOfAVXRegister(regop));
        current += PrintRightOperand(current);
        break;
      case 0x6F:
        AppendToBuffer("vmovdqa %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        break;
      case 0x70:
        AppendToBuffer("vpshufd %s,", NameOfAVXRegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0x71:
        AppendToBuffer("vps%sw %s,", sf_str[regop / 2],
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%u", *current++);
        break;
      case 0x72:
        AppendToBuffer("vps%sd %s,", sf_str[regop / 2],
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%u", *current++);
        break;
      case 0x73:
        AppendToBuffer("vps%sq %s,", sf_str[regop / 2],
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",%u", *current++);
        break;
      case 0x7E:
        AppendToBuffer("vmov%c ", vex_w() ? 'q' : 'd');
        current += PrintRightOperand(current);
        AppendToBuffer(",%s", NameOfAVXRegister(regop));
        break;
      case 0xC2: {
        AppendToBuffer("vcmppd %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(", (%s)", cmp_pseudo_op[*current]);
        current += 1;
        break;
      }
      case 0xC4:
        AppendToBuffer("vpinsrw %s,%s,", NameOfAVXRegister(regop),
                       NameOfAVXRegister(vvvv));
        current += PrintRightOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0xC5:
        AppendToBuffer("vpextrw %s,", NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        AppendToBuffer(",0x%x", *current++);
        break;
      case 0xD7:
        AppendToBuffer("vpmovmskb %s,", NameOfCPURegister(regop));
        current += PrintRightAVXOperand(current);
        break;
#define DECLARE_SSE_AVX_DIS_CASE(instruction, notUsed1, notUsed2, opcode) \
  case 0x##opcode: {                                                      \
    AppendToBuffer("v" #instruction " %s,%s,", NameOfAVXRegister(regop),  \
                   NameOfAVXRegister(vvvv));                              \
    current += PrintRightAVXOperand(current);                             \
    break;                                                                \
  }

        SSE2_INSTRUCTION_LIST(DECLARE_SSE_AVX_DIS_CASE)
#undef DECLARE_SSE_AVX_DIS_CASE
#define DECLARE_SSE_UNOP_AVX_DIS_CASE(instruction, opcode, SIMDRegister)  \
  case 0x##opcode: {                                                      \
    AppendToBuffer("v" #instruction " %s,", NameOf##SIMDRegister(regop)); \
    current += PrintRightAVXOperand(current);                             \
    break;                                                                \
  }
        DECLARE_SSE_UNOP_AVX_DIS_CASE(ucomisd, 2E, AVXRegister)
        DECLARE_SSE_UNOP_AVX_DIS_CASE(sqrtpd, 51, AVXRegister)
        DECLARE_SSE_UNOP_AVX_DIS_CASE(cvtpd2ps, 5A, XMMRegister)
        DECLARE_SSE_UNOP_AVX_DIS_CASE(cvtps2dq, 5B, AVXRegister)
        DECLARE_SSE_UNOP_AVX_DIS_CASE(cvttpd2dq, E6, XMMRegister)
#undef DECLARE_SSE_UNOP_AVX_DIS_CASE
      default:
        UnimplementedInstruction();
    }

  } else {
    UnimplementedInstruction();
  }

  return static_cast<int>(current - data);
}

// Returns number of bytes used, including *data.
int DisassemblerX64::FPUInstruction(uint8_t* data) {
  uint8_t escape_opcode = *data;
  DCHECK_EQ(0xD8, escape_opcode & 0xF8);
  uint8_t modrm_byte = *(data + 1);

  if (modrm_byte >= 0xC0) {
    return RegisterFPUInstruction(escape_opcode, modrm_byte);
  } else {
    return MemoryFPUInstruction(escape_opcode, modrm_byte, data + 1);
  }
}

int DisassemblerX64::MemoryFPUInstruction(int escape_opcode, int modrm_byte,
                                          uint8_t* modrm_start) {
  const char* mnem = "?";
  int regop = (modrm_byte >> 3) & 0x7;  // reg/op field of modrm byte.
  switch (escape_opcode) {
    case 0xD9:
      switch (regop) {
        case 0:
          mnem = "fld_s";
          break;
        case 3:
          mnem = "fstp_s";
          break;
        case 7:
          mnem = "fstcw";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    case 0xDB:
      switch (regop) {
        case 0:
          mnem = "fild_s";
          break;
        case 1:
          mnem = "fisttp_s";
          break;
        case 2:
          mnem = "fist_s";
          break;
        case 3:
          mnem = "fistp_s";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    case 0xDD:
      switch (regop) {
        case 0:
          mnem = "fld_d";
          break;
        case 3:
          mnem = "fstp_d";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    case 0xDF:
      switch (regop) {
        case 5:
          mnem = "fild_d";
          break;
        case 7:
          mnem = "fistp_d";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    default:
      UnimplementedInstruction();
  }
  AppendToBuffer("%s ", mnem);
  int count = PrintRightOperand(modrm_start);
  return count + 1;
}

int DisassemblerX64::RegisterFPUInstruction(int escape_opcode,
                                            uint8_t modrm_byte) {
  bool has_register = false;  // Is the FPU register encoded in modrm_byte?
  const char* mnem = "?";

  switch (escape_opcode) {
    case 0xD8:
      UnimplementedInstruction();
      break;

    case 0xD9:
      switch (modrm_byte & 0xF8) {
        case 0xC0:
          mnem = "fld";
          has_register = true;
          break;
        case 0xC8:
          mnem = "fxch";
          has_register = true;
          break;
        default:
          switch (modrm_byte) {
            case 0xE0:
              mnem = "fchs";
              break;
            case 0xE1:
              mnem = "fabs";
              break;
            case 0xE3:
              mnem = "fninit";
              break;
            case 0xE4:
              mnem = "ftst";
              break;
            case 0xE8:
              mnem = "fld1";
              break;
            case 0xEB:
              mnem = "fldpi";
              break;
            case 0xED:
              mnem = "fldln2";
              break;
            case 0xEE:
              mnem = "fldz";
              break;
            case 0xF0:
              mnem = "f2xm1";
              break;
            case 0xF1:
              mnem = "fyl2x";
              break;
            case 0xF2:
              mnem = "fptan";
              break;
            case 0xF5:
              mnem = "fprem1";
              break;
            case 0xF7:
              mnem = "fincstp";
              break;
            case 0xF8:
              mnem = "fprem";
              break;
            case 0xFC:
              mnem = "frndint";
              break;
            case 0xFD:
              mnem = "fscale";
              break;
            case 0xFE:
              mnem = "fsin";
              break;
            case 0xFF:
              mnem = "fcos";
              break;
            default:
              UnimplementedInstruction();
          }
      }
      break;

    case 0xDA:
      if (modrm_byte == 0xE9) {
        mnem = "fucompp";
      } else {
        UnimplementedInstruction();
      }
      break;

    case 0xDB:
      if ((modrm_byte & 0xF8) == 0xE8) {
        mnem = "fucomi";
        has_register = true;
      } else if (modrm_byte == 0xE2) {
        mnem = "fclex";
      } else if (modrm_byte == 0xE3) {
        mnem = "fninit";
      } else {
        UnimplementedInstruction();
      }
      break;

    case 0xDC:
      has_register = true;
      switch (modrm_byte & 0xF8) {
        case 0xC0:
          mnem = "fadd";
          break;
        case 0xE8:
          mnem = "fsub";
          break;
        case 0xC8:
          mnem = "fmul";
          break;
        case 0xF8:
          mnem = "fdiv";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    case 0xDD:
      has_register = true;
      switch (modrm_byte & 0xF8) {
        case 0xC0:
          mnem = "ffree";
          break;
        case 0xD8:
          mnem = "fstp";
          break;
        default:
          UnimplementedInstruction();
      }
      break;

    case 0xDE:
      if (modrm_byte == 0xD9) {
        mnem = "fcompp";
      } else {
        has_register = true;
        switch (modrm_byte & 0xF8) {
          case 0xC0:
            mnem = "faddp";
            break;
          case 0xE8:
            mnem = "fsubp";
            break;
          case 0xC8:
            mnem = "fmulp";
            break;
          case 0xF8:
            mnem = "fdivp";
            break;
          default:
            UnimplementedInstruction();
        }
      }
      break;

    case 0xDF:
      if (modrm_byte == 0xE0) {
        mnem = "fnstsw_ax";
      } else if ((modrm_byte & 0xF8) == 0xE8) {
        mnem = "fucomip";
        has_register = true;
      }
      break;

    default:
      UnimplementedInstruction();
  }

  if (has_register) {
    AppendToBuffer("%s st%d", mnem, modrm_byte & 0x7);
  } else {
    AppendToBuffer("%s", mnem);
  }
  return 2;
}

// Handle all two-byte opcodes, which start with 0x0F.
// These instructions may be affected by an 0x66, 0xF2, or 0xF3 prefix.
int DisassemblerX64::TwoByteOpcodeInstruction(uint8_t* data) {
  uint8_t opcode = *(data + 1);
  uint8_t* current = data + 2;
  // At return, "current" points to the start of the next instruction.
  const char* mnemonic = TwoByteMnemonic(opcode);
  // Not every instruction will use this, but it doesn't hurt to figure it out
  // here, since it doesn't update any pointers.
  int mod, regop, rm;
  get_modrm(*current, &mod, &regop, &rm);
  if (operand_size_ == 0x66) {
    // These are three-byte opcodes, see ThreeByteOpcodeInstruction.
    DCHECK_NE(0x38, opcode);
    DCHECK_NE(0x3A, opcode);
    // 0x66 0x0F prefix.
    if (opcode == 0xC1) {
      current += PrintOperands("xadd", OPER_REG_OP_ORDER, current);
    } else if (opcode == 0x1F) {
      current++;
      if (rm == 4) {  // SIB byte present.
        current++;
      }
      if (mod == 1) {  // Byte displacement.
        current += 1;
      } else if (mod == 2) {  // 32-bit displacement.
        current += 4;
      }  // else no immediate displacement.
      AppendToBuffer("nop");
    } else if (opcode == 0x10) {
      current += PrintOperands("movupd", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x11) {
      current += PrintOperands("movupd", XMMOPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0x28) {
      current += PrintOperands("movapd", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x29) {
      current += PrintOperands("movapd", XMMOPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0x6E) {
      current += PrintOperands(rex_w() ? "movq" : "movd", XMMREG_OPER_OP_ORDER,
                               current);
    } else if (opcode == 0x6F) {
      current += PrintOperands("movdqa", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x7E) {
      current += PrintOperands(rex_w() ? "movq" : "movd", OPER_XMMREG_OP_ORDER,
                               current);
    } else if (opcode == 0x7F) {
      current += PrintOperands("movdqa", XMMOPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0xD6) {
      current += PrintOperands("movq", XMMOPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0x50) {
      AppendToBuffer("movmskpd %s,", NameOfCPURegister(regop));
      current += PrintRightXMMOperand(current);
    } else if (opcode == 0x70) {
      current += PrintOperands("pshufd", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", *current++);
    } else if (opcode == 0x71) {
      current += 1;
      AppendToBuffer("ps%sw %s,%d", sf_str[regop / 2], NameOfXMMRegister(rm),
                     *current & 0x7F);
      current += 1;
    } else if (opcode == 0x72) {
      current += 1;
      AppendToBuffer("ps%sd %s,%d", sf_str[regop / 2], NameOfXMMRegister(rm),
                     *current & 0x7F);
      current += 1;
    } else if (opcode == 0x73) {
      current += 1;
      AppendToBuffer("ps%sq %s,%d", sf_str[regop / 2], NameOfXMMRegister(rm),
                     *current & 0x7F);
      current += 1;
    } else if (opcode == 0xB1) {
      current += PrintOperands("cmpxchg", OPER_REG_OP_ORDER, current);
    } else if (opcode == 0xC2) {
      AppendToBuffer("cmppd %s,", NameOfXMMRegister(regop));
      current += PrintRightXMMOperand(current);
      AppendToBuffer(", (%s)", cmp_pseudo_op[*current++]);
    } else if (opcode == 0xC4) {
      current += PrintOperands("pinsrw", XMMREG_OPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", (*current++) & 7);
    } else if (opcode == 0xD7) {
      current += PrintOperands("pmovmskb", OPER_XMMREG_OP_ORDER, current);
    } else {
#define SSE2_CASE(instruction, notUsed1, notUsed2, opcode) \
  case 0x##opcode:                                         \
    mnemonic = "" #instruction;                            \
    break;

      switch (opcode) {
        SSE2_INSTRUCTION_LIST(SSE2_CASE)
        SSE2_UNOP_INSTRUCTION_LIST(SSE2_CASE)
      }
#undef SSE2_CASE
      AppendToBuffer("%s %s,", mnemonic, NameOfXMMRegister(regop));
      current += PrintRightXMMOperand(current);
    }
  } else if (group_1_prefix_ == 0xF2) {
    // Beginning of instructions with prefix 0xF2.
    if (opcode == 0x10) {
      // MOVSD: Move scalar double-precision fp to/from/between XMM registers.
      current += PrintOperands("movsd", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x11) {
      current += PrintOperands("movsd", XMMOPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0x12) {
      current += PrintOperands("movddup", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x2A) {
      // CVTSI2SD: integer to XMM double conversion.
      current += PrintOperands(mnemonic, XMMREG_OPER_OP_ORDER, current);
    } else if (opcode == 0x2C) {
      // CVTTSD2SI:
      // Convert with truncation scalar double-precision FP to integer.
      AppendToBuffer("cvttsd2si%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightXMMOperand(current);
    } else if (opcode == 0x2D) {
      // CVTSD2SI: Convert scalar double-precision FP to integer.
      AppendToBuffer("cvtsd2si%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightXMMOperand(current);
    } else if (opcode == 0x5B) {
      // CVTTPS2DQ: Convert packed single-precision FP values to packed signed
      // doubleword integer values
      AppendToBuffer("cvttps2dq%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightXMMOperand(current);
    } else if ((opcode & 0xF8) == 0x58 || opcode == 0x51) {
      // XMM arithmetic. Mnemonic was retrieved at the start of this function.
      current += PrintOperands(mnemonic, XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x70) {
      current += PrintOperands("pshuflw", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",%d", *current++);
    } else if (opcode == 0xC2) {
      AppendToBuffer("cmp%ssd %s,%s", cmp_pseudo_op[current[1]],
                     NameOfXMMRegister(regop), NameOfXMMRegister(rm));
      current += 2;
    } else if (opcode == 0xF0) {
      current += PrintOperands("lddqu", XMMREG_OPER_OP_ORDER, current);
    } else if (opcode == 0x7C) {
      current += PrintOperands("haddps", XMMREG_XMMOPER_OP_ORDER, current);
    } else {
      UnimplementedInstruction();
    }
  } else if (group_1_prefix_ == 0xF3) {
    // Instructions with prefix 0xF3.
    if (opcode == 0x10) {
      // MOVSS: Move scalar double-precision fp to/from/between XMM registers.
      current += PrintOperands("movss", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x11) {
      current += PrintOperands("movss", OPER_XMMREG_OP_ORDER, current);
    } else if (opcode == 0x16) {
      current += PrintOperands("movshdup", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x2A) {
      // CVTSI2SS: integer to XMM single conversion.
      current += PrintOperands(mnemonic, XMMREG_OPER_OP_ORDER, current);
    } else if (opcode == 0x2C) {
      // CVTTSS2SI:
      // Convert with truncation scalar single-precision FP to dword integer.
      AppendToBuffer("cvttss2si%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightXMMOperand(current);
    } else if (opcode == 0x70) {
      current += PrintOperands("pshufhw", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",%d", *current++);
    } else if (opcode == 0x6F) {
      current += PrintOperands("movdqu", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x7E) {
      current += PrintOperands("movq", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0x7F) {
      current += PrintOperands("movdqu", XMMOPER_XMMREG_OP_ORDER, current);
    } else if ((opcode & 0xF8) == 0x58 || opcode == 0x51) {
      // XMM arithmetic. Mnemonic was retrieved at the start of this function.
      current += PrintOperands(mnemonic, XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0xB8) {
      AppendToBuffer("popcnt%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightOperand(current);
    } else if (opcode == 0xBC) {
      AppendToBuffer("tzcnt%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightOperand(current);
    } else if (opcode == 0xBD) {
      AppendToBuffer("lzcnt%c %s,", operand_size_code(),
                     NameOfCPURegister(regop));
      current += PrintRightOperand(current);
    } else if (opcode == 0xC2) {
      AppendToBuffer("cmp%sss %s,%s", cmp_pseudo_op[current[1]],
                     NameOfXMMRegister(regop), NameOfXMMRegister(rm));
      current += 2;
    } else if (opcode == 0xE6) {
      current += PrintOperands("cvtdq2pd", XMMREG_XMMOPER_OP_ORDER, current);
    } else if (opcode == 0xAE) {
      // incssp[d|q]
      AppendToBuffer("incssp%c ", operand_size_code());
      current += PrintRightOperand(current);
    } else {
      UnimplementedInstruction();
    }
  } else if (opcode == 0x10) {
    // movups xmm, xmm/m128
    current += PrintOperands("movups", XMMREG_XMMOPER_OP_ORDER, current);
  } else if (opcode == 0x11) {
    // movups xmm/m128, xmm
    current += PrintOperands("movups", XMMOPER_XMMREG_OP_ORDER, current);
  } else if (opcode == 0x12) {
    // movhlps xmm1, xmm2
    // movlps xmm1, m64
    if (mod == 0b11) {
      current += PrintOperands("movhlps", XMMREG_XMMOPER_OP_ORDER, current);
    } else {
      current += PrintOperands("movlps", XMMREG_OPER_OP_ORDER, current);
    }
  } else if (opcode == 0x13) {
    // movlps m64, xmm1
    current += PrintOperands("movlps", XMMOPER_XMMREG_OP_ORDER, current);
  } else if (opcode == 0x16) {
    if (mod == 0b11) {
      current += PrintOperands("movlhps", XMMREG_XMMOPER_OP_ORDER, current);
    } else {
      current += PrintOperands("movhps", XMMREG_XMMOPER_OP_ORDER, current);
    }
  } else if (opcode == 0x17) {
    current += PrintOperands("movhps", XMMOPER_XMMREG_OP_ORDER, current);
  } else if (opcode == 0x1F) {
    // NOP
    current++;
    if (rm == 4) {  // SIB byte present.
      current++;
    }
    if (mod == 1) {  // Byte displacement.
      current += 1;
    } else if (mod == 2) {  // 32-bit displacement.
      current += 4;
    }  // else no immediate displacement.
    AppendToBuffer("nop");

  } else if (opcode == 0x28) {
    current += PrintOperands("movaps", XMMREG_XMMOPER_OP_ORDER, current);
  } else if (opcode == 0x29) {
    current += PrintOperands("movaps", XMMOPER_XMMREG_OP_ORDER, current);
  } else if (opcode == 0x2E) {
    current += PrintOperands("ucomiss", XMMREG_XMMOPER_OP_ORDER, current);
  } else if (opcode == 0xA2) {
    // CPUID
    AppendToBuffer("%s", mnemonic);
  } else if ((opcode & 0xF0) == 0x40) {
    // CMOVcc: conditional move.
    int condition = opcode & 0x0F;
    const InstructionDesc& idesc = cmov_instructions[condition];
    byte_size_operand_ = idesc.byte_size_operation;
    current += PrintOperands(idesc.mnem, idesc.op_order_, current);
  } else if (opcode == 0xC0) {
    byte_size_operand_ = true;
    current += PrintOperands("xadd", OPER_REG_OP_ORDER, current);
  } else if (opcode == 0xC1) {
    current += PrintOperands("xadd", OPER_REG_OP_ORDER, current);
  } else if (opcode == 0xC2) {
    // cmpps xmm, xmm/m128, imm8
    AppendToBuffer("cmpps %s, ", NameOfXMMRegister(regop));
    current += PrintRightXMMOperand(current);
    AppendToBuffer(", %s", cmp_pseudo_op[*current]);
    current += 1;
  } else if (opcode == 0xC6) {
    // shufps xmm, xmm/m128, imm8
    AppendToBuffer("shufps %s,", NameOfXMMRegister(regop));
    current += PrintRightXMMOperand(current);
    AppendToBuffer(",%d", *current);
    current += 1;
  } else if (opcode >= 0xC8 && opcode <= 0xCF) {
    // bswap
    int reg = (opcode - 0xC8) | (rex_b() ? 8 : 0);
    AppendToBuffer("bswap%c %s", operand_size_code(), NameOfCPURegister(reg));
  } else if (opcode == 0x50) {
    // movmskps reg, xmm
    AppendToBuffer("movmskps %s,", NameOfCPURegister(regop));
    current += PrintRightXMMOperand(current);
  } else if ((opcode & 0xF0) == 0x80) {
    // Jcc: Conditional jump (branch).
    current = data + JumpConditional(data);

  } else if (opcode == 0xBE || opcode == 0xBF || opcode == 0xB6 ||
             opcode == 0xB7 || opcode == 0xAF) {
    // Size-extending moves, IMUL.
    current += PrintOperands(mnemonic, REG_OPER_OP_ORDER, current);
  } else if ((opcode & 0xF0) == 0x90) {
    // SETcc: Set byte on condition. Needs pointer to beginning of instruction.
    current = data + SetCC(data);
  } else if (opcode == 0xA3 || opcode == 0xA5 || opcode == 0xAB ||
             opcode == 0xAD) {
    // BT (bit test), SHLD, BTS (bit test and set),
    // SHRD (double-precision shift)
    AppendToBuffer("%s ", mnemonic);
    current += PrintRightOperand(current);
    if (opcode == 0xAB) {
      AppendToBuffer(",%s", NameOfCPURegister(regop));
    } else {
      AppendToBuffer(",%s,cl", NameOfCPURegister(regop));
    }
  } else if (opcode == 0xBA) {
    // BTS / BTR (bit test and set/reset) with immediate
    mnemonic = regop == 5 ? "bts" : regop == 6 ? "btr" : "?";
    AppendToBuffer("%s ", mnemonic);
    current += PrintRightOperand(current);
    AppendToBuffer(",%d", *current++);
  } else if (opcode == 0xB8 || opcode == 0xBC || opcode == 0xBD) {
    // POPCNT, CTZ, CLZ.
    AppendToBuffer("%s%c ", mnemonic, operand_size_code());
    AppendToBuffer("%s,", NameOfCPURegister(regop));
    current += PrintRightOperand(current);
  } else if (opcode == 0x0B) {
    AppendToBuffer("ud2");
  } else if (opcode == 0xB0 || opcode == 0xB1) {
    // CMPXCHG.
    if (opcode == 0xB0) {
      byte_size_operand_ = true;
    }
    current += PrintOperands(mnemonic, OPER_REG_OP_ORDER, current);
  } else if (opcode == 0xAE && (data[2] & 0xF8) == 0xF0) {
    AppendToBuffer("mfence");
    current = data + 3;
  } else if (opcode == 0xAE && (data[2] & 0xF8) == 0xE8) {
    AppendToBuffer("lfence");
    current = data + 3;
    // clang-format off
#define SSE_DISASM_CASE(instruction, unused, code) \
  } else if (opcode == 0x##code) {                 \
    current += PrintOperands(#instruction, XMMREG_XMMOPER_OP_ORDER, current);
    SSE_UNOP_INSTRUCTION_LIST(SSE_DISASM_CASE)
    SSE_BINOP_INSTRUCTION_LIST(SSE_DISASM_CASE)
#undef SSE_DISASM_CASE
    // clang-format on
  } else {
    UnimplementedInstruction();
  }
  return static_cast<int>(current - data);
}

// Handle all three-byte opcodes, which start with 0x0F38 or 0x0F3A.
// These instructions may be affected by an 0x66, 0xF2, or 0xF3 prefix, but we
// only have instructions prefixed with 0x66 for now.
int DisassemblerX64::ThreeByteOpcodeInstruction(uint8_t* data) {
  DCHECK_EQ(0x0F, *data);
  // Only support 3-byte opcodes prefixed with 0x66 for now.
  DCHECK_EQ(0x66, operand_size_);
  uint8_t second_byte = *(data + 1);
  uint8_t third_byte = *(data + 2);
  uint8_t* current = data + 3;
  int mod, regop, rm;
  get_modrm(*current, &mod, &regop, &rm);
  if (second_byte == 0x38) {
    switch (third_byte) {
      case 0x10: {
        current += PrintOperands("pblendvb", XMMREG_XMMOPER_OP_ORDER, current);
        AppendToBuffer(",<xmm0>");
        break;
      }
      case 0x14: {
        current += PrintOperands("blendvps", XMMREG_XMMOPER_OP_ORDER, current);
        AppendToBuffer(",<xmm0>");
        break;
      }
      case 0x15: {
        current += PrintOperands("blendvpd", XMMREG_XMMOPER_OP_ORDER, current);
        AppendToBuffer(",<xmm0>");
        break;
      }
#define SSE34_DIS_CASE(instruction, notUsed1, notUsed2, notUsed3, opcode)     \
  case 0x##opcode: {                                                          \
    current += PrintOperands(#instruction, XMMREG_XMMOPER_OP_ORDER, current); \
    break;                                                                    \
  }

        SSSE3_INSTRUCTION_LIST(SSE34_DIS_CASE)
        SSSE3_UNOP_INSTRUCTION_LIST(SSE34_DIS_CASE)
        SSE4_INSTRUCTION_LIST(SSE34_DIS_CASE)
        SSE4_UNOP_INSTRUCTION_LIST(SSE34_DIS_CASE)
        SSE4_2_INSTRUCTION_LIST(SSE34_DIS_CASE)
#undef SSE34_DIS_CASE
      default:
        UnimplementedInstruction();
    }
  } else {
    DCHECK_EQ(0x3A, second_byte);
    if (third_byte == 0x17) {
      current += PrintOperands("extractps", OPER_XMMREG_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 3);
    } else if (third_byte == 0x08) {
      current += PrintOperands("roundps", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", (*current++) & 3);
    } else if (third_byte == 0x09) {
      current += PrintOperands("roundpd", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", (*current++) & 3);
    } else if (third_byte == 0x0A) {
      current += PrintOperands("roundss", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", (*current++) & 3);
    } else if (third_byte == 0x0B) {
      current += PrintOperands("roundsd", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", (*current++) & 3);
    } else if (third_byte == 0x0E) {
      current += PrintOperands("pblendw", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", *current++);
    } else if (third_byte == 0x0F) {
      current += PrintOperands("palignr", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", *current++);
    } else if (third_byte == 0x14) {
      current += PrintOperands("pextrb", OPER_XMMREG_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 0xf);
    } else if (third_byte == 0x15) {
      current += PrintOperands("pextrw", OPER_XMMREG_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 7);
    } else if (third_byte == 0x16) {
      const char* mnem = rex_w() ? "pextrq" : "pextrd";
      current += PrintOperands(mnem, OPER_XMMREG_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 3);
    } else if (third_byte == 0x20) {
      current += PrintOperands("pinsrb", XMMREG_OPER_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 0xf);
    } else if (third_byte == 0x21) {
      current += PrintOperands("insertps", XMMREG_XMMOPER_OP_ORDER, current);
      AppendToBuffer(",0x%x", *current++);
    } else if (third_byte == 0x22) {
      const char* mnem = rex_w() ? "pinsrq" : "pinsrd";
      current += PrintOperands(mnem, XMMREG_OPER_OP_ORDER, current);
      AppendToBuffer(",%d", (*current++) & 3);
    } else {
      UnimplementedInstruction();
    }
  }
  return static_cast<int>(current - data);
}

// Mnemonics for two-byte opcode instructions starting with 0x0F.
// The argument is the second byte of the two-byte opcode.
// Returns nullptr if the instruction is not handled here.
const char* DisassemblerX64::TwoByteMnemonic(uint8_t opcode) {
  if (opcode >= 0xC8 && opcode <= 0xCF) return "bswap";
  switch (opcode) {
    case 0x1F:
      return "nop";
    case 0x2A:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "cvtsi2sd" : "cvtsi2ss";
    case 0x51:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "sqrtsd" : "sqrtss";
    case 0x58:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "addsd" : "addss";
    case 0x59:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "mulsd" : "mulss";
    case 0x5A:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "cvtsd2ss" : "cvtss2sd";
    case 0x5B:  // F2/F3 prefix.
      return "cvttps2dq";
    case 0x5D:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "minsd" : "minss";
    case 0x5C:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "subsd" : "subss";
    case 0x5E:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "divsd" : "divss";
    case 0x5F:  // F2/F3 prefix.
      return (group_1_prefix_ == 0xF2) ? "maxsd" : "maxss";
    case 0xA2:
      return "cpuid";
    case 0xA3:
      return "bt";
    case 0xA5:
      return "shld";
    case 0xAB:
      return "bts";
    case 0xAD:
      return "shrd";
    case 0xAF:
      return "imul";
    case 0xB0:
    case 0xB1:
      return "cmpxchg";
    case 0xB6:
      return "movzxb";
    case 0xB7:
      return "movzxw";
    case 0xBC:
      return "bsf";
    case 0xBD:
      return "bsr";
    case 0xBE:
      return "movsxb";
    case 0xBF:
      return "movsxw";
    case 0xC2:
      return "cmpss";
    default:
      return nullptr;
  }
}

// Disassembles the instruction at instr, and writes it into out_buffer.
int DisassemblerX64::InstructionDecode(v8::base::Vector<char> out_buffer,
                                       uint8_t* instr) {
  tmp_buffer_pos_ = 0;  // starting to write as position 0
  uint8_t* data = instr;
  bool processed = true;  // Will be set to false if the current instruction
                          // is not in 'instructions' table.
  uint8_t current;

  // Scan for prefixes.
  while (true) {
    current = *data;
    if (current == OPERAND_SIZE_OVERRIDE_PREFIX) {  // Group 3 prefix.
      operand_size_ = current;
    } else if ((current & 0xF0) == 0x40) {  // REX prefix.
      setRex(current);
      if (rex_w()) AppendToBuffer("REX.W ");
    } else if ((current & 0xFE) == 0xF2) {  // Group 1 prefix (0xF2 or 0xF3).
      group_1_prefix_ = current;
    } else if (current == LOCK_PREFIX) {
      AppendToBuffer("lock ");
    } else if (current == VEX3_PREFIX) {
      vex_byte0_ = current;
      vex_byte1_ = *(data + 1);
      vex_byte2_ = *(data + 2);
      setRex(0x40 | (~(vex_byte1_ >> 5) & 7) | ((vex_byte2_ >> 4) & 8));
      data += 3;
      break;  // Vex is the last prefix.
    } else if (current == VEX2_PREFIX) {
      vex_byte0_ = current;
      vex_byte1_ = *(data + 1);
      setRex(0x40 | (~(vex_byte1_ >> 5) & 4));
      data += 2;
      break;  // Vex is the last prefix.
    } else if (current == SEGMENT_FS_OVERRIDE_PREFIX) {
      segment_prefix_ = current;
    } else if (current == ADDRESS_SIZE_OVERRIDE_PREFIX) {
      address_size_prefix_ = current;
    } else {  // Not a prefix - an opcode.
      break;
    }
    data++;
  }

  // Decode AVX instructions.
  if (vex_byte0_ != 0) {
    processed = true;
    data += AVXInstruction(data);
  } else if (segment_prefix_ != 0 && address_size_prefix_ != 0) {
    if (*data == 0x90 && *(data + 1) == 0x90 && *(data + 2) == 0x90) {
      AppendToBuffer("sscmark");
      processed = true;
      data += 3;
    }
  } else {
    const InstructionDesc& idesc = instruction_table_->Get(current);
    byte_size_operand_ = idesc.byte_size_operation;
    switch (idesc.type) {
      case ZERO_OPERANDS_INSTR:
        if ((current >= 0xA4 && current <= 0xA7) ||
            (current >= 0xAA && current <= 0xAD)) {
          // String move or compare operations.
          if (group_1_prefix_ == REP_PREFIX) {
            // REP.
            AppendToBuffer("rep ");
          }
          AppendToBuffer("%s%c", idesc.mnem, operand_size_code());
        } else {
          AppendToBuffer("%s%c", idesc.mnem, operand_size_code());
        }
        data++;
        break;

      case TWO_OPERANDS_INSTR:
        data++;
        data += PrintOperands(idesc.mnem, idesc.op_order_, data);
        break;

      case JUMP_CONDITIONAL_SHORT_INSTR:
        data += JumpConditionalShort(data);
        break;

      case REGISTER_INSTR:
        AppendToBuffer("%s%c %s", idesc.mnem, operand_size_code(),
                       NameOfCPURegister(base_reg(current & 0x07)));
        data++;
        break;
      case PUSHPOP_INSTR:
        AppendToBuffer("%s %s", idesc.mnem,
                       NameOfCPURegister(base_reg(current & 0x07)));
        data++;
        break;
      case MOVE_REG_INSTR: {
        uint8_t* addr = nullptr;
        switch (operand_size()) {
          case OPERAND_WORD_SIZE:
            addr = reinterpret_cast<uint8_t*>(Imm16(data + 1));
            data += 3;
            break;
          case OPERAND_DOUBLEWORD_SIZE:
            addr = reinterpret_cast<uint8_t*>(Imm32_U(data + 1));
            data += 5;
            break;
          case OPERAND_QUADWORD_SIZE:
            addr = reinterpret_cast<uint8_t*>(Imm64(data + 1));
            data += 9;
            break;
          default:
            UNREACHABLE();
        }
        AppendToBuffer("mov%c %s,%s", operand_size_code(),
                       NameOfCPURegister(base_reg(current & 0x07)),
                       NameOfAddress(addr));
        break;
      }

      case CALL_JUMP_INSTR: {
        uint8_t* addr = data + Imm32(data + 1) + 5;
        AppendToBuffer("%s %s", idesc.mnem, NameOfAddress(addr));
        data += 5;
        break;
      }

      case SHORT_IMMEDIATE_INSTR: {
        int32_t imm;
        if (operand_size() == OPERAND_WORD_SIZE) {
          imm = Imm16(data + 1);
          data += 3;
        } else {
          imm = Imm32(data + 1);
          data += 5;
        }
        AppendToBuffer("%s rax,0x%x", idesc.mnem, imm);
        break;
      }

      case NO_INSTR:
        processed = false;
        break;

      default:
        UNIMPLEMENTED();  // This type is not implemented.
    }
  }

  // The first byte didn't match any of the simple opcodes, so we
  // need to do special processing on it.
  if (!processed) {
    switch (*data) {
      case 0xC2:
        AppendToBuffer("ret 0x%x", Imm16_U(data + 1));
        data += 3;
        break;

      case 0x69:  // fall through
      case 0x6B: {
        int count = 1;
        count += PrintOperands("imul", REG_OPER_OP_ORDER, data + count);
        AppendToBuffer(",0x");
        if (*data == 0x69) {
          count += PrintImmediate(data + count, operand_size());
        } else {
          count += PrintImmediate(data + count, OPERAND_BYTE_SIZE);
        }
        data += count;
        break;
      }

      case 0x80:
        byte_size_operand_ = true;
        [[fallthrough]];
      case 0x81:  // fall through
      case 0x83:  // 0x81 with sign extension bit set
        data += PrintImmediateOp(data);
        break;

      case 0x0F:
        // Check for three-byte opcodes, 0x0F38 or 0x0F3A.
        if (*(data + 1) == 0x38 || *(data + 1) == 0x3A) {
          data += ThreeByteOpcodeInstruction(data);
        } else {
          data += TwoByteOpcodeInstruction(data);
        }
        break;

      case 0x8F: {
        data++;
        int mod, regop, rm;
        get_modrm(*data, &mod, &regop, &rm);
        if (regop == 0) {
          AppendToBuffer("pop ");
          data += PrintRightOperand(data);
        }
      } break;

      case 0xFF: {
        data++;
        int mod, regop, rm;
        get_modrm(*data, &mod, &regop, &rm);
        const char* mnem = nullptr;
        switch (regop) {
          case 0:
            mnem = "inc";
            break;
          case 1:
            mnem = "dec";
            break;
          case 2:
            mnem = "call";
            break;
          case 4:
            mnem = "jmp";
            break;
          case 6:
            mnem = "push";
            break;
          default:
            mnem = "???";
        }
        if (regop <= 1) {
          AppendToBuffer("%s%c ", mnem, operand_size_code());
        } else {
          AppendToBuffer("%s ", mnem);
        }
        data += PrintRightOperand(data);
      } break;

      case 0xC7:  // imm32, fall through
      case 0xC6:  // imm8
      {
        bool is_byte = *data == 0xC6;
        data++;
        if (is_byte) {
          AppendToBuffer("movb ");
          data += PrintRightByteOperand(data);
          int32_t imm = *data;
          AppendToBuffer(",0x%x", imm);
          data++;
        } else {
          AppendToBuffer("mov%c ", operand_size_code());
          data += PrintRightOperand(data);
          if (operand_size() == OPERAND_WORD_SIZE) {
            AppendToBuffer(",0x%x", Imm16(data));
            data += 2;
          } else {
            AppendToBuffer(",0x%x", Imm32(data));
            data += 4;
          }
        }
      } break;

      case 0x88:  // 8bit, fall through
      case 0x89:  // 32bit
      {
        bool is_byte = *data == 0x88;
        int mod, regop, rm;
        data++;
        get_modrm(*data, &mod, &regop, &rm);
        if (is_byte) {
          AppendToBuffer("movb ");
          data += PrintRightByteOperand(data);
          AppendToBuffer(",%s", NameOfByteCPURegister(regop));
        } else {
          AppendToBuffer("mov%c ", operand_size_code());
          data += PrintRightOperand(data);
          AppendToBuffer(",%s", NameOfCPURegister(regop));
        }
      } break;

      case 0x90:
      case 0x91:
      case 0x92:
      case 0x93:
      case 0x94:
      case 0x95:
      case 0x96:
      case 0x97: {
        int reg = (*data & 0x7) | (rex_b() ? 8 : 0);
        if (group_1_prefix_ == 0xF3 && *data == 0x90) {
          AppendToBuffer("pause");
        } else if (reg == 0) {
          AppendToBuffer("nop");  // Common name for xchg rax,rax.
        } else {
          AppendToBuffer("xchg%c rax,%s", operand_size_code(),
                         NameOfCPURegister(reg));
        }
        data++;
      } break;
      case 0xB0:
      case 0xB1:
      case 0xB2:
      case 0xB3:
      case 0xB4:
      case 0xB5:
      case 0xB6:
      case 0xB7:
      case 0xB8:
      case 0xB9:
      case 0xBA:
      case 0xBB:
      case 0xBC:
      case 0xBD:
      case 0xBE:
      case 0xBF: {
        // mov reg8,imm8 or mov reg32,imm32
        uint8_t opcode = *data;
        data++;
        bool is_32bit = (opcode >= 0xB8);
        int reg = (opcode & 0x7) | (rex_b() ? 8 : 0);
        if (is_32bit) {
          AppendToBuffer("mov%c %s,", operand_size_code(),
                         NameOfCPURegister(reg));
          data += PrintImmediate(data, OPERAND_DOUBLEWORD_SIZE);
        } else {
          AppendToBuffer("movb %s,", NameOfByteCPURegister(reg));
          data += PrintImmediate(data, OPERAND_BYTE_SIZE);
        }
        break;
      }
      case 0xFE: {
        data++;
        int mod, regop, rm;
        get_modrm(*data, &mod, &regop, &rm);
        if (regop == 1) {
          AppendToBuffer("decb ");
          data += PrintRightByteOperand(data);
        } else {
          UnimplementedInstruction();
        }
        break;
      }
      case 0x68:
        AppendToBuffer("push 0x%x", Imm32(data + 1));
        data += 5;
        break;

      case 0x6A:
        AppendToBuffer("push 0x%x", Imm8(data + 1));
        data += 2;
        break;

      case 0xA1:  // Fall through.
      case 0xA3:
        switch (operand_size()) {
          case OPERAND_DOUBLEWORD_SIZE: {
            const char* memory_location =
                NameOfAddress(reinterpret_cast<uint8_t*>(Imm32(data + 1)));
            if (*data == 0xA1) {  // Opcode 0xA1
              AppendToBuffer("movzxlq rax,(%s)", memory_location);
            } else {  // Opcode 0xA3
              AppendToBuffer("movzxlq (%s),rax", memory_location);
            }
            data += 5;
            break;
          }
          case OPERAND_QUADWORD_SIZE: {
            // New x64 instruction mov rax,(imm_64).
            const char* memory_location =
                NameOfAddress(reinterpret_cast<uint8_t*>(Imm64(data + 1)));
            if (*data == 0xA1) {  // Opcode 0xA1
              AppendToBuffer("movq rax,(%s)", memory_location);
            } else {  // Opcode 0xA3
              AppendToBuffer("movq (%s),rax", memory_location);
            }
            data += 9;
            break;
          }
          default:
            UnimplementedInstruction();
            data += 2;
        }
        break;

      case 0xA8:
        AppendToBuffer("test al,0x%x", Imm8_U(data + 1));
        data += 2;
        break;

      case 0xA9: {
        int64_t value = 0;
        switch (operand_size()) {
          case OPERAND_WORD_SIZE:
            value = Imm16_U(data + 1);
            data += 3;
            break;
          case OPERAND_DOUBLEWORD_SIZE:
            value = Imm32_U(data + 1);
            data += 5;
            break;
          case OPERAND_QUADWORD_SIZE:
            value = Imm32(data + 1);
            data += 5;
            break;
          default:
            UNREACHABLE();
        }
        AppendToBuffer("test%c rax,0x%" PRIx64, operand_size_code(), value);
        break;
      }
      case 0xD1:  // fall through
      case 0xD3:  // fall through
      case 0xC1:
        data += ShiftInstruction(data);
        break;
      case 0xD0:  // fall through
      case 0xD2:  // fall through
      case 0xC0:
        byte_size_operand_ = true;
        data += ShiftInstruction(data);
        break;

      case 0xD9:  // fall through
      case 0xDA:  // fall through
      case 0xDB:  // fall through
      case 0xDC:  // fall through
      case 0xDD:  // fall through
      case 0xDE:  // fall through
      case 0xDF:
        data += FPUInstruction(data);
        break;

      case 0xEB:
        data += JumpShort(data);
        break;

      case 0xF6:
        byte_size_operand_ = true;
        [[fallthrough]];
      case 0xF7:
        data += F6F7Instruction(data);
        break;

      case 0x3C:
        AppendToBuffer("cmpb al,0x%x", Imm8(data + 1));
        data += 2;
        break;

      default:
        UnimplementedInstruction();
        data += 1;
    }
  }  // !processed

  if (tmp_buffer_pos_ < sizeof tmp_buffer_) {
    tmp_buffer_[tmp_buffer_pos_] = '\0';
  }

  int instr_len = static_cast<int>(data - instr);
  DCHECK_GT(instr_len, 0);  // Ensure progress.

  int outp = 0;
  // Instruction bytes.
  for (uint8_t* bp = instr; bp < data; bp++) {
    outp += v8::base::SNPrintF(out_buffer + outp, "%02x", *bp);
  }
  // Indent instruction, leaving space for 10 bytes, i.e. 20 characters in hex.
  // 10-byte mov is (probably) the largest we emit.
  while (outp < 20) {
    outp += v8::base::SNPrintF(out_buffer + outp, "  ");
  }

  outp += v8::base::SNPrintF(out_buffer + outp, " %s", tmp_buffer_.begin());
  return instr_len;
}

//------------------------------------------------------------------------------

static const char* const cpu_regs[16] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};

static const char* const byte_cpu_regs[16] = {
    "al",  "cl",  "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
    "r8l", "r9l", "r10l", "r11l", "r12l", "r13l", "r14l", "r15l"};

static const char* const xmm_regs[16] = {
    "xmm0", "xmm1", "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",  "xmm7",
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"};

static const char* const ymm_regs[16] = {
    "ymm0", "ymm1", "ymm2",  "ymm3",  "ymm4",  "ymm5",  "ymm6",  "ymm7",
    "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15"};

const char* NameConverter::NameOfAddress(uint8_t* addr) const {
  v8::base::SNPrintF(tmp_buffer_, "%p", static_cast<void*>(addr));
  return tmp_buffer_.begin();
}

const char* NameConverter::NameOfConstant(uint8_t* addr) const {
  return NameOfAddress(addr);
}

const char* NameConverter::NameOfCPURegister(int reg) const {
  if (0 <= reg && reg < 16) return cpu_regs[reg];
  return "noreg";
}

const char* NameConverter::NameOfByteCPURegister(int reg) const {
  if (0 <= reg && reg < 16) return byte_cpu_regs[reg];
  return "noreg";
}

const char* NameConverter::NameOfXMMRegister(int reg) const {
  if (0 <= reg && reg < 16) return xmm_regs[reg];
  return "noxmmreg";
}

const char* NameOfYMMRegister(int reg) {
  if (0 <= reg && reg < 16) return ymm_regs[reg];
  return "noymmreg";
}

const char* NameConverter::NameInCode(uint8_t* addr) const {
  // X64 does not embed debug strings at the moment.
  UNREACHABLE();
}

//------------------------------------------------------------------------------

int Disassembler::InstructionDecode(v8::base::Vector<char> buffer,
                                    uint8_t* instruction) {
  DisassemblerX64 d(converter_, unimplemented_opcode_action());
  return d.InstructionDecode(buffer, instruction);
}

// The X64 assembler does not use constant pools.
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
    fprintf(f, "%p", static_cast<void*>(prev_pc));
    fprintf(f, "    ");

    for (uint8_t* bp = prev_pc; bp < pc; bp++) {
      fprintf(f, "%02x", *bp);
    }
    for (int i = 6 - static_cast<int>(pc - prev_pc); i >= 0; i--) {
      fprintf(f, "  ");
    }
    fprintf(f, "  %s\n", buffer.begin());
  }
}

}  // namespace disasm

#endif  // V8_TARGET_ARCH_X64
