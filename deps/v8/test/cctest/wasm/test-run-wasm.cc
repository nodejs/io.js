// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/overflowing-math.h"
#include "src/utils/utils.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8::internal::wasm {

// for even shorter tests.
#define B1(a) WASM_BLOCK(a)
#define B2(a, b) WASM_BLOCK(a, b)
#define RET(x) x, kExprReturn
#define RET_I8(x) WASM_I32V_2(x), kExprReturn

WASM_EXEC_TEST(Int32Const) {
  WasmRunner<int32_t> r(execution_tier);
  const int32_t kExpectedValue = 0x11223344;
  // return(kExpectedValue)
  r.Build({WASM_I32V_5(kExpectedValue)});
  CHECK_EQ(kExpectedValue, r.Call());
}

WASM_EXEC_TEST(Int32Const_many) {
  FOR_INT32_INPUTS(i) {
    WasmRunner<int32_t> r(execution_tier);
    const int32_t kExpectedValue = i;
    // return(kExpectedValue)
    r.Build({WASM_I32V(kExpectedValue)});
    CHECK_EQ(kExpectedValue, r.Call());
  }
}

WASM_EXEC_TEST(GraphTrimming) {
  // This WebAssembly code requires graph trimming in the TurboFan compiler.
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({kExprLocalGet, 0, kExprLocalGet, 0, kExprLocalGet, 0, kExprI32RemS,
           kExprI32Eq, kExprLocalGet, 0, kExprI32DivS, kExprUnreachable});
  r.Call(1);
}

WASM_EXEC_TEST(Int32Param0) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // return(local[0])
  r.Build({WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Int32Param0_fallthru) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // local[0]
  r.Build({WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Int32Param1) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  // local[1]
  r.Build({WASM_LOCAL_GET(1)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(-111, i)); }
}

WASM_EXEC_TEST(Int32Add) {
  WasmRunner<int32_t> r(execution_tier);
  // 11 + 44
  r.Build({WASM_I32_ADD(WASM_I32V_1(11), WASM_I32V_1(44))});
  CHECK_EQ(55, r.Call());
}

WASM_EXEC_TEST(Int32Add_P) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // p0 + 13
  r.Build({WASM_I32_ADD(WASM_I32V_1(13), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(base::AddWithWraparound(i, 13), r.Call(i)); }
}

WASM_EXEC_TEST(Int32Add_P_fallthru) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // p0 + 13
  r.Build({WASM_I32_ADD(WASM_I32V_1(13), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(base::AddWithWraparound(i, 13), r.Call(i)); }
}

static void RunInt32AddTest(TestExecutionTier execution_tier,
                            const uint8_t* code, size_t size) {
  TestSignatures sigs;
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.builder().AddSignature(sigs.ii_v());
  r.builder().AddSignature(sigs.iii_v());
  r.Build(base::VectorOf(code, size));
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(i) +
                                              static_cast<uint32_t>(j));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(Int32Add_P2) {
  static const uint8_t code[] = {
      WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))};
  RunInt32AddTest(execution_tier, code, sizeof(code));
}

WASM_EXEC_TEST(Int32Add_block1) {
  static const uint8_t code[] = {
      WASM_BLOCK_X(1, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprI32Add};
  RunInt32AddTest(execution_tier, code, sizeof(code));
}

WASM_EXEC_TEST(Int32Add_block2) {
  static const uint8_t code[] = {
      WASM_BLOCK_X(1, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), kExprBr, DEPTH_0),
      kExprI32Add};
  RunInt32AddTest(execution_tier, code, sizeof(code));
}

WASM_EXEC_TEST(Int32Add_multi_if) {
  static const uint8_t code[] = {
      WASM_IF_ELSE_X(1, WASM_LOCAL_GET(0),
                     WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                     WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))),
      kExprI32Add};
  RunInt32AddTest(execution_tier, code, sizeof(code));
}

WASM_EXEC_TEST(Float32Add) {
  WasmRunner<int32_t> r(execution_tier);
  // int(11.5f + 44.5f)
  r.Build(
      {WASM_I32_SCONVERT_F32(WASM_F32_ADD(WASM_F32(11.5f), WASM_F32(44.5f)))});
  CHECK_EQ(56, r.Call());
}

WASM_EXEC_TEST(Float64Add) {
  WasmRunner<int32_t> r(execution_tier);
  // return int(13.5d + 43.5d)
  r.Build(
      {WASM_I32_SCONVERT_F64(WASM_F64_ADD(WASM_F64(13.5), WASM_F64(43.5)))});
  CHECK_EQ(57, r.Call());
}

// clang-format messes up the FOR_INT32_INPUTS macros.
// clang-format off
template<typename ctype>
static void TestInt32Binop(TestExecutionTier execution_tier, WasmOpcode opcode,
                           ctype(*expected)(ctype, ctype)) {
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      WasmRunner<ctype> r(execution_tier);
      // Apply {opcode} on two constants.
      r.Build({WASM_BINOP(opcode, WASM_I32V(i), WASM_I32V(j))});
      CHECK_EQ(expected(i, j), r.Call());
    }
  }
  {
    WasmRunner<ctype, ctype, ctype> r(execution_tier);
    // Apply {opcode} on two parameters.
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        CHECK_EQ(expected(i, j), r.Call(i, j));
      }
    }
  }
  FOR_INT32_INPUTS(i) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on constant and parameter.
    r.Build({WASM_BINOP(opcode, WASM_I32V(i), WASM_LOCAL_GET(0))});
    FOR_INT32_INPUTS(j) {
      CHECK_EQ(expected(i, j), r.Call(j));
    }
  }
  FOR_INT32_INPUTS(j) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on parameter and constant.
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_I32V(j))});
    FOR_INT32_INPUTS(i) {
      CHECK_EQ(expected(i, j), r.Call(i));
    }
  }
  auto to_bool = [](ctype value) -> ctype {
    return value == static_cast<ctype>(0xDEADBEEF) ? value : !!value;
  };
  FOR_INT32_INPUTS(i) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on constant and parameter, followed by {if}.
    r.Build({WASM_IF(WASM_BINOP(opcode, WASM_I32V(i), WASM_LOCAL_GET(0)),
                     WASM_RETURN(WASM_ONE)),
             WASM_ZERO});
    FOR_INT32_INPUTS(j) {
      CHECK_EQ(to_bool(expected(i, j)), r.Call(j));
    }
  }
  FOR_INT32_INPUTS(j) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on parameter and constant, followed by {if}.
    r.Build({WASM_IF(WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_I32V(j)),
                     WASM_RETURN(WASM_ONE)),
             WASM_ZERO});
    FOR_INT32_INPUTS(i) {
      CHECK_EQ(to_bool(expected(i, j)), r.Call(i));
    }
  }
  FOR_INT32_INPUTS(i) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on constant and parameter, followed by {br_if}.
    r.Build({WASM_BR_IFD(0, WASM_ONE,
                         WASM_BINOP(opcode, WASM_I32V(i), WASM_LOCAL_GET(0))),
             WASM_ZERO});
    FOR_INT32_INPUTS(j) {
      CHECK_EQ(to_bool(expected(i, j)), r.Call(j));
    }
  }
  FOR_INT32_INPUTS(j) {
    WasmRunner<ctype, ctype> r(execution_tier);
    // Apply {opcode} on parameter and constant, followed by {br_if}.
    r.Build({WASM_BR_IFD(0, WASM_ONE,
                         WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_I32V(j))),
             WASM_ZERO});
    FOR_INT32_INPUTS(i) {
      CHECK_EQ(to_bool(expected(i, j)), r.Call(i));
    }
  }
}
// clang-format on

#define WASM_I32_BINOP_TEST(expr, ctype, expected)                             \
  WASM_EXEC_TEST(I32Binop_##expr) {                                            \
    TestInt32Binop<ctype>(execution_tier, kExprI32##expr,                      \
                          [](ctype a, ctype b) -> ctype { return expected; }); \
  }

WASM_I32_BINOP_TEST(Add, int32_t, base::AddWithWraparound(a, b))
WASM_I32_BINOP_TEST(Sub, int32_t, base::SubWithWraparound(a, b))
WASM_I32_BINOP_TEST(Mul, int32_t, base::MulWithWraparound(a, b))
WASM_I32_BINOP_TEST(DivS, int32_t,
                    (a == kMinInt && b == -1) || b == 0
                        ? static_cast<int32_t>(0xDEADBEEF)
                        : a / b)
WASM_I32_BINOP_TEST(DivU, uint32_t, b == 0 ? 0xDEADBEEF : a / b)
WASM_I32_BINOP_TEST(RemS, int32_t, b == 0 ? 0xDEADBEEF : b == -1 ? 0 : a % b)
WASM_I32_BINOP_TEST(RemU, uint32_t, b == 0 ? 0xDEADBEEF : a % b)
WASM_I32_BINOP_TEST(And, int32_t, a& b)
WASM_I32_BINOP_TEST(Ior, int32_t, a | b)
WASM_I32_BINOP_TEST(Xor, int32_t, a ^ b)
WASM_I32_BINOP_TEST(Shl, int32_t, base::ShlWithWraparound(a, b))
WASM_I32_BINOP_TEST(ShrU, uint32_t, a >> (b & 0x1F))
WASM_I32_BINOP_TEST(ShrS, int32_t, a >> (b & 0x1F))
WASM_I32_BINOP_TEST(Ror, uint32_t, (a >> (b & 0x1F)) | (a << ((32 - b) & 0x1F)))
WASM_I32_BINOP_TEST(Rol, uint32_t, (a << (b & 0x1F)) | (a >> ((32 - b) & 0x1F)))
WASM_I32_BINOP_TEST(Eq, int32_t, a == b)
WASM_I32_BINOP_TEST(Ne, int32_t, a != b)
WASM_I32_BINOP_TEST(LtS, int32_t, a < b)
WASM_I32_BINOP_TEST(LeS, int32_t, a <= b)
WASM_I32_BINOP_TEST(LtU, uint32_t, a < b)
WASM_I32_BINOP_TEST(LeU, uint32_t, a <= b)
WASM_I32_BINOP_TEST(GtS, int32_t, a > b)
WASM_I32_BINOP_TEST(GeS, int32_t, a >= b)
WASM_I32_BINOP_TEST(GtU, uint32_t, a > b)
WASM_I32_BINOP_TEST(GeU, uint32_t, a >= b)

#undef WASM_I32_BINOP_TEST

void TestInt32Unop(TestExecutionTier execution_tier, WasmOpcode opcode,
                   int32_t expected, int32_t a) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return op K
    r.Build({WASM_UNOP(opcode, WASM_I32V(a))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    // return op a
    r.Build({WASM_UNOP(opcode, WASM_LOCAL_GET(0))});
    CHECK_EQ(expected, r.Call(a));
  }
}

WASM_EXEC_TEST(Int32Clz) {
  TestInt32Unop(execution_tier, kExprI32Clz, 0, 0x80001000);
  TestInt32Unop(execution_tier, kExprI32Clz, 1, 0x40000500);
  TestInt32Unop(execution_tier, kExprI32Clz, 2, 0x20000300);
  TestInt32Unop(execution_tier, kExprI32Clz, 3, 0x10000003);
  TestInt32Unop(execution_tier, kExprI32Clz, 4, 0x08050000);
  TestInt32Unop(execution_tier, kExprI32Clz, 5, 0x04006000);
  TestInt32Unop(execution_tier, kExprI32Clz, 6, 0x02000000);
  TestInt32Unop(execution_tier, kExprI32Clz, 7, 0x010000A0);
  TestInt32Unop(execution_tier, kExprI32Clz, 8, 0x00800C00);
  TestInt32Unop(execution_tier, kExprI32Clz, 9, 0x00400000);
  TestInt32Unop(execution_tier, kExprI32Clz, 10, 0x0020000D);
  TestInt32Unop(execution_tier, kExprI32Clz, 11, 0x00100F00);
  TestInt32Unop(execution_tier, kExprI32Clz, 12, 0x00080000);
  TestInt32Unop(execution_tier, kExprI32Clz, 13, 0x00041000);
  TestInt32Unop(execution_tier, kExprI32Clz, 14, 0x00020020);
  TestInt32Unop(execution_tier, kExprI32Clz, 15, 0x00010300);
  TestInt32Unop(execution_tier, kExprI32Clz, 16, 0x00008040);
  TestInt32Unop(execution_tier, kExprI32Clz, 17, 0x00004005);
  TestInt32Unop(execution_tier, kExprI32Clz, 18, 0x00002050);
  TestInt32Unop(execution_tier, kExprI32Clz, 19, 0x00001700);
  TestInt32Unop(execution_tier, kExprI32Clz, 20, 0x00000870);
  TestInt32Unop(execution_tier, kExprI32Clz, 21, 0x00000405);
  TestInt32Unop(execution_tier, kExprI32Clz, 22, 0x00000203);
  TestInt32Unop(execution_tier, kExprI32Clz, 23, 0x00000101);
  TestInt32Unop(execution_tier, kExprI32Clz, 24, 0x00000089);
  TestInt32Unop(execution_tier, kExprI32Clz, 25, 0x00000041);
  TestInt32Unop(execution_tier, kExprI32Clz, 26, 0x00000022);
  TestInt32Unop(execution_tier, kExprI32Clz, 27, 0x00000013);
  TestInt32Unop(execution_tier, kExprI32Clz, 28, 0x00000008);
  TestInt32Unop(execution_tier, kExprI32Clz, 29, 0x00000004);
  TestInt32Unop(execution_tier, kExprI32Clz, 30, 0x00000002);
  TestInt32Unop(execution_tier, kExprI32Clz, 31, 0x00000001);
  TestInt32Unop(execution_tier, kExprI32Clz, 32, 0x00000000);
}

WASM_EXEC_TEST(Int32Ctz) {
  TestInt32Unop(execution_tier, kExprI32Ctz, 32, 0x00000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 31, 0x80000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 30, 0x40000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 29, 0x20000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 28, 0x10000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 27, 0xA8000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 26, 0xF4000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 25, 0x62000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 24, 0x91000000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 23, 0xCD800000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 22, 0x09400000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 21, 0xAF200000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 20, 0xAC100000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 19, 0xE0B80000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 18, 0x9CE40000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 17, 0xC7920000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 16, 0xB8F10000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 15, 0x3B9F8000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 14, 0xDB4C4000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 13, 0xE9A32000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 12, 0xFCA61000);
  TestInt32Unop(execution_tier, kExprI32Ctz, 11, 0x6C8A7800);
  TestInt32Unop(execution_tier, kExprI32Ctz, 10, 0x8CE5A400);
  TestInt32Unop(execution_tier, kExprI32Ctz, 9, 0xCB7D0200);
  TestInt32Unop(execution_tier, kExprI32Ctz, 8, 0xCB4DC100);
  TestInt32Unop(execution_tier, kExprI32Ctz, 7, 0xDFBEC580);
  TestInt32Unop(execution_tier, kExprI32Ctz, 6, 0x27A9DB40);
  TestInt32Unop(execution_tier, kExprI32Ctz, 5, 0xDE3BCB20);
  TestInt32Unop(execution_tier, kExprI32Ctz, 4, 0xD7E8A610);
  TestInt32Unop(execution_tier, kExprI32Ctz, 3, 0x9AFDBC88);
  TestInt32Unop(execution_tier, kExprI32Ctz, 2, 0x9AFDBC84);
  TestInt32Unop(execution_tier, kExprI32Ctz, 1, 0x9AFDBC82);
  TestInt32Unop(execution_tier, kExprI32Ctz, 0, 0x9AFDBC81);
}

WASM_EXEC_TEST(Int32Popcnt) {
  TestInt32Unop(execution_tier, kExprI32Popcnt, 32, 0xFFFFFFFF);
  TestInt32Unop(execution_tier, kExprI32Popcnt, 0, 0x00000000);
  TestInt32Unop(execution_tier, kExprI32Popcnt, 1, 0x00008000);
  TestInt32Unop(execution_tier, kExprI32Popcnt, 13, 0x12345678);
  TestInt32Unop(execution_tier, kExprI32Popcnt, 19, 0xFEDCBA09);
}

WASM_EXEC_TEST(I32Eqz) {
  TestInt32Unop(execution_tier, kExprI32Eqz, 0, 1);
  TestInt32Unop(execution_tier, kExprI32Eqz, 0, -1);
  TestInt32Unop(execution_tier, kExprI32Eqz, 0, -827343);
  TestInt32Unop(execution_tier, kExprI32Eqz, 0, 8888888);
  TestInt32Unop(execution_tier, kExprI32Eqz, 1, 0);
}


WASM_EXEC_TEST(Int32DivS_trap) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_I32_DIVS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(0, r.Call(0, 100));
  CHECK_TRAP(r.Call(100, 0));
  CHECK_TRAP(r.Call(-1001, 0));
  CHECK_TRAP(r.Call(kMin, -1));
  CHECK_TRAP(r.Call(kMin, 0));
}

WASM_EXEC_TEST(Int32RemS_trap) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_I32_REMS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(33, r.Call(133, 100));
  CHECK_EQ(0, r.Call(kMin, -1));
  CHECK_TRAP(r.Call(100, 0));
  CHECK_TRAP(r.Call(-1001, 0));
  CHECK_TRAP(r.Call(kMin, 0));
}

WASM_EXEC_TEST(Int32DivU_trap) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_I32_DIVU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(0, r.Call(0, 100));
  CHECK_EQ(0, r.Call(kMin, -1));
  CHECK_TRAP(r.Call(100, 0));
  CHECK_TRAP(r.Call(-1001, 0));
  CHECK_TRAP(r.Call(kMin, 0));
}

WASM_EXEC_TEST(Int32RemU_trap) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_I32_REMU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  CHECK_EQ(17, r.Call(217, 100));
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_TRAP(r.Call(100, 0));
  CHECK_TRAP(r.Call(-1001, 0));
  CHECK_TRAP(r.Call(kMin, 0));
  CHECK_EQ(kMin, r.Call(kMin, -1));
}

WASM_EXEC_TEST(Int32DivS_byzero_const) {
  for (int8_t denom = -2; denom < 8; ++denom) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    r.Build({WASM_I32_DIVS(WASM_LOCAL_GET(0), WASM_I32V_1(denom))});
    for (int32_t val = -7; val < 8; ++val) {
      if (denom == 0) {
        CHECK_TRAP(r.Call(val));
      } else {
        CHECK_EQ(val / denom, r.Call(val));
      }
    }
  }
}

WASM_EXEC_TEST(Int32DivU_byzero_const) {
  for (uint32_t denom = 0xFFFFFFFE; denom < 8; ++denom) {
    WasmRunner<uint32_t, uint32_t> r(execution_tier);
    r.Build({WASM_I32_DIVU(WASM_LOCAL_GET(0), WASM_I32V_1(denom))});

    for (uint32_t val = 0xFFFFFFF0; val < 8; ++val) {
      if (denom == 0) {
        CHECK_TRAP(r.Call(val));
      } else {
        CHECK_EQ(val / denom, r.Call(val));
      }
    }
  }
}

WASM_EXEC_TEST(Int32DivS_trap_effect) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);

  r.Build({WASM_IF_ELSE_I(
      WASM_LOCAL_GET(0),
      WASM_I32_DIVS(WASM_BLOCK_I(WASM_STORE_MEM(MachineType::Int8(), WASM_ZERO,
                                                WASM_LOCAL_GET(0)),
                                 WASM_LOCAL_GET(0)),
                    WASM_LOCAL_GET(1)),
      WASM_I32_DIVS(WASM_BLOCK_I(WASM_STORE_MEM(MachineType::Int8(), WASM_ZERO,
                                                WASM_LOCAL_GET(0)),
                                 WASM_LOCAL_GET(0)),
                    WASM_LOCAL_GET(1)))});
  CHECK_EQ(0, r.Call(0, 100));
  CHECK_TRAP(r.Call(8, 0));
  CHECK_TRAP(r.Call(4, 0));
  CHECK_TRAP(r.Call(0, 0));
}

void TestFloat32Binop(TestExecutionTier execution_tier, WasmOpcode opcode,
                      int32_t expected, float a, float b) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return K op K
    r.Build({WASM_BINOP(opcode, WASM_F32(a), WASM_F32(b))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, float, float> r(execution_tier);
    // return a op b
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

void TestFloat32BinopWithConvert(TestExecutionTier execution_tier,
                                 WasmOpcode opcode, int32_t expected, float a,
                                 float b) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return int(K op K)
    r.Build(
        {WASM_I32_SCONVERT_F32(WASM_BINOP(opcode, WASM_F32(a), WASM_F32(b)))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, float, float> r(execution_tier);
    // return int(a op b)
    r.Build({WASM_I32_SCONVERT_F32(
        WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

void TestFloat32UnopWithConvert(TestExecutionTier execution_tier,
                                WasmOpcode opcode, int32_t expected, float a) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return int(op(K))
    r.Build({WASM_I32_SCONVERT_F32(WASM_UNOP(opcode, WASM_F32(a)))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, float> r(execution_tier);
    // return int(op(a))
    r.Build({WASM_I32_SCONVERT_F32(WASM_UNOP(opcode, WASM_LOCAL_GET(0)))});
    CHECK_EQ(expected, r.Call(a));
  }
}

void TestFloat64Binop(TestExecutionTier execution_tier, WasmOpcode opcode,
                      int32_t expected, double a, double b) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return K op K
    r.Build({WASM_BINOP(opcode, WASM_F64(a), WASM_F64(b))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, double, double> r(execution_tier);
    // return a op b
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

void TestFloat64BinopWithConvert(TestExecutionTier execution_tier,
                                 WasmOpcode opcode, int32_t expected, double a,
                                 double b) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return int(K op K)
    r.Build(
        {WASM_I32_SCONVERT_F64(WASM_BINOP(opcode, WASM_F64(a), WASM_F64(b)))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, double, double> r(execution_tier);
    r.Build({WASM_I32_SCONVERT_F64(
        WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

void TestFloat64UnopWithConvert(TestExecutionTier execution_tier,
                                WasmOpcode opcode, int32_t expected, double a) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return int(op(K))
    r.Build({WASM_I32_SCONVERT_F64(WASM_UNOP(opcode, WASM_F64(a)))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, double> r(execution_tier);
    // return int(op(a))
    r.Build({WASM_I32_SCONVERT_F64(WASM_UNOP(opcode, WASM_LOCAL_GET(0)))});
    CHECK_EQ(expected, r.Call(a));
  }
}

WASM_EXEC_TEST(Float32Binops) {
  TestFloat32Binop(execution_tier, kExprF32Eq, 1, 8.125f, 8.125f);
  TestFloat32Binop(execution_tier, kExprF32Ne, 1, 8.125f, 8.127f);
  TestFloat32Binop(execution_tier, kExprF32Lt, 1, -9.5f, -9.0f);
  TestFloat32Binop(execution_tier, kExprF32Le, 1, -1111.0f, -1111.0f);
  TestFloat32Binop(execution_tier, kExprF32Gt, 1, -9.0f, -9.5f);
  TestFloat32Binop(execution_tier, kExprF32Ge, 1, -1111.0f, -1111.0f);

  TestFloat32BinopWithConvert(execution_tier, kExprF32Add, 10, 3.5f, 6.5f);
  TestFloat32BinopWithConvert(execution_tier, kExprF32Sub, 2, 44.5f, 42.5f);
  TestFloat32BinopWithConvert(execution_tier, kExprF32Mul, -66, -132.1f, 0.5f);
  TestFloat32BinopWithConvert(execution_tier, kExprF32Div, 11, 22.1f, 2.0f);
}

WASM_EXEC_TEST(Float32Unops) {
  TestFloat32UnopWithConvert(execution_tier, kExprF32Abs, 8, 8.125f);
  TestFloat32UnopWithConvert(execution_tier, kExprF32Abs, 9, -9.125f);
  TestFloat32UnopWithConvert(execution_tier, kExprF32Neg, -213, 213.125f);
  TestFloat32UnopWithConvert(execution_tier, kExprF32Sqrt, 12, 144.4f);
}

WASM_EXEC_TEST(Float64Binops) {
  TestFloat64Binop(execution_tier, kExprF64Eq, 1, 16.25, 16.25);
  TestFloat64Binop(execution_tier, kExprF64Ne, 1, 16.25, 16.15);
  TestFloat64Binop(execution_tier, kExprF64Lt, 1, -32.4, 11.7);
  TestFloat64Binop(execution_tier, kExprF64Le, 1, -88.9, -88.9);
  TestFloat64Binop(execution_tier, kExprF64Gt, 1, 11.7, -32.4);
  TestFloat64Binop(execution_tier, kExprF64Ge, 1, -88.9, -88.9);

  TestFloat64BinopWithConvert(execution_tier, kExprF64Add, 100, 43.5, 56.5);
  TestFloat64BinopWithConvert(execution_tier, kExprF64Sub, 200, 12200.1,
                              12000.1);
  TestFloat64BinopWithConvert(execution_tier, kExprF64Mul, -33, 134, -0.25);
  TestFloat64BinopWithConvert(execution_tier, kExprF64Div, -1111, -2222.3, 2);
}

WASM_EXEC_TEST(Float64Unops) {
  TestFloat64UnopWithConvert(execution_tier, kExprF64Abs, 108, 108.125);
  TestFloat64UnopWithConvert(execution_tier, kExprF64Abs, 209, -209.125);
  TestFloat64UnopWithConvert(execution_tier, kExprF64Neg, -209, 209.125);
  TestFloat64UnopWithConvert(execution_tier, kExprF64Sqrt, 13, 169.4);
}

WASM_EXEC_TEST(Float32Neg) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_NEG(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    CHECK_EQ(0x80000000,
             base::bit_cast<uint32_t>(i) ^ base::bit_cast<uint32_t>(r.Call(i)));
  }
}

WASM_EXEC_TEST(Float64Neg) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_NEG(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    CHECK_EQ(0x8000000000000000,
             base::bit_cast<uint64_t>(i) ^ base::bit_cast<uint64_t>(r.Call(i)));
  }
}

WASM_EXEC_TEST(IfElse_P) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // if (p0) return 11; else return 22;
  r.Build({WASM_IF_ELSE_I(WASM_LOCAL_GET(0),   // --
                          WASM_I32V_1(11),     // --
                          WASM_I32V_1(22))});  // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(If_empty1) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOCAL_GET(0), kExprIf, kVoidCode, kExprEnd, WASM_LOCAL_GET(1)});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i - 9, i)); }
}

WASM_EXEC_TEST(IfElse_empty1) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOCAL_GET(0), kExprIf, kVoidCode, kExprElse, kExprEnd,
           WASM_LOCAL_GET(1)});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i - 8, i)); }
}

WASM_EXEC_TEST(IfElse_empty2) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOCAL_GET(0), kExprIf, kVoidCode, WASM_NOP, kExprElse, kExprEnd,
           WASM_LOCAL_GET(1)});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i - 7, i)); }
}

WASM_EXEC_TEST(IfElse_empty3) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOCAL_GET(0), kExprIf, kVoidCode, kExprElse, WASM_NOP, kExprEnd,
           WASM_LOCAL_GET(1)});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i - 6, i)); }
}

WASM_EXEC_TEST(If_chain1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // if (p0) 13; if (p0) 14; 15
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_NOP),
           WASM_IF(WASM_LOCAL_GET(0), WASM_NOP), WASM_I32V_1(15)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(15, r.Call(i)); }
}

WASM_EXEC_TEST(If_chain_set) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  // if (p0) p1 = 73; if (p0) p1 = 74; p1
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_LOCAL_SET(1, WASM_I32V_2(73))),
           WASM_IF(WASM_LOCAL_GET(0), WASM_LOCAL_SET(1, WASM_I32V_2(74))),
           WASM_LOCAL_GET(1)});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 74 : i;
    CHECK_EQ(expected, r.Call(i, i));
  }
}

WASM_EXEC_TEST(IfElse_Unreachable1) {
  WasmRunner<int32_t> r(execution_tier);
  // 0 ? unreachable : 27
  r.Build({WASM_IF_ELSE_I(WASM_ZERO,           // --
                          WASM_UNREACHABLE,    // --
                          WASM_I32V_1(27))});  // --
  CHECK_EQ(27, r.Call());
}

WASM_EXEC_TEST(IfElse_Unreachable2) {
  WasmRunner<int32_t> r(execution_tier);
  // 1 ? 28 : unreachable
  r.Build({WASM_IF_ELSE_I(WASM_I32V_1(1),       // --
                          WASM_I32V_1(28),      // --
                          WASM_UNREACHABLE)});  // --
  CHECK_EQ(28, r.Call());
}

WASM_EXEC_TEST(Return12) {
  WasmRunner<int32_t> r(execution_tier);

  r.Build({RET_I8(12)});
  CHECK_EQ(12, r.Call());
}

WASM_EXEC_TEST(Return17) {
  WasmRunner<int32_t> r(execution_tier);

  r.Build({WASM_BLOCK(RET_I8(17)), WASM_ZERO});
  CHECK_EQ(17, r.Call());
}

WASM_EXEC_TEST(Return_I32) {
  WasmRunner<int32_t, int32_t> r(execution_tier);

  r.Build({RET(WASM_LOCAL_GET(0))});

  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Return_F32) {
  WasmRunner<float, float> r(execution_tier);

  r.Build({RET(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    float expect = i;
    float result = r.Call(expect);
    if (std::isnan(expect)) {
      CHECK(std::isnan(result));
    } else {
      CHECK_EQ(expect, result);
    }
  }
}

WASM_EXEC_TEST(Return_F64) {
  WasmRunner<double, double> r(execution_tier);

  r.Build({RET(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    double expect = i;
    double result = r.Call(expect);
    if (std::isnan(expect)) {
      CHECK(std::isnan(result));
    } else {
      CHECK_EQ(expect, result);
    }
  }
}

WASM_EXEC_TEST(Select_float_parameters) {
  WasmRunner<float, float, float, int32_t> r(execution_tier);
  r.Build(
      {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), WASM_LOCAL_GET(2))});
  CHECK_FLOAT_EQ(2.0f, r.Call(2.0f, 1.0f, 1));
}

WASM_EXEC_TEST(Select_s128_parameters) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* g0 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g1 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* output = r.builder().AddGlobal<int32_t>(kWasmS128);
  // select(v128(0, 1, 2, 3), v128(4, 5, 6, 7), 1) == v128(0, 1, 2, 3)
  for (int i = 0; i < 4; i++) {
    LANE(g0, i) = i;
    LANE(g1, i) = i + 4;
  }
  r.Build(
      {WASM_GLOBAL_SET(2, WASM_SELECT(WASM_GLOBAL_GET(0), WASM_GLOBAL_GET(1),
                                      WASM_LOCAL_GET(0))),
       WASM_ONE});
  r.Call(1);
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(i, LANE(output, i));
  }
}

WASM_EXEC_TEST(SelectWithType_float_parameters) {
  WasmRunner<float, float, float, int32_t> r(execution_tier);
  r.Build(
      {WASM_SELECT_F(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), WASM_LOCAL_GET(2))});
  CHECK_FLOAT_EQ(2.0f, r.Call(2.0f, 1.0f, 1));
  CHECK_FLOAT_EQ(1.0f, r.Call(2.0f, 1.0f, 0));
}

WASM_EXEC_TEST(Select_double_parameters) {
  WasmRunner<double, double, double, int32_t> r(execution_tier);
  r.Build(
      {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), WASM_LOCAL_GET(2))});
  CHECK_FLOAT_EQ(2.0f, r.Call(2.0f, 1.0f, 1));
  CHECK_FLOAT_EQ(1.0f, r.Call(2.0f, 1.0f, 0));
}

WASM_EXEC_TEST(SelectWithType_double_parameters) {
  WasmRunner<double, double, double, int32_t> r(execution_tier);
  r.Build(
      {WASM_SELECT_D(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), WASM_LOCAL_GET(2))});
  CHECK_FLOAT_EQ(2.0f, r.Call(2.0f, 1.0f, 1));
  CHECK_FLOAT_EQ(1.0f, r.Call(2.0f, 1.0f, 0));
}

WASM_EXEC_TEST(Select) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // return select(11, 22, a);
  r.Build({WASM_SELECT(WASM_I32V_1(11), WASM_I32V_1(22), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(SelectWithType) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // return select(11, 22, a);
  r.Build({WASM_SELECT_I(WASM_I32V_1(11), WASM_I32V_1(22), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select_strict1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // select(a=0, a=1, a=2); return a
  r.Build({WASM_SELECT(WASM_LOCAL_TEE(0, WASM_ZERO),
                       WASM_LOCAL_TEE(0, WASM_I32V_1(1)),
                       WASM_LOCAL_TEE(0, WASM_I32V_1(2))),
           WASM_DROP, WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(2, r.Call(i)); }
}

WASM_EXEC_TEST(SelectWithType_strict1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // select(a=0, a=1, a=2); return a
  r.Build({WASM_SELECT_I(WASM_LOCAL_TEE(0, WASM_ZERO),
                         WASM_LOCAL_TEE(0, WASM_I32V_1(1)),
                         WASM_LOCAL_TEE(0, WASM_I32V_1(2))),
           WASM_DROP, WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(2, r.Call(i)); }
}

WASM_EXEC_TEST(Select_strict2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmI32);
  // select(b=5, c=6, a)
  r.Build({WASM_SELECT(WASM_LOCAL_TEE(1, WASM_I32V_1(5)),
                       WASM_LOCAL_TEE(2, WASM_I32V_1(6)), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 5 : 6;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(SelectWithType_strict2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmI32);
  // select(b=5, c=6, a)
  r.Build(
      {WASM_SELECT_I(WASM_LOCAL_TEE(1, WASM_I32V_1(5)),
                     WASM_LOCAL_TEE(2, WASM_I32V_1(6)), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 5 : 6;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select_strict3) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmI32);
  // select(b=5, c=6, a=b)
  r.Build({WASM_SELECT(WASM_LOCAL_TEE(1, WASM_I32V_1(5)),
                       WASM_LOCAL_TEE(2, WASM_I32V_1(6)),
                       WASM_LOCAL_TEE(0, WASM_LOCAL_GET(1)))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = 5;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(SelectWithType_strict3) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmI32);
  // select(b=5, c=6, a=b)
  r.Build({WASM_SELECT_I(WASM_LOCAL_TEE(1, WASM_I32V_1(5)),
                         WASM_LOCAL_TEE(2, WASM_I32V_1(6)),
                         WASM_LOCAL_TEE(0, WASM_LOCAL_GET(1)))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = 5;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select64) {
  WasmRunner<int64_t, int32_t> r(execution_tier);
  // return select(11, 22, a);
  r.Build({WASM_SELECT(WASM_I64V_1(11), WASM_I64V_1(22), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int64_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select64WithType) {
  WasmRunner<int64_t, int32_t> r(execution_tier);
  // return select(11, 22, a);
  r.Build({WASM_SELECT_L(WASM_I64V_1(11), WASM_I64V_1(22), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int64_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select64_strict1) {
  WasmRunner<int64_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI64);
  r.AllocateLocal(kWasmI64);
  // select(b=5, c=6, a)
  r.Build({WASM_SELECT(WASM_LOCAL_TEE(1, WASM_I64V_1(5)),
                       WASM_LOCAL_TEE(2, WASM_I64V_1(6)), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int64_t expected = i ? 5 : 6;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Select64WithType_strict1) {
  WasmRunner<int64_t, int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI64);
  r.AllocateLocal(kWasmI64);
  // select(b=5, c=6, a)
  r.Build(
      {WASM_SELECT_L(WASM_LOCAL_TEE(1, WASM_I64V_1(5)),
                     WASM_LOCAL_TEE(2, WASM_I64V_1(6)), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) {
    int64_t expected = i ? 5 : 6;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(BrIf_strict) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(
      WASM_BRV_IF(0, WASM_LOCAL_GET(0), WASM_LOCAL_TEE(0, WASM_I32V_2(99))))});

  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Br_height) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(
      WASM_BLOCK(WASM_BRV_IFD(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(0)),
                 WASM_RETURN(WASM_I32V_1(9))),
      WASM_BRV(0, WASM_I32V_1(8)))});

  for (int32_t i = 0; i < 5; i++) {
    int32_t expected = i != 0 ? 8 : 9;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Regression_660262) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({kExprI32Const, 0x00, kExprI32Const, 0x00, kExprI32LoadMem, 0x00,
           0x0F, kExprBrTable, 0x00, 0x80, 0x00});  // entries=0
  r.Call();
}

WASM_EXEC_TEST(BrTable0a) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 0, BR_TARGET(0)))),
           WASM_I32V_2(91)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(91, r.Call(i)); }
}

WASM_EXEC_TEST(BrTable0b) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {B1(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 1, BR_TARGET(0), BR_TARGET(0)))),
       WASM_I32V_2(92)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(92, r.Call(i)); }
}

WASM_EXEC_TEST(BrTable0c) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(B2(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 1, BR_TARGET(0),
                                  BR_TARGET(1))),
                 RET_I8(76))),
           WASM_I32V_2(77)});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i == 0 ? 76 : 77;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(BrTable1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 0, BR_TARGET(0))), RET_I8(93)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(93, r.Call(i)); }
}

WASM_EXEC_TEST(BrTable_loop) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {B2(B1(WASM_LOOP(WASM_BR_TABLE(WASM_INC_LOCAL_BYV(0, 1), 2, BR_TARGET(2),
                                     BR_TARGET(1), BR_TARGET(0)))),
          RET_I8(99)),
       WASM_I32V_2(98)});
  CHECK_EQ(99, r.Call(0));
  CHECK_EQ(98, r.Call(-1));
  CHECK_EQ(98, r.Call(-2));
  CHECK_EQ(98, r.Call(-3));
  CHECK_EQ(98, r.Call(-100));
}

WASM_EXEC_TEST(BrTable_br) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {B2(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 1, BR_TARGET(1), BR_TARGET(0))),
          RET_I8(91)),
       WASM_I32V_2(99)});
  CHECK_EQ(99, r.Call(0));
  CHECK_EQ(91, r.Call(1));
  CHECK_EQ(91, r.Call(2));
  CHECK_EQ(91, r.Call(3));
}

WASM_EXEC_TEST(BrTable_br2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);

  r.Build({B2(B2(B2(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 3, BR_TARGET(1),
                                     BR_TARGET(2), BR_TARGET(3), BR_TARGET(0))),
                    RET_I8(85)),
                 RET_I8(86)),
              RET_I8(87)),
           WASM_I32V_2(88)});
  CHECK_EQ(86, r.Call(0));
  CHECK_EQ(87, r.Call(1));
  CHECK_EQ(88, r.Call(2));
  CHECK_EQ(85, r.Call(3));
  CHECK_EQ(85, r.Call(4));
  CHECK_EQ(85, r.Call(5));
}

WASM_EXEC_TEST(BrTable4) {
  for (int i = 0; i < 4; ++i) {
    for (int t = 0; t < 4; ++t) {
      uint32_t cases[] = {0, 1, 2, 3};
      cases[i] = t;

      WasmRunner<int32_t, int32_t> r(execution_tier);
      r.Build({B2(B2(B2(B2(B1(WASM_BR_TABLE(
                               WASM_LOCAL_GET(0), 3, BR_TARGET(cases[0]),
                               BR_TARGET(cases[1]), BR_TARGET(cases[2]),
                               BR_TARGET(cases[3]))),
                           RET_I8(70)),
                        RET_I8(71)),
                     RET_I8(72)),
                  RET_I8(73)),
               WASM_I32V_2(75)});

      for (int x = -3; x < 50; ++x) {
        int index = (x > 3 || x < 0) ? 3 : x;
        int32_t expected = 70 + cases[index];
        CHECK_EQ(expected, r.Call(x));
      }
    }
  }
}

WASM_EXEC_TEST(BrTable4x4) {
  for (uint8_t a = 0; a < 4; ++a) {
    for (uint8_t b = 0; b < 4; ++b) {
      for (uint8_t c = 0; c < 4; ++c) {
        for (uint8_t d = 0; d < 4; ++d) {
          for (int i = 0; i < 4; ++i) {
            uint32_t cases[] = {a, b, c, d};

            WasmRunner<int32_t, int32_t> r(execution_tier);
            r.Build({B2(B2(B2(B2(B1(WASM_BR_TABLE(
                                     WASM_LOCAL_GET(0), 3, BR_TARGET(cases[0]),
                                     BR_TARGET(cases[1]), BR_TARGET(cases[2]),
                                     BR_TARGET(cases[3]))),
                                 RET_I8(50)),
                              RET_I8(51)),
                           RET_I8(52)),
                        RET_I8(53)),
                     WASM_I32V_2(55)});

            for (int x = -6; x < 47; ++x) {
              int index = (x > 3 || x < 0) ? 3 : x;
              int32_t expected = 50 + cases[index];
              CHECK_EQ(expected, r.Call(x));
            }
          }
        }
      }
    }
  }
}

WASM_EXEC_TEST(BrTable4_fallthru) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build(
      {B2(B2(B2(B2(B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 3, BR_TARGET(0),
                                    BR_TARGET(1), BR_TARGET(2), BR_TARGET(3))),
                   WASM_INC_LOCAL_BY(1, 1)),
                WASM_INC_LOCAL_BY(1, 2)),
             WASM_INC_LOCAL_BY(1, 4)),
          WASM_INC_LOCAL_BY(1, 8)),
       WASM_LOCAL_GET(1)});

  CHECK_EQ(15, r.Call(0, 0));
  CHECK_EQ(14, r.Call(1, 0));
  CHECK_EQ(12, r.Call(2, 0));
  CHECK_EQ(8, r.Call(3, 0));
  CHECK_EQ(8, r.Call(4, 0));

  CHECK_EQ(115, r.Call(0, 100));
  CHECK_EQ(114, r.Call(1, 100));
  CHECK_EQ(112, r.Call(2, 100));
  CHECK_EQ(108, r.Call(3, 100));
  CHECK_EQ(108, r.Call(4, 100));
}

WASM_EXEC_TEST(BrTable_loop_target) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {WASM_LOOP_I(WASM_BLOCK(WASM_BR_TABLE(WASM_LOCAL_GET(0), 2, BR_TARGET(0),
                                            BR_TARGET(1), BR_TARGET(1))),
                   WASM_ONE)});

  CHECK_EQ(1, r.Call(0));
}

WASM_EXEC_TEST(I32ReinterpretF32) {
  WasmRunner<int32_t> r(execution_tier);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));

  r.Build({WASM_I32_REINTERPRET_F32(
      WASM_LOAD_MEM(MachineType::Float32(), WASM_ZERO))});

  FOR_FLOAT32_INPUTS(i) {
    float input = i;
    int32_t expected = base::bit_cast<int32_t, float>(input);
    r.builder().WriteMemory(&memory[0], input);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(F32ReinterpretI32) {
  WasmRunner<float> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));

  r.Build({WASM_F32_REINTERPRET_I32(
      WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO))});

  FOR_INT32_INPUTS(i) {
    int32_t input = i;
    float expected = base::bit_cast<float, int32_t>(input);
    r.builder().WriteMemory(&memory[0], input);
    float result = r.Call();
    if (std::isnan(expected)) {
      CHECK(std::isnan(result));
      CHECK(IsSameNan(expected, result));
    } else {
      CHECK_EQ(expected, result);
    }
  }
}

// Do not run this test in a simulator because of signalling NaN issues on ia32.
#ifndef USE_SIMULATOR

WASM_EXEC_TEST(SignallingNanSurvivesI32ReinterpretF32) {
  WasmRunner<int32_t> r(execution_tier);

  r.Build({WASM_I32_REINTERPRET_F32(
      WASM_SEQ(kExprF32Const, 0x00, 0x00, 0xA0, 0x7F))});

  // This is a signalling nan.
  CHECK_EQ(0x7FA00000, r.Call());
}

#endif

WASM_EXEC_TEST(LoadMaxUint32Offset) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);

  r.Build({WASM_LOAD_MEM_OFFSET(MachineType::Int32(),  // type
                                U32V_5(0xFFFFFFFF),    // offset
                                WASM_ZERO)});          // index

  CHECK_TRAP32(r.Call());
}

WASM_EXEC_TEST(LoadStoreLoad) {
  WasmRunner<int32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));

  r.Build({WASM_STORE_MEM(MachineType::Int32(), WASM_ZERO,
                          WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO)),
           WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO)});

  FOR_INT32_INPUTS(i) {
    int32_t expected = i;
    r.builder().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(UnalignedFloat32Load) {
  WasmRunner<float> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_LOAD_MEM_ALIGNMENT(MachineType::Float32(), WASM_ONE, 2)});
  r.Call();
}

WASM_EXEC_TEST(UnalignedFloat64Load) {
  WasmRunner<double> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_LOAD_MEM_ALIGNMENT(MachineType::Float64(), WASM_ONE, 3)});
  r.Call();
}

WASM_EXEC_TEST(UnalignedInt32Load) {
  WasmRunner<uint32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_LOAD_MEM_ALIGNMENT(MachineType::Int32(), WASM_ONE, 2)});
  r.Call();
}

WASM_EXEC_TEST(UnalignedInt32Store) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_SEQ(WASM_STORE_MEM_ALIGNMENT(MachineType::Int32(), WASM_ONE, 2,
                                             WASM_I32V_1(1)),
                    WASM_I32V_1(12))});
  r.Call();
}

WASM_EXEC_TEST(UnalignedFloat32Store) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_SEQ(WASM_STORE_MEM_ALIGNMENT(MachineType::Float32(), WASM_ONE,
                                             2, WASM_F32(1.0)),
                    WASM_I32V_1(12))});
  r.Call();
}

WASM_EXEC_TEST(UnalignedFloat64Store) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build({WASM_SEQ(WASM_STORE_MEM_ALIGNMENT(MachineType::Float64(), WASM_ONE,
                                             3, WASM_F64(1.0)),
                    WASM_I32V_1(12))});
  r.Call();
}

WASM_EXEC_TEST(VoidReturn1) {
  const int32_t kExpected = -414444;
  WasmRunner<int32_t> r(execution_tier);

  // Build the test function.
  WasmFunctionCompiler& test_func = r.NewFunction<void>();
  test_func.Build({kExprNop});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION0(test_func.function_index()),
           WASM_I32V_3(kExpected)});

  // Call and check.
  int32_t result = r.Call();
  CHECK_EQ(kExpected, result);
}

WASM_EXEC_TEST(VoidReturn2) {
  const int32_t kExpected = -414444;
  WasmRunner<int32_t> r(execution_tier);

  // Build the test function.
  WasmFunctionCompiler& test_func = r.NewFunction<void>();
  test_func.Build({WASM_RETURN0});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION0(test_func.function_index()),
           WASM_I32V_3(kExpected)});

  // Call and check.
  int32_t result = r.Call();
  CHECK_EQ(kExpected, result);
}

WASM_EXEC_TEST(BrEmpty) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BRV(0, WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(BrIfEmpty) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BRV_IF(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_empty) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({kExprBlock, kVoidCode, kExprEnd, WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_empty_br1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(WASM_BR(0)), WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_empty_brif1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK(WASM_BR_IF(0, WASM_ZERO)), WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_empty_brif2) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_BLOCK(WASM_BR_IF(0, WASM_LOCAL_GET(1))), WASM_LOCAL_GET(0)});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i, i + 1)); }
}

WASM_EXEC_TEST(Block_i) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_f) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_BLOCK_F(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_d) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_BLOCK_D(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Block_br2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV(0, WASM_LOCAL_GET(0)))});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, static_cast<uint32_t>(r.Call(i))); }
}

WASM_EXEC_TEST(Block_If_P) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // block { if (p0) break 51; 52; }
  r.Build({WASM_BLOCK_I(                      // --
      WASM_IF(WASM_LOCAL_GET(0),              // --
              WASM_BRV(1, WASM_I32V_1(51))),  // --
      WASM_I32V_1(52))});                     // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 51 : 52;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Loop_empty) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({kExprLoop, kVoidCode, kExprEnd, WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_i) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_LOOP_I(WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_f) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_LOOP_F(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_d) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_LOOP_D(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_empty_br1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(WASM_LOOP(WASM_BR(1))), WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_empty_brif1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(WASM_LOOP(WASM_BR_IF(1, WASM_ZERO))), WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(Loop_empty_brif2) {
  WasmRunner<uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOOP_I(WASM_BRV_IF(1, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(i, r.Call(i, i + 1)); }
}

WASM_EXEC_TEST(Loop_empty_brif3) {
  WasmRunner<uint32_t, uint32_t, uint32_t, uint32_t> r(execution_tier);
  r.Build({WASM_LOOP(WASM_BRV_IFD(1, WASM_LOCAL_GET(2), WASM_LOCAL_GET(0))),
           WASM_LOCAL_GET(1)});
  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      CHECK_EQ(i, r.Call(0, i, j));
      CHECK_EQ(j, r.Call(1, i, j));
    }
  }
}

WASM_EXEC_TEST(Block_BrIf_P) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV_IFD(0, WASM_I32V_1(51), WASM_LOCAL_GET(0)),
                        WASM_I32V_1(52))});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 51 : 52;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Block_IfElse_P_assign) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // { if (p0) p0 = 71; else p0 = 72; return p0; }
  r.Build({WASM_IF_ELSE(WASM_LOCAL_GET(0),                    // --
                        WASM_LOCAL_SET(0, WASM_I32V_2(71)),   // --
                        WASM_LOCAL_SET(0, WASM_I32V_2(72))),  // --
           WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 71 : 72;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Block_IfElse_P_return) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // if (p0) return 81; else return 82;
  r.Build({WASM_IF_ELSE(WASM_LOCAL_GET(0),  // --
                        RET_I8(81),         // --
                        RET_I8(82)),        // --
           WASM_ZERO});                     // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 81 : 82;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Block_If_P_assign) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // { if (p0) p0 = 61; p0; }
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_LOCAL_SET(0, WASM_I32V_1(61))),
           WASM_LOCAL_GET(0)});
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 61 : i;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(DanglingAssign) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // { return 0; p0 = 0; }
  r.Build({WASM_BLOCK_I(RET_I8(99), WASM_LOCAL_TEE(0, WASM_ZERO))});
  CHECK_EQ(99, r.Call(1));
}

WASM_EXEC_TEST(ExprIf_P) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // p0 ? 11 : 22;
  r.Build({WASM_IF_ELSE_I(WASM_LOCAL_GET(0),   // --
                          WASM_I32V_1(11),     // --
                          WASM_I32V_1(22))});  // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = i ? 11 : 22;
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(CountDown) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_LOOP(WASM_IF(WASM_LOCAL_GET(0),
                             WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0),
                                                            WASM_I32V_1(1))),
                             WASM_BR(1))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(0, r.Call(1));
  CHECK_EQ(0, r.Call(10));
  CHECK_EQ(0, r.Call(100));
}

WASM_EXEC_TEST(CountDown_fallthru) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {WASM_LOOP(
           WASM_IF(WASM_NOT(WASM_LOCAL_GET(0)), WASM_BRV(2, WASM_LOCAL_GET(0))),
           WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(1))),
           WASM_CONTINUE(0)),
       WASM_LOCAL_GET(0)});
  CHECK_EQ(0, r.Call(1));
  CHECK_EQ(0, r.Call(10));
  CHECK_EQ(0, r.Call(100));
}

WASM_EXEC_TEST(WhileCountDown) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_WHILE(WASM_LOCAL_GET(0),
                      WASM_LOCAL_SET(
                          0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(1)))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(0, r.Call(1));
  CHECK_EQ(0, r.Call(10));
  CHECK_EQ(0, r.Call(100));
}

WASM_EXEC_TEST(Loop_if_break1) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_LOOP(WASM_IF(WASM_LOCAL_GET(0), WASM_BRV(2, WASM_LOCAL_GET(1))),
                     WASM_LOCAL_SET(0, WASM_I32V_2(99))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(99, r.Call(0, 11));
  CHECK_EQ(65, r.Call(3, 65));
  CHECK_EQ(10001, r.Call(10000, 10001));
  CHECK_EQ(-29, r.Call(-28, -29));
}

WASM_EXEC_TEST(Loop_if_break2) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_LOOP(WASM_BRV_IF(1, WASM_LOCAL_GET(1), WASM_LOCAL_GET(0)),
                     WASM_DROP, WASM_LOCAL_SET(0, WASM_I32V_2(99))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(99, r.Call(0, 33));
  CHECK_EQ(3, r.Call(1, 3));
  CHECK_EQ(10000, r.Call(99, 10000));
  CHECK_EQ(-29, r.Call(-11, -29));
}

WASM_EXEC_TEST(Loop_if_break_fallthru) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(WASM_LOOP(WASM_IF(WASM_LOCAL_GET(0), WASM_BR(2)),
                        WASM_LOCAL_SET(0, WASM_I32V_2(93)))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(93, r.Call(0));
  CHECK_EQ(3, r.Call(3));
  CHECK_EQ(10001, r.Call(10001));
  CHECK_EQ(-22, r.Call(-22));
}

WASM_EXEC_TEST(Loop_if_break_fallthru2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({B1(B1(WASM_LOOP(WASM_IF(WASM_LOCAL_GET(0), WASM_BR(2)),
                           WASM_LOCAL_SET(0, WASM_I32V_2(93))))),
           WASM_LOCAL_GET(0)});
  CHECK_EQ(93, r.Call(0));
  CHECK_EQ(3, r.Call(3));
  CHECK_EQ(10001, r.Call(10001));
  CHECK_EQ(-22, r.Call(-22));
}

WASM_EXEC_TEST(IfBreak1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_SEQ(WASM_BR(0), WASM_UNREACHABLE)),
           WASM_I32V_2(91)});
  CHECK_EQ(91, r.Call(0));
  CHECK_EQ(91, r.Call(1));
  CHECK_EQ(91, r.Call(-8734));
}

WASM_EXEC_TEST(IfBreak2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_SEQ(WASM_BR(0), RET_I8(77))),
           WASM_I32V_2(81)});
  CHECK_EQ(81, r.Call(0));
  CHECK_EQ(81, r.Call(1));
  CHECK_EQ(81, r.Call(-8734));
}

WASM_EXEC_TEST(LoadMemI32) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().RandomizeMemory(1111);

  r.Build({WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO)});

  r.builder().WriteMemory(&memory[0], 99999999);
  CHECK_EQ(99999999, r.Call(0));

  r.builder().WriteMemory(&memory[0], 88888888);
  CHECK_EQ(88888888, r.Call(0));

  r.builder().WriteMemory(&memory[0], 77777777);
  CHECK_EQ(77777777, r.Call(0));
}

WASM_EXEC_TEST(LoadMemI32_alignment) {
  for (uint8_t alignment = 0; alignment <= 2; ++alignment) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* memory =
        r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    r.builder().RandomizeMemory(1111);

    r.Build(
        {WASM_LOAD_MEM_ALIGNMENT(MachineType::Int32(), WASM_ZERO, alignment)});

    r.builder().WriteMemory(&memory[0], 0x1A2B3C4D);
    CHECK_EQ(0x1A2B3C4D, r.Call(0));

    r.builder().WriteMemory(&memory[0], 0x5E6F7A8B);
    CHECK_EQ(0x5E6F7A8B, r.Call(0));

    r.builder().WriteMemory(&memory[0], 0x7CA0B1C2);
    CHECK_EQ(0x7CA0B1C2, r.Call(0));
  }
}

WASM_EXEC_TEST(LoadMemI32_oob) {
  WasmRunner<int32_t, uint32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().RandomizeMemory(1111);

  r.Build({WASM_LOAD_MEM(MachineType::Int32(), WASM_LOCAL_GET(0))});

  r.builder().WriteMemory(&memory[0], 88888888);
  CHECK_EQ(88888888, r.Call(0u));
  for (uint32_t offset = kWasmPageSize - 3; offset < kWasmPageSize + 40;
       ++offset) {
    CHECK_TRAP(r.Call(offset));
  }

  for (uint32_t offset = 0x80000000; offset < 0x80000010; ++offset) {
    CHECK_TRAP(r.Call(offset));
  }
}

WASM_EXEC_TEST(LoadMem_offset_oob) {
  static const MachineType machineTypes[] = {
      MachineType::Int8(),   MachineType::Uint8(),  MachineType::Int16(),
      MachineType::Uint16(), MachineType::Int32(),  MachineType::Uint32(),
      MachineType::Int64(),  MachineType::Uint64(), MachineType::Float32(),
      MachineType::Float64()};

  constexpr size_t num_bytes = kWasmPageSize;

  for (size_t m = 0; m < arraysize(machineTypes); ++m) {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<uint8_t>(num_bytes);
    r.builder().RandomizeMemory(1116 + static_cast<int>(m));

    constexpr uint8_t kOffset = 8;
    uint32_t boundary = num_bytes - kOffset - machineTypes[m].MemSize();

    r.Build({WASM_LOAD_MEM_OFFSET(machineTypes[m], kOffset, WASM_LOCAL_GET(0)),
             WASM_DROP, WASM_ZERO});

    CHECK_EQ(0, r.Call(boundary));  // in bounds.

    for (uint32_t offset = boundary + 1; offset < boundary + 19; ++offset) {
      CHECK_TRAP(r.Call(offset));  // out of bounds.
    }
  }
}

WASM_EXEC_TEST(LoadMemI32_offset) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().RandomizeMemory(1111);

  r.Build({WASM_LOAD_MEM_OFFSET(MachineType::Int32(), 4, WASM_LOCAL_GET(0))});

  r.builder().WriteMemory(&memory[0], 66666666);
  r.builder().WriteMemory(&memory[1], 77777777);
  r.builder().WriteMemory(&memory[2], 88888888);
  r.builder().WriteMemory(&memory[3], 99999999);
  CHECK_EQ(77777777, r.Call(0));
  CHECK_EQ(88888888, r.Call(4));
  CHECK_EQ(99999999, r.Call(8));

  r.builder().WriteMemory(&memory[0], 11111111);
  r.builder().WriteMemory(&memory[1], 22222222);
  r.builder().WriteMemory(&memory[2], 33333333);
  r.builder().WriteMemory(&memory[3], 44444444);
  CHECK_EQ(22222222, r.Call(0));
  CHECK_EQ(33333333, r.Call(4));
  CHECK_EQ(44444444, r.Call(8));
}

WASM_EXEC_TEST(LoadMemI32_const_oob_misaligned) {
  // This test accesses memory starting at kRunwayLength bytes before the end of
  // the memory until a few bytes beyond.
  constexpr uint8_t kRunwayLength = 12;
  // TODO(titzer): Fix misaligned accesses on MIPS and re-enable.
  for (uint8_t offset = 0; offset < kRunwayLength + 5; ++offset) {
    for (uint32_t index = kWasmPageSize - kRunwayLength;
         index < kWasmPageSize + 5; ++index) {
      WasmRunner<int32_t> r(execution_tier);
      r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
      r.builder().RandomizeMemory();

      r.Build({WASM_LOAD_MEM_OFFSET(MachineType::Int32(), offset,
                                    WASM_I32V_3(index))});

      if (offset + index + sizeof(int32_t) <= kWasmPageSize) {
        CHECK_EQ(r.builder().raw_val_at<int32_t>(offset + index), r.Call());
      } else {
        CHECK_TRAP(r.Call());
      }
    }
  }
}

WASM_EXEC_TEST(LoadMemI32_const_oob) {
  // This test accesses memory starting at kRunwayLength bytes before the end of
  // the memory until a few bytes beyond.
  constexpr uint8_t kRunwayLength = 24;
  for (uint8_t offset = 0; offset < kRunwayLength + 5; offset += 4) {
    for (uint32_t index = kWasmPageSize - kRunwayLength;
         index < kWasmPageSize + 5; index += 4) {
      WasmRunner<int32_t> r(execution_tier);
      r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
      r.builder().RandomizeMemory();

      r.Build({WASM_LOAD_MEM_OFFSET(MachineType::Int32(), offset,
                                    WASM_I32V_3(index))});

      if (offset + index + sizeof(int32_t) <= kWasmPageSize) {
        CHECK_EQ(r.builder().raw_val_at<int32_t>(offset + index), r.Call());
      } else {
        CHECK_TRAP(r.Call());
      }
    }
  }
}

WASM_EXEC_TEST(StoreMemI32_alignment) {
  const int32_t kWritten = 0x12345678;

  for (uint8_t i = 0; i <= 2; ++i) {
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* memory =
        r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    r.Build({WASM_STORE_MEM_ALIGNMENT(MachineType::Int32(), WASM_ZERO, i,
                                      WASM_LOCAL_GET(0)),
             WASM_LOCAL_GET(0)});
    r.builder().RandomizeMemory(1111);
    memory[0] = 0;

    CHECK_EQ(kWritten, r.Call(kWritten));
    CHECK_EQ(kWritten, r.builder().ReadMemory(&memory[0]));
  }
}

WASM_EXEC_TEST(StoreMemI32_offset) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  const int32_t kWritten = 0xAABBCCDD;

  r.Build({WASM_STORE_MEM_OFFSET(MachineType::Int32(), 4, WASM_LOCAL_GET(0),
                                 WASM_I32V_5(kWritten)),
           WASM_I32V_5(kWritten)});

  for (int i = 0; i < 2; ++i) {
    r.builder().RandomizeMemory(1111);
    r.builder().WriteMemory(&memory[0], 66666666);
    r.builder().WriteMemory(&memory[1], 77777777);
    r.builder().WriteMemory(&memory[2], 88888888);
    r.builder().WriteMemory(&memory[3], 99999999);
    CHECK_EQ(kWritten, r.Call(i * 4));
    CHECK_EQ(66666666, r.builder().ReadMemory(&memory[0]));
    CHECK_EQ(i == 0 ? kWritten : 77777777, r.builder().ReadMemory(&memory[1]));
    CHECK_EQ(i == 1 ? kWritten : 88888888, r.builder().ReadMemory(&memory[2]));
    CHECK_EQ(i == 2 ? kWritten : 99999999, r.builder().ReadMemory(&memory[3]));
  }
}

WASM_EXEC_TEST(StoreMem_offset_oob) {
  // 64-bit cases are handled in test-run-wasm-64.cc
  static const MachineType machineTypes[] = {
      MachineType::Int8(),    MachineType::Uint8(),  MachineType::Int16(),
      MachineType::Uint16(),  MachineType::Int32(),  MachineType::Uint32(),
      MachineType::Float32(), MachineType::Float64()};

  constexpr size_t num_bytes = kWasmPageSize;

  for (size_t m = 0; m < arraysize(machineTypes); ++m) {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(num_bytes);

    r.builder().RandomizeMemory(1119 + static_cast<int>(m));

    r.Build({WASM_STORE_MEM_OFFSET(machineTypes[m], 8, WASM_LOCAL_GET(0),
                                   WASM_LOAD_MEM(machineTypes[m], WASM_ZERO)),
             WASM_ZERO});

    uint8_t memsize = machineTypes[m].MemSize();
    uint32_t boundary = num_bytes - 8 - memsize;
    CHECK_EQ(0, r.Call(boundary));  // in bounds.
    CHECK_EQ(0, memcmp(&memory[0], &memory[8 + boundary], memsize));

    for (uint32_t offset = boundary + 1; offset < boundary + 19; ++offset) {
      CHECK_TRAP(r.Call(offset));  // out of bounds.
    }
  }
}

WASM_EXEC_TEST(Store_i32_narrowed) {
  constexpr uint8_t kOpcodes[] = {kExprI32StoreMem8, kExprI32StoreMem16,
                                  kExprI32StoreMem};
  int stored_size_in_bytes = 0;
  for (auto opcode : kOpcodes) {
    stored_size_in_bytes = std::max(1, stored_size_in_bytes * 2);
    constexpr int kBytes = 24;
    uint8_t expected_memory[kBytes] = {0};
    WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
    uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
    constexpr uint32_t kPattern = 0x12345678;

    r.Build({WASM_LOCAL_GET(0),                    // index
             WASM_LOCAL_GET(1),                    // value
             opcode, ZERO_ALIGNMENT, ZERO_OFFSET,  // store
             WASM_ZERO});                          // return value

    for (int i = 0; i <= kBytes - stored_size_in_bytes; ++i) {
      uint32_t pattern = base::bits::RotateLeft32(kPattern, i % 32);
      r.Call(i, pattern);
      for (int b = 0; b < stored_size_in_bytes; ++b) {
        expected_memory[i + b] = static_cast<uint8_t>(pattern >> (b * 8));
      }
      for (int w = 0; w < kBytes; ++w) {
        CHECK_EQ(expected_memory[w], memory[w]);
      }
    }
  }
}

WASM_EXEC_TEST(LoadMemI32_P) {
  const int kNumElems = 8;
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().RandomizeMemory(2222);

  r.Build({WASM_LOAD_MEM(MachineType::Int32(), WASM_LOCAL_GET(0))});

  for (int i = 0; i < kNumElems; ++i) {
    CHECK_EQ(r.builder().ReadMemory(&memory[i]), r.Call(i * 4));
  }
}

WASM_EXEC_TEST(MemI32_Sum) {
  const int kNumElems = 20;
  WasmRunner<uint32_t, int32_t> r(execution_tier);
  uint32_t* memory =
      r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(int32_t));
  const uint8_t kSum = r.AllocateLocal(kWasmI32);

  r.Build(
      {WASM_WHILE(
           WASM_LOCAL_GET(0),
           WASM_BLOCK(WASM_LOCAL_SET(
                          kSum, WASM_I32_ADD(WASM_LOCAL_GET(kSum),
                                             WASM_LOAD_MEM(MachineType::Int32(),
                                                           WASM_LOCAL_GET(0)))),
                      WASM_LOCAL_SET(
                          0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(4))))),
       WASM_LOCAL_GET(1)});

  // Run 4 trials.
  for (int i = 0; i < 3; ++i) {
    r.builder().RandomizeMemory(i * 33);
    uint32_t expected = 0;
    for (size_t j = kNumElems - 1; j > 0; --j) {
      expected += r.builder().ReadMemory(&memory[j]);
    }
    uint32_t result = r.Call(4 * (kNumElems - 1));
    CHECK_EQ(expected, result);
  }
}

WASM_EXEC_TEST(CheckMachIntsZero) {
  const int kNumElems = 55;
  WasmRunner<uint32_t, int32_t> r(execution_tier);
  r.builder().AddMemoryElems<uint32_t>(kWasmPageSize / sizeof(uint32_t));

  r.Build({
      // clang-format off
      kExprLoop, kVoidCode,
        kExprLocalGet, 0,
        kExprIf, kVoidCode,
          kExprLocalGet, 0,
          kExprI32LoadMem, 0, 0,
          kExprIf, kVoidCode,
            kExprI32Const, 127,
            kExprReturn,
          kExprEnd,
          kExprLocalGet, 0,
          kExprI32Const, 4,
          kExprI32Sub,
          kExprLocalTee, 0,
          kExprBr, DEPTH_0,
        kExprEnd,
      kExprEnd,
      kExprI32Const, 0
      // clang-format on
  });

  r.builder().BlankMemory();
  CHECK_EQ(0, r.Call((kNumElems - 1) * 4));
}

WASM_EXEC_TEST(MemF32_Sum) {
  const int kSize = 5;
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  float* buffer = r.builder().raw_mem_start<float>();
  r.builder().WriteMemory(&buffer[0], -99.25f);
  r.builder().WriteMemory(&buffer[1], -888.25f);
  r.builder().WriteMemory(&buffer[2], -77.25f);
  r.builder().WriteMemory(&buffer[3], 66666.25f);
  r.builder().WriteMemory(&buffer[4], 5555.25f);
  const uint8_t kSum = r.AllocateLocal(kWasmF32);

  r.Build(
      {WASM_WHILE(WASM_LOCAL_GET(0),
                  WASM_BLOCK(WASM_LOCAL_SET(
                                 kSum, WASM_F32_ADD(
                                           WASM_LOCAL_GET(kSum),
                                           WASM_LOAD_MEM(MachineType::Float32(),
                                                         WASM_LOCAL_GET(0)))),
                             WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0),
                                                            WASM_I32V_1(4))))),
       WASM_STORE_MEM(MachineType::Float32(), WASM_ZERO, WASM_LOCAL_GET(kSum)),
       WASM_LOCAL_GET(0)});

  CHECK_EQ(0, r.Call(4 * (kSize - 1)));
  CHECK_NE(-99.25f, r.builder().ReadMemory(&buffer[0]));
  CHECK_EQ(71256.0f, r.builder().ReadMemory(&buffer[0]));
}

template <typename T>
T GenerateAndRunFold(TestExecutionTier execution_tier, WasmOpcode binop,
                     T* buffer, uint32_t size, ValueType astType,
                     MachineType memType) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  T* memory = r.builder().AddMemoryElems<T>(static_cast<uint32_t>(
      RoundUp(size * sizeof(T), kWasmPageSize) / sizeof(sizeof(T))));
  for (uint32_t i = 0; i < size; ++i) {
    r.builder().WriteMemory(&memory[i], buffer[i]);
  }
  const uint8_t kAccum = r.AllocateLocal(astType);

  r.Build(
      {WASM_LOCAL_SET(kAccum, WASM_LOAD_MEM(memType, WASM_ZERO)),
       WASM_WHILE(
           WASM_LOCAL_GET(0),
           WASM_BLOCK(WASM_LOCAL_SET(
                          kAccum, WASM_BINOP(binop, WASM_LOCAL_GET(kAccum),
                                             WASM_LOAD_MEM(memType,
                                                           WASM_LOCAL_GET(0)))),
                      WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0),
                                                     WASM_I32V_1(sizeof(T)))))),
       WASM_STORE_MEM(memType, WASM_ZERO, WASM_LOCAL_GET(kAccum)),
       WASM_LOCAL_GET(0)});
  r.Call(static_cast<int>(sizeof(T) * (size - 1)));
  return r.builder().ReadMemory(&memory[0]);
}

WASM_EXEC_TEST(MemF64_Mul) {
  const size_t kSize = 6;
  double buffer[kSize] = {1, 2, 2, 2, 2, 2};
  double result =
      GenerateAndRunFold<double>(execution_tier, kExprF64Mul, buffer, kSize,
                                 kWasmF64, MachineType::Float64());
  CHECK_EQ(32, result);
}

WASM_EXEC_TEST(Build_Wasm_Infinite_Loop) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Only build the graph and compile, don't run.
  r.Build({WASM_INFINITE_LOOP, WASM_ZERO});
}

WASM_EXEC_TEST(Build_Wasm_Infinite_Loop_effect) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);

  // Only build the graph and compile, don't run.
  r.Build({WASM_LOOP(WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO), WASM_DROP),
           WASM_ZERO});
}

WASM_EXEC_TEST(Unreachable0a) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV(0, WASM_I32V_1(9)), RET(WASM_LOCAL_GET(0)))});
  CHECK_EQ(9, r.Call(0));
  CHECK_EQ(9, r.Call(1));
}

WASM_EXEC_TEST(Unreachable0b) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV(0, WASM_I32V_1(7)), WASM_UNREACHABLE)});
  CHECK_EQ(7, r.Call(0));
  CHECK_EQ(7, r.Call(1));
}

WASM_COMPILED_EXEC_TEST(Build_Wasm_Unreachable1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_UNREACHABLE});
}

WASM_COMPILED_EXEC_TEST(Build_Wasm_Unreachable2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_UNREACHABLE, WASM_UNREACHABLE});
}

WASM_COMPILED_EXEC_TEST(Build_Wasm_Unreachable3) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_UNREACHABLE, WASM_UNREACHABLE, WASM_UNREACHABLE});
}

WASM_COMPILED_EXEC_TEST(Build_Wasm_UnreachableIf1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_UNREACHABLE,
           WASM_IF(WASM_LOCAL_GET(0), WASM_SEQ(WASM_LOCAL_GET(0), WASM_DROP)),
           WASM_ZERO});
}

WASM_COMPILED_EXEC_TEST(Build_Wasm_UnreachableIf2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {WASM_UNREACHABLE,
       WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_UNREACHABLE)});
}

WASM_EXEC_TEST(Unreachable_Load) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.builder().AddMemory(kWasmPageSize);
  r.Build(
      {WASM_BLOCK_I(WASM_BRV(0, WASM_LOCAL_GET(0)),
                    WASM_LOAD_MEM(MachineType::Int8(), WASM_LOCAL_GET(0)))});
  CHECK_EQ(11, r.Call(11));
  CHECK_EQ(21, r.Call(21));
}

WASM_EXEC_TEST(BrV_Fallthrough) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BLOCK(WASM_BRV(1, WASM_I32V_1(42))),
                        WASM_I32V_1(22))});
  CHECK_EQ(42, r.Call());
}

WASM_EXEC_TEST(Infinite_Loop_not_taken1) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_IF(WASM_LOCAL_GET(0), WASM_INFINITE_LOOP), WASM_I32V_1(45)});
  // Run the code, but don't go into the infinite loop.
  CHECK_EQ(45, r.Call(0));
}

WASM_EXEC_TEST(Infinite_Loop_not_taken2) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(
      WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_BRV(1, WASM_I32V_1(45)),
                   WASM_INFINITE_LOOP),
      WASM_ZERO)});
  // Run the code, but don't go into the infinite loop.
  CHECK_EQ(45, r.Call(1));
}

WASM_EXEC_TEST(Infinite_Loop_not_taken2_brif) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV_IF(0, WASM_I32V_1(45), WASM_LOCAL_GET(0)),
                        WASM_INFINITE_LOOP)});
  // Run the code, but don't go into the infinite loop.
  CHECK_EQ(45, r.Call(1));
}

static void TestBuildGraphForSimpleExpression(WasmOpcode opcode) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  Zone zone(isolate->allocator(), ZONE_NAME, kCompressGraphZone);
  HandleScope scope(isolate);
  // TODO(ahaas): Enable this test for externref opcodes when code generation
  // for them is implemented.
  if (WasmOpcodes::IsExternRefOpcode(opcode)) return;
  // Enable all optional operators.
  compiler::CommonOperatorBuilder common(&zone);
  compiler::MachineOperatorBuilder machine(
      &zone, MachineType::PointerRepresentation(),
      compiler::MachineOperatorBuilder::kAllOptionalOps);
  compiler::Graph graph(&zone);
  compiler::JSGraph jsgraph(isolate, &graph, &common, nullptr, nullptr,
                            &machine);
  const FunctionSig* sig = WasmOpcodes::Signature(opcode);
  CompilationEnv env = CompilationEnv::NoModuleAllFeatures();

  if (sig->parameter_count() == 1) {
    uint8_t code[] = {WASM_NO_LOCALS, kExprLocalGet, 0,
                      static_cast<uint8_t>(opcode), WASM_END};
    TestBuildingGraph(&zone, &jsgraph, &env, sig, nullptr, code,
                      code + arraysize(code));
  } else {
    CHECK_EQ(2, sig->parameter_count());
    uint8_t code[] = {WASM_NO_LOCALS,
                      kExprLocalGet,
                      0,
                      kExprLocalGet,
                      1,
                      static_cast<uint8_t>(opcode),
                      WASM_END};
    TestBuildingGraph(&zone, &jsgraph, &env, sig, nullptr, code,
                      code + arraysize(code));
  }
}

TEST(Build_Wasm_SimpleExprs) {
// Test that the decoder can build a graph for all supported simple expressions.
#define GRAPH_BUILD_TEST(name, ...) \
  TestBuildGraphForSimpleExpression(kExpr##name);

  FOREACH_SIMPLE_OPCODE(GRAPH_BUILD_TEST);

#undef GRAPH_BUILD_TEST
}

WASM_EXEC_TEST(Int32LoadInt8_signext) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  const int kNumElems = kWasmPageSize;
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(kNumElems);
  r.builder().RandomizeMemory();
  memory[0] = -1;
  r.Build({WASM_LOAD_MEM(MachineType::Int8(), WASM_LOCAL_GET(0))});

  for (int i = 0; i < kNumElems; ++i) {
    CHECK_EQ(memory[i], r.Call(i));
  }
}

WASM_EXEC_TEST(Int32LoadInt8_zeroext) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  const int kNumElems = kWasmPageSize;
  uint8_t* memory = r.builder().AddMemory(kNumElems);
  r.builder().RandomizeMemory(77);
  memory[0] = 255;
  r.Build({WASM_LOAD_MEM(MachineType::Uint8(), WASM_LOCAL_GET(0))});

  for (int i = 0; i < kNumElems; ++i) {
    CHECK_EQ(memory[i], r.Call(i));
  }
}

WASM_EXEC_TEST(Int32LoadInt16_signext) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  const int kNumBytes = kWasmPageSize;
  uint8_t* memory = r.builder().AddMemory(kNumBytes);
  r.builder().RandomizeMemory(888);
  memory[1] = 200;
  r.Build({WASM_LOAD_MEM(MachineType::Int16(), WASM_LOCAL_GET(0))});

  for (int i = 0; i < kNumBytes; i += 2) {
    int32_t expected = static_cast<int16_t>(memory[i] | (memory[i + 1] << 8));
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Int32LoadInt16_zeroext) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  const int kNumBytes = kWasmPageSize;
  uint8_t* memory = r.builder().AddMemory(kNumBytes);
  r.builder().RandomizeMemory(9999);
  memory[1] = 204;
  r.Build({WASM_LOAD_MEM(MachineType::Uint16(), WASM_LOCAL_GET(0))});

  for (int i = 0; i < kNumBytes; i += 2) {
    int32_t expected = memory[i] | (memory[i + 1] << 8);
    CHECK_EQ(expected, r.Call(i));
  }
}

WASM_EXEC_TEST(Int32Global) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int32_t* global = r.builder().AddGlobal<int32_t>();
  // global = global + p0
  r.Build(
      {WASM_GLOBAL_SET(0, WASM_I32_ADD(WASM_GLOBAL_GET(0), WASM_LOCAL_GET(0))),
       WASM_ZERO});

  *global = 116;
  for (int i = 9; i < 444444; i += 111111) {
    int32_t expected = *global + i;
    r.Call(i);
    CHECK_EQ(expected, *global);
  }
}

WASM_EXEC_TEST(Int32Globals_DontAlias) {
  const int kNumGlobals = 3;
  for (int g = 0; g < kNumGlobals; ++g) {
    // global = global + p0
    WasmRunner<int32_t, int32_t> r(execution_tier);
    int32_t* globals[] = {r.builder().AddGlobal<int32_t>(),
                          r.builder().AddGlobal<int32_t>(),
                          r.builder().AddGlobal<int32_t>()};

    r.Build({WASM_GLOBAL_SET(
                 g, WASM_I32_ADD(WASM_GLOBAL_GET(g), WASM_LOCAL_GET(0))),
             WASM_GLOBAL_GET(g)});

    // Check that reading/writing global number {g} doesn't alter the others.
    *(globals[g]) = 116 * g;
    int32_t before[kNumGlobals];
    for (int i = 9; i < 444444; i += 111113) {
      int32_t sum = *(globals[g]) + i;
      for (int j = 0; j < kNumGlobals; ++j) before[j] = *(globals[j]);
      int32_t result = r.Call(i);
      CHECK_EQ(sum, result);
      for (int j = 0; j < kNumGlobals; ++j) {
        int32_t expected = j == g ? sum : before[j];
        CHECK_EQ(expected, *(globals[j]));
      }
    }
  }
}

WASM_EXEC_TEST(Float32Global) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  float* global = r.builder().AddGlobal<float>();
  // global = global + p0
  r.Build({WASM_GLOBAL_SET(
               0, WASM_F32_ADD(WASM_GLOBAL_GET(0),
                               WASM_F32_SCONVERT_I32(WASM_LOCAL_GET(0)))),
           WASM_ZERO});

  *global = 1.25;
  for (int i = 9; i < 4444; i += 1111) {
    volatile float expected = *global + i;
    r.Call(i);
    CHECK_EQ(expected, *global);
  }
}

WASM_EXEC_TEST(Float64Global) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  double* global = r.builder().AddGlobal<double>();
  // global = global + p0
  r.Build({WASM_GLOBAL_SET(
               0, WASM_F64_ADD(WASM_GLOBAL_GET(0),
                               WASM_F64_SCONVERT_I32(WASM_LOCAL_GET(0)))),
           WASM_ZERO});

  *global = 1.25;
  for (int i = 9; i < 4444; i += 1111) {
    volatile double expected = *global + i;
    r.Call(i);
    CHECK_EQ(expected, *global);
  }
}

WASM_EXEC_TEST(MixedGlobals) {
  WasmRunner<int32_t, int32_t> r(execution_tier);

  int32_t* unused = r.builder().AddGlobal<int32_t>();
  uint8_t* memory = r.builder().AddMemory(kWasmPageSize);

  int32_t* var_int32 = r.builder().AddGlobal<int32_t>();
  uint32_t* var_uint32 = r.builder().AddGlobal<uint32_t>();
  float* var_float = r.builder().AddGlobal<float>();
  double* var_double = r.builder().AddGlobal<double>();

  r.Build({WASM_GLOBAL_SET(1, WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO)),
           WASM_GLOBAL_SET(2, WASM_LOAD_MEM(MachineType::Uint32(), WASM_ZERO)),
           WASM_GLOBAL_SET(3, WASM_LOAD_MEM(MachineType::Float32(), WASM_ZERO)),
           WASM_GLOBAL_SET(4, WASM_LOAD_MEM(MachineType::Float64(), WASM_ZERO)),
           WASM_ZERO});

  memory[0] = 0xAA;
  memory[1] = 0xCC;
  memory[2] = 0x55;
  memory[3] = 0xEE;
  memory[4] = 0x33;
  memory[5] = 0x22;
  memory[6] = 0x11;
  memory[7] = 0x99;
  r.Call(1);

  CHECK_EQ(static_cast<int32_t>(0xEE55CCAA), *var_int32);
  CHECK_EQ(static_cast<uint32_t>(0xEE55CCAA), *var_uint32);
  CHECK_EQ(base::bit_cast<float>(0xEE55CCAA), *var_float);
  CHECK_EQ(base::bit_cast<double>(0x99112233EE55CCAAULL), *var_double);

  USE(unused);
}

WASM_EXEC_TEST(CallEmpty) {
  const int32_t kExpected = -414444;
  WasmRunner<int32_t> r(execution_tier);

  // Build the target function.
  WasmFunctionCompiler& target_func = r.NewFunction<int>();
  target_func.Build({WASM_I32V_3(kExpected)});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION0(target_func.function_index())});

  int32_t result = r.Call();
  CHECK_EQ(kExpected, result);
}

WASM_EXEC_TEST(CallF32StackParameter) {
  WasmRunner<float> r(execution_tier);

  // Build the target function.
  ValueType param_types[20];
  for (int i = 0; i < 20; ++i) param_types[i] = kWasmF32;
  FunctionSig sig(1, 19, param_types);
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  t.Build({WASM_LOCAL_GET(17)});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION(
      t.function_index(), WASM_F32(1.0f), WASM_F32(2.0f), WASM_F32(4.0f),
      WASM_F32(8.0f), WASM_F32(16.0f), WASM_F32(32.0f), WASM_F32(64.0f),
      WASM_F32(128.0f), WASM_F32(256.0f), WASM_F32(1.5f), WASM_F32(2.5f),
      WASM_F32(4.5f), WASM_F32(8.5f), WASM_F32(16.5f), WASM_F32(32.5f),
      WASM_F32(64.5f), WASM_F32(128.5f), WASM_F32(256.5f), WASM_F32(512.5f))});

  float result = r.Call();
  CHECK_EQ(256.5f, result);
}

WASM_EXEC_TEST(CallF64StackParameter) {
  WasmRunner<double> r(execution_tier);

  // Build the target function.
  ValueType param_types[20];
  for (int i = 0; i < 20; ++i) param_types[i] = kWasmF64;
  FunctionSig sig(1, 19, param_types);
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  t.Build({WASM_LOCAL_GET(17)});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION(
      t.function_index(), WASM_F64(1.0), WASM_F64(2.0), WASM_F64(4.0),
      WASM_F64(8.0), WASM_F64(16.0), WASM_F64(32.0), WASM_F64(64.0),
      WASM_F64(128.0), WASM_F64(256.0), WASM_F64(1.5), WASM_F64(2.5),
      WASM_F64(4.5), WASM_F64(8.5), WASM_F64(16.5), WASM_F64(32.5),
      WASM_F64(64.5), WASM_F64(128.5), WASM_F64(256.5), WASM_F64(512.5))});

  float result = r.Call();
  CHECK_EQ(256.5, result);
}

WASM_EXEC_TEST(CallVoid) {
  WasmRunner<int32_t> r(execution_tier);

  const uint8_t kMemOffset = 8;
  const int32_t kElemNum = kMemOffset / sizeof(int32_t);
  const int32_t kExpected = 414444;
  // Build the target function.
  TestSignatures sigs;
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  r.builder().RandomizeMemory();
  WasmFunctionCompiler& t = r.NewFunction(sigs.v_v());
  t.Build({WASM_STORE_MEM(MachineType::Int32(), WASM_I32V_1(kMemOffset),
                          WASM_I32V_3(kExpected))});

  // Build the calling function.
  r.Build({WASM_CALL_FUNCTION0(t.function_index()),
           WASM_LOAD_MEM(MachineType::Int32(), WASM_I32V_1(kMemOffset))});

  int32_t result = r.Call();
  CHECK_EQ(kExpected, result);
  CHECK_EQ(static_cast<int64_t>(kExpected),
           static_cast<int64_t>(r.builder().ReadMemory(&memory[kElemNum])));
}

WASM_EXEC_TEST(Call_Int32Add) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);

  // Build the target function.
  WasmFunctionCompiler& t = r.NewFunction<int32_t, int32_t, int32_t>();
  t.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  // Build the caller function.
  r.Build({WASM_CALL_FUNCTION(t.function_index(), WASM_LOCAL_GET(0),
                              WASM_LOCAL_GET(1))});

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(i) +
                                              static_cast<uint32_t>(j));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(Call_Float32Sub) {
  WasmRunner<float, float, float> r(execution_tier);

  // Build the target function.
  WasmFunctionCompiler& target_func = r.NewFunction<float, float, float>();
  target_func.Build({WASM_F32_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  // Build the caller function.
  r.Build({WASM_CALL_FUNCTION(target_func.function_index(), WASM_LOCAL_GET(0),
                              WASM_LOCAL_GET(1))});

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(i - j, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(Call_Float64Sub) {
  WasmRunner<int32_t> r(execution_tier);
  double* memory =
      r.builder().AddMemoryElems<double>(kWasmPageSize / sizeof(double));

  r.Build(
      {WASM_STORE_MEM(
           MachineType::Float64(), WASM_ZERO,
           WASM_F64_SUB(WASM_LOAD_MEM(MachineType::Float64(), WASM_ZERO),
                        WASM_LOAD_MEM(MachineType::Float64(), WASM_I32V_1(8)))),
       WASM_I32V_2(107)});

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], i);
      r.builder().WriteMemory(&memory[1], j);
      double expected = i - j;
      CHECK_EQ(107, r.Call());

      if (expected != expected) {
        CHECK(r.builder().ReadMemory(&memory[0]) !=
              r.builder().ReadMemory(&memory[0]));
      } else {
        CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
      }
    }
  }
}

template <typename T>
static T factorial(T v) {
  T expected = 1;
  for (T i = v; i > 1; i--) {
    expected *= i;
  }
  return expected;
}

template <typename T>
static T sum_1_to_n(T v) {
  return v * (v + 1) / 2;
}

// We use unsigned arithmetic because of ubsan validation.
WASM_EXEC_TEST(Regular_Factorial) {
  WasmRunner<uint32_t, uint32_t> r(execution_tier);

  WasmFunctionCompiler& fact_aux_fn =
      r.NewFunction<uint32_t, uint32_t, uint32_t>("fact_aux");
  r.Build({WASM_CALL_FUNCTION(fact_aux_fn.function_index(), WASM_LOCAL_GET(0),
                              WASM_I32V(1))});

  fact_aux_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LES(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_CALL_FUNCTION(fact_aux_fn.function_index(),
                         WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
                         WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))))});

  uint32_t test_values[] = {1, 2, 5, 10, 20};

  for (uint32_t v : test_values) {
    CHECK_EQ(factorial(v), r.Call(v));
  }
}

namespace {
// TODO(cleanup): Define in cctest.h and re-use where appropriate.
class IsolateScope {
 public:
  IsolateScope() {
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
    isolate_ = v8::Isolate::New(create_params);
    isolate_->Enter();
  }

  ~IsolateScope() {
    isolate_->Exit();
    isolate_->Dispose();
  }

  v8::Isolate* isolate() { return isolate_; }
  Isolate* i_isolate() { return reinterpret_cast<Isolate*>(isolate_); }

 private:
  v8::Isolate* isolate_;
};
}  // namespace

// Tail-recursive variation on factorial:
// fact(N) => f(N,1).
//
// f(N,X) where N=<1 => X
// f(N,X) => f(N-1,X*N).

UNINITIALIZED_WASM_EXEC_TEST(ReturnCall_Factorial) {
  // Run in bounded amount of stack - 8kb.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext current(isolate_scope.isolate());

  WasmRunner<uint32_t, uint32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                                   isolate_scope.i_isolate());

  WasmFunctionCompiler& fact_aux_fn =
      r.NewFunction<uint32_t, uint32_t, uint32_t>("fact_aux");
  r.Build({WASM_RETURN_CALL_FUNCTION(fact_aux_fn.function_index(),
                                     WASM_LOCAL_GET(0), WASM_I32V(1))});

  fact_aux_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LES(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_RETURN_CALL_FUNCTION(
          fact_aux_fn.function_index(),
          WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))))});

  uint32_t test_values[] = {1, 2, 5, 10, 20, 2000};

  for (uint32_t v : test_values) {
    CHECK_EQ(factorial<uint32_t>(v), r.Call(v));
  }
}

// Mutually recursive factorial mixing it up
// f(0,X)=>X
// f(N,X) => g(X*N,N-1)
// g(X,0) => X.
// g(X,N) => f(N-1,X*N).

UNINITIALIZED_WASM_EXEC_TEST(ReturnCall_MutualFactorial) {
  // Run in bounded amount of stack - 8kb.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext current(isolate_scope.isolate());

  WasmRunner<uint32_t, uint32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                                   isolate_scope.i_isolate());

  WasmFunctionCompiler& f_fn = r.NewFunction<uint32_t, uint32_t, uint32_t>("f");
  WasmFunctionCompiler& g_fn = r.NewFunction<uint32_t, uint32_t, uint32_t>("g");

  r.Build({WASM_RETURN_CALL_FUNCTION(f_fn.function_index(), WASM_LOCAL_GET(0),
                                     WASM_I32V(1))});

  f_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LES(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_RETURN_CALL_FUNCTION(
          g_fn.function_index(),
          WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
          WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1))))});

  g_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LES(WASM_LOCAL_GET(1), WASM_I32V(1)), WASM_LOCAL_GET(0),
      WASM_RETURN_CALL_FUNCTION(
          f_fn.function_index(), WASM_I32_SUB(WASM_LOCAL_GET(1), WASM_I32V(1)),
          WASM_I32_MUL(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))))});

  uint32_t test_values[] = {1, 2, 5, 10, 20, 2000};

  for (uint32_t v : test_values) {
    CHECK_EQ(factorial(v), r.Call(v));
  }
}

// Indirect variant of factorial. Pass the function ID as an argument:
// fact(N) => f(N,1,f).
//
// f(N,X,_) where N=<1 => X
// f(N,X,F) => F(N-1,X*N,F).

UNINITIALIZED_WASM_EXEC_TEST(ReturnCall_IndirectFactorial) {
  // Run in bounded amount of stack - 8kb.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext current(isolate_scope.isolate());

  WasmRunner<uint32_t, uint32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                                   isolate_scope.i_isolate());

  TestSignatures sigs;

  WasmFunctionCompiler& f_ind_fn = r.NewFunction(sigs.i_iii(), "f_ind");
  uint32_t sig_index = r.builder().AddSignature(sigs.i_iii());
  f_ind_fn.SetSigIndex(sig_index);

  // Function table.
  uint16_t indirect_function_table[] = {
      static_cast<uint16_t>(f_ind_fn.function_index())};
  const int f_ind_index = 0;

  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  r.Build(
      {WASM_RETURN_CALL_FUNCTION(f_ind_fn.function_index(), WASM_LOCAL_GET(0),
                                 WASM_I32V(1), WASM_I32V(f_ind_index))});

  f_ind_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LES(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_RETURN_CALL_INDIRECT(
          sig_index, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_LOCAL_GET(2),
          WASM_LOCAL_GET(2)))});

  uint32_t test_values[] = {1, 2, 5, 10, 10000};

  for (uint32_t v : test_values) {
    CHECK_EQ(factorial(v), r.Call(v));
  }
}

// This is 'more stable' (does not degenerate so quickly) than factorial
// sum(N,k) where N<1 =>k.
// sum(N,k) => sum(N-1,k+N).

UNINITIALIZED_WASM_EXEC_TEST(ReturnCall_Sum) {
  // Run in bounded amount of stack - 8kb.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext current(isolate_scope.isolate());

  WasmRunner<int32_t, int32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                                 isolate_scope.i_isolate());
  TestSignatures sigs;

  WasmFunctionCompiler& sum_aux_fn = r.NewFunction(sigs.i_ii(), "sum_aux");
  r.Build({WASM_RETURN_CALL_FUNCTION(sum_aux_fn.function_index(),
                                     WASM_LOCAL_GET(0), WASM_I32V(0))});

  sum_aux_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LTS(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_RETURN_CALL_FUNCTION(
          sum_aux_fn.function_index(),
          WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))))});

  int32_t test_values[] = {1, 2, 5, 10, 1000};

  for (int32_t v : test_values) {
    CHECK_EQ(sum_1_to_n(v), r.Call(v));
  }
}

// 'Bouncing' mutual recursive sum with different #s of arguments
// b1(N,k) where N<1 =>k.
// b1(N,k) => b2(N-1,N,k+N).

// b2(N,_,k) where N<1 =>k.
// b2(N,l,k) => b3(N-1,N,l,k+N).

// b3(N,_,_,k) where N<1 =>k.
// b3(N,_,_,k) => b1(N-1,k+N).

UNINITIALIZED_WASM_EXEC_TEST(ReturnCall_Bounce_Sum) {
  // Run in bounded amount of stack - 8kb.
  FlagScope<int32_t> stack_size(&v8_flags.stack_size, 8);

  IsolateScope isolate_scope;
  LocalContext current(isolate_scope.isolate());

  WasmRunner<int32_t, int32_t> r(execution_tier, kWasmOrigin, nullptr, "main",
                                 isolate_scope.i_isolate());
  TestSignatures sigs;

  WasmFunctionCompiler& b1_fn = r.NewFunction(sigs.i_ii(), "b1");
  WasmFunctionCompiler& b2_fn = r.NewFunction(sigs.i_iii(), "b2");
  WasmFunctionCompiler& b3_fn =
      r.NewFunction<int32_t, int32_t, int32_t, int32_t, int32_t>("b3");

  r.Build({WASM_RETURN_CALL_FUNCTION(b1_fn.function_index(), WASM_LOCAL_GET(0),
                                     WASM_I32V(0))});

  b1_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LTS(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(1),
      WASM_RETURN_CALL_FUNCTION(
          b2_fn.function_index(), WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_LOCAL_GET(0),
          WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))))});

  b2_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LTS(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(2),
      WASM_RETURN_CALL_FUNCTION(
          b3_fn.function_index(), WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
          WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(2))))});

  b3_fn.Build({WASM_IF_ELSE_I(
      WASM_I32_LTS(WASM_LOCAL_GET(0), WASM_I32V(1)), WASM_LOCAL_GET(3),
      WASM_RETURN_CALL_FUNCTION(
          b1_fn.function_index(), WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V(1)),
          WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(3))))});

  int32_t test_values[] = {1, 2, 5, 10, 1000};

  for (int32_t v : test_values) {
    CHECK_EQ(sum_1_to_n(v), r.Call(v));
  }
}

static void Run_WasmMixedCall_N(TestExecutionTier execution_tier, int start) {
  const int kExpected = 6333;
  const int kElemSize = 8;
  TestSignatures sigs;

  // 64-bit cases handled in test-run-wasm-64.cc.
  static MachineType mixed[] = {
      MachineType::Int32(),   MachineType::Float32(), MachineType::Float64(),
      MachineType::Float32(), MachineType::Int32(),   MachineType::Float64(),
      MachineType::Float32(), MachineType::Float64(), MachineType::Int32(),
      MachineType::Int32(),   MachineType::Int32()};

  int num_params = static_cast<int>(arraysize(mixed)) - start;
  for (int which = 0; which < num_params; ++which) {
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    WasmRunner<int32_t> r(execution_tier);
    r.builder().AddMemory(kWasmPageSize);
    MachineType* memtypes = &mixed[start];
    MachineType result = memtypes[which];

    // =========================================================================
    // Build the selector function.
    // =========================================================================
    FunctionSig::Builder b(&zone, 1, num_params);
    b.AddReturn(ValueType::For(result));
    for (int i = 0; i < num_params; ++i) {
      b.AddParam(ValueType::For(memtypes[i]));
    }
    WasmFunctionCompiler& f = r.NewFunction(b.Build());
    f.Build({WASM_LOCAL_GET(which)});

    // =========================================================================
    // Build the calling function.
    // =========================================================================
    std::vector<uint8_t> code;

    // Load the arguments.
    for (int i = 0; i < num_params; ++i) {
      int offset = (i + 1) * kElemSize;
      ADD_CODE(code, WASM_LOAD_MEM(memtypes[i], WASM_I32V_2(offset)));
    }

    // Call the selector function.
    ADD_CODE(code, WASM_CALL_FUNCTION0(f.function_index()));

    // Store the result in a local.
    uint8_t local_index = r.AllocateLocal(ValueType::For(result));
    ADD_CODE(code, kExprLocalSet, local_index);

    // Store the result in memory.
    ADD_CODE(code,
             WASM_STORE_MEM(result, WASM_ZERO, WASM_LOCAL_GET(local_index)));

    // Return the expected value.
    ADD_CODE(code, WASM_I32V_2(kExpected));

    r.Build(base::VectorOf(code));

    // Run the code.
    for (int t = 0; t < 10; ++t) {
      r.builder().RandomizeMemory();
      CHECK_EQ(kExpected, r.Call());

      int size = result.MemSize();
      for (int i = 0; i < size; ++i) {
        int base = (which + 1) * kElemSize;
        uint8_t expected = r.builder().raw_mem_at<uint8_t>(base + i);
        uint8_t actual = r.builder().raw_mem_at<uint8_t>(i);
        CHECK_EQ(expected, actual);
      }
    }
  }
}

WASM_EXEC_TEST(MixedCall_0) { Run_WasmMixedCall_N(execution_tier, 0); }
WASM_EXEC_TEST(MixedCall_1) { Run_WasmMixedCall_N(execution_tier, 1); }
WASM_EXEC_TEST(MixedCall_2) { Run_WasmMixedCall_N(execution_tier, 2); }
WASM_EXEC_TEST(MixedCall_3) { Run_WasmMixedCall_N(execution_tier, 3); }

WASM_EXEC_TEST(AddCall) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  WasmFunctionCompiler& t1 = r.NewFunction<int32_t, int32_t, int32_t>();
  t1.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  uint8_t local = r.AllocateLocal(kWasmI32);
  r.Build({WASM_LOCAL_SET(local, WASM_I32V_2(99)),
           WASM_I32_ADD(
               WASM_CALL_FUNCTION(t1.function_index(), WASM_LOCAL_GET(0),
                                  WASM_LOCAL_GET(0)),
               WASM_CALL_FUNCTION(t1.function_index(), WASM_LOCAL_GET(local),
                                  WASM_LOCAL_GET(local)))});

  CHECK_EQ(198, r.Call(0));
  CHECK_EQ(200, r.Call(1));
  CHECK_EQ(100, r.Call(-49));
}

WASM_EXEC_TEST(MultiReturnSub) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);

  ValueType storage[] = {kWasmI32, kWasmI32, kWasmI32, kWasmI32};
  FunctionSig sig_ii_ii(2, 2, storage);
  WasmFunctionCompiler& t1 = r.NewFunction(&sig_ii_ii);
  t1.Build({WASM_LOCAL_GET(1), WASM_LOCAL_GET(0)});

  r.Build({WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
           WASM_CALL_FUNCTION0(t1.function_index()), kExprI32Sub});

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(j) -
                                              static_cast<uint32_t>(i));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

template <typename T>
void RunMultiReturnSelect(TestExecutionTier execution_tier, const T* inputs) {
  ValueType type = ValueType::For(MachineTypeForC<T>());
  ValueType storage[] = {type, type, type, type, type, type};
  const size_t kNumReturns = 2;
  const size_t kNumParams = arraysize(storage) - kNumReturns;
  FunctionSig sig(kNumReturns, kNumParams, storage);

  for (size_t i = 0; i < kNumParams; i++) {
    for (size_t j = 0; j < kNumParams; j++) {
      for (int k = 0; k < 2; k++) {
        WasmRunner<T, T, T, T, T> r(execution_tier);
        WasmFunctionCompiler& r1 = r.NewFunction(&sig);

        r1.Build({WASM_LOCAL_GET(i), WASM_LOCAL_GET(j)});

        if (k == 0) {
          r.Build({WASM_CALL_FUNCTION(r1.function_index(), WASM_LOCAL_GET(0),
                                      WASM_LOCAL_GET(1), WASM_LOCAL_GET(2),
                                      WASM_LOCAL_GET(3)),
                   WASM_DROP});
        } else {
          r.Build({WASM_CALL_FUNCTION(r1.function_index(), WASM_LOCAL_GET(0),
                                      WASM_LOCAL_GET(1), WASM_LOCAL_GET(2),
                                      WASM_LOCAL_GET(3)),
                   kExprLocalSet, 0, WASM_DROP, WASM_LOCAL_GET(0)});
        }

        T expected = inputs[k == 0 ? i : j];
        CHECK_EQ(expected, r.Call(inputs[0], inputs[1], inputs[2], inputs[3]));
      }
    }
  }
}

WASM_EXEC_TEST(MultiReturnSelect_i32) {
  static const int32_t inputs[] = {3333333, 4444444, -55555555, -7777777};
  RunMultiReturnSelect<int32_t>(execution_tier, inputs);
}

WASM_EXEC_TEST(MultiReturnSelect_f32) {
  static const float inputs[] = {33.33333f, 444.4444f, -55555.555f, -77777.77f};
  RunMultiReturnSelect<float>(execution_tier, inputs);
}

WASM_EXEC_TEST(MultiReturnSelect_i64) {
#if !V8_TARGET_ARCH_32_BIT || V8_TARGET_ARCH_X64
  // TODO(titzer): implement int64-lowering for multiple return values
  static const int64_t inputs[] = {33333338888, 44444446666, -555555553333,
                                   -77777771111};
  RunMultiReturnSelect<int64_t>(execution_tier, inputs);
#endif
}

WASM_EXEC_TEST(MultiReturnSelect_f64) {
  static const double inputs[] = {3.333333, 44444.44, -55.555555, -7777.777};
  RunMultiReturnSelect<double>(execution_tier, inputs);
}

WASM_EXEC_TEST(ExprBlock2a) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_IF(WASM_LOCAL_GET(0), WASM_BRV(1, WASM_I32V_1(1))),
                        WASM_I32V_1(1))});
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(1, r.Call(1));
}

WASM_EXEC_TEST(ExprBlock2b) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_IF(WASM_LOCAL_GET(0), WASM_BRV(1, WASM_I32V_1(1))),
                        WASM_I32V_1(2))});
  CHECK_EQ(2, r.Call(0));
  CHECK_EQ(1, r.Call(1));
}

WASM_EXEC_TEST(ExprBlock2c) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV_IFD(0, WASM_I32V_1(1), WASM_LOCAL_GET(0)),
                        WASM_I32V_1(1))});
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(1, r.Call(1));
}

WASM_EXEC_TEST(ExprBlock2d) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_BRV_IFD(0, WASM_I32V_1(1), WASM_LOCAL_GET(0)),
                        WASM_I32V_1(2))});
  CHECK_EQ(2, r.Call(0));
  CHECK_EQ(1, r.Call(1));
}

WASM_EXEC_TEST(ExprBlock_ManualSwitch) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(1)),
                                WASM_BRV(1, WASM_I32V_1(11))),
                        WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(2)),
                                WASM_BRV(1, WASM_I32V_1(12))),
                        WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(3)),
                                WASM_BRV(1, WASM_I32V_1(13))),
                        WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(4)),
                                WASM_BRV(1, WASM_I32V_1(14))),
                        WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(5)),
                                WASM_BRV(1, WASM_I32V_1(15))),
                        WASM_I32V_2(99))});
  CHECK_EQ(99, r.Call(0));
  CHECK_EQ(11, r.Call(1));
  CHECK_EQ(12, r.Call(2));
  CHECK_EQ(13, r.Call(3));
  CHECK_EQ(14, r.Call(4));
  CHECK_EQ(15, r.Call(5));
  CHECK_EQ(99, r.Call(6));
}

WASM_EXEC_TEST(ExprBlock_ManualSwitch_brif) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build({WASM_BLOCK_I(
      WASM_BRV_IFD(0, WASM_I32V_1(11),
                   WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(1))),
      WASM_BRV_IFD(0, WASM_I32V_1(12),
                   WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(2))),
      WASM_BRV_IFD(0, WASM_I32V_1(13),
                   WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(3))),
      WASM_BRV_IFD(0, WASM_I32V_1(14),
                   WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(4))),
      WASM_BRV_IFD(0, WASM_I32V_1(15),
                   WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(5))),
      WASM_I32V_2(99))});
  CHECK_EQ(99, r.Call(0));
  CHECK_EQ(11, r.Call(1));
  CHECK_EQ(12, r.Call(2));
  CHECK_EQ(13, r.Call(3));
  CHECK_EQ(14, r.Call(4));
  CHECK_EQ(15, r.Call(5));
  CHECK_EQ(99, r.Call(6));
}

WASM_EXEC_TEST(If_nested) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);

  r.Build({WASM_IF_ELSE_I(
      WASM_LOCAL_GET(0),
      WASM_IF_ELSE_I(WASM_LOCAL_GET(1), WASM_I32V_1(11), WASM_I32V_1(12)),
      WASM_IF_ELSE_I(WASM_LOCAL_GET(1), WASM_I32V_1(13), WASM_I32V_1(14)))});

  CHECK_EQ(11, r.Call(1, 1));
  CHECK_EQ(12, r.Call(1, 0));
  CHECK_EQ(13, r.Call(0, 1));
  CHECK_EQ(14, r.Call(0, 0));
}

WASM_EXEC_TEST(ExprBlock_if) {
  WasmRunner<int32_t, int32_t> r(execution_tier);

  r.Build({WASM_BLOCK_I(WASM_IF_ELSE_I(WASM_LOCAL_GET(0),
                                       WASM_BRV(0, WASM_I32V_1(11)),
                                       WASM_BRV(1, WASM_I32V_1(14))))});

  CHECK_EQ(11, r.Call(1));
  CHECK_EQ(14, r.Call(0));
}

WASM_EXEC_TEST(ExprBlock_nested_ifs) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);

  r.Build({WASM_BLOCK_I(WASM_IF_ELSE_I(
      WASM_LOCAL_GET(0),
      WASM_IF_ELSE_I(WASM_LOCAL_GET(1), WASM_BRV(0, WASM_I32V_1(11)),
                     WASM_BRV(1, WASM_I32V_1(12))),
      WASM_IF_ELSE_I(WASM_LOCAL_GET(1), WASM_BRV(0, WASM_I32V_1(13)),
                     WASM_BRV(1, WASM_I32V_1(14)))))});

  CHECK_EQ(11, r.Call(1, 1));
  CHECK_EQ(12, r.Call(1, 0));
  CHECK_EQ(13, r.Call(0, 1));
  CHECK_EQ(14, r.Call(0, 0));
}

WASM_EXEC_TEST(SimpleCallIndirect) {
  TestSignatures sigs;
  WasmRunner<int32_t, int32_t> r(execution_tier);

  WasmFunctionCompiler& t1 = r.NewFunction(sigs.i_ii());
  t1.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  t1.SetSigIndex(1);

  WasmFunctionCompiler& t2 = r.NewFunction(sigs.i_ii());
  t2.Build({WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  t2.SetSigIndex(1);

  // Signature table.
  r.builder().AddSignature(sigs.f_ff());
  r.builder().AddSignature(sigs.i_ii());
  r.builder().AddSignature(sigs.d_dd());

  // Function table.
  uint16_t indirect_function_table[] = {
      static_cast<uint16_t>(t1.function_index()),
      static_cast<uint16_t>(t2.function_index())};
  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  // Build the caller function.
  r.Build({WASM_CALL_INDIRECT(1, WASM_I32V_2(66), WASM_I32V_1(22),
                              WASM_LOCAL_GET(0))});

  CHECK_EQ(88, r.Call(0));
  CHECK_EQ(44, r.Call(1));
  CHECK_TRAP(r.Call(2));
}

WASM_EXEC_TEST(MultipleCallIndirect) {
  TestSignatures sigs;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(execution_tier);

  WasmFunctionCompiler& t1 = r.NewFunction(sigs.i_ii());
  t1.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  t1.SetSigIndex(1);

  WasmFunctionCompiler& t2 = r.NewFunction(sigs.i_ii());
  t2.Build({WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  t2.SetSigIndex(1);

  // Signature table.
  r.builder().AddSignature(sigs.f_ff());
  r.builder().AddSignature(sigs.i_ii());
  r.builder().AddSignature(sigs.d_dd());

  // Function table.
  uint16_t indirect_function_table[] = {
      static_cast<uint16_t>(t1.function_index()),
      static_cast<uint16_t>(t2.function_index())};
  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  // Build the caller function.
  r.Build(
      {WASM_I32_ADD(WASM_CALL_INDIRECT(1, WASM_LOCAL_GET(1), WASM_LOCAL_GET(2),
                                       WASM_LOCAL_GET(0)),
                    WASM_CALL_INDIRECT(1, WASM_LOCAL_GET(2), WASM_LOCAL_GET(0),
                                       WASM_LOCAL_GET(1)))});

  CHECK_EQ(5, r.Call(0, 1, 2));
  CHECK_EQ(19, r.Call(0, 1, 9));
  CHECK_EQ(1, r.Call(1, 0, 2));
  CHECK_EQ(1, r.Call(1, 0, 9));

  CHECK_TRAP(r.Call(0, 2, 1));
  CHECK_TRAP(r.Call(1, 2, 0));
  CHECK_TRAP(r.Call(2, 0, 1));
  CHECK_TRAP(r.Call(2, 1, 0));
}

WASM_EXEC_TEST(CallIndirect_EmptyTable) {
  TestSignatures sigs;
  WasmRunner<int32_t, int32_t> r(execution_tier);

  // One function.
  WasmFunctionCompiler& t1 = r.NewFunction(sigs.i_ii());
  t1.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  t1.SetSigIndex(1);

  // Signature table.
  r.builder().AddSignature(sigs.f_ff());
  r.builder().AddSignature(sigs.i_ii());
  r.builder().AddIndirectFunctionTable(nullptr, 0);

  // Build the caller function.
  r.Build({WASM_CALL_INDIRECT(1, WASM_I32V_2(66), WASM_I32V_1(22),
                              WASM_LOCAL_GET(0))});

  CHECK_TRAP(r.Call(0));
  CHECK_TRAP(r.Call(1));
  CHECK_TRAP(r.Call(2));
}

WASM_EXEC_TEST(CallIndirect_canonical) {
  TestSignatures sigs;
  WasmRunner<int32_t, int32_t> r(execution_tier);

  WasmFunctionCompiler& t1 = r.NewFunction(sigs.i_ii());
  t1.Build({WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  WasmFunctionCompiler& t2 = r.NewFunction(sigs.i_ii());
  t2.Build({WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  WasmFunctionCompiler& t3 = r.NewFunction(sigs.f_ff());
  t3.Build({WASM_F32_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  // Function table.
  uint16_t i1 = static_cast<uint16_t>(t1.function_index());
  uint16_t i2 = static_cast<uint16_t>(t2.function_index());
  uint16_t i3 = static_cast<uint16_t>(t3.function_index());
  uint16_t indirect_function_table[] = {i1, i2, i3, i1, i2};

  r.builder().AddIndirectFunctionTable(indirect_function_table,
                                       arraysize(indirect_function_table));

  // Build the caller function.
  r.Build({WASM_CALL_INDIRECT(1, WASM_I32V_2(77), WASM_I32V_1(11),
                              WASM_LOCAL_GET(0))});

  CHECK_EQ(88, r.Call(0));
  CHECK_EQ(66, r.Call(1));
  CHECK_TRAP(r.Call(2));
  CHECK_EQ(88, r.Call(3));
  CHECK_EQ(66, r.Call(4));
  CHECK_TRAP(r.Call(5));
}

WASM_EXEC_TEST(Regress_PushReturns) {
  ValueType kSigTypes[] = {kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                           kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                           kWasmI32, kWasmI32, kWasmI32, kWasmI32};
  FunctionSig sig(12, 0, kSigTypes);
  WasmRunner<int32_t> r(execution_tier);

  WasmFunctionCompiler& f1 = r.NewFunction(&sig);
  f1.Build({WASM_I32V(1), WASM_I32V(2), WASM_I32V(3), WASM_I32V(4),
            WASM_I32V(5), WASM_I32V(6), WASM_I32V(7), WASM_I32V(8),
            WASM_I32V(9), WASM_I32V(10), WASM_I32V(11), WASM_I32V(12)});

  r.Build({WASM_CALL_FUNCTION0(f1.function_index()), WASM_DROP, WASM_DROP,
           WASM_DROP, WASM_DROP, WASM_DROP, WASM_DROP, WASM_DROP, WASM_DROP,
           WASM_DROP, WASM_DROP, WASM_DROP});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(Regress_EnsureArguments) {
  ValueType kSigTypes[] = {kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                           kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                           kWasmI32, kWasmI32, kWasmI32, kWasmI32};
  FunctionSig sig(0, 12, kSigTypes);
  WasmRunner<int32_t> r(execution_tier);

  WasmFunctionCompiler& f2 = r.NewFunction(&sig);
  f2.Build({kExprReturn});

  r.Build({WASM_I32V(42), kExprReturn,
           WASM_CALL_FUNCTION(f2.function_index(), WASM_I32V(1))});
  CHECK_EQ(42, r.Call());
}

WASM_EXEC_TEST(Regress_PushControl) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_I32V(42), WASM_IF(WASM_I32V(0), WASM_UNREACHABLE, kExprIf,
                                  kVoidCode, kExprEnd)});
  CHECK_EQ(42, r.Call());
}

WASM_EXEC_TEST(F32Floor) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_FLOOR(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(floorf(i), r.Call(i)); }
}

WASM_EXEC_TEST(F32Ceil) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_CEIL(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(ceilf(i), r.Call(i)); }
}

WASM_EXEC_TEST(F32Trunc) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_TRUNC(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(truncf(i), r.Call(i)); }
}

WASM_EXEC_TEST(F32NearestInt) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_NEARESTINT(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    float value = nearbyintf(i);
#if V8_OS_AIX
    value = FpOpWorkaround<float>(i, value);
#endif
    CHECK_FLOAT_EQ(value, r.Call(i));
  }
}

WASM_EXEC_TEST(F64Floor) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_FLOOR(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(floor(i), r.Call(i)); }
}

WASM_EXEC_TEST(F64Ceil) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_CEIL(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(ceil(i), r.Call(i)); }
}

WASM_EXEC_TEST(F64Trunc) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_TRUNC(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(trunc(i), r.Call(i)); }
}

WASM_EXEC_TEST(F64NearestInt) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_NEARESTINT(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    double value = nearbyint(i);
#if V8_OS_AIX
    value = FpOpWorkaround<double>(i, value);
#endif
    CHECK_DOUBLE_EQ(value, r.Call(i));
  }
}

WASM_EXEC_TEST(F32Min) {
  WasmRunner<float, float, float> r(execution_tier);
  r.Build({WASM_F32_MIN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_DOUBLE_EQ(JSMin(i, j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(F32MinSameValue) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_MIN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))});
  float result = r.Call(5.0f);
  CHECK_FLOAT_EQ(5.0f, result);
}

WASM_EXEC_TEST(F64Min) {
  WasmRunner<double, double, double> r(execution_tier);
  r.Build({WASM_F64_MIN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(JSMin(i, j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(F64MinSameValue) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_MIN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))});
  double result = r.Call(5.0);
  CHECK_DOUBLE_EQ(5.0, result);
}

WASM_EXEC_TEST(F32Max) {
  WasmRunner<float, float, float> r(execution_tier);
  r.Build({WASM_F32_MAX(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(JSMax(i, j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(F32MaxSameValue) {
  WasmRunner<float, float> r(execution_tier);
  r.Build({WASM_F32_MAX(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))});
  float result = r.Call(5.0f);
  CHECK_FLOAT_EQ(5.0f, result);
}

WASM_EXEC_TEST(F64Max) {
  WasmRunner<double, double, double> r(execution_tier);
  r.Build({WASM_F64_MAX(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      double result = r.Call(i, j);
      CHECK_DOUBLE_EQ(JSMax(i, j), result);
    }
  }
}

WASM_EXEC_TEST(F64MaxSameValue) {
  WasmRunner<double, double> r(execution_tier);
  r.Build({WASM_F64_MAX(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))});
  double result = r.Call(5.0);
  CHECK_DOUBLE_EQ(5.0, result);
}

WASM_EXEC_TEST(I32SConvertF32) {
  WasmRunner<int32_t, float> r(execution_tier);
  r.Build({WASM_I32_SCONVERT_F32(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    if (is_inbounds<int32_t>(i)) {
      CHECK_EQ(static_cast<int32_t>(i), r.Call(i));
    } else {
      CHECK_TRAP32(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I32SConvertSatF32) {
  WasmRunner<int32_t, float> r(execution_tier);
  r.Build({WASM_I32_SCONVERT_SAT_F32(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    int32_t expected =
        is_inbounds<int32_t>(i)
            ? static_cast<int32_t>(i)
            : std::isnan(i) ? 0
                            : i < 0.0 ? std::numeric_limits<int32_t>::min()
                                      : std::numeric_limits<int32_t>::max();
    int32_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I32SConvertF64) {
  WasmRunner<int32_t, double> r(execution_tier);
  r.Build({WASM_I32_SCONVERT_F64(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    if (is_inbounds<int32_t>(i)) {
      CHECK_EQ(static_cast<int32_t>(i), r.Call(i));
    } else {
      CHECK_TRAP32(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I32SConvertSatF64) {
  WasmRunner<int32_t, double> r(execution_tier);
  r.Build({WASM_I32_SCONVERT_SAT_F64(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) {
    int32_t expected =
        is_inbounds<int32_t>(i)
            ? static_cast<int32_t>(i)
            : std::isnan(i) ? 0
                            : i < 0.0 ? std::numeric_limits<int32_t>::min()
                                      : std::numeric_limits<int32_t>::max();
    int32_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I32UConvertF32) {
  WasmRunner<uint32_t, float> r(execution_tier);
  r.Build({WASM_I32_UCONVERT_F32(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) {
    if (is_inbounds<uint32_t>(i)) {
      CHECK_EQ(static_cast<uint32_t>(i), r.Call(i));
    } else {
      CHECK_TRAP32(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I32UConvertSatF32) {
  WasmRunner<uint32_t, float> r(execution_tier);
  r.Build({WASM_I32_UCONVERT_SAT_F32(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) {
    int32_t expected =
        is_inbounds<uint32_t>(i)
            ? static_cast<uint32_t>(i)
            : std::isnan(i) ? 0
                            : i < 0.0 ? std::numeric_limits<uint32_t>::min()
                                      : std::numeric_limits<uint32_t>::max();
    int32_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I32UConvertF64) {
  WasmRunner<uint32_t, double> r(execution_tier);
  r.Build({WASM_I32_UCONVERT_F64(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) {
    if (is_inbounds<uint32_t>(i)) {
      CHECK_EQ(static_cast<uint32_t>(i), r.Call(i));
    } else {
      CHECK_TRAP32(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I32UConvertSatF64) {
  WasmRunner<uint32_t, double> r(execution_tier);
  r.Build({WASM_I32_UCONVERT_SAT_F64(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) {
    int32_t expected =
        is_inbounds<uint32_t>(i)
            ? static_cast<uint32_t>(i)
            : std::isnan(i) ? 0
                            : i < 0.0 ? std::numeric_limits<uint32_t>::min()
                                      : std::numeric_limits<uint32_t>::max();
    int32_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(F64CopySign) {
  WasmRunner<double, double, double> r(execution_tier);
  r.Build({WASM_F64_COPYSIGN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(copysign(i, j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(F32CopySign) {
  WasmRunner<float, float, float> r(execution_tier);
  r.Build({WASM_F32_COPYSIGN(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(copysignf(i, j), r.Call(i, j)); }
  }
}

static void CompileCallIndirectMany(TestExecutionTier tier, ValueType param) {
  // Make sure we don't run out of registers when compiling indirect calls
  // with many many parameters.
  TestSignatures sigs;
  for (uint8_t num_params = 0; num_params < 40; ++num_params) {
    WasmRunner<void> r(tier);
    FunctionSig* sig = sigs.many(r.zone(), kWasmVoid, param, num_params);

    r.builder().AddSignature(sig);
    r.builder().AddSignature(sig);
    r.builder().AddIndirectFunctionTable(nullptr, 0);

    WasmFunctionCompiler& t = r.NewFunction(sig);

    std::vector<uint8_t> code;
    for (uint8_t p = 0; p < num_params; ++p) {
      ADD_CODE(code, kExprLocalGet, p);
    }
    ADD_CODE(code, kExprI32Const, 0);
    ADD_CODE(code, kExprCallIndirect, 1, TABLE_ZERO);

    t.Build(base::VectorOf(code));
  }
}

WASM_COMPILED_EXEC_TEST(Compile_Wasm_CallIndirect_Many_i32) {
  CompileCallIndirectMany(execution_tier, kWasmI32);
}

WASM_COMPILED_EXEC_TEST(Compile_Wasm_CallIndirect_Many_f32) {
  CompileCallIndirectMany(execution_tier, kWasmF32);
}

WASM_COMPILED_EXEC_TEST(Compile_Wasm_CallIndirect_Many_f64) {
  CompileCallIndirectMany(execution_tier, kWasmF64);
}

WASM_EXEC_TEST(Int32RemS_dead) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  r.Build({WASM_I32_REMS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_DROP,
           WASM_ZERO});
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(0, r.Call(133, 100));
  CHECK_EQ(0, r.Call(kMin, -1));
  CHECK_EQ(0, r.Call(0, 1));
  CHECK_TRAP(r.Call(100, 0));
  CHECK_TRAP(r.Call(-1001, 0));
  CHECK_TRAP(r.Call(kMin, 0));
}

WASM_EXEC_TEST(BrToLoopWithValue) {
  WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);
  // Subtracts <1> times 3 from <0> and returns the result.
  r.Build({// loop i32
           kExprLoop, kI32Code,
           // decrement <0> by 3.
           WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(3))),
           // decrement <1> by 1.
           WASM_LOCAL_SET(1, WASM_I32_SUB(WASM_LOCAL_GET(1), WASM_ONE)),
           // load return value <0>, br_if will drop if if the branch is taken.
           WASM_LOCAL_GET(0),
           // continue loop if <1> is != 0.
           WASM_BR_IF(0, WASM_LOCAL_GET(1)),
           // end of loop, value loaded above is the return value.
           kExprEnd});
  CHECK_EQ(12, r.Call(27, 5));
}

WASM_EXEC_TEST(BrToLoopWithoutValue) {
  // This was broken in the interpreter, see http://crbug.com/715454
  WasmRunner<int32_t, int32_t> r(execution_tier);
  r.Build(
      {kExprLoop, kI32Code,  // loop i32
       WASM_LOCAL_SET(0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_ONE)),  // dec <0>
       WASM_BR_IF(0, WASM_LOCAL_GET(0)),  // br_if <0> != 0
       kExprUnreachable,                  // unreachable
       kExprEnd});                        // end
  CHECK_TRAP32(r.Call(2));
}

WASM_EXEC_TEST(LoopsWithValues) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_LOOP_I(WASM_LOOP_I(WASM_ONE), WASM_ONE, kExprI32Add)});
  CHECK_EQ(2, r.Call());
}

WASM_EXEC_TEST(InvalidStackAfterUnreachable) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({kExprUnreachable, kExprI32Add});
  CHECK_TRAP32(r.Call());
}

WASM_EXEC_TEST(InvalidStackAfterBr) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_BRV(0, WASM_I32V_1(27)), kExprI32Add});
  CHECK_EQ(27, r.Call());
}

WASM_EXEC_TEST(InvalidStackAfterReturn) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_RETURN(WASM_I32V_1(17)), kExprI32Add});
  CHECK_EQ(17, r.Call());
}

WASM_EXEC_TEST(BranchOverUnreachableCode) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({// Start a block which breaks in the middle (hence unreachable code
           // afterwards) and continue execution after this block.
           WASM_BLOCK_I(WASM_BRV(0, WASM_I32V_1(17)), kExprI32Add),
           // Add one to the 17 returned from the block.
           WASM_ONE, kExprI32Add});
  CHECK_EQ(18, r.Call());
}

WASM_EXEC_TEST(BranchOverUnreachableCodeInLoop0) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_BLOCK_I(
           // Start a loop which breaks in the middle (hence unreachable code
           // afterwards) and continue execution after this loop.
           // This should validate even though there is no value on the stack
           // at the end of the loop.
           WASM_LOOP_I(WASM_BRV(1, WASM_I32V_1(17)))),
       // Add one to the 17 returned from the block.
       WASM_ONE, kExprI32Add});
  CHECK_EQ(18, r.Call());
}

WASM_EXEC_TEST(BranchOverUnreachableCodeInLoop1) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_BLOCK_I(
           // Start a loop which breaks in the middle (hence unreachable code
           // afterwards) and continue execution after this loop.
           // Even though unreachable, the loop leaves one value on the stack.
           WASM_LOOP_I(WASM_BRV(1, WASM_I32V_1(17)), WASM_ONE)),
       // Add one to the 17 returned from the block.
       WASM_ONE, kExprI32Add});
  CHECK_EQ(18, r.Call());
}

WASM_EXEC_TEST(BranchOverUnreachableCodeInLoop2) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_BLOCK_I(
           // Start a loop which breaks in the middle (hence unreachable code
           // afterwards) and continue execution after this loop.
           // The unreachable code is allowed to pop non-existing values off
           // the stack and push back the result.
           WASM_LOOP_I(WASM_BRV(1, WASM_I32V_1(17)), kExprI32Add)),
       // Add one to the 17 returned from the block.
       WASM_ONE, kExprI32Add});
  CHECK_EQ(18, r.Call());
}

WASM_EXEC_TEST(BlockInsideUnreachable) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_RETURN(WASM_I32V_1(17)), WASM_BLOCK(WASM_BR(0))});
  CHECK_EQ(17, r.Call());
}

WASM_EXEC_TEST(IfInsideUnreachable) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_RETURN(WASM_I32V_1(17)),
       WASM_IF_ELSE_I(WASM_ONE, WASM_BRV(0, WASM_ONE), WASM_RETURN(WASM_ONE))});
  CHECK_EQ(17, r.Call());
}

WASM_EXEC_TEST(IndirectNull) {
  WasmRunner<int32_t> r(execution_tier);
  FunctionSig sig(1, 0, &kWasmI32);
  uint8_t sig_index = r.builder().AddSignature(&sig);
  r.builder().AddIndirectFunctionTable(nullptr, 1);

  r.Build({WASM_CALL_INDIRECT(sig_index, WASM_I32V(0))});

  CHECK_TRAP(r.Call());
}

WASM_EXEC_TEST(IndirectNullTyped) {
  WasmRunner<int32_t> r(execution_tier);
  FunctionSig sig(1, 0, &kWasmI32);
  uint8_t sig_index = r.builder().AddSignature(&sig);
  r.builder().AddIndirectFunctionTable(nullptr, 1,
                                       ValueType::RefNull(sig_index));

  r.Build({WASM_CALL_INDIRECT(sig_index, WASM_I32V(0))});

  CHECK_TRAP(r.Call());
}

// This test targets binops in Liftoff.
// Initialize a number of local variables to force them into different
// registers, then perform a binary operation on two of the locals.
// Afterwards, write back all locals to memory, to check that their value was
// not overwritten.
template <typename ctype>
void BinOpOnDifferentRegisters(
    TestExecutionTier execution_tier, ValueType type,
    base::Vector<const ctype> inputs, WasmOpcode opcode,
    std::function<ctype(ctype, ctype, bool*)> expect_fn) {
  static constexpr int kMaxNumLocals = 8;
  for (int num_locals = 1; num_locals < kMaxNumLocals; ++num_locals) {
    // {init_locals_code} is shared by all code generated in the loop below.
    std::vector<uint8_t> init_locals_code;
    // Load from memory into the locals.
    for (int i = 0; i < num_locals; ++i) {
      ADD_CODE(
          init_locals_code,
          WASM_LOCAL_SET(i, WASM_LOAD_MEM(type.machine_type(),
                                          WASM_I32V_2(sizeof(ctype) * i))));
    }
    // {write_locals_code} is shared by all code generated in the loop below.
    std::vector<uint8_t> write_locals_code;
    // Write locals back into memory, shifted by one element to the right.
    for (int i = 0; i < num_locals; ++i) {
      ADD_CODE(write_locals_code,
               WASM_STORE_MEM(type.machine_type(),
                              WASM_I32V_2(sizeof(ctype) * (i + 1)),
                              WASM_LOCAL_GET(i)));
    }
    for (int lhs = 0; lhs < num_locals; ++lhs) {
      for (int rhs = 0; rhs < num_locals; ++rhs) {
        WasmRunner<int32_t> r(execution_tier);
        ctype* memory =
            r.builder().AddMemoryElems<ctype>(kWasmPageSize / sizeof(ctype));
        for (int i = 0; i < num_locals; ++i) {
          r.AllocateLocal(type);
        }
        std::vector<uint8_t> code(init_locals_code);
        ADD_CODE(code,
                 // Store the result of the binary operation at memory[0].
                 WASM_STORE_MEM(type.machine_type(), WASM_ZERO,
                                WASM_BINOP(opcode, WASM_LOCAL_GET(lhs),
                                           WASM_LOCAL_GET(rhs))),
                 // Return 0.
                 WASM_ZERO);
        code.insert(code.end(), write_locals_code.begin(),
                    write_locals_code.end());
        r.Build(base::VectorOf(code));
        for (ctype lhs_value : inputs) {
          for (ctype rhs_value : inputs) {
            if (lhs == rhs) lhs_value = rhs_value;
            for (int i = 0; i < num_locals; ++i) {
              ctype value =
                  i == lhs ? lhs_value
                           : i == rhs ? rhs_value : static_cast<ctype>(i + 47);
              WriteLittleEndianValue<ctype>(&memory[i], value);
            }
            bool trap = false;
            int64_t expect = expect_fn(lhs_value, rhs_value, &trap);
            if (trap) {
              CHECK_TRAP(r.Call());
              continue;
            }
            CHECK_EQ(0, r.Call());
            CHECK_EQ(expect, ReadLittleEndianValue<ctype>(&memory[0]));
            for (int i = 0; i < num_locals; ++i) {
              ctype value =
                  i == lhs ? lhs_value
                           : i == rhs ? rhs_value : static_cast<ctype>(i + 47);
              CHECK_EQ(value, ReadLittleEndianValue<ctype>(&memory[i + 1]));
            }
          }
        }
      }
    }
  }
}

// Keep this list small, the BinOpOnDifferentRegisters test is running long
// enough already.
static constexpr int32_t kSome32BitInputs[] = {
    0, 1, -1, 31, static_cast<int32_t>(0xff112233)};
static constexpr int64_t kSome64BitInputs[] = {
    0, 1, -1, 31, 63, 0x100000000, static_cast<int64_t>(0xff11223344556677)};

WASM_EXEC_TEST(I32AddOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32Add,
      [](int32_t lhs, int32_t rhs, bool* trap) { return lhs + rhs; });
}

WASM_EXEC_TEST(I32SubOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32Sub,
      [](int32_t lhs, int32_t rhs, bool* trap) { return lhs - rhs; });
}

WASM_EXEC_TEST(I32MulOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32Mul, [](int32_t lhs, int32_t rhs, bool* trap) {
        return base::MulWithWraparound(lhs, rhs);
      });
}

WASM_EXEC_TEST(I32ShlOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32Shl, [](int32_t lhs, int32_t rhs, bool* trap) {
        return base::ShlWithWraparound(lhs, rhs);
      });
}

WASM_EXEC_TEST(I32ShrSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32ShrS,
      [](int32_t lhs, int32_t rhs, bool* trap) { return lhs >> (rhs & 31); });
}

WASM_EXEC_TEST(I32ShrUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32ShrU, [](int32_t lhs, int32_t rhs, bool* trap) {
        return static_cast<uint32_t>(lhs) >> (rhs & 31);
      });
}

WASM_EXEC_TEST(I32DivSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32DivS, [](int32_t lhs, int32_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap ? 0 : lhs / rhs;
      });
}

WASM_EXEC_TEST(I32DivUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32DivU, [](uint32_t lhs, uint32_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap ? 0 : lhs / rhs;
      });
}

WASM_EXEC_TEST(I32RemSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32RemS, [](int32_t lhs, int32_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap || rhs == -1 ? 0 : lhs % rhs;
      });
}

WASM_EXEC_TEST(I32RemUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int32_t>(
      execution_tier, kWasmI32, base::ArrayVector(kSome32BitInputs),
      kExprI32RemU, [](uint32_t lhs, uint32_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap ? 0 : lhs % rhs;
      });
}

WASM_EXEC_TEST(I64AddOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64Add,
      [](int64_t lhs, int64_t rhs, bool* trap) { return lhs + rhs; });
}

WASM_EXEC_TEST(I64SubOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64Sub,
      [](int64_t lhs, int64_t rhs, bool* trap) { return lhs - rhs; });
}

WASM_EXEC_TEST(I64MulOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64Mul, [](int64_t lhs, int64_t rhs, bool* trap) {
        return base::MulWithWraparound(lhs, rhs);
      });
}

WASM_EXEC_TEST(I64ShlOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64Shl, [](int64_t lhs, int64_t rhs, bool* trap) {
        return base::ShlWithWraparound(lhs, rhs);
      });
}

WASM_EXEC_TEST(I64ShrSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64ShrS,
      [](int64_t lhs, int64_t rhs, bool* trap) { return lhs >> (rhs & 63); });
}

WASM_EXEC_TEST(I64ShrUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64ShrU, [](int64_t lhs, int64_t rhs, bool* trap) {
        return static_cast<uint64_t>(lhs) >> (rhs & 63);
      });
}

WASM_EXEC_TEST(I64DivSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64DivS, [](int64_t lhs, int64_t rhs, bool* trap) {
        *trap = rhs == 0 ||
                (rhs == -1 && lhs == std::numeric_limits<int64_t>::min());
        return *trap ? 0 : lhs / rhs;
      });
}

WASM_EXEC_TEST(I64DivUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64DivU, [](uint64_t lhs, uint64_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap ? 0 : lhs / rhs;
      });
}

WASM_EXEC_TEST(I64RemSOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64RemS, [](int64_t lhs, int64_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap || rhs == -1 ? 0 : lhs % rhs;
      });
}

WASM_EXEC_TEST(I64RemUOnDifferentRegisters) {
  BinOpOnDifferentRegisters<int64_t>(
      execution_tier, kWasmI64, base::ArrayVector(kSome64BitInputs),
      kExprI64RemU, [](uint64_t lhs, uint64_t rhs, bool* trap) {
        *trap = rhs == 0;
        return *trap ? 0 : lhs % rhs;
      });
}

#undef B1
#undef B2
#undef RET
#undef RET_I8

}  // namespace v8::internal::wasm
