// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-decoder.h"

#include <iomanip>
#include <vector>

#include "src/init/v8.h"
#include "src/objects/contexts.h"
#include "src/runtime/runtime.h"
#include "test/unittests/interpreter/bytecode-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

#define B(Name) static_cast<uint8_t>(Bytecode::k##Name)

TEST(BytecodeDecoder, DecodeBytecodeAndOperands) {
  struct BytecodesAndResult {
    const uint8_t bytecode[32];
    const size_t length;
    const char* output;
  };

  const BytecodesAndResult cases[] = {
      {{B(LdaSmi), U8(1)}, 2, "            LdaSmi [1]"},
      {{B(Wide), B(LdaSmi), U16(1000)}, 4, "      LdaSmi.Wide [1000]"},
      {{B(ExtraWide), B(LdaSmi), U32(100000)}, 6, "LdaSmi.ExtraWide [100000]"},
      {{B(LdaSmi), U8(-1)}, 2, "            LdaSmi [-1]"},
      {{B(Wide), B(LdaSmi), U16(-1000)}, 4, "      LdaSmi.Wide [-1000]"},
      {{B(ExtraWide), B(LdaSmi), U32(-100000)},
       6,
       "LdaSmi.ExtraWide [-100000]"},
      {{B(Star), R8(5)}, 2, "            Star r5"},
      {{B(Wide), B(Star), R16(136)}, 4, "      Star.Wide r136"},
      {{B(Wide), B(CallAnyReceiver), R16(134), R16(135), U16(10), U16(177)},
       10,
       "CallAnyReceiver.Wide r134, r135-r144, [177]"},
      {{B(ForInPrepare), R8(10), U8(11)},
       3,
       "         ForInPrepare r10-r12, [11]"},
      {{B(CallRuntime), U16(Runtime::FunctionId::kIsSmi), R8(0), U8(0)},
       5,
       "   CallRuntime [IsSmi], r0-r0"},
      {{B(Ldar),
        static_cast<uint8_t>(Register::FromParameterIndex(2).ToOperand())},
       2,
       "            Ldar a1"},
      {{B(Wide), B(CreateObjectLiteral), U16(513), U16(1027), U8(165)},
       7,
       "CreateObjectLiteral.Wide [513], [1027], #165"},
      {{B(ExtraWide), B(JumpIfNull), U32(123456789)},
       6,
       "JumpIfNull.ExtraWide [123456789]"},
      {{B(CallJSRuntime), U8(Context::BOOLEAN_FUNCTION_INDEX), R8(0), U8(0)},
       4,
       "      CallJSRuntime [boolean_function], r0-r0"}};

  for (size_t i = 0; i < arraysize(cases); ++i) {
    // Generate reference string by prepending formatted bytes.
    std::stringstream expected_ss;
    std::ios default_format(nullptr);
    default_format.copyfmt(expected_ss);
    // Match format of BytecodeDecoder::Decode() for byte representations.
    expected_ss.fill('0');
    expected_ss.flags(std::ios::right | std::ios::hex);
    for (size_t b = 0; b < cases[i].length; b++) {
      expected_ss << std::setw(2) << static_cast<uint32_t>(cases[i].bytecode[b])
                  << ' ';
    }
    expected_ss.copyfmt(default_format);
    expected_ss << cases[i].output;

    // Generate decoded byte output.
    std::stringstream actual_ss;
    BytecodeDecoder::Decode(actual_ss, cases[i].bytecode);

    // Compare.
    CHECK_EQ(actual_ss.str(), expected_ss.str());
  }
}

#undef B

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
