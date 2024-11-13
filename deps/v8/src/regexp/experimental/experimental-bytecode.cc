// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental-bytecode.h"

#include <cctype>
#include <iomanip>

namespace v8 {
namespace internal {

namespace {

std::ostream& PrintAsciiOrHex(std::ostream& os, base::uc16 c) {
  if (c < 128 && std::isprint(c)) {
    os << static_cast<char>(c);
  } else {
    os << "0x" << std::hex << static_cast<int>(c);
  }
  return os;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const RegExpInstruction& inst) {
  switch (inst.opcode) {
    case RegExpInstruction::CONSUME_RANGE: {
      os << "CONSUME_RANGE [";
      PrintAsciiOrHex(os, inst.payload.consume_range.min);
      os << ", ";
      PrintAsciiOrHex(os, inst.payload.consume_range.max);
      os << "]";
      break;
    }
    case RegExpInstruction::ASSERTION:
      os << "ASSERTION ";
      switch (inst.payload.assertion_type) {
        case RegExpAssertion::Type::START_OF_INPUT:
          os << "START_OF_INPUT";
          break;
        case RegExpAssertion::Type::END_OF_INPUT:
          os << "END_OF_INPUT";
          break;
        case RegExpAssertion::Type::START_OF_LINE:
          os << "START_OF_LINE";
          break;
        case RegExpAssertion::Type::END_OF_LINE:
          os << "END_OF_LINE";
          break;
        case RegExpAssertion::Type::BOUNDARY:
          os << "BOUNDARY";
          break;
        case RegExpAssertion::Type::NON_BOUNDARY:
          os << "NON_BOUNDARY";
          break;
      }
      break;
    case RegExpInstruction::FORK:
      os << "FORK " << inst.payload.pc;
      break;
    case RegExpInstruction::JMP:
      os << "JMP " << inst.payload.pc;
      break;
    case RegExpInstruction::ACCEPT:
      os << "ACCEPT";
      break;
    case RegExpInstruction::SET_REGISTER_TO_CP:
      os << "SET_REGISTER_TO_CP " << inst.payload.register_index;
      break;
    case RegExpInstruction::CLEAR_REGISTER:
      os << "CLEAR_REGISTER " << inst.payload.register_index;
      break;
    case RegExpInstruction::SET_QUANTIFIER_TO_CLOCK:
      os << "SET_QUANTIFIER_TO_CLOCK " << inst.payload.quantifier_id;
      break;
    case RegExpInstruction::FILTER_QUANTIFIER:
      os << "FILTER_QUANTIFIER " << inst.payload.quantifier_id;
      break;
    case RegExpInstruction::FILTER_GROUP:
      os << "FILTER_GROUP " << inst.payload.group_id;
      break;
    case RegExpInstruction::FILTER_CHILD:
      os << "FILTER_CHILD " << inst.payload.pc;
      break;
    case RegExpInstruction::BEGIN_LOOP:
      os << "BEGIN_LOOP";
      break;
    case RegExpInstruction::END_LOOP:
      os << "END_LOOP";
      break;
    case RegExpInstruction::WRITE_LOOKBEHIND_TABLE:
      os << "WRITE_LOOKBEHIND_TABLE " << inst.payload.looktable_index;
      break;
    case RegExpInstruction::READ_LOOKBEHIND_TABLE:
      os << "READ_LOOKBEHIND_TABLE "
         << inst.payload.read_lookbehind.lookbehind_index() << " ("
         << (inst.payload.read_lookbehind.is_positive() ? "positive"
                                                        : "negative")
         << ")";
      break;
  }
  return os;
}

namespace {

// The maximum number of digits required to display a non-negative number < n
// in base 10.
int DigitsRequiredBelow(int n) {
  DCHECK_GE(n, 0);

  int result = 1;
  for (int i = 10; i < n; i *= 10) {
    result += 1;
  }
  return result;
}

}  // namespace

std::ostream& operator<<(std::ostream& os,
                         base::Vector<const RegExpInstruction> insts) {
  int inst_num = insts.length();
  int line_digit_num = DigitsRequiredBelow(inst_num);

  for (int i = 0; i != inst_num; ++i) {
    const RegExpInstruction& inst = insts[i];
    os << std::setfill('0') << std::setw(line_digit_num) << i << ": " << inst
       << std::endl;
  }
  return os;
}

}  // namespace internal
}  // namespace v8
