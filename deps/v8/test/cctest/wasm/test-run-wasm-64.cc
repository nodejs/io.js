// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/bits.h"
#include "src/base/overflowing-math.h"
#include "src/base/safe_conversions.h"
#include "src/codegen/assembler-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_64 {

WASM_EXEC_TEST(I64Const) {
  WasmRunner<int64_t> r(execution_tier);
  const int64_t kExpectedValue = 0x1122334455667788LL;
  // return(kExpectedValue)
  r.Build({WASM_I64V_9(kExpectedValue)});
  CHECK_EQ(kExpectedValue, r.Call());
}

WASM_EXEC_TEST(I64Const_many) {
  int cntr = 0;
  FOR_UINT32_INPUTS(i) {
    WasmRunner<int64_t> r(execution_tier);
    const int64_t kExpectedValue = (static_cast<uint64_t>(i) << 32) | cntr;
    // return(kExpectedValue)
    r.Build({WASM_I64V(kExpectedValue)});
    CHECK_EQ(kExpectedValue, r.Call());
    cntr++;
  }
}

WASM_EXEC_TEST(Return_I64) {
  WasmRunner<int64_t, int64_t> r(execution_tier);

  r.Build({WASM_RETURN(WASM_LOCAL_GET(0))});

  FOR_INT64_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
}

WASM_EXEC_TEST(I64Add) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(base::AddWithWraparound(i, j), r.Call(i, j));
    }
  }
}

// The i64 add and subtract regression tests need a 64-bit value with a non-zero
// upper half. This upper half was clobbering eax, leading to the function
// returning 1 rather than 0.
const int64_t kHasBit33On = 0x100000000;

WASM_EXEC_TEST(Regress5800_Add) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_BLOCK(WASM_BR_IF(0, WASM_I64_EQZ(WASM_I64_ADD(
                                        WASM_I64V(0), WASM_I64V(kHasBit33On)))),
                      WASM_RETURN(WASM_I32V(0))),
           WASM_I32V(0)});
  CHECK_EQ(0, r.Call());
}

WASM_EXEC_TEST(I64Sub) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(base::SubWithWraparound(i, j), r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(Regress5800_Sub) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_BLOCK(WASM_BR_IF(0, WASM_I64_EQZ(WASM_I64_SUB(
                                        WASM_I64V(0), WASM_I64V(kHasBit33On)))),
                      WASM_RETURN(WASM_I32V(0))),
           WASM_I32V(0)});
  CHECK_EQ(0, r.Call());
}

WASM_EXEC_TEST(I64AddUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(static_cast<int32_t>(base::AddWithWraparound(i, j)),
               r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64SubUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(static_cast<int32_t>(base::SubWithWraparound(i, j)),
               r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64MulUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(static_cast<int32_t>(base::MulWithWraparound(i, j)),
               r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64ShlUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(base::ShlWithWraparound(i, j));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64ShrUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      int32_t expected = static_cast<int32_t>((i) >> (j & 0x3F));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64SarUseOnlyLowWord) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I32_CONVERT_I64(
      WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int32_t expected = static_cast<int32_t>((i) >> (j & 0x3F));
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64DivS) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_DIVS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      if (j == 0) {
        CHECK_TRAP64(r.Call(i, j));
      } else if (j == -1 && i == std::numeric_limits<int64_t>::min()) {
        CHECK_TRAP64(r.Call(i, j));
      } else {
        CHECK_EQ(i / j, r.Call(i, j));
      }
    }
  }
}

WASM_EXEC_TEST(I64DivS_Trap) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_DIVS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  CHECK_EQ(0, r.Call(int64_t{0}, int64_t{100}));
  CHECK_TRAP64(r.Call(int64_t{100}, int64_t{0}));
  CHECK_TRAP64(r.Call(int64_t{-1001}, int64_t{0}));
  CHECK_TRAP64(r.Call(std::numeric_limits<int64_t>::min(), int64_t{-1}));
  CHECK_TRAP64(r.Call(std::numeric_limits<int64_t>::min(), int64_t{0}));
}

WASM_EXEC_TEST(I64DivS_Byzero_Const) {
  for (int8_t denom = -2; denom < 8; denom++) {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_DIVS(WASM_LOCAL_GET(0), WASM_I64V_1(denom))});
    for (int64_t val = -7; val < 8; val++) {
      if (denom == 0) {
        CHECK_TRAP64(r.Call(val));
      } else {
        CHECK_EQ(val / denom, r.Call(val));
      }
    }
  }
}

WASM_EXEC_TEST(I64DivU) {
  WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_DIVU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      if (j == 0) {
        CHECK_TRAP64(r.Call(i, j));
      } else {
        CHECK_EQ(i / j, r.Call(i, j));
      }
    }
  }
}

WASM_EXEC_TEST(I64DivU_Trap) {
  WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_DIVU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  CHECK_EQ(0, r.Call(uint64_t{0}, uint64_t{100}));
  CHECK_TRAP64(r.Call(uint64_t{100}, uint64_t{0}));
  CHECK_TRAP64(r.Call(uint64_t{1001}, uint64_t{0}));
  CHECK_TRAP64(r.Call(std::numeric_limits<uint64_t>::max(), uint64_t{0}));
}

WASM_EXEC_TEST(I64DivU_Byzero_Const) {
  for (uint64_t denom = 0xFFFFFFFFFFFFFFFE; denom < 8; denom++) {
    WasmRunner<uint64_t, uint64_t> r(execution_tier);
    r.Build({WASM_I64_DIVU(WASM_LOCAL_GET(0), WASM_I64V_1(denom))});

    for (uint64_t val = 0xFFFFFFFFFFFFFFF0; val < 8; val++) {
      if (denom == 0) {
        CHECK_TRAP64(r.Call(val));
      } else {
        CHECK_EQ(val / denom, r.Call(val));
      }
    }
  }
}

WASM_EXEC_TEST(I64RemS) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_REMS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      if (j == 0) {
        CHECK_TRAP64(r.Call(i, j));
      } else if (j == -1) {
        CHECK_EQ(0, r.Call(i, j));
      } else {
        CHECK_EQ(i % j, r.Call(i, j));
      }
    }
  }
}

WASM_EXEC_TEST(I64RemS_Trap) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_REMS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  CHECK_EQ(33, r.Call(int64_t{133}, int64_t{100}));
  CHECK_EQ(0, r.Call(std::numeric_limits<int64_t>::min(), int64_t{-1}));
  CHECK_TRAP64(r.Call(int64_t{100}, int64_t{0}));
  CHECK_TRAP64(r.Call(int64_t{-1001}, int64_t{0}));
  CHECK_TRAP64(r.Call(std::numeric_limits<int64_t>::min(), int64_t{0}));
}

WASM_EXEC_TEST(I64RemU) {
  WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_REMU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      if (j == 0) {
        CHECK_TRAP64(r.Call(i, j));
      } else {
        CHECK_EQ(i % j, r.Call(i, j));
      }
    }
  }
}

WASM_EXEC_TEST(I64RemU_Trap) {
  WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_REMU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  CHECK_EQ(17, r.Call(uint64_t{217}, uint64_t{100}));
  CHECK_TRAP64(r.Call(uint64_t{100}, uint64_t{0}));
  CHECK_TRAP64(r.Call(uint64_t{1001}, uint64_t{0}));
  CHECK_TRAP64(r.Call(std::numeric_limits<uint64_t>::max(), uint64_t{0}));
}

WASM_EXEC_TEST(I64And) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_AND(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((i) & (j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64Ior) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_IOR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((i) | (j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64Xor) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_XOR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((i) ^ (j), r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64Shl) {
  {
    WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
    r.Build({WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

    FOR_UINT64_INPUTS(i) {
      FOR_UINT64_INPUTS(j) {
        uint64_t expected = (i) << (j & 0x3F);
        CHECK_EQ(expected, r.Call(i, j));
      }
    }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_I64V_1(0))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i << 0, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_I64V_1(32))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i << 32, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_I64V_1(20))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i << 20, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_I64V_1(40))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i << 40, r.Call(i)); }
  }
}

WASM_EXEC_TEST(I64ShrU) {
  {
    WasmRunner<uint64_t, uint64_t, uint64_t> r(execution_tier);
    r.Build({WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

    FOR_UINT64_INPUTS(i) {
      FOR_UINT64_INPUTS(j) {
        uint64_t expected = (i) >> (j & 0x3F);
        CHECK_EQ(expected, r.Call(i, j));
      }
    }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_I64V_1(0))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i >> 0, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_I64V_1(32))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i >> 32, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_I64V_1(20))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i >> 20, r.Call(i)); }
  }
  {
    WasmRunner<uint64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_I64V_1(40))});
    FOR_UINT64_INPUTS(i) { CHECK_EQ(i >> 40, r.Call(i)); }
  }
}

WASM_EXEC_TEST(I64ShrS) {
  {
    WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

    FOR_INT64_INPUTS(i) {
      FOR_INT64_INPUTS(j) {
        int64_t expected = (i) >> (j & 0x3F);
        CHECK_EQ(expected, r.Call(i, j));
      }
    }
  }
  {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_I64V_1(0))});
    FOR_INT64_INPUTS(i) { CHECK_EQ(i >> 0, r.Call(i)); }
  }
  {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_I64V_1(32))});
    FOR_INT64_INPUTS(i) { CHECK_EQ(i >> 32, r.Call(i)); }
  }
  {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_I64V_1(20))});
    FOR_INT64_INPUTS(i) { CHECK_EQ(i >> 20, r.Call(i)); }
  }
  {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_I64V_1(40))});
    FOR_INT64_INPUTS(i) { CHECK_EQ(i >> 40, r.Call(i)); }
  }
}

WASM_EXEC_TEST(I64Eq) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_EQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i == j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64Ne) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_NE(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i != j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64LtS) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_LTS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i < j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64LeS) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_LES(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i <= j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64LtU) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_LTU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(i < j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64LeU) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_LEU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(i <= j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64GtS) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_GTS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i > j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64GeS) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_GES(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(i >= j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64GtU) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_GTU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(i > j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I64GeU) {
  WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_GEU(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(i >= j ? 1 : 0, r.Call(i, j)); }
  }
}

WASM_EXEC_TEST(I32ConvertI64) {
  FOR_INT64_INPUTS(i) {
    WasmRunner<int32_t> r(execution_tier);
    r.Build({WASM_I32_CONVERT_I64(WASM_I64V(i))});
    CHECK_EQ(static_cast<int32_t>(i), r.Call());
  }
}

WASM_EXEC_TEST(I64SConvertI32) {
  WasmRunner<int64_t, int32_t> r(execution_tier);
  r.Build({WASM_I64_SCONVERT_I32(WASM_LOCAL_GET(0))});
  FOR_INT32_INPUTS(i) { CHECK_EQ(static_cast<int64_t>(i), r.Call(i)); }
}

WASM_EXEC_TEST(I64UConvertI32) {
  WasmRunner<int64_t, uint32_t> r(execution_tier);
  r.Build({WASM_I64_UCONVERT_I32(WASM_LOCAL_GET(0))});
  FOR_UINT32_INPUTS(i) { CHECK_EQ(static_cast<int64_t>(i), r.Call(i)); }
}

WASM_EXEC_TEST(I64Popcnt) {
  struct {
    int64_t expected;
    uint64_t input;
  } values[] = {{64, 0xFFFFFFFFFFFFFFFF},
                {0, 0x0000000000000000},
                {2, 0x0000080000008000},
                {26, 0x1123456782345678},
                {38, 0xFFEDCBA09EDCBA09}};

  WasmRunner<int64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_POPCNT(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(values[i].expected, r.Call(values[i].input));
  }
}

WASM_EXEC_TEST(F32SConvertI64) {
  WasmRunner<float, int64_t> r(execution_tier);
  r.Build({WASM_F32_SCONVERT_I64(WASM_LOCAL_GET(0))});
  FOR_INT64_INPUTS(i) { CHECK_FLOAT_EQ(static_cast<float>(i), r.Call(i)); }
}

WASM_EXEC_TEST(F32UConvertI64) {
  struct {
    uint64_t input;
    uint32_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3F800000},
                {0xFFFFFFFF, 0x4F800000},
                {0x1B09788B, 0x4DD84BC4},
                {0x4C5FCE8, 0x4C98BF9D},
                {0xCC0DE5BF, 0x4F4C0DE6},
                {0x2, 0x40000000},
                {0x3, 0x40400000},
                {0x4, 0x40800000},
                {0x5, 0x40A00000},
                {0x8, 0x41000000},
                {0x9, 0x41100000},
                {0xFFFFFFFFFFFFFFFF, 0x5F800000},
                {0xFFFFFFFFFFFFFFFE, 0x5F800000},
                {0xFFFFFFFFFFFFFFFD, 0x5F800000},
                {0x0, 0x0},
                {0x100000000, 0x4F800000},
                {0xFFFFFFFF00000000, 0x5F800000},
                {0x1B09788B00000000, 0x5DD84BC4},
                {0x4C5FCE800000000, 0x5C98BF9D},
                {0xCC0DE5BF00000000, 0x5F4C0DE6},
                {0x200000000, 0x50000000},
                {0x300000000, 0x50400000},
                {0x400000000, 0x50800000},
                {0x500000000, 0x50A00000},
                {0x800000000, 0x51000000},
                {0x900000000, 0x51100000},
                {0x273A798E187937A3, 0x5E1CE9E6},
                {0xECE3AF835495A16B, 0x5F6CE3B0},
                {0xB668ECC11223344, 0x5D3668ED},
                {0x9E, 0x431E0000},
                {0x43, 0x42860000},
                {0xAF73, 0x472F7300},
                {0x116B, 0x458B5800},
                {0x658ECC, 0x4ACB1D98},
                {0x2B3B4C, 0x4A2CED30},
                {0x88776655, 0x4F087766},
                {0x70000000, 0x4EE00000},
                {0x7200000, 0x4CE40000},
                {0x7FFFFFFF, 0x4F000000},
                {0x56123761, 0x4EAC246F},
                {0x7FFFFF00, 0x4EFFFFFE},
                {0x761C4761EEEEEEEE, 0x5EEC388F},
                {0x80000000EEEEEEEE, 0x5F000000},
                {0x88888888DDDDDDDD, 0x5F088889},
                {0xA0000000DDDDDDDD, 0x5F200000},
                {0xDDDDDDDDAAAAAAAA, 0x5F5DDDDE},
                {0xE0000000AAAAAAAA, 0x5F600000},
                {0xEEEEEEEEEEEEEEEE, 0x5F6EEEEF},
                {0xFFFFFFFDEEEEEEEE, 0x5F800000},
                {0xF0000000DDDDDDDD, 0x5F700000},
                {0x7FFFFFDDDDDDDD, 0x5B000000},
                {0x3FFFFFAAAAAAAA, 0x5A7FFFFF},
                {0x1FFFFFAAAAAAAA, 0x59FFFFFD},
                {0xFFFFF, 0x497FFFF0},
                {0x7FFFF, 0x48FFFFE0},
                {0x3FFFF, 0x487FFFC0},
                {0x1FFFF, 0x47FFFF80},
                {0xFFFF, 0x477FFF00},
                {0x7FFF, 0x46FFFE00},
                {0x3FFF, 0x467FFC00},
                {0x1FFF, 0x45FFF800},
                {0xFFF, 0x457FF000},
                {0x7FF, 0x44FFE000},
                {0x3FF, 0x447FC000},
                {0x1FF, 0x43FF8000},
                {0x3FFFFFFFFFFF, 0x56800000},
                {0x1FFFFFFFFFFF, 0x56000000},
                {0xFFFFFFFFFFF, 0x55800000},
                {0x7FFFFFFFFFF, 0x55000000},
                {0x3FFFFFFFFFF, 0x54800000},
                {0x1FFFFFFFFFF, 0x54000000},
                {0x8000008000000000, 0x5F000000},
                {0x8000008000000001, 0x5F000001},
                {0x8000000000000400, 0x5F000000},
                {0x8000000000000401, 0x5F000000},
                {0x20000020000001, 0x5a000001},
                {0xFFFFFe8000000001, 0x5f7FFFFF}};
  WasmRunner<float, uint64_t> r(execution_tier);
  r.Build({WASM_F32_UCONVERT_I64(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(base::bit_cast<float>(values[i].expected),
             r.Call(values[i].input));
  }
}

WASM_EXEC_TEST(F64SConvertI64) {
  WasmRunner<double, int64_t> r(execution_tier);
  r.Build({WASM_F64_SCONVERT_I64(WASM_LOCAL_GET(0))});
  FOR_INT64_INPUTS(i) { CHECK_DOUBLE_EQ(static_cast<double>(i), r.Call(i)); }
}

WASM_EXEC_TEST(F64UConvertI64) {
  struct {
    uint64_t input;
    uint64_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3FF0000000000000},
                {0xFFFFFFFF, 0x41EFFFFFFFE00000},
                {0x1B09788B, 0x41BB09788B000000},
                {0x4C5FCE8, 0x419317F3A0000000},
                {0xCC0DE5BF, 0x41E981BCB7E00000},
                {0x2, 0x4000000000000000},
                {0x3, 0x4008000000000000},
                {0x4, 0x4010000000000000},
                {0x5, 0x4014000000000000},
                {0x8, 0x4020000000000000},
                {0x9, 0x4022000000000000},
                {0xFFFFFFFFFFFFFFFF, 0x43F0000000000000},
                {0xFFFFFFFFFFFFFFFE, 0x43F0000000000000},
                {0xFFFFFFFFFFFFFFFD, 0x43F0000000000000},
                {0x100000000, 0x41F0000000000000},
                {0xFFFFFFFF00000000, 0x43EFFFFFFFE00000},
                {0x1B09788B00000000, 0x43BB09788B000000},
                {0x4C5FCE800000000, 0x439317F3A0000000},
                {0xCC0DE5BF00000000, 0x43E981BCB7E00000},
                {0x200000000, 0x4200000000000000},
                {0x300000000, 0x4208000000000000},
                {0x400000000, 0x4210000000000000},
                {0x500000000, 0x4214000000000000},
                {0x800000000, 0x4220000000000000},
                {0x900000000, 0x4222000000000000},
                {0x273A798E187937A3, 0x43C39D3CC70C3C9C},
                {0xECE3AF835495A16B, 0x43ED9C75F06A92B4},
                {0xB668ECC11223344, 0x43A6CD1D98224467},
                {0x9E, 0x4063C00000000000},
                {0x43, 0x4050C00000000000},
                {0xAF73, 0x40E5EE6000000000},
                {0x116B, 0x40B16B0000000000},
                {0x658ECC, 0x415963B300000000},
                {0x2B3B4C, 0x41459DA600000000},
                {0x88776655, 0x41E10EECCAA00000},
                {0x70000000, 0x41DC000000000000},
                {0x7200000, 0x419C800000000000},
                {0x7FFFFFFF, 0x41DFFFFFFFC00000},
                {0x56123761, 0x41D5848DD8400000},
                {0x7FFFFF00, 0x41DFFFFFC0000000},
                {0x761C4761EEEEEEEE, 0x43DD8711D87BBBBC},
                {0x80000000EEEEEEEE, 0x43E00000001DDDDE},
                {0x88888888DDDDDDDD, 0x43E11111111BBBBC},
                {0xA0000000DDDDDDDD, 0x43E40000001BBBBC},
                {0xDDDDDDDDAAAAAAAA, 0x43EBBBBBBBB55555},
                {0xE0000000AAAAAAAA, 0x43EC000000155555},
                {0xEEEEEEEEEEEEEEEE, 0x43EDDDDDDDDDDDDE},
                {0xFFFFFFFDEEEEEEEE, 0x43EFFFFFFFBDDDDE},
                {0xF0000000DDDDDDDD, 0x43EE0000001BBBBC},
                {0x7FFFFFDDDDDDDD, 0x435FFFFFF7777777},
                {0x3FFFFFAAAAAAAA, 0x434FFFFFD5555555},
                {0x1FFFFFAAAAAAAA, 0x433FFFFFAAAAAAAA},
                {0xFFFFF, 0x412FFFFE00000000},
                {0x7FFFF, 0x411FFFFC00000000},
                {0x3FFFF, 0x410FFFF800000000},
                {0x1FFFF, 0x40FFFFF000000000},
                {0xFFFF, 0x40EFFFE000000000},
                {0x7FFF, 0x40DFFFC000000000},
                {0x3FFF, 0x40CFFF8000000000},
                {0x1FFF, 0x40BFFF0000000000},
                {0xFFF, 0x40AFFE0000000000},
                {0x7FF, 0x409FFC0000000000},
                {0x3FF, 0x408FF80000000000},
                {0x1FF, 0x407FF00000000000},
                {0x3FFFFFFFFFFF, 0x42CFFFFFFFFFFF80},
                {0x1FFFFFFFFFFF, 0x42BFFFFFFFFFFF00},
                {0xFFFFFFFFFFF, 0x42AFFFFFFFFFFE00},
                {0x7FFFFFFFFFF, 0x429FFFFFFFFFFC00},
                {0x3FFFFFFFFFF, 0x428FFFFFFFFFF800},
                {0x1FFFFFFFFFF, 0x427FFFFFFFFFF000},
                {0x8000008000000000, 0x43E0000010000000},
                {0x8000008000000001, 0x43E0000010000000},
                {0x8000000000000400, 0x43E0000000000000},
                {0x8000000000000401, 0x43E0000000000001}};
  WasmRunner<double, uint64_t> r(execution_tier);
  r.Build({WASM_F64_UCONVERT_I64(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(base::bit_cast<double>(values[i].expected),
             r.Call(values[i].input));
  }
}

WASM_EXEC_TEST(I64SConvertF32) {
  WasmRunner<int64_t, float> r(execution_tier);
  r.Build({WASM_I64_SCONVERT_F32(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      CHECK_EQ(static_cast<int64_t>(i), r.Call(i));
    } else {
      CHECK_TRAP64(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I64SConvertSatF32) {
  WasmRunner<int64_t, float> r(execution_tier);
  r.Build({WASM_I64_SCONVERT_SAT_F32(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) {
    int64_t expected;
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      expected = static_cast<int64_t>(i);
    } else if (std::isnan(i)) {
      expected = static_cast<int64_t>(0);
    } else if (i < 0.0) {
      expected = std::numeric_limits<int64_t>::min();
    } else {
      expected = std::numeric_limits<int64_t>::max();
    }
    int64_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I64SConvertF64) {
  WasmRunner<int64_t, double> r(execution_tier);
  r.Build({WASM_I64_SCONVERT_F64(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      CHECK_EQ(static_cast<int64_t>(i), r.Call(i));
    } else {
      CHECK_TRAP64(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I64SConvertSatF64) {
  WasmRunner<int64_t, double> r(execution_tier);
  r.Build({WASM_I64_SCONVERT_SAT_F64(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) {
    int64_t expected;
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      expected = static_cast<int64_t>(i);
    } else if (std::isnan(i)) {
      expected = static_cast<int64_t>(0);
    } else if (i < 0.0) {
      expected = std::numeric_limits<int64_t>::min();
    } else {
      expected = std::numeric_limits<int64_t>::max();
    }
    int64_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I64UConvertF32) {
  WasmRunner<uint64_t, float> r(execution_tier);
  r.Build({WASM_I64_UCONVERT_F32(WASM_LOCAL_GET(0))});

  FOR_FLOAT32_INPUTS(i) {
    if (i < static_cast<float>(std::numeric_limits<uint64_t>::max()) &&
        i > -1) {
      CHECK_EQ(static_cast<uint64_t>(i), r.Call(i));
    } else {
      CHECK_TRAP64(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I64UConvertSatF32) {
  WasmRunner<int64_t, float> r(execution_tier);
  r.Build({WASM_I64_UCONVERT_SAT_F32(WASM_LOCAL_GET(0))});
  FOR_FLOAT32_INPUTS(i) {
    uint64_t expected;
    if (i < static_cast<float>(std::numeric_limits<uint64_t>::max()) &&
        i > -1) {
      expected = static_cast<uint64_t>(i);
    } else if (std::isnan(i)) {
      expected = static_cast<uint64_t>(0);
    } else if (i < 0.0) {
      expected = std::numeric_limits<uint64_t>::min();
    } else {
      expected = std::numeric_limits<uint64_t>::max();
    }
    uint64_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(I64UConvertF64) {
  WasmRunner<uint64_t, double> r(execution_tier);
  r.Build({WASM_I64_UCONVERT_F64(WASM_LOCAL_GET(0))});

  FOR_FLOAT64_INPUTS(i) {
    if (i < static_cast<float>(std::numeric_limits<uint64_t>::max()) &&
        i > -1) {
      CHECK_EQ(static_cast<uint64_t>(i), r.Call(i));
    } else {
      CHECK_TRAP64(r.Call(i));
    }
  }
}

WASM_EXEC_TEST(I64UConvertSatF64) {
  WasmRunner<int64_t, double> r(execution_tier);
  r.Build({WASM_I64_UCONVERT_SAT_F64(WASM_LOCAL_GET(0))});
  FOR_FLOAT64_INPUTS(i) {
    int64_t expected;
    if (i < static_cast<float>(std::numeric_limits<uint64_t>::max()) &&
        i > -1) {
      expected = static_cast<uint64_t>(i);
    } else if (std::isnan(i)) {
      expected = static_cast<uint64_t>(0);
    } else if (i < 0.0) {
      expected = std::numeric_limits<uint64_t>::min();
    } else {
      expected = std::numeric_limits<uint64_t>::max();
    }
    int64_t found = r.Call(i);
    CHECK_EQ(expected, found);
  }
}

WASM_EXEC_TEST(CallI64Parameter) {
  ValueType param_types[20];
  for (int i = 0; i < 20; i++) param_types[i] = kWasmI64;
  param_types[3] = kWasmI32;
  param_types[4] = kWasmI32;
  FunctionSig sig(1, 19, param_types);
  for (int i = 0; i < 19; i++) {
    if (i == 2 || i == 3) continue;
    WasmRunner<int32_t> r(execution_tier);
    // Build the target function.
    WasmFunctionCompiler& t = r.NewFunction(&sig);
    t.Build({WASM_LOCAL_GET(i)});

    // Build the calling function.
    r.Build({WASM_I32_CONVERT_I64(WASM_CALL_FUNCTION(
        t.function_index(), WASM_I64V_9(0xBCD12340000000B),
        WASM_I64V_9(0xBCD12340000000C), WASM_I32V_1(0xD),
        WASM_I32_CONVERT_I64(WASM_I64V_9(0xBCD12340000000E)),
        WASM_I64V_9(0xBCD12340000000F), WASM_I64V_10(0xBCD1234000000010),
        WASM_I64V_10(0xBCD1234000000011), WASM_I64V_10(0xBCD1234000000012),
        WASM_I64V_10(0xBCD1234000000013), WASM_I64V_10(0xBCD1234000000014),
        WASM_I64V_10(0xBCD1234000000015), WASM_I64V_10(0xBCD1234000000016),
        WASM_I64V_10(0xBCD1234000000017), WASM_I64V_10(0xBCD1234000000018),
        WASM_I64V_10(0xBCD1234000000019), WASM_I64V_10(0xBCD123400000001A),
        WASM_I64V_10(0xBCD123400000001B), WASM_I64V_10(0xBCD123400000001C),
        WASM_I64V_10(0xBCD123400000001D)))});

    CHECK_EQ(i + 0xB, r.Call());
  }
}

WASM_EXEC_TEST(CallI64Return) {
  ValueType return_types[3];  // TODO(rossberg): support more in the future
  for (int i = 0; i < 3; i++) return_types[i] = kWasmI64;
  return_types[1] = kWasmI32;
  FunctionSig sig(2, 1, return_types);

  WasmRunner<int64_t> r(execution_tier);
  // Build the target function.
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  t.Build({WASM_LOCAL_GET(0), WASM_I32V(7)});

  // Build the first calling function.
  r.Build({WASM_CALL_FUNCTION(t.function_index(), WASM_I64V(0xBCD12340000000B)),
           WASM_DROP});

  CHECK_EQ(0xBCD12340000000B, r.Call());
}

void TestI64Binop(TestExecutionTier execution_tier, WasmOpcode opcode,
                  int64_t expected, int64_t a, int64_t b) {
  {
    WasmRunner<int64_t> r(execution_tier);
    // return K op K
    r.Build({WASM_BINOP(opcode, WASM_I64V(a), WASM_I64V(b))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
    // return a op b
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

void TestI64Cmp(TestExecutionTier execution_tier, WasmOpcode opcode,
                int64_t expected, int64_t a, int64_t b) {
  {
    WasmRunner<int32_t> r(execution_tier);
    // return K op K
    r.Build({WASM_BINOP(opcode, WASM_I64V(a), WASM_I64V(b))});
    CHECK_EQ(expected, r.Call());
  }
  {
    WasmRunner<int32_t, int64_t, int64_t> r(execution_tier);
    // return a op b
    r.Build({WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    CHECK_EQ(expected, r.Call(a, b));
  }
}

WASM_EXEC_TEST(I64Binops) {
  TestI64Binop(execution_tier, kExprI64Add, -5586332274295447011,
               0x501B72EBABC26847, 0x625DE9793D8F79D6);
  TestI64Binop(execution_tier, kExprI64Sub, 9001903251710731490,
               0xF24FE6474640002E, 0x7562B6F711991B4C);
  TestI64Binop(execution_tier, kExprI64Mul, -4569547818546064176,
               0x231A263C2CBC6451, 0xEAD44DE6BD3E23D0);
  TestI64Binop(execution_tier, kExprI64Mul, -25963122347507043,
               0x4DA1FA47C9352B73, 0x91FE82317AA035AF);
  TestI64Binop(execution_tier, kExprI64Mul, 7640290486138131960,
               0x185731ABE8EEA47C, 0x714EC59F1380D4C2);
  TestI64Binop(execution_tier, kExprI64DivS, -91517, 0x93B1190A34DE56A0,
               0x00004D8F68863948);
  TestI64Binop(execution_tier, kExprI64DivU, 149016, 0xE15B3727E8A2080A,
               0x0000631BFA72DB8B);
  TestI64Binop(execution_tier, kExprI64RemS, -664128064149968,
               0x9A78B4E4FE708692, 0x0003E0B6B3BE7609);
  TestI64Binop(execution_tier, kExprI64RemU, 1742040017332765,
               0x0CE84708C6258C81, 0x000A6FDE82016697);
  TestI64Binop(execution_tier, kExprI64And, 2531040582801836054,
               0xAF257D1602644A16, 0x33B290A91A10D997);
  TestI64Binop(execution_tier, kExprI64Ior, 8556201506536114940,
               0x169D9BE7BD3F0A5C, 0x66BCA28D77AF40E8);
  TestI64Binop(execution_tier, kExprI64Xor, -4605655183785456377,
               0xB6EA20A5D48E85B8, 0x76FF4DA6C80688BF);
  TestI64Binop(execution_tier, kExprI64Shl, -7240704056088331264,
               0xEF4DC1ED030E8FFE, 9);
  TestI64Binop(execution_tier, kExprI64ShrU, 12500673744059159,
               0xB1A52FA7DEEC5D14, 10);
  TestI64Binop(execution_tier, kExprI64ShrS, 1725103446999874,
               0x3107C791461A112B, 11);
  TestI64Binop(execution_tier, kExprI64Ror, -8960135652432576946,
               0x73418D1717E4E83A, 12);
  TestI64Binop(execution_tier, kExprI64Ror, 7617662827409989779,
               0xEBFF67CF0C126D36, 13);
  TestI64Binop(execution_tier, kExprI64Rol, -2097714064174346012,
               0x43938B8DB0B0F230, 14);
  TestI64Binop(execution_tier, kExprI64Rol, 8728493013947314237,
               0xE07AF243AC4D219D, 15);
}

WASM_EXEC_TEST(I64Compare) {
  TestI64Cmp(execution_tier, kExprI64Eq, 0, 0xB915D8FA494064F0,
             0x04D700B2536019A3);
  TestI64Cmp(execution_tier, kExprI64Ne, 1, 0xC2FAFAAAB0446CDC,
             0x52A3328F780C97A3);
  TestI64Cmp(execution_tier, kExprI64LtS, 0, 0x673636E6306B0578,
             0x028EC9ECA78F7227);
  TestI64Cmp(execution_tier, kExprI64LeS, 1, 0xAE5214114B86A0FA,
             0x7C1D21DA3DFD0CCF);
  TestI64Cmp(execution_tier, kExprI64LtU, 0, 0x7D52166381EC1CE0,
             0x59F4A6A9E78CD3D8);
  TestI64Cmp(execution_tier, kExprI64LeU, 1, 0xE4169A385C7EA0E0,
             0xFBDBED2C8781E5BC);
  TestI64Cmp(execution_tier, kExprI64GtS, 0, 0x9D08FF8FB5F42E81,
             0xD4E5C9D7FE09F621);
  TestI64Cmp(execution_tier, kExprI64GeS, 1, 0x78DA3B2F73264E0F,
             0x6FE5E2A67C501CBE);
  TestI64Cmp(execution_tier, kExprI64GtU, 0, 0x8F691284E44F7DA9,
             0xD5EA9BC1EE149192);
  TestI64Cmp(execution_tier, kExprI64GeU, 0, 0x0886A0C58C7AA224,
             0x5DDBE5A81FD7EE47);
}

WASM_EXEC_TEST(I64Clz) {
  struct {
    int64_t expected;
    uint64_t input;
  } values[] = {{0, 0x8000100000000000},  {1, 0x4000050000000000},
                {2, 0x2000030000000000},  {3, 0x1000000300000000},
                {4, 0x0805000000000000},  {5, 0x0400600000000000},
                {6, 0x0200000000000000},  {7, 0x010000A000000000},
                {8, 0x00800C0000000000},  {9, 0x0040000000000000},
                {10, 0x0020000D00000000}, {11, 0x00100F0000000000},
                {12, 0x0008000000000000}, {13, 0x0004100000000000},
                {14, 0x0002002000000000}, {15, 0x0001030000000000},
                {16, 0x0000804000000000}, {17, 0x0000400500000000},
                {18, 0x0000205000000000}, {19, 0x0000170000000000},
                {20, 0x0000087000000000}, {21, 0x0000040500000000},
                {22, 0x0000020300000000}, {23, 0x0000010100000000},
                {24, 0x0000008900000000}, {25, 0x0000004100000000},
                {26, 0x0000002200000000}, {27, 0x0000001300000000},
                {28, 0x0000000800000000}, {29, 0x0000000400000000},
                {30, 0x0000000200000000}, {31, 0x0000000100000000},
                {32, 0x0000000080001000}, {33, 0x0000000040000500},
                {34, 0x0000000020000300}, {35, 0x0000000010000003},
                {36, 0x0000000008050000}, {37, 0x0000000004006000},
                {38, 0x0000000002000000}, {39, 0x00000000010000A0},
                {40, 0x0000000000800C00}, {41, 0x0000000000400000},
                {42, 0x000000000020000D}, {43, 0x0000000000100F00},
                {44, 0x0000000000080000}, {45, 0x0000000000041000},
                {46, 0x0000000000020020}, {47, 0x0000000000010300},
                {48, 0x0000000000008040}, {49, 0x0000000000004005},
                {50, 0x0000000000002050}, {51, 0x0000000000001700},
                {52, 0x0000000000000870}, {53, 0x0000000000000405},
                {54, 0x0000000000000203}, {55, 0x0000000000000101},
                {56, 0x0000000000000089}, {57, 0x0000000000000041},
                {58, 0x0000000000000022}, {59, 0x0000000000000013},
                {60, 0x0000000000000008}, {61, 0x0000000000000004},
                {62, 0x0000000000000002}, {63, 0x0000000000000001},
                {64, 0x0000000000000000}};

  WasmRunner<int64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_CLZ(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(values[i].expected, r.Call(values[i].input));
  }
}

WASM_EXEC_TEST(I64Ctz) {
  struct {
    int64_t expected;
    uint64_t input;
  } values[] = {{64, 0x0000000000000000}, {63, 0x8000000000000000},
                {62, 0x4000000000000000}, {61, 0x2000000000000000},
                {60, 0x1000000000000000}, {59, 0xA800000000000000},
                {58, 0xF400000000000000}, {57, 0x6200000000000000},
                {56, 0x9100000000000000}, {55, 0xCD80000000000000},
                {54, 0x0940000000000000}, {53, 0xAF20000000000000},
                {52, 0xAC10000000000000}, {51, 0xE0B8000000000000},
                {50, 0x9CE4000000000000}, {49, 0xC792000000000000},
                {48, 0xB8F1000000000000}, {47, 0x3B9F800000000000},
                {46, 0xDB4C400000000000}, {45, 0xE9A3200000000000},
                {44, 0xFCA6100000000000}, {43, 0x6C8A780000000000},
                {42, 0x8CE5A40000000000}, {41, 0xCB7D020000000000},
                {40, 0xCB4DC10000000000}, {39, 0xDFBEC58000000000},
                {38, 0x27A9DB4000000000}, {37, 0xDE3BCB2000000000},
                {36, 0xD7E8A61000000000}, {35, 0x9AFDBC8800000000},
                {34, 0x9AFDBC8400000000}, {33, 0x9AFDBC8200000000},
                {32, 0x9AFDBC8100000000}, {31, 0x0000000080000000},
                {30, 0x0000000040000000}, {29, 0x0000000020000000},
                {28, 0x0000000010000000}, {27, 0x00000000A8000000},
                {26, 0x00000000F4000000}, {25, 0x0000000062000000},
                {24, 0x0000000091000000}, {23, 0x00000000CD800000},
                {22, 0x0000000009400000}, {21, 0x00000000AF200000},
                {20, 0x00000000AC100000}, {19, 0x00000000E0B80000},
                {18, 0x000000009CE40000}, {17, 0x00000000C7920000},
                {16, 0x00000000B8F10000}, {15, 0x000000003B9F8000},
                {14, 0x00000000DB4C4000}, {13, 0x00000000E9A32000},
                {12, 0x00000000FCA61000}, {11, 0x000000006C8A7800},
                {10, 0x000000008CE5A400}, {9, 0x00000000CB7D0200},
                {8, 0x00000000CB4DC100},  {7, 0x00000000DFBEC580},
                {6, 0x0000000027A9DB40},  {5, 0x00000000DE3BCB20},
                {4, 0x00000000D7E8A610},  {3, 0x000000009AFDBC88},
                {2, 0x000000009AFDBC84},  {1, 0x000000009AFDBC82},
                {0, 0x000000009AFDBC81}};

  WasmRunner<int64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_CTZ(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(values[i].expected, r.Call(values[i].input));
  }
}

WASM_EXEC_TEST(I64Popcnt2) {
  struct {
    int64_t expected;
    uint64_t input;
  } values[] = {{64, 0xFFFFFFFFFFFFFFFF},
                {0, 0x0000000000000000},
                {2, 0x0000080000008000},
                {26, 0x1123456782345678},
                {38, 0xFFEDCBA09EDCBA09}};

  WasmRunner<int64_t, uint64_t> r(execution_tier);
  r.Build({WASM_I64_POPCNT(WASM_LOCAL_GET(0))});
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(values[i].expected, r.Call(values[i].input));
  }
}

// Test the WasmRunner with an Int64 return value and different numbers of
// Int64 parameters.
WASM_EXEC_TEST(I64WasmRunner) {
  FOR_INT64_INPUTS(i) {
    WasmRunner<int64_t> r(execution_tier);
    r.Build({WASM_I64V(i)});
    CHECK_EQ(i, r.Call());
  }
  {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    r.Build({WASM_LOCAL_GET(0)});
    FOR_INT64_INPUTS(i) { CHECK_EQ(i, r.Call(i)); }
  }
  {
    WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_XOR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    FOR_INT64_INPUTS(i) {
      FOR_INT64_INPUTS(j) { CHECK_EQ(i ^ j, r.Call(i, j)); }
    }
  }
  {
    WasmRunner<int64_t, int64_t, int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_XOR(WASM_LOCAL_GET(0),
                          WASM_I64_XOR(WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)))});
    FOR_INT64_INPUTS(i) {
      FOR_INT64_INPUTS(j) {
        CHECK_EQ(i ^ j ^ j, r.Call(i, j, j));
        CHECK_EQ(j ^ i ^ j, r.Call(j, i, j));
        CHECK_EQ(j ^ j ^ i, r.Call(j, j, i));
      }
    }
  }
  {
    WasmRunner<int64_t, int64_t, int64_t, int64_t, int64_t> r(execution_tier);
    r.Build({WASM_I64_XOR(
        WASM_LOCAL_GET(0),
        WASM_I64_XOR(WASM_LOCAL_GET(1),
                     WASM_I64_XOR(WASM_LOCAL_GET(2), WASM_LOCAL_GET(3))))});
    FOR_INT64_INPUTS(i) {
      FOR_INT64_INPUTS(j) {
        CHECK_EQ(i ^ j ^ j ^ j, r.Call(i, j, j, j));
        CHECK_EQ(j ^ i ^ j ^ j, r.Call(j, i, j, j));
        CHECK_EQ(j ^ j ^ i ^ j, r.Call(j, j, i, j));
        CHECK_EQ(j ^ j ^ j ^ i, r.Call(j, j, j, i));
      }
    }
  }
}

WASM_EXEC_TEST(Call_Int64Sub) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  // Build the target function.
  TestSignatures sigs;
  WasmFunctionCompiler& t = r.NewFunction(sigs.l_ll());
  t.Build({WASM_I64_SUB(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  // Build the caller function.
  r.Build({WASM_CALL_FUNCTION(t.function_index(), WASM_LOCAL_GET(0),
                              WASM_LOCAL_GET(1))});

  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      CHECK_EQ(base::SubWithWraparound(i, j), r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(LoadStoreI64_sx) {
  uint8_t loads[] = {kExprI64LoadMem8S, kExprI64LoadMem16S, kExprI64LoadMem32S,
                     kExprI64LoadMem};

  for (size_t m = 0; m < arraysize(loads); m++) {
    WasmRunner<int64_t> r(execution_tier);
    uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);

    uint8_t code[] = {
        kExprI32Const,    8,  // --
        kExprI32Const,    0,  // --
        loads[m],             // --
        ZERO_ALIGNMENT,       // --
        ZERO_OFFSET,          // --
        kExprI64StoreMem,     // --
        ZERO_ALIGNMENT,       // --
        ZERO_OFFSET,          // --
        kExprI32Const,    0,  // --
        loads[m],             // --
        ZERO_ALIGNMENT,       // --
        ZERO_OFFSET,          // --
    };

    r.Build(code, code + arraysize(code));

    // Try a bunch of different negative values.
    for (int i = -1; i >= -128; i -= 11) {
      int size = 1 << m;
      r.builder().BlankMemory();
      memory[size - 1] = static_cast<uint8_t>(i);  // set the high order byte.

      int64_t expected = static_cast<uint64_t>(static_cast<int64_t>(i))
                         << ((size - 1) * 8);

      CHECK_EQ(expected, r.Call());
      CHECK_EQ(static_cast<uint8_t>(i), memory[8 + size - 1]);
      for (int j = size; j < 8; j++) {
        CHECK_EQ(255, memory[8 + j]);
      }
    }
  }
}

WASM_EXEC_TEST(I64ReinterpretF64) {
  WasmRunner<int64_t> r(execution_tier);
  int64_t* memory =
      r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));

  r.Build({WASM_I64_REINTERPRET_F64(
      WASM_LOAD_MEM(MachineType::Float64(), WASM_ZERO))});

  FOR_INT32_INPUTS(i) {
    int64_t expected =
        base::MulWithWraparound(static_cast<int64_t>(i), int64_t{0x300010001L});
    r.builder().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}

WASM_EXEC_TEST(SignallingNanSurvivesI64ReinterpretF64) {
  WasmRunner<int64_t> r(execution_tier);
  r.Build({WASM_I64_REINTERPRET_F64(WASM_SEQ(kExprF64Const, 0x00, 0x00, 0x00,
                                             0x00, 0x00, 0x00, 0xF4, 0x7F))});

  // This is a signalling nan.
  CHECK_EQ(0x7FF4000000000000, r.Call());
}

WASM_EXEC_TEST(F64ReinterpretI64) {
  WasmRunner<int64_t, int64_t> r(execution_tier);
  int64_t* memory =
      r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));

  r.Build({WASM_STORE_MEM(MachineType::Float64(), WASM_ZERO,
                          WASM_F64_REINTERPRET_I64(WASM_LOCAL_GET(0))),
           WASM_LOCAL_GET(0)});

  FOR_INT32_INPUTS(i) {
    int64_t expected =
        base::MulWithWraparound(static_cast<int64_t>(i), int64_t{0x300010001L});
    CHECK_EQ(expected, r.Call(expected));
    CHECK_EQ(expected, r.builder().ReadMemory<int64_t>(&memory[0]));
  }
}

WASM_EXEC_TEST(LoadMemI64) {
  WasmRunner<int64_t> r(execution_tier);
  int64_t* memory =
      r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));
  r.builder().RandomizeMemory(1111);

  r.Build({WASM_LOAD_MEM(MachineType::Int64(), WASM_ZERO)});

  r.builder().WriteMemory<int64_t>(&memory[0], 0x1ABBCCDD00112233LL);
  CHECK_EQ(0x1ABBCCDD00112233LL, r.Call());

  r.builder().WriteMemory<int64_t>(&memory[0], 0x33AABBCCDD001122LL);
  CHECK_EQ(0x33AABBCCDD001122LL, r.Call());

  r.builder().WriteMemory<int64_t>(&memory[0], 77777777);
  CHECK_EQ(77777777, r.Call());
}

WASM_EXEC_TEST(LoadMemI64_alignment) {
  for (uint8_t alignment = 0; alignment <= 3; alignment++) {
    WasmRunner<int64_t> r(execution_tier);
    int64_t* memory =
        r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));
    r.builder().RandomizeMemory(1111);

    r.Build(
        {WASM_LOAD_MEM_ALIGNMENT(MachineType::Int64(), WASM_ZERO, alignment)});

    r.builder().WriteMemory<int64_t>(&memory[0], 0x1ABBCCDD00112233LL);
    CHECK_EQ(0x1ABBCCDD00112233LL, r.Call());

    r.builder().WriteMemory<int64_t>(&memory[0], 0x33AABBCCDD001122LL);
    CHECK_EQ(0x33AABBCCDD001122LL, r.Call());

    r.builder().WriteMemory<int64_t>(&memory[0], 77777777);
    CHECK_EQ(77777777, r.Call());
  }
}

WASM_EXEC_TEST(MemI64_Sum) {
  const int kNumElems = 20;
  WasmRunner<uint64_t, int32_t> r(execution_tier);
  uint64_t* memory =
      r.builder().AddMemoryElems<uint64_t>(kWasmPageSize / sizeof(uint64_t));
  const uint8_t kSum = r.AllocateLocal(kWasmI64);

  r.Build(
      {WASM_WHILE(
           WASM_LOCAL_GET(0),
           WASM_BLOCK(WASM_LOCAL_SET(
                          kSum, WASM_I64_ADD(WASM_LOCAL_GET(kSum),
                                             WASM_LOAD_MEM(MachineType::Int64(),
                                                           WASM_LOCAL_GET(0)))),
                      WASM_LOCAL_SET(
                          0, WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(8))))),
       WASM_LOCAL_GET(1)});

  // Run 4 trials.
  for (int i = 0; i < 3; i++) {
    r.builder().RandomizeMemory(i * 33);
    uint64_t expected = 0;
    for (size_t j = kNumElems - 1; j > 0; j--) {
      expected += r.builder().ReadMemory(&memory[j]);
    }
    uint64_t result = r.Call(8 * (kNumElems - 1));
    CHECK_EQ(expected, result);
  }
}

WASM_EXEC_TEST(StoreMemI64_alignment) {
  const int64_t kWritten = 0x12345678ABCD0011ll;

  for (uint8_t i = 0; i <= 3; i++) {
    WasmRunner<int64_t, int64_t> r(execution_tier);
    int64_t* memory =
        r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));
    r.Build({WASM_STORE_MEM_ALIGNMENT(MachineType::Int64(), WASM_ZERO, i,
                                      WASM_LOCAL_GET(0)),
             WASM_LOCAL_GET(0)});
    r.builder().RandomizeMemory(1111);
    r.builder().WriteMemory<int64_t>(&memory[0], 0);

    CHECK_EQ(kWritten, r.Call(kWritten));
    CHECK_EQ(kWritten, r.builder().ReadMemory(&memory[0]));
  }
}

WASM_EXEC_TEST(I64Global) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  int64_t* global = r.builder().AddGlobal<int64_t>();
  // global = global + p0
  r.Build({WASM_GLOBAL_SET(
               0, WASM_I64_AND(WASM_GLOBAL_GET(0),
                               WASM_I64_SCONVERT_I32(WASM_LOCAL_GET(0)))),
           WASM_ZERO});

  r.builder().WriteMemory<int64_t>(global, 0xFFFFFFFFFFFFFFFFLL);
  for (int i = 9; i < 444444; i += 111111) {
    int64_t expected = *global & i;
    r.Call(i);
    CHECK_EQ(expected, *global);
  }
}

WASM_EXEC_TEST(I64Eqz) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_EQZ(WASM_LOCAL_GET(0))});

  FOR_INT64_INPUTS(i) {
    int32_t result = i == 0 ? 1 : 0;
    CHECK_EQ(result, r.Call(i));
  }
}

WASM_EXEC_TEST(I64Ror) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_ROR(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      int64_t expected = base::bits::RotateRight64(i, j & 0x3F);
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(I64Rol) {
  WasmRunner<int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_ROL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      int64_t expected = base::bits::RotateLeft64(i, j & 0x3F);
      CHECK_EQ(expected, r.Call(i, j));
    }
  }
}

WASM_EXEC_TEST(StoreMem_offset_oob_i64) {
  static const MachineType machineTypes[] = {
      MachineType::Int8(),   MachineType::Uint8(),  MachineType::Int16(),
      MachineType::Uint16(), MachineType::Int32(),  MachineType::Uint32(),
      MachineType::Int64(),  MachineType::Uint64(), MachineType::Float32(),
      MachineType::Float64()};

  constexpr size_t num_bytes = kWasmPageSize;

  for (size_t m = 0; m < arraysize(machineTypes); m++) {
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

    for (uint32_t offset = boundary + 1; offset < boundary + 19; offset++) {
      CHECK_TRAP32(r.Call(offset));  // out of bounds.
    }
  }
}

WASM_EXEC_TEST(Store_i64_narrowed) {
  constexpr uint8_t kOpcodes[] = {kExprI64StoreMem8, kExprI64StoreMem16,
                                  kExprI64StoreMem32, kExprI64StoreMem};
  int stored_size_in_bytes = 0;
  for (auto opcode : kOpcodes) {
    stored_size_in_bytes = std::max(1, stored_size_in_bytes * 2);
    constexpr int kBytes = 24;
    uint8_t expected_memory[kBytes] = {0};
    WasmRunner<int32_t, int32_t, int64_t> r(execution_tier);
    uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(kWasmPageSize);
    constexpr uint64_t kPattern = 0x0123456789abcdef;

    r.Build({WASM_LOCAL_GET(0),                    // index
             WASM_LOCAL_GET(1),                    // value
             opcode, ZERO_ALIGNMENT, ZERO_OFFSET,  // store
             WASM_ZERO});                          // return value

    for (int i = 0; i <= kBytes - stored_size_in_bytes; ++i) {
      uint64_t pattern = base::bits::RotateLeft64(kPattern, i % 64);
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

WASM_EXEC_TEST(UnalignedInt64Load) {
  WasmRunner<uint64_t> r(execution_tier);
  r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));
  r.Build({WASM_LOAD_MEM_ALIGNMENT(MachineType::Int64(), WASM_ONE, 3)});
  r.Call();
}

WASM_EXEC_TEST(UnalignedInt64Store) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemoryElems<uint64_t>(kWasmPageSize / sizeof(int64_t));
  r.Build({WASM_SEQ(WASM_STORE_MEM_ALIGNMENT(MachineType::Int64(), WASM_ONE, 3,
                                             WASM_I64V_1(1)),
                    WASM_I32V_1(12))});
  r.Call();
}

static void CompileCallIndirectMany(TestExecutionTier tier, ValueType param) {
  // Make sure we don't run out of registers when compiling indirect calls
  // with many many parameters.
  TestSignatures sigs;
  for (uint8_t num_params = 0; num_params < 40; num_params++) {
    WasmRunner<void> r(tier);
    FunctionSig* sig = sigs.many(r.zone(), kWasmVoid, param, num_params);

    r.builder().AddSignature(sig);
    r.builder().AddSignature(sig);
    r.builder().AddIndirectFunctionTable(nullptr, 0);

    WasmFunctionCompiler& t = r.NewFunction(sig);

    std::vector<uint8_t> code;
    for (uint8_t p = 0; p < num_params; p++) {
      ADD_CODE(code, kExprLocalGet, p);
    }
    ADD_CODE(code, kExprI32Const, 0);
    ADD_CODE(code, kExprCallIndirect, 1, TABLE_ZERO);

    t.Build(base::VectorOf(code));
  }
}

WASM_EXEC_TEST(Compile_Wasm_CallIndirect_Many_i64) {
  CompileCallIndirectMany(execution_tier, kWasmI64);
}

static void Run_WasmMixedCall_N(TestExecutionTier execution_tier, int start) {
  const int kExpected = 6333;
  const int kElemSize = 8;

  static MachineType mixed[] = {
      MachineType::Int32(),   MachineType::Float32(), MachineType::Int64(),
      MachineType::Float64(), MachineType::Float32(), MachineType::Int64(),
      MachineType::Int32(),   MachineType::Float64(), MachineType::Float32(),
      MachineType::Float64(), MachineType::Int32(),   MachineType::Int64(),
      MachineType::Int32(),   MachineType::Int32()};

  int num_params = static_cast<int>(arraysize(mixed)) - start;
  for (int which = 0; which < num_params; which++) {
    v8::internal::AccountingAllocator allocator;
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
    for (int i = 0; i < num_params; i++) {
      b.AddParam(ValueType::For(memtypes[i]));
    }
    WasmFunctionCompiler& f = r.NewFunction(b.Get());
    f.Build({WASM_LOCAL_GET(which)});

    // =========================================================================
    // Build the calling function.
    // =========================================================================
    std::vector<uint8_t> code;

    // Load the arguments.
    for (int i = 0; i < num_params; i++) {
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

    r.Build(&code[0], &code[0] + code.size());

    // Run the code.
    for (int t = 0; t < 10; t++) {
      r.builder().RandomizeMemory();
      CHECK_EQ(kExpected, r.Call());

      int size = result.MemSize();
      for (int i = 0; i < size; i++) {
        int base = (which + 1) * kElemSize;
        uint8_t expected = r.builder().raw_mem_at<uint8_t>(base + i);
        uint8_t actual = r.builder().raw_mem_at<uint8_t>(i);
        CHECK_EQ(expected, actual);
      }
    }
  }
}

WASM_EXEC_TEST(MixedCall_i64_0) { Run_WasmMixedCall_N(execution_tier, 0); }
WASM_EXEC_TEST(MixedCall_i64_1) { Run_WasmMixedCall_N(execution_tier, 1); }
WASM_EXEC_TEST(MixedCall_i64_2) { Run_WasmMixedCall_N(execution_tier, 2); }
WASM_EXEC_TEST(MixedCall_i64_3) { Run_WasmMixedCall_N(execution_tier, 3); }

WASM_EXEC_TEST(Regress5874) {
  WasmRunner<int32_t> r(execution_tier);
  r.builder().AddMemoryElems<int64_t>(kWasmPageSize / sizeof(int64_t));

  r.Build({kExprI64Const, 0x00,           // --
           kExprI32ConvertI64,            // --
           kExprI64Const, 0x00,           // --
           kExprI64StoreMem, 0x03, 0x00,  // --
           kExprI32Const, 0x00});         // --

  r.Call();
}

WASM_EXEC_TEST(Regression_6858) {
  // WasmRunner with 5 params and returns, which is the maximum.
  WasmRunner<int64_t, int64_t, int64_t, int64_t, int64_t> r(execution_tier);
  r.Build({WASM_I64_DIVS(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
  int64_t dividend = 15;
  int64_t divisor = 0;
  int64_t filler = 34;
  CHECK_TRAP64(r.Call(dividend, divisor, filler, filler));
}

// clang-format gets confused about these closing parentheses (wants to change
// the first comment to "// namespace v8". Disable it.
// clang-format off
}  // namespace test_run_wasm_64
}  // namespace wasm
}  // namespace internal
}  // namespace v8
// clang-format on
