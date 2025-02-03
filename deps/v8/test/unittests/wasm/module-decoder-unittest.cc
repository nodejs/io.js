// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/module-decoder.h"

#include "src/wasm/branch-hint-map.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

using testing::HasSubstr;

namespace v8 {
namespace internal {
namespace wasm {
namespace module_decoder_unittest {

#define WASM_INIT_EXPR_I32V_1(val) WASM_I32V_1(val), kExprEnd
#define WASM_INIT_EXPR_I32V_2(val) WASM_I32V_2(val), kExprEnd
#define WASM_INIT_EXPR_I32V_3(val) WASM_I32V_3(val), kExprEnd
#define WASM_INIT_EXPR_I32V_4(val) WASM_I32V_4(val), kExprEnd
#define WASM_INIT_EXPR_I32V_5(val) WASM_I32V_5(val), kExprEnd
#define WASM_INIT_EXPR_F32(val) WASM_F32(val), kExprEnd
#define WASM_INIT_EXPR_I64(val) WASM_I64(val), kExprEnd
#define WASM_INIT_EXPR_F64(val) WASM_F64(val), kExprEnd
#define WASM_INIT_EXPR_EXTERN_REF_NULL WASM_REF_NULL(kExternRefCode), kExprEnd
#define WASM_INIT_EXPR_FUNC_REF_NULL WASM_REF_NULL(kFuncRefCode), kExprEnd
#define WASM_INIT_EXPR_REF_FUNC(val) WASM_REF_FUNC(val), kExprEnd
#define WASM_INIT_EXPR_GLOBAL(index) WASM_GLOBAL_GET(index), kExprEnd
#define WASM_INIT_EXPR_STRUCT_NEW(index, ...) \
  WASM_STRUCT_NEW(index, __VA_ARGS__), kExprEnd
#define WASM_INIT_EXPR_ARRAY_NEW_FIXED(index, length, ...) \
  WASM_ARRAY_NEW_FIXED(index, length, __VA_ARGS__), kExprEnd

#define REF_NULL_ELEMENT kExprRefNull, kFuncRefCode, kExprEnd
#define REF_FUNC_ELEMENT(v) kExprRefFunc, U32V_1(v), kExprEnd

#define EMPTY_BODY 0
#define NOP_BODY 2, 0, kExprNop

#define SIG_ENTRY_i_i SIG_ENTRY_x_x(kI32Code, kI32Code)

#define UNKNOWN_SECTION(size) 0, U32V_1(size + 5), ADD_COUNT('l', 'u', 'l', 'z')
#define TYPE_SECTION(count, ...) SECTION(Type, U32V_1(count), __VA_ARGS__)
#define FUNCTION_SECTION(count, ...) \
  SECTION(Function, U32V_1(count), __VA_ARGS__)

#define FOO_STRING ADD_COUNT('f', 'o', 'o')
#define NO_LOCAL_NAMES 0

#define EMPTY_TYPE_SECTION SECTION(Type, ENTRY_COUNT(0))
#define EMPTY_FUNCTION_SECTION SECTION(Function, ENTRY_COUNT(0))
#define EMPTY_FUNCTION_BODIES_SECTION SECTION(Code, ENTRY_COUNT(0))
#define SECTION_NAMES(...) \
  SECTION(Unknown, ADD_COUNT('n', 'a', 'm', 'e'), ##__VA_ARGS__)
#define EMPTY_NAMES_SECTION SECTION_NAMES()
#define SECTION_SRC_MAP(...)                                               \
  SECTION(Unknown,                                                         \
          ADD_COUNT('s', 'o', 'u', 'r', 'c', 'e', 'M', 'a', 'p', 'p', 'i', \
                    'n', 'g', 'U', 'R', 'L'),                              \
          ADD_COUNT(__VA_ARGS__))
#define SECTION_COMPILATION_HINTS(...)                                     \
  SECTION(Unknown,                                                         \
          ADD_COUNT('c', 'o', 'm', 'p', 'i', 'l', 'a', 't', 'i', 'o', 'n', \
                    'H', 'i', 'n', 't', 's'),                              \
          ADD_COUNT(__VA_ARGS__))

#define SECTION_BRANCH_HINTS(...)                                          \
  SECTION(Unknown,                                                         \
          ADD_COUNT('m', 'e', 't', 'a', 'd', 'a', 't', 'a', '.', 'c', 'o', \
                    'd', 'e', '.', 'b', 'r', 'a', 'n', 'c', 'h', '_', 'h', \
                    'i', 'n', 't'),                                        \
          __VA_ARGS__)

#define X1(...) __VA_ARGS__
#define X2(...) __VA_ARGS__, __VA_ARGS__
#define X3(...) __VA_ARGS__, __VA_ARGS__, __VA_ARGS__
#define X4(...) __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__

#define ONE_EMPTY_FUNCTION(sig_index) \
  SECTION(Function, ENTRY_COUNT(1), X1(sig_index))

#define TWO_EMPTY_FUNCTIONS(sig_index) \
  SECTION(Function, ENTRY_COUNT(2), X2(sig_index))

#define THREE_EMPTY_FUNCTIONS(sig_index) \
  SECTION(Function, ENTRY_COUNT(3), X3(sig_index))

#define FOUR_EMPTY_FUNCTIONS(sig_index) \
  SECTION(Function, ENTRY_COUNT(4), X4(sig_index))

#define ONE_EMPTY_BODY SECTION(Code, ENTRY_COUNT(1), X1(EMPTY_BODY))
#define TWO_EMPTY_BODIES SECTION(Code, ENTRY_COUNT(2), X2(EMPTY_BODY))
#define THREE_EMPTY_BODIES SECTION(Code, ENTRY_COUNT(3), X3(EMPTY_BODY))
#define FOUR_EMPTY_BODIES SECTION(Code, ENTRY_COUNT(4), X4(EMPTY_BODY))

#define TYPE_SECTION_ONE_SIG_VOID_VOID \
  SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v)

#define LINEAR_MEMORY_INDEX_0 0

#define EXCEPTION_ENTRY(sig_index) U32V_1(kExceptionAttribute), sig_index

#define FIELD_COUNT(count) U32V_1(count)
#define STRUCT_FIELD(type, mutability) type, (mutability ? 1 : 0)
#define WASM_REF(index) kRefCode, index
#define WASM_OPT_REF(index) kRefNullCode, index
#define WASM_STRUCT_DEF(...) kWasmStructTypeCode, __VA_ARGS__
#define WASM_ARRAY_DEF(type, mutability) \
  kWasmArrayTypeCode, type, (mutability ? 1 : 0)
#define WASM_FUNCTION_DEF(...) kWasmFunctionTypeCode, __VA_ARGS__

#define EXPECT_VERIFIES(data)                                     \
  do {                                                            \
    ModuleResult _result = DecodeModule(base::ArrayVector(data)); \
    EXPECT_OK(_result);                                           \
  } while (false)

#define EXPECT_FAILURE_LEN(data, length)                               \
  do {                                                                 \
    ModuleResult _result = DecodeModule(base::VectorOf(data, length)); \
    EXPECT_FALSE(_result.ok());                                        \
  } while (false)

#define EXPECT_FAILURE(data) EXPECT_FAILURE_LEN(data, sizeof(data))

#define EXPECT_FAILURE_WITH_MSG(data, msg)                        \
  do {                                                            \
    ModuleResult _result = DecodeModule(base::ArrayVector(data)); \
    EXPECT_FALSE(_result.ok());                                   \
    if (!_result.ok()) {                                          \
      EXPECT_THAT(_result.error().message(), HasSubstr(msg));     \
    }                                                             \
  } while (false)

#define EXPECT_OFF_END_FAILURE(data, min)                              \
  do {                                                                 \
    static_assert(min < arraysize(data));                              \
    for (size_t _length = min; _length < arraysize(data); _length++) { \
      EXPECT_FAILURE_LEN(data, _length);                               \
    }                                                                  \
  } while (false)

#define EXPECT_OK(result)                                        \
  do {                                                           \
    if (!result.ok()) {                                          \
      GTEST_NONFATAL_FAILURE_(result.error().message().c_str()); \
      return;                                                    \
    }                                                            \
  } while (false)

#define EXPECT_NOT_OK(result, msg)                           \
  do {                                                       \
    EXPECT_FALSE(result.ok());                               \
    if (!result.ok()) {                                      \
      EXPECT_THAT(result.error().message(), HasSubstr(msg)); \
    }                                                        \
  } while (false)

static size_t SizeOfVarInt(size_t value) {
  size_t size = 0;
  do {
    size++;
    value = value >> 7;
  } while (value > 0);
  return size;
}

struct ValueTypePair {
  uint8_t code;
  ValueType type;
} kValueTypes[] = {
    {kI32Code, kWasmI32},                          // --
    {kI64Code, kWasmI64},                          // --
    {kF32Code, kWasmF32},                          // --
    {kF64Code, kWasmF64},                          // --
    {kFuncRefCode, kWasmFuncRef},                  // --
    {kNoFuncCode, kWasmNullFuncRef},               // --
    {kExternRefCode, kWasmExternRef},              // --
    {kNoExternCode, kWasmNullExternRef},           // --
    {kNoExnCode, kWasmNullExnRef},                 // --
    {kAnyRefCode, kWasmAnyRef},                    // --
    {kEqRefCode, kWasmEqRef},                      // --
    {kI31RefCode, kWasmI31Ref},                    // --
    {kStructRefCode, kWasmStructRef},              // --
    {kArrayRefCode, kWasmArrayRef},                // --
    {kNoneCode, kWasmNullRef},                     // --
    {kStringRefCode, kWasmStringRef},              // --
    {kStringViewWtf8Code, kWasmStringViewWtf8},    // --
    {kStringViewWtf16Code, kWasmStringViewWtf16},  // --
    {kStringViewIterCode, kWasmStringViewIter},    // --
};

class WasmModuleVerifyTest : public TestWithIsolateAndZone {
 public:
  WasmEnabledFeatures enabled_features_ = WasmEnabledFeatures::None();

  ModuleResult DecodeModule(base::Vector<const uint8_t> module_bytes) {
    // Add the wasm magic and version number automatically.
    size_t size = module_bytes.size();
    uint8_t header[] = {WASM_MODULE_HEADER};
    size_t total = sizeof(header) + size;
    auto temp = new uint8_t[total];
    memcpy(temp, header, sizeof(header));
    if (size > 0) {
      memcpy(temp + sizeof(header), module_bytes.begin(), size);
    }
    ModuleResult result = DecodeWasmModule(
        enabled_features_, base::VectorOf(temp, total), false, kWasmOrigin);
    delete[] temp;
    return result;
  }
  ModuleResult DecodeModuleNoHeader(base::Vector<const uint8_t> bytes) {
    return DecodeWasmModule(enabled_features_, bytes, false, kWasmOrigin);
  }
};

TEST_F(WasmModuleVerifyTest, WrongMagic) {
  for (uint32_t x = 1; x; x <<= 1) {
    const uint8_t data[] = {U32_LE(kWasmMagic ^ x), U32_LE(kWasmVersion)};
    ModuleResult result = DecodeModuleNoHeader(base::ArrayVector(data));
    EXPECT_FALSE(result.ok());
  }
}

TEST_F(WasmModuleVerifyTest, WrongVersion) {
  for (uint32_t x = 1; x; x <<= 1) {
    const uint8_t data[] = {U32_LE(kWasmMagic), U32_LE(kWasmVersion ^ x)};
    ModuleResult result = DecodeModuleNoHeader(base::ArrayVector(data));
    EXPECT_FALSE(result.ok());
  }
}

TEST_F(WasmModuleVerifyTest, WrongSection) {
  constexpr uint8_t kInvalidSection = 0x1c;
  const uint8_t data[] = {kInvalidSection, 0};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_FALSE(result.ok());
}

TEST_F(WasmModuleVerifyTest, DecodeEmpty) {
  ModuleResult result = DecodeModule(base::VectorOf<uint8_t>(nullptr, 0));
  EXPECT_TRUE(result.ok());
}

TEST_F(WasmModuleVerifyTest, OneGlobal) {
  static const uint8_t data[] = {
      SECTION(Global,                     // --
              ENTRY_COUNT(1),             // --
              kI32Code,                   // local type
              0,                          // immutable
              WASM_INIT_EXPR_I32V_1(13))  // init
  };

  {
    // Should decode to exactly one global.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(1u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* global = &result.value()->globals.back();

    EXPECT_EQ(kWasmI32, global->type);
    EXPECT_EQ(0u, global->offset);
    EXPECT_FALSE(global->mutability);
  }

  EXPECT_OFF_END_FAILURE(data, 1);
}

TEST_F(WasmModuleVerifyTest, S128Global) {
  std::array<uint8_t, kSimd128Size> v = {1, 2,  3,  4,  5,  6,  7, 8,
                                         9, 10, 11, 12, 13, 14, 15};
  static const uint8_t data[] = {SECTION(Global,          // --
                                         ENTRY_COUNT(1),  // --
                                         kS128Code,       // memory type
                                         0,               // immutable
                                         WASM_SIMD_CONSTANT(v.data()),
                                         kExprEnd)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  if (!CheckHardwareSupportsSimd()) {
    EXPECT_NOT_OK(result, "Wasm SIMD unsupported");
  } else {
    EXPECT_OK(result);
    const WasmGlobal* global = &result.value()->globals.back();
    EXPECT_EQ(kWasmS128, global->type);
    EXPECT_EQ(0u, global->offset);
    EXPECT_FALSE(global->mutability);
  }
}

TEST_F(WasmModuleVerifyTest, ExternRefGlobal) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      TWO_EMPTY_FUNCTIONS(SIG_INDEX(0)),
      SECTION(Global,                          // --
              ENTRY_COUNT(2),                  // --
              kExternRefCode,                  // local type
              0,                               // immutable
              WASM_INIT_EXPR_EXTERN_REF_NULL,  // init
              kFuncRefCode,                    // local type
              0,                               // immutable
              WASM_INIT_EXPR_REF_FUNC(1)),     // init
      SECTION(Element,                         // section name
              ENTRY_COUNT(2),                  // entry count
              DECLARATIVE,                     // flags 0
              kExternalFunction,               // type
              ENTRY_COUNT(1),                  // func entry count
              FUNC_INDEX(0),                   // func index
              DECLARATIVE_WITH_ELEMENTS,       // flags 1
              kFuncRefCode,                    // local type
              ENTRY_COUNT(1),                  // func ref count
              REF_FUNC_ELEMENT(1)),            // func ref
      TWO_EMPTY_BODIES};

  {
    // Should decode to two globals.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(2u, result.value()->globals.size());
    EXPECT_EQ(2u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* global = &result.value()->globals[0];
    EXPECT_EQ(kWasmExternRef, global->type);
    EXPECT_FALSE(global->mutability);

    global = &result.value()->globals[1];
    EXPECT_EQ(kWasmFuncRef, global->type);
    EXPECT_FALSE(global->mutability);
  }
}

TEST_F(WasmModuleVerifyTest, FuncRefGlobal) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      TWO_EMPTY_FUNCTIONS(SIG_INDEX(0)),
      SECTION(Global,                        // --
              ENTRY_COUNT(2),                // --
              kFuncRefCode,                  // local type
              0,                             // immutable
              WASM_INIT_EXPR_FUNC_REF_NULL,  // init
              kFuncRefCode,                  // local type
              0,                             // immutable
              WASM_INIT_EXPR_REF_FUNC(1)),   // init
      SECTION(Element,                       // section name
              ENTRY_COUNT(2),                // entry count
              DECLARATIVE,                   // flags 0
              kExternalFunction,             // type
              ENTRY_COUNT(1),                // func entry count
              FUNC_INDEX(0),                 // func index
              DECLARATIVE_WITH_ELEMENTS,     // flags 1
              kFuncRefCode,                  // local type
              ENTRY_COUNT(1),                // func ref count
              REF_FUNC_ELEMENT(1)),          // func ref
      TWO_EMPTY_BODIES};
  {
    // Should decode to two globals.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(2u, result.value()->globals.size());
    EXPECT_EQ(2u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* global = &result.value()->globals[0];
    EXPECT_EQ(kWasmFuncRef, global->type);
    EXPECT_FALSE(global->mutability);

    global = &result.value()->globals[1];
    EXPECT_EQ(kWasmFuncRef, global->type);
    EXPECT_FALSE(global->mutability);
  }
}

TEST_F(WasmModuleVerifyTest, InvalidFuncRefGlobal) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      TWO_EMPTY_FUNCTIONS(SIG_INDEX(0)),
      SECTION(Global,                       // --
              ENTRY_COUNT(1),               // --
              kFuncRefCode,                 // local type
              0,                            // immutable
              WASM_INIT_EXPR_REF_FUNC(7)),  // invalid function index
      TWO_EMPTY_BODIES};
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ExternRefGlobalWithGlobalInit) {
  static const uint8_t data[] = {
      SECTION(Import,           // --
              ENTRY_COUNT(1),   // number of imports
              ADD_COUNT('m'),   // module name
              ADD_COUNT('f'),   // global name
              kExternalGlobal,  // import kind
              kExternRefCode,   // type
              0),               // mutability
      SECTION(Global,           // --
              ENTRY_COUNT(1),
              kExternRefCode,  // local type
              0,               // immutable
              WASM_INIT_EXPR_GLOBAL(0)),
  };

  {
    // Should decode to exactly one global.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(2u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* global = &result.value()->globals.back();

    EXPECT_EQ(kWasmExternRef, global->type);
    EXPECT_FALSE(global->mutability);
  }
}

TEST_F(WasmModuleVerifyTest, NullGlobalWithGlobalInit) {
  static const uint8_t data[] = {
      SECTION(Import,           // --
              ENTRY_COUNT(1),   // number of imports
              ADD_COUNT('m'),   // module name
              ADD_COUNT('n'),   // global name
              kExternalGlobal,  // import kind
              kExternRefCode,   // type
              0),               // mutability
      SECTION(Global,           // --
              ENTRY_COUNT(1),
              kExternRefCode,  // local type
              0,               // immutable
              WASM_INIT_EXPR_GLOBAL(0)),
  };

  {
    // Should decode to exactly one global.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    std::cout << result.error().message() << std::endl;
    EXPECT_OK(result);
    EXPECT_EQ(2u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* global = &result.value()->globals.back();

    EXPECT_EQ(kWasmExternRef, global->type);
    EXPECT_FALSE(global->mutability);
  }
}

TEST_F(WasmModuleVerifyTest, GlobalInvalidType) {
  static const uint8_t data[] = {
      SECTION(Global,                      // --
              ENTRY_COUNT(1),              // --
              64,                          // invalid value type
              1,                           // mutable
              WASM_INIT_EXPR_I32V_1(33)),  // init
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, GlobalInvalidType2) {
  static const uint8_t data[] = {
      SECTION(Global,                      // --
              ENTRY_COUNT(1),              // --
              kVoidCode,                   // invalid value type
              1,                           // mutable
              WASM_INIT_EXPR_I32V_1(33)),  // init
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, GlobalInitializer) {
  static const uint8_t no_initializer_no_end[] = {
      SECTION(Global,          //--
              ENTRY_COUNT(1),  //--
              kI32Code,        // type
              1)               // mutable
  };
  EXPECT_FAILURE_WITH_MSG(no_initializer_no_end, "Beyond end of code");

  static const uint8_t no_initializer[] = {
      SECTION(Global,          //--
              ENTRY_COUNT(1),  //--
              kI32Code,        // type
              1,               // mutable
              kExprEnd)        // --
  };
  EXPECT_FAILURE_WITH_MSG(
      no_initializer,
      "expected 1 elements on the stack for constant expression, found 0");

  static const uint8_t too_many_initializers_no_end[] = {
      SECTION(Global,           // --
              ENTRY_COUNT(1),   // --
              kI32Code,         // type
              1,                // mutable
              WASM_I32V_1(42),  // one value is good
              WASM_I32V_1(43))  // another value is too much
  };
  EXPECT_FAILURE_WITH_MSG(too_many_initializers_no_end,
                          "constant expression is missing 'end'");

  static const uint8_t too_many_initializers[] = {
      SECTION(Global,           // --
              ENTRY_COUNT(1),   // --
              kI32Code,         // type
              1,                // mutable
              WASM_I32V_1(42),  // one value is good
              WASM_I32V_1(43),  // another value is too much
              kExprEnd)};
  EXPECT_FAILURE_WITH_MSG(
      too_many_initializers,
      "expected 1 elements on the stack for constant expression, found 2");

  static const uint8_t missing_end_opcode[] = {
      SECTION(Global,           // --
              ENTRY_COUNT(1),   // --
              kI32Code,         // type
              1,                // mutable
              WASM_I32V_1(42))  // init value
  };
  EXPECT_FAILURE_WITH_MSG(missing_end_opcode,
                          "constant expression is missing 'end'");

  static const uint8_t referencing_out_of_bounds_global[] = {
      SECTION(Global, ENTRY_COUNT(1),         // --
              kI32Code,                       // type
              1,                              // mutable
              WASM_GLOBAL_GET(42), kExprEnd)  // init value
  };
  EXPECT_FAILURE_WITH_MSG(referencing_out_of_bounds_global,
                          "Invalid global index: 42");

  static const uint8_t referencing_undefined_global[] = {
      SECTION(Global, ENTRY_COUNT(2),        // --
              kI32Code,                      // type
              0,                             // mutable
              WASM_GLOBAL_GET(1), kExprEnd,  // init value
              kI32Code,                      // type
              0,                             // mutable
              WASM_I32V(0), kExprEnd)        // init value
  };
  EXPECT_FAILURE_WITH_MSG(referencing_undefined_global,
                          "Invalid global index: 1");

  {
    static const uint8_t referencing_undefined_global_nested[] = {
        SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI32Code, true)),
        SECTION(Global, ENTRY_COUNT(2),                      // --
                kRefCode, 0,                                 // type
                0,                                           // mutable
                WASM_ARRAY_NEW_DEFAULT(0,                    // init value
                                       WASM_GLOBAL_GET(1)),  // --
                kExprEnd,                                    // --
                kI32Code,                                    // type
                0,                                           // mutable
                WASM_I32V(10), kExprEnd)                     // init value
    };
    EXPECT_FAILURE_WITH_MSG(referencing_undefined_global_nested,
                            "Invalid global index: 1");
  }

  static const uint8_t referencing_mutable_global[] = {
      SECTION(Global, ENTRY_COUNT(2),        // --
              kI32Code,                      // type
              1,                             // mutable
              WASM_I32V(1), kExprEnd,        // init value
              kI32Code,                      // type
              0,                             // mutable
              WASM_GLOBAL_GET(0), kExprEnd)  // init value
  };
  EXPECT_FAILURE_WITH_MSG(
      referencing_mutable_global,
      "mutable globals cannot be used in constant expressions");

  static const uint8_t referencing_mutable_imported_global[] = {
      SECTION(Import, ENTRY_COUNT(1),          // --
              ADD_COUNT('m'), ADD_COUNT('n'),  // module, name
              kExternalGlobal,                 // --
              kI32Code,                        // type
              1),                              // mutable
      SECTION(Global, ENTRY_COUNT(1),          // --
              kI32Code,                        // type
              0,                               // mutable
              WASM_GLOBAL_GET(0), kExprEnd)    // init value
  };
  EXPECT_FAILURE_WITH_MSG(
      referencing_mutable_imported_global,
      "mutable globals cannot be used in constant expressions");

  static const uint8_t referencing_immutable_imported_global[] = {
      SECTION(Import, ENTRY_COUNT(1),          // --
              ADD_COUNT('m'), ADD_COUNT('n'),  // module, name
              kExternalGlobal,                 // --
              kI32Code,                        // type
              0),                              // mutable
      SECTION(Global, ENTRY_COUNT(1),          // --
              kI32Code,                        // type
              0,                               // mutable
              WASM_GLOBAL_GET(0), kExprEnd)    // init value
  };
  EXPECT_VERIFIES(referencing_immutable_imported_global);

  static const uint8_t referencing_local_global[] = {
      SECTION(Global, ENTRY_COUNT(2),        // --
              kI32Code,                      // type
              0,                             // mutable
              WASM_I32V(1), kExprEnd,        // init value
              kI32Code,                      // type
              0,                             // mutable
              WASM_GLOBAL_GET(0), kExprEnd)  // init value
  };
  EXPECT_VERIFIES(referencing_local_global);
}

TEST_F(WasmModuleVerifyTest, ZeroGlobals) {
  static const uint8_t data[] = {SECTION(Global, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, ExportMutableGlobal) {
  {
    static const uint8_t data[] = {
        SECTION(Global,                         // --
                ENTRY_COUNT(1),                 // --
                kI32Code,                       // local type
                0,                              // immutable
                WASM_INIT_EXPR_I32V_1(13)),     // init
        SECTION(Export,                         // --
                ENTRY_COUNT(1),                 // export count
                ADD_COUNT('n', 'a', 'm', 'e'),  // name
                kExternalGlobal,                // global
                0),                             // global index
    };
    EXPECT_VERIFIES(data);
  }
  {
    static const uint8_t data[] = {
        SECTION(Global,                         // --
                ENTRY_COUNT(1),                 // --
                kI32Code,                       // local type
                1,                              // mutable
                WASM_INIT_EXPR_I32V_1(13)),     // init
        SECTION(Export,                         // --
                ENTRY_COUNT(1),                 // export count
                ADD_COUNT('n', 'a', 'm', 'e'),  // name
                kExternalGlobal,                // global
                0),                             // global index
    };
    EXPECT_VERIFIES(data);
  }
}

static void AppendUint32v(std::vector<uint8_t>* buffer, uint32_t val) {
  while (true) {
    uint32_t next = val >> 7;
    uint32_t out = val & 0x7F;
    if (next) {
      buffer->push_back(static_cast<uint8_t>(0x80 | out));
      val = next;
    } else {
      buffer->push_back(static_cast<uint8_t>(out));
      break;
    }
  }
}

TEST_F(WasmModuleVerifyTest, NGlobals) {
  static const uint8_t data[] = {
      kF32Code,                 // memory type
      0,                        // immutable
      WASM_INIT_EXPR_F32(7.7),  // init
  };

  for (uint32_t i = 0; i < kV8MaxWasmGlobals; i = i * 13 + 1) {
    std::vector<uint8_t> buffer;
    size_t size = SizeOfVarInt(i) + i * sizeof(data);
    const uint8_t globals[] = {kGlobalSectionCode, U32V_5(size)};
    for (size_t g = 0; g != sizeof(globals); ++g) {
      buffer.push_back(globals[g]);
    }
    AppendUint32v(&buffer, i);  // Number of globals.
    for (uint32_t j = 0; j < i; j++) {
      buffer.insert(buffer.end(), data, data + sizeof(data));
    }

    ModuleResult result = DecodeModule(base::VectorOf(buffer));
    EXPECT_OK(result);
  }
}

TEST_F(WasmModuleVerifyTest, TwoGlobals) {
  static const uint8_t data[] = {SECTION(Global,                    // --
                                         ENTRY_COUNT(2),            // --
                                         kF32Code,                  // type
                                         0,                         // immutable
                                         WASM_INIT_EXPR_F32(22.0),  // --
                                         kF64Code,                  // type
                                         1,                         // mutable
                                         WASM_INIT_EXPR_F64(23.0))};  // --

  {
    // Should decode to exactly two globals.
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(2u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(0u, result.value()->data_segments.size());

    const WasmGlobal* g0 = &result.value()->globals[0];

    EXPECT_EQ(kWasmF32, g0->type);
    EXPECT_EQ(0u, g0->offset);
    EXPECT_FALSE(g0->mutability);

    const WasmGlobal* g1 = &result.value()->globals[1];

    EXPECT_EQ(kWasmF64, g1->type);
    EXPECT_EQ(8u, g1->offset);
    EXPECT_TRUE(g1->mutability);
  }

  EXPECT_OFF_END_FAILURE(data, 1);
}

TEST_F(WasmModuleVerifyTest, RefNullGlobal) {
  static const uint8_t data[] = {SECTION(Global, ENTRY_COUNT(1), kFuncRefCode,
                                         1, WASM_REF_NULL(kFuncRefCode),
                                         kExprEnd)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, RefNullGlobalInvalid1) {
  static const uint8_t data[] = {SECTION(Global, ENTRY_COUNT(1), kRefNullCode,
                                         0, 1, WASM_REF_NULL(0), kExprEnd)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "Type index 0 is out of bounds");
}

TEST_F(WasmModuleVerifyTest, RefNullGlobalInvalid2) {
  static const uint8_t data[] = {SECTION(Global, ENTRY_COUNT(1), kFuncRefCode,
                                         1, kExprRefNull, U32V_5(1000001),
                                         kExprEnd)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result,
                "Type index 1000001 is greater than the maximum number 1000000 "
                "of type definitions supported by V8");
}

TEST_F(WasmModuleVerifyTest, StructNewInitExpr) {
  static const uint8_t basic[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true))),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 0, 0,          // type, mutability
              WASM_INIT_EXPR_STRUCT_NEW(0, WASM_I32V(42)))};
  EXPECT_VERIFIES(basic);

  static const uint8_t global_args[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true))),
      SECTION(Global, ENTRY_COUNT(2),     // --
              kI32Code, 0,                // type, mutability
              WASM_INIT_EXPR_I32V_1(10),  // --
              kRefCode, 0, 0,             // type, mutability
              WASM_INIT_EXPR_STRUCT_NEW(0, WASM_GLOBAL_GET(0)))};
  EXPECT_VERIFIES(global_args);

  static const uint8_t type_error[] = {
      SECTION(Type, ENTRY_COUNT(2),  // --
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)),
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI64Code, true))),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 1, 0,          // type, mutability
              WASM_INIT_EXPR_STRUCT_NEW(0, WASM_I32V(42)))};
  EXPECT_FAILURE_WITH_MSG(
      type_error,
      "type error in constant expression[0] (expected (ref 1), got (ref 0))");
}

TEST_F(WasmModuleVerifyTest, ArrayNewFixedInitExpr) {
  static const uint8_t basic[] = {
      SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI16Code, true)),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 0, 0,          // type, mutability
              WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 3, WASM_I32V(10), WASM_I32V(20),
                                             WASM_I32V(30)))};
  EXPECT_VERIFIES(basic);

  static const uint8_t basic_static[] = {
      SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI16Code, true)),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 0, 0,          // type, mutability
              WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 3, WASM_I32V(10), WASM_I32V(20),
                                             WASM_I32V(30)))};
  EXPECT_VERIFIES(basic_static);

  static const uint8_t basic_immutable[] = {
      SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI32Code, false)),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 0, 0,          // type, mutability
              WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 3, WASM_I32V(10), WASM_I32V(20),
                                             WASM_I32V(30)))};
  EXPECT_VERIFIES(basic_immutable);

  static const uint8_t type_error[] = {
      SECTION(Type, ENTRY_COUNT(2),  // --
              WASM_ARRAY_DEF(kI32Code, true),
              WASM_ARRAY_DEF(WASM_SEQ(kRefCode, 0), true)),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 1, 0,          // type, mutability
              WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 1, WASM_I32V(42)))};
  EXPECT_FAILURE_WITH_MSG(
      type_error,
      "type error in constant expression[0] (expected (ref 1), got (ref 0))");

  static const uint8_t subexpr_type_error[] = {
      SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI64Code, true)),
      SECTION(
          Global, ENTRY_COUNT(1),  // --
          kRefCode, 0, 0,          // type, mutability
          WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 2, WASM_I64V(42), WASM_I32V(142)))};
  EXPECT_FAILURE_WITH_MSG(subexpr_type_error,
                          "array.new_fixed[1] expected type i64, found "
                          "i32.const of type i32");

  static const uint8_t length_error[] = {
      SECTION(Type, ENTRY_COUNT(1), WASM_ARRAY_DEF(kI16Code, true)),
      SECTION(Global, ENTRY_COUNT(1),  // --
              kRefCode, 0, 0,          // type, mutability
              WASM_INIT_EXPR_ARRAY_NEW_FIXED(0, 10, WASM_I32V(10),
                                             WASM_I32V(20), WASM_I32V(30)))};
  EXPECT_FAILURE_WITH_MSG(length_error,
                          "not enough arguments on the stack for "
                          "array.new_fixed (need 10, got 3)");
}

TEST_F(WasmModuleVerifyTest, EmptyStruct) {
  static const uint8_t empty_struct[] = {SECTION(Type, ENTRY_COUNT(1),  // --
                                                 kWasmStructTypeCode,   // --
                                                 U32V_1(0))};  // field count

  EXPECT_VERIFIES(empty_struct);
}

TEST_F(WasmModuleVerifyTest, InvalidStructTypeDef) {
  static const uint8_t all_good[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kI32Code,              // perfectly valid field type
              1)};                   // mutability
  EXPECT_VERIFIES(all_good);

  static const uint8_t invalid_field_type[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kWasmArrayTypeCode,    // bogus field type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(invalid_field_type, "invalid value type");

  static const uint8_t field_type_oob_ref[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kRefNullCode,          // field type: reference...
              3,                     // ...to nonexistent type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_oob_ref, "Type index 3 is out of bounds");

  static const uint8_t field_type_invalid_ref[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kRefNullCode,          // field type: reference...
              U32V_4(1234567),       // ...to a type > kV8MaxWasmTypes
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_invalid_ref, "greater than the maximum");

  static const uint8_t field_type_invalid_ref2[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kRefNullCode,          // field type: reference...
              kI32Code,              // ...to a non-referenceable type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_invalid_ref2, "Unknown heap type");

  static const uint8_t not_enough_field_types[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(2),             // field count
              kI32Code,              // field type 1
              1)};                   // mutability 1
  EXPECT_FAILURE_WITH_MSG(not_enough_field_types, "expected 1 byte");

  static const uint8_t not_enough_field_types2[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(2),             // field count
              kI32Code,              // field type 1
              1,                     // mutability 1
              kI32Code)};            // field type 2
  EXPECT_FAILURE_WITH_MSG(not_enough_field_types2, "expected 1 byte");

  static const uint8_t invalid_mutability[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmStructTypeCode,   // --
              U32V_1(1),             // field count
              kI32Code,              // field type
              2)};                   // invalid mutability value
  EXPECT_FAILURE_WITH_MSG(invalid_mutability, "invalid mutability");
}

TEST_F(WasmModuleVerifyTest, InvalidArrayTypeDef) {
  static const uint8_t all_good[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kI32Code,              // perfectly valid field type
              1)};                   // mutability
  EXPECT_VERIFIES(all_good);

  static const uint8_t invalid_field_type[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kWasmArrayTypeCode,    // bogus field type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(invalid_field_type, "invalid value type");

  static const uint8_t field_type_oob_ref[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kRefNullCode,          // field type: reference...
              3,                     // ...to nonexistent type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_oob_ref, "Type index 3 is out of bounds");

  static const uint8_t field_type_invalid_ref[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kRefNullCode,          // field type: reference...
              U32V_3(1234567),       // ...to a type > kV8MaxWasmTypes
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_invalid_ref, "Unknown heap type");

  static const uint8_t field_type_invalid_ref2[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kRefNullCode,          // field type: reference...
              kI32Code,              // ...to a non-referenceable type
              1)};                   // mutability
  EXPECT_FAILURE_WITH_MSG(field_type_invalid_ref2, "Unknown heap type");

  static const uint8_t invalid_mutability[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode,    // --
              kI32Code,              // field type
              2)};                   // invalid mutability value
  EXPECT_FAILURE_WITH_MSG(invalid_mutability, "invalid mutability");

  static const uint8_t immutable[] = {SECTION(Type,
                                              ENTRY_COUNT(1),      // --
                                              kWasmArrayTypeCode,  // --
                                              kI32Code,            // field type
                                              0)};  // immmutability
  EXPECT_VERIFIES(immutable);
}

TEST_F(WasmModuleVerifyTest, TypeCanonicalization) {
  static const uint8_t identical_group[] = {
      SECTION(Type,            // --
              ENTRY_COUNT(2),  // two identical rec. groups
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode, kI32Code, 0,              // --
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode, kI32Code, 0),
      SECTION(Global,                          // --
              ENTRY_COUNT(1), kRefCode, 0, 0,  // Type, mutability
              WASM_ARRAY_NEW_FIXED(1, 1, WASM_I32V(10)),
              kExprEnd)  // initial value
  };

  // Global initializer should verify as identical type in other group
  EXPECT_VERIFIES(identical_group);

  static const uint8_t non_identical_group[] = {
      SECTION(Type,            // --
              ENTRY_COUNT(2),  // two distrinct rec. groups
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),  // --
              kWasmArrayTypeCode, kI32Code, 0,              // --
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(2),  // --
              kWasmArrayTypeCode, kI32Code, 0,              // --
              kWasmStructTypeCode, ENTRY_COUNT(0)),
      SECTION(Global,                          // --
              ENTRY_COUNT(1), kRefCode, 0, 0,  // Type, mutability
              WASM_ARRAY_NEW_FIXED(1, 1, WASM_I32V(10)),
              kExprEnd)  // initial value
  };

  // Global initializer should not verify as type in distinct rec. group.
  EXPECT_FAILURE_WITH_MSG(
      non_identical_group,
      "type error in constant expression[0] (expected (ref 0), got (ref 1))");

  static const uint8_t empty_group[] = {
      SECTION(Type,            // --
              ENTRY_COUNT(1),  // one rec. group
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(0))};

  EXPECT_VERIFIES(empty_group);

  static const uint8_t mixed_empty_and_nonempty_groups[] = {SECTION(
      Type,                                         // --
      ENTRY_COUNT(4),                               // one rec. group
      kWasmRecursiveTypeGroupCode, ENTRY_COUNT(0),  // empty
      SIG_ENTRY_v_v,                                // one type
      kWasmRecursiveTypeGroupCode, ENTRY_COUNT(0),  // empty
      SIG_ENTRY_v_v                                 // one type
      )};

  EXPECT_VERIFIES(mixed_empty_and_nonempty_groups);
}

// Tests that all types in a rec. group are checked for supertype validity.
TEST_F(WasmModuleVerifyTest, InvalidSupertypeInRecGroup) {
  static const uint8_t invalid_supertype[] = {
      SECTION(Type, ENTRY_COUNT(1),                         // --
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(2),  // --
              kWasmSubtypeCode, 0,              // 0 supertypes, non-final
              kWasmArrayTypeCode, kI32Code, 0,  // --
              kWasmSubtypeCode, 1, 0,           // supertype count, supertype
              kWasmArrayTypeCode, kI64Code, 0)};

  EXPECT_FAILURE_WITH_MSG(invalid_supertype,
                          "type 1 has invalid explicit supertype 0");
}

// Tests supertype declaration with 0 supertypes.
TEST_F(WasmModuleVerifyTest, SuperTypeDeclarationWith0Supertypes) {
  static const uint8_t zero_supertypes[] = {
      SECTION(Type, ENTRY_COUNT(1),  // --
              kWasmSubtypeCode, 0,   // supertype count
              kWasmArrayTypeCode, kI32Code, 0)};

  EXPECT_VERIFIES(zero_supertypes);
}

TEST_F(WasmModuleVerifyTest, NoSupertypeSupertype) {
  static const uint8_t no_supertype[] = {
      SECTION(Type, ENTRY_COUNT(1),          // --
              kWasmSubtypeCode, 1,           // supertype count
              0xff, 0xff, 0xff, 0xff, 0x0f,  // supertype = "kNoSuperType"
              kWasmArrayTypeCode, kI32Code, 0)};

  EXPECT_FAILURE_WITH_MSG(
      no_supertype, "is greater than the maximum number of type definitions");
}

TEST_F(WasmModuleVerifyTest, NonSpecifiedFinalType) {
  static const uint8_t final_supertype[] = {
      SECTION(Type, ENTRY_COUNT(2),                 // --
              kWasmStructTypeCode, 1, kI32Code, 1,  // --
              kWasmSubtypeCode, 1, 0,               // --
              kWasmStructTypeCode, 2, kI32Code, 1, kI32Code, 1)};
  EXPECT_FAILURE_WITH_MSG(final_supertype, "type 1 extends final type 0");
}

TEST_F(WasmModuleVerifyTest, SpecifiedFinalType) {
  static const uint8_t final_supertype[] = {
      SECTION(Type, ENTRY_COUNT(2),                 // --
              kWasmSubtypeFinalCode, 0,             // --
              kWasmStructTypeCode, 1, kI32Code, 1,  // --
              kWasmSubtypeCode, 1, 0,               // --
              kWasmStructTypeCode, 2, kI32Code, 1, kI32Code, 1)};
  EXPECT_FAILURE_WITH_MSG(final_supertype, "type 1 extends final type 0");
}

TEST_F(WasmModuleVerifyTest, ZeroExceptions) {
  static const uint8_t data[] = {SECTION(Tag, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(0u, result.value()->tags.size());
}

TEST_F(WasmModuleVerifyTest, OneI32Exception) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_x(kI32Code)),  // sig#0 (i32)
      SECTION(Tag, ENTRY_COUNT(1),
              EXCEPTION_ENTRY(SIG_INDEX(0)))};  // except[0] (sig#0)
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(1u, result.value()->tags.size());

  const WasmTag& e0 = result.value()->tags.front();
  EXPECT_EQ(1u, e0.sig->parameter_count());
  EXPECT_EQ(kWasmI32, e0.sig->GetParam(0));
}

TEST_F(WasmModuleVerifyTest, TwoExceptions) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(2),
              SIG_ENTRY_v_x(kI32Code),              // sig#0 (i32)
              SIG_ENTRY_v_xx(kF32Code, kI64Code)),  // sig#1 (f32, i64)
      SECTION(Tag, ENTRY_COUNT(2),
              EXCEPTION_ENTRY(SIG_INDEX(1)),    // except[0] (sig#1)
              EXCEPTION_ENTRY(SIG_INDEX(0)))};  // except[1] (sig#0)
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(2u, result.value()->tags.size());
  const WasmTag& e0 = result.value()->tags.front();
  EXPECT_EQ(2u, e0.sig->parameter_count());
  EXPECT_EQ(kWasmF32, e0.sig->GetParam(0));
  EXPECT_EQ(kWasmI64, e0.sig->GetParam(1));
  const WasmTag& e1 = result.value()->tags.back();
  EXPECT_EQ(kWasmI32, e1.sig->GetParam(0));
}

TEST_F(WasmModuleVerifyTest, Exception_invalid_sig_index) {
  static const uint8_t data[] = {
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      SECTION(Tag, ENTRY_COUNT(1),
              EXCEPTION_ENTRY(
                  SIG_INDEX(23)))};  // except[0] (sig#23 [out-of-bounds])
  // Should fail decoding exception section.
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "no signature at index 23 (1 types)");
}

TEST_F(WasmModuleVerifyTest, Exception_invalid_sig_return) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_i_i),
      SECTION(Tag, ENTRY_COUNT(1),
              EXCEPTION_ENTRY(
                  SIG_INDEX(0)))};  // except[0] (sig#0 [invalid-return-type])
  // Should fail decoding exception section.
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "tag signature 0 has non-void return");
}

TEST_F(WasmModuleVerifyTest, Exception_invalid_attribute) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_i_i),
      SECTION(Tag, ENTRY_COUNT(1), 23,
              SIG_INDEX(0))};  // except[0] (sig#0) [invalid-attribute]
  // Should fail decoding exception section.
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "exception attribute 23 not supported");
}

TEST_F(WasmModuleVerifyTest, TagSectionCorrectPlacement) {
  static const uint8_t data[] = {SECTION(Memory, ENTRY_COUNT(0)),
                                 SECTION(Tag, ENTRY_COUNT(0)),
                                 SECTION(Global, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, TagSectionAfterGlobal) {
  static const uint8_t data[] = {SECTION(Global, ENTRY_COUNT(0)),
                                 SECTION(Tag, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result,
                "The Tag section must appear before the Global section");
}

TEST_F(WasmModuleVerifyTest, TagSectionBeforeMemory) {
  static const uint8_t data[] = {SECTION(Tag, ENTRY_COUNT(0)),
                                 SECTION(Memory, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "unexpected section <Memory>");
}

TEST_F(WasmModuleVerifyTest, TagSectionAfterTableBeforeMemory) {
  static_assert(kMemorySectionCode + 1 == kGlobalSectionCode);
  static const uint8_t data[] = {SECTION(Table, ENTRY_COUNT(0)),
                                 SECTION(Tag, ENTRY_COUNT(0)),
                                 SECTION(Memory, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "unexpected section <Memory>");
}

TEST_F(WasmModuleVerifyTest, TagImport) {
  static const uint8_t data[] = {
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      SECTION(Import,                           // section header
              ENTRY_COUNT(1),                   // number of imports
              ADD_COUNT('m'),                   // module name
              ADD_COUNT('e', 'x'),              // tag name
              kExternalTag,                     // import kind
              EXCEPTION_ENTRY(SIG_INDEX(0)))};  // except[0] (sig#0)
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(1u, result.value()->tags.size());
  EXPECT_EQ(1u, result.value()->import_table.size());
}

TEST_F(WasmModuleVerifyTest, ExceptionExport) {
  static const uint8_t data[] = {
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      SECTION(Tag, ENTRY_COUNT(1),
              EXCEPTION_ENTRY(SIG_INDEX(0))),  // except[0] (sig#0)
      SECTION(Export, ENTRY_COUNT(1),          // --
              NO_NAME,                         // --
              kExternalTag,                    // --
              EXCEPTION_INDEX(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(1u, result.value()->tags.size());
  EXPECT_EQ(1u, result.value()->export_table.size());
}

TEST_F(WasmModuleVerifyTest, OneSignature) {
  {
    static const uint8_t data[] = {TYPE_SECTION_ONE_SIG_VOID_VOID};
    EXPECT_VERIFIES(data);
  }

  {
    static const uint8_t data[] = {
        SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_i_i)};
    EXPECT_VERIFIES(data);
  }
}

TEST_F(WasmModuleVerifyTest, MultipleSignatures) {
  static const uint8_t data[] = {
      SECTION(Type,                                           // --
              ENTRY_COUNT(3),                                 // --
              SIG_ENTRY_v_v,                                  // void -> void
              SIG_ENTRY_x_x(kI32Code, kF32Code),              // f32 -> i32
              SIG_ENTRY_x_xx(kI32Code, kF64Code, kF64Code)),  // f64,f64 -> i32
  };

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(3u, result.value()->types.size());
  if (result.value()->types.size() == 3) {
    EXPECT_EQ(0u, result.value()->signature(0)->return_count());
    EXPECT_EQ(1u, result.value()->signature(1)->return_count());
    EXPECT_EQ(1u, result.value()->signature(2)->return_count());

    EXPECT_EQ(0u, result.value()->signature(0)->parameter_count());
    EXPECT_EQ(1u, result.value()->signature(1)->parameter_count());
    EXPECT_EQ(2u, result.value()->signature(2)->parameter_count());
  }

  EXPECT_OFF_END_FAILURE(data, 1);
}

TEST_F(WasmModuleVerifyTest, CanonicalTypeIds) {
  static const uint8_t data[] = {
      SECTION(Type,                               // --
              ENTRY_COUNT(7),                     // --
              WASM_STRUCT_DEF(                    // Struct definition
                  FIELD_COUNT(1),                 // --
                  STRUCT_FIELD(kI32Code, true)),  // --
              SIG_ENTRY_x_x(kI32Code, kF32Code),  // f32 -> i32
              SIG_ENTRY_x_x(kI32Code, kF64Code),  // f64 -> i32
              SIG_ENTRY_x_x(kI32Code, kF32Code),  // f32 -> i32 (again)
              WASM_ARRAY_DEF(kI32Code, true),     // Array definition
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),
              WASM_ARRAY_DEF(kI16Code, true),  // Predefined i16 array
              kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),
              WASM_ARRAY_DEF(kI8Code, true))  // Predefined i8 array
  };

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  const WasmModule* module = result.value().get();

  EXPECT_EQ(7u, module->types.size());
  EXPECT_EQ(7u, module->isorecursive_canonical_type_ids.size());

  static constexpr uint32_t kBase = TypeCanonicalizer::kNumberOfPredefinedTypes;
  EXPECT_EQ(kBase + 0u, module->isorecursive_canonical_type_ids[0]);
  EXPECT_EQ(kBase + 1u, module->isorecursive_canonical_type_ids[1]);
  EXPECT_EQ(kBase + 2u, module->isorecursive_canonical_type_ids[2]);
  EXPECT_EQ(kBase + 1u, module->isorecursive_canonical_type_ids[3]);
  EXPECT_EQ(kBase + 3u, module->isorecursive_canonical_type_ids[4]);

  EXPECT_EQ(TypeCanonicalizer::kPredefinedArrayI16Index,
            module->isorecursive_canonical_type_ids[5]);
  EXPECT_EQ(TypeCanonicalizer::kPredefinedArrayI8Index,
            module->isorecursive_canonical_type_ids[6]);
}

TEST_F(WasmModuleVerifyTest, DataSegmentWithImmutableImportedGlobal) {
  // Import 2 globals so that we can initialize data with a global index != 0.
  const uint8_t data[] = {
      SECTION(Import,           // section header
              ENTRY_COUNT(2),   // number of imports
              ADD_COUNT('m'),   // module name
              ADD_COUNT('f'),   // global name
              kExternalGlobal,  // import kind
              kI32Code,         // type
              0,                // mutability
              ADD_COUNT('n'),   // module name
              ADD_COUNT('g'),   // global name
              kExternalGlobal,  // import kind
              kI32Code,         // type
              0),               // mutability
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_GLOBAL(1),  // dest addr
              U32V_1(3),                 // source size
              'a', 'b', 'c')             // data bytes
  };
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, DataSegmentWithMutableImportedGlobal) {
  // Only an immutable global can be used as an init_expr.
  const uint8_t data[] = {
      SECTION(Import,           // section header
              ENTRY_COUNT(1),   // number of imports
              ADD_COUNT('m'),   // module name
              ADD_COUNT('f'),   // global name
              kExternalGlobal,  // import kind
              kI32Code,         // type
              1),               // mutability
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_GLOBAL(0),  // dest addr
              U32V_1(3),                 // source size
              'a', 'b', 'c')             // data bytes
  };
  EXPECT_FAILURE(data);
}
TEST_F(WasmModuleVerifyTest, DataSegmentWithImmutableGlobal) {
  // An immutable global can be used in an init_expr.
  const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Global, ENTRY_COUNT(1),
              kI32Code,                         // local type
              0,                                // immutable
              WASM_INIT_EXPR_I32V_3(0x9BBAA)),  // init
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_GLOBAL(0),  // dest addr
              U32V_1(3),                 // source size
              'a', 'b', 'c')             // data bytes
  };
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, OneDataSegment) {
  const uint8_t kDataSegmentSourceOffset = 24;
  const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_I32V_3(0x9BBAA),  // dest addr
              U32V_1(3),                       // source size
              'a', 'b', 'c')                   // data bytes
  };

  {
    EXPECT_VERIFIES(data);
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(0u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(1u, result.value()->data_segments.size());

    const WasmDataSegment* segment = &result.value()->data_segments.back();

    EXPECT_EQ(kDataSegmentSourceOffset, segment->source.offset());
    EXPECT_EQ(3u, segment->source.length());
  }

  EXPECT_OFF_END_FAILURE(data, 14);
}

TEST_F(WasmModuleVerifyTest, TwoDataSegments) {
  const uint8_t kDataSegment0SourceOffset = 24;
  const uint8_t kDataSegment1SourceOffset = kDataSegment0SourceOffset + 11;

  const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data,
              ENTRY_COUNT(2),  // segment count
              LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_I32V_3(0x7FFEE),  // #0: dest addr
              U32V_1(4),                       // source size
              1, 2, 3, 4,                      // data bytes
              LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_I32V_3(0x6DDCC),  // #1: dest addr
              U32V_1(10),                      // source size
              1, 2, 3, 4, 5, 6, 7, 8, 9, 10)   // data bytes
  };

  {
    ModuleResult result = DecodeModule(base::ArrayVector(data));
    EXPECT_OK(result);
    EXPECT_EQ(0u, result.value()->globals.size());
    EXPECT_EQ(0u, result.value()->functions.size());
    EXPECT_EQ(2u, result.value()->data_segments.size());

    const WasmDataSegment* s0 = &result.value()->data_segments[0];
    const WasmDataSegment* s1 = &result.value()->data_segments[1];

    EXPECT_EQ(kDataSegment0SourceOffset, s0->source.offset());
    EXPECT_EQ(4u, s0->source.length());

    EXPECT_EQ(kDataSegment1SourceOffset, s1->source.offset());
    EXPECT_EQ(10u, s1->source.length());
  }

  EXPECT_OFF_END_FAILURE(data, 14);
}

TEST_F(WasmModuleVerifyTest, DataWithoutMemory) {
  const uint8_t data[] = {
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_I32V_3(0x9BBAA),  // dest addr
              U32V_1(3),                       // source size
              'a', 'b', 'c')                   // data bytes
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, MaxMaximumMemorySize) {
  {
    const uint8_t data[] = {
        SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 0, U32V_3(65536))};
    EXPECT_VERIFIES(data);
  }
  {
    const uint8_t data[] = {
        SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 0, U32V_3(65537))};
    EXPECT_FAILURE(data);
  }
}

TEST_F(WasmModuleVerifyTest, InvalidMemoryLimits) {
  {
    const uint8_t kInvalidLimits = 0x15;
    const uint8_t data[] = {
        SECTION(Memory, ENTRY_COUNT(1), kInvalidLimits, 0, 10)};
    EXPECT_FAILURE_WITH_MSG(data, "invalid memory limits flags 0x15");
  }
}

TEST_F(WasmModuleVerifyTest, DataSegment_wrong_init_type) {
  const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,
              WASM_INIT_EXPR_F64(9.9),  // dest addr
              U32V_1(3),                // source size
              'a', 'b', 'c')            // data bytes
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, DataSegmentEndOverflow) {
  const uint8_t data[] = {
      SECTION(Memory,  // memory section
              ENTRY_COUNT(1), kWithMaximum, 28, 28),
      SECTION(Data,                      // data section
              ENTRY_COUNT(1),            // one entry
              LINEAR_MEMORY_INDEX_0,     // mem index
              WASM_INIT_EXPR_I32V_1(0),  // offset
              U32V_5(0xFFFFFFFF))        // size
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, OneIndirectFunction) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  if (result.ok()) {
    EXPECT_EQ(1u, result.value()->types.size());
    EXPECT_EQ(1u, result.value()->functions.size());
    EXPECT_EQ(1u, result.value()->tables.size());
    EXPECT_EQ(1u, result.value()->tables[0].initial_size);
  }
}

TEST_F(WasmModuleVerifyTest, ElementSectionWithInternalTable) {
  static const uint8_t data[] = {
      // table ---------------------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // elements ------------------------------------------------------------
      SECTION(Element, ENTRY_COUNT(0))};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionWithImportedTable) {
  static const uint8_t data[] = {
      // imports -------------------------------------------------------------
      SECTION(Import, ENTRY_COUNT(1),
              ADD_COUNT('m'),  // module name
              ADD_COUNT('t'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              1),              // initial size
      // elements ------------------------------------------------------------
      SECTION(Element, ENTRY_COUNT(0))};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionWithoutTable) {
  // Test that an element section without a table causes a validation error.
  static const uint8_t data[] = {
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),  // entry count
              0,               // table index
              0,               // offset
              0)               // number of elements
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, Regression_735887) {
  // Test with an invalid function index in the element section.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),  // entry count
              TABLE_INDEX0, WASM_INIT_EXPR_I32V_1(0),
              1,     // elements count
              0x9A)  // invalid I32V as function index
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, OneIndirectFunction_one_entry) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),  // entry count
              TABLE_INDEX0, WASM_INIT_EXPR_I32V_1(0),
              1,  // elements count
              FUNC_INDEX(0)),
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(1u, result.value()->types.size());
  EXPECT_EQ(1u, result.value()->functions.size());
  EXPECT_EQ(1u, result.value()->tables.size());
  EXPECT_EQ(1u, result.value()->tables[0].initial_size);
}

TEST_F(WasmModuleVerifyTest, MultipleIndirectFunctions) {
  static const uint8_t data[] = {
      // sig#0 -------------------------------------------------------
      SECTION(Type,
              ENTRY_COUNT(2),            // --
              SIG_ENTRY_v_v,             // void -> void
              SIG_ENTRY_v_x(kI32Code)),  // void -> i32
      // funcs ------------------------------------------------------
      FOUR_EMPTY_FUNCTIONS(SIG_INDEX(0)),
      // table declaration -------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 8),
      // table elements ----------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),  // entry count
              TABLE_INDEX0, WASM_INIT_EXPR_I32V_1(0),
              ADD_COUNT(FUNC_INDEX(0), FUNC_INDEX(1), FUNC_INDEX(2),
                        FUNC_INDEX(3), FUNC_INDEX(0), FUNC_INDEX(1),
                        FUNC_INDEX(2), FUNC_INDEX(3))),
      FOUR_EMPTY_BODIES};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(2u, result.value()->types.size());
  EXPECT_EQ(4u, result.value()->functions.size());
  EXPECT_EQ(1u, result.value()->tables.size());
  EXPECT_EQ(8u, result.value()->tables[0].initial_size);
}

TEST_F(WasmModuleVerifyTest, ElementSectionMultipleTables) {
  // Test that if we have multiple tables, in the element section we can target
  // and initialize all tables.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),         // section header
              kFuncRefCode, kNoMaximum, 5,   // table 0
              kFuncRefCode, kNoMaximum, 9),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(2),            // entry count
              TABLE_INDEX0,              // element for table 0
              WASM_INIT_EXPR_I32V_1(0),  // index
              1,                         // elements count
              FUNC_INDEX(0),             // function
              TABLE_INDEX(1),            // element for table 1
              WASM_INIT_EXPR_I32V_1(7),  // index
              kExternalFunction,         // type
              2,                         // elements count
              FUNC_INDEX(0),             // entry 0
              FUNC_INDEX(0)),            // entry 1
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionMixedTables) {
  // Test that if we have multiple tables, both imported and module-defined, in
  // the element section we can target and initialize all tables.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // imports -------------------------------------------------------------
      SECTION(Import, ENTRY_COUNT(2),
              ADD_COUNT('m'),  // module name
              ADD_COUNT('t'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              5,               // initial size
              ADD_COUNT('m'),  // module name
              ADD_COUNT('s'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              10),             // initial size
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),          // section header
              kFuncRefCode, kNoMaximum, 15,   // table 0
              kFuncRefCode, kNoMaximum, 19),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              4,                          // entry count
              TABLE_INDEX0,               // element for table 0
              WASM_INIT_EXPR_I32V_1(0),   // index
              1,                          // elements count
              FUNC_INDEX(0),              // function
              TABLE_INDEX(1),             // element for table 1
              WASM_INIT_EXPR_I32V_1(7),   // index
              kExternalFunction,          // type
              2,                          // elements count
              FUNC_INDEX(0),              // entry 0
              FUNC_INDEX(0),              // entry 1
              TABLE_INDEX(2),             // element for table 2
              WASM_INIT_EXPR_I32V_1(12),  // index
              kExternalFunction,          // type
              1,                          // elements count
              FUNC_INDEX(0),              // function
              TABLE_INDEX(3),             // element for table 1
              WASM_INIT_EXPR_I32V_1(17),  // index
              kExternalFunction,          // type
              2,                          // elements count
              FUNC_INDEX(0),              // entry 0
              FUNC_INDEX(0)),             // entry 1
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionMultipleTablesArbitraryOrder) {
  // Test that the order in which tables are targeted in the element secion
  // can be arbitrary.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),         // section header
              kFuncRefCode, kNoMaximum, 5,   // table 0
              kFuncRefCode, kNoMaximum, 9),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(3),            // entry count
              TABLE_INDEX0,              // element for table 1
              WASM_INIT_EXPR_I32V_1(0),  // index
              1,                         // elements count
              FUNC_INDEX(0),             // function
              TABLE_INDEX(1),            // element for table 0
              WASM_INIT_EXPR_I32V_1(7),  // index
              kExternalFunction,         // type
              2,                         // elements count
              FUNC_INDEX(0),             // entry 0
              FUNC_INDEX(0),             // entry 1
              TABLE_INDEX0,              // element for table 1
              WASM_INIT_EXPR_I32V_1(3),  // index
              1,                         // elements count
              FUNC_INDEX(0)),            // function
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionMixedTablesArbitraryOrder) {
  // Test that the order in which tables are targeted in the element secion can
  // be arbitrary. In this test, tables can be both imported and module-defined.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // imports -------------------------------------------------------------
      SECTION(Import, ENTRY_COUNT(2),
              ADD_COUNT('m'),  // module name
              ADD_COUNT('t'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              5,               // initial size
              ADD_COUNT('m'),  // module name
              ADD_COUNT('s'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              10),             // initial size
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),          // section header
              kFuncRefCode, kNoMaximum, 15,   // table 0
              kFuncRefCode, kNoMaximum, 19),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              4,                          // entry count
              TABLE_INDEX(2),             // element for table 0
              WASM_INIT_EXPR_I32V_1(10),  // index
              kExternalFunction,          // type
              1,                          // elements count
              FUNC_INDEX(0),              // function
              TABLE_INDEX(3),             // element for table 1
              WASM_INIT_EXPR_I32V_1(17),  // index
              kExternalFunction,          // type
              2,                          // elements count
              FUNC_INDEX(0),              // entry 0
              FUNC_INDEX(0),              // entry 1
              TABLE_INDEX0,               // element for table 2
              WASM_INIT_EXPR_I32V_1(2),   // index
              1,                          // elements count
              FUNC_INDEX(0),              // function
              TABLE_INDEX(1),             // element for table 1
              WASM_INIT_EXPR_I32V_1(7),   // index
              kExternalFunction,          // type
              2,                          // elements count
              FUNC_INDEX(0),              // entry 0
              FUNC_INDEX(0)),             // entry 1
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionInitExternRefTableWithFuncRef) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),          // section header
              kExternRefCode, kNoMaximum, 5,  // table 0
              kFuncRefCode, kNoMaximum, 9),   // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(2),            // entry count
              TABLE_INDEX0,              // element for table 0
              WASM_INIT_EXPR_I32V_1(0),  // index
              1,                         // elements count
              FUNC_INDEX(0),             // function
              TABLE_INDEX(1),            // element for table 1
              WASM_INIT_EXPR_I32V_1(7),  // index
              kExternalFunction,         // type
              2,                         // elements count
              FUNC_INDEX(0),             // entry 0
              FUNC_INDEX(0)),            // entry 1
      // code ----------------------------------------------------------------
      ONE_EMPTY_BODY,
  };

  EXPECT_FAILURE_WITH_MSG(data,
                          "Element segment of type (ref func) is not a subtype "
                          "of referenced table 0 (of type externref)");
}

TEST_F(WasmModuleVerifyTest, ElementSectionIndexElementsTableWithNarrowType) {
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1),            // section header
              kRefNullCode, 0, kNoMaximum, 9),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),            // entry count
              TABLE_INDEX0,              // element for table 0
              WASM_INIT_EXPR_I32V_1(0),  // index
              1,                         // elements count
              FUNC_INDEX(0))             // function
  };

  EXPECT_FAILURE_WITH_MSG(data,
                          "Element segment of type (ref func) is not a subtype "
                          "of referenced table 0 (of type (ref null 0))");
}

TEST_F(WasmModuleVerifyTest, ElementSectionInitFuncRefTableWithFuncRefNull) {
  static const uint8_t data[] = {
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1),         // section header
              kFuncRefCode, kNoMaximum, 9),  // table 0
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),                      // entry count
              ACTIVE_WITH_ELEMENTS, TABLE_INDEX0,  // element for table 0
              WASM_INIT_EXPR_I32V_1(0),            // index
              kFuncRefCode,                        // .
              1,                                   // elements count
              WASM_INIT_EXPR_FUNC_REF_NULL)        // function
  };

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionInitFuncRefTableWithExternRefNull) {
  static const uint8_t data[] = {
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1),         // section header
              kFuncRefCode, kNoMaximum, 9),  // table 0
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(1),                      // entry count
              ACTIVE_WITH_ELEMENTS, TABLE_INDEX0,  // element for table 0
              WASM_INIT_EXPR_I32V_1(0),            // index
              kFuncRefCode,                        // .
              1,                                   // elements count
              WASM_INIT_EXPR_EXTERN_REF_NULL)      // function
  };

  EXPECT_FAILURE_WITH_MSG(
      data,
      "type error in constant expression[0] (expected funcref, got externref)");
}

TEST_F(WasmModuleVerifyTest, ElementSectionDontInitExternRefImportedTable) {
  // Test that imported tables of type ExternRef cannot be initialized in the
  // elements section.
  static const uint8_t data[] = {
      // sig#0 ---------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // imports -------------------------------------------------------------
      SECTION(Import, ENTRY_COUNT(2),
              ADD_COUNT('m'),  // module name
              ADD_COUNT('t'),  // table name
              kExternalTable,  // import kind
              kFuncRefCode,    // elem_type
              kNoMaximum,      // maximum
              5,               // initial size
              ADD_COUNT('m'),  // module name
              ADD_COUNT('s'),  // table name
              kExternalTable,  // import kind
              kExternRefCode,  // elem_type
              kNoMaximum,      // maximum
              10),             // initial size
      // funcs ---------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration ---------------------------------------------------
      SECTION(Table, ENTRY_COUNT(2),          // section header
              kFuncRefCode, kNoMaximum, 15,   // table 0
              kFuncRefCode, kNoMaximum, 19),  // table 1
      // elements ------------------------------------------------------------
      SECTION(Element,
              ENTRY_COUNT(4),             // entry count
              TABLE_INDEX0,               // element for table 0
              WASM_INIT_EXPR_I32V_1(10),  // index
              1,                          // elements count
              FUNC_INDEX(0),              // function
              TABLE_INDEX(1),             // element for table 1
              WASM_INIT_EXPR_I32V_1(17),  // index
              kExternalFunction,          // type
              2,                          // elements count
              FUNC_INDEX(0),              // entry 0
              FUNC_INDEX(0)),             // entry 1
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ElementSectionGlobalGetOutOfBounds) {
  static const uint8_t data[] = {
      SECTION(Element, ENTRY_COUNT(1),
              0x05,            // Mode: Passive with expressions-as-elements
              kFuncRefCode,    // type
              ENTRY_COUNT(1),  // element count
              kExprGlobalGet, 0x00, kExprEnd)};  // initial value
  EXPECT_FAILURE_WITH_MSG(data, "Invalid global index: 0");
}

TEST_F(WasmModuleVerifyTest, ExtendedConstantsI32) {
  static const uint8_t data[] = {
      SECTION(Import, ENTRY_COUNT(1),         // one import
              0x01, 'm', 0x01, 'g',           // module, name
              kExternalGlobal, kI32Code, 0),  // type, mutability
      SECTION(Global, ENTRY_COUNT(1),         // one defined global
              kI32Code, 0,                    // type, mutability
              // initializer
              kExprGlobalGet, 0x00, kExprGlobalGet, 0x00, kExprI32Add,
              kExprGlobalGet, 0x00, kExprI32Sub, kExprGlobalGet, 0x00,
              kExprI32Mul, kExprEnd)};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ExtendedConstantsI64) {
  static const uint8_t data[] = {
      SECTION(Import, ENTRY_COUNT(1),         // one import
              0x01, 'm', 0x01, 'g',           // module, name
              kExternalGlobal, kI64Code, 0),  // type, mutability
      SECTION(Global, ENTRY_COUNT(1),         // one defined global
              kI64Code, 0,                    // type, mutability
              // initializer
              kExprGlobalGet, 0x00, kExprGlobalGet, 0x00, kExprI64Add,
              kExprGlobalGet, 0x00, kExprI64Sub, kExprGlobalGet, 0x00,
              kExprI64Mul, kExprEnd)};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ExtendedConstantsTypeError) {
  static const uint8_t data[] = {
      SECTION(Import, ENTRY_COUNT(1),         // one import
              0x01, 'm', 0x01, 'g',           // module, name
              kExternalGlobal, kI32Code, 0),  // type, mutability
      SECTION(Global, ENTRY_COUNT(1),         // one defined global
              kI32Code, 0,                    // type, mutability
              // initializer
              kExprGlobalGet, 0x00, kExprI64Const, 1, kExprI32Add, kExprEnd)};
  EXPECT_FAILURE_WITH_MSG(
      data, "i32.add[1] expected type i32, found i64.const of type i64");
}

TEST_F(WasmModuleVerifyTest, IndirectFunctionNoFunctions) {
  static const uint8_t data[] = {
      // sig#0 -------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // indirect table ----------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), 1, 0, 0)};

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, IndirectFunctionInvalidIndex) {
  static const uint8_t data[] = {
      // sig#0 -------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // functions ---------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // indirect table ----------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), 1, 1, 0)};

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, MultipleTables) {
  static const uint8_t data[] = {
      SECTION(Table,           // table section
              ENTRY_COUNT(2),  // 2 tables
              kFuncRefCode,    // table 1: type
              kNoMaximum,      // table 1: no maximum
              10,              // table 1: minimum size
              kExternRefCode,  // table 2: type
              kNoMaximum,      // table 2: no maximum
              11),             // table 2: minimum size
  };

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(2u, result.value()->tables.size());

  EXPECT_EQ(10u, result.value()->tables[0].initial_size);
  EXPECT_EQ(kWasmFuncRef, result.value()->tables[0].type);

  EXPECT_EQ(11u, result.value()->tables[1].initial_size);
  EXPECT_EQ(kWasmExternRef, result.value()->tables[1].type);
}

TEST_F(WasmModuleVerifyTest, TypedFunctionTable) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_x(kI32Code)),
      SECTION(Table,             // table section
              ENTRY_COUNT(1),    // 1 table
              kRefNullCode, 0,   // table 0: type
              kNoMaximum, 10)};  // table 0: limits

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(ValueType::RefNull(0), result.value()->tables[0].type);
}

TEST_F(WasmModuleVerifyTest, NullableTableIllegalInitializer) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),  // type section
      ONE_EMPTY_FUNCTION(0),                         // function section
      SECTION(Table,                                 // table section
              ENTRY_COUNT(1),                        // 1 table
              kRefNullCode, 0,                       // table 0: type
              kNoMaximum, 10,                        // table 0: limits
              kExprRefFunc, 0, kExprEnd)};           // table 0: initializer

  EXPECT_FAILURE_WITH_MSG(
      data,
      "section was shorter than expected size (8 bytes expected, 5 decoded)");
}

TEST_F(WasmModuleVerifyTest, IllegalTableTypes) {
  using Vec = std::vector<uint8_t>;

  static Vec table_types[] = {{kI32Code}, {kF64Code}};

  for (Vec type : table_types) {
    Vec data = {
        SECTION(Type, ENTRY_COUNT(2),
                WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)),
                WASM_ARRAY_DEF(kI32Code, true)),
        kTableSectionCode, static_cast<uint8_t>(type.size() + 3), uint8_t{1}};
    // Last elements are section size and entry count

    // Add table type
    data.insert(data.end(), type.begin(), type.end());
    // Add table limits
    data.insert(data.end(), {uint8_t{0}, uint8_t{10}});

    auto result = DecodeModule(base::VectorOf(data));
    EXPECT_NOT_OK(result, "Only reference types can be used as table types");
  }
}

TEST_F(WasmModuleVerifyTest, TableWithInitializer) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),  // type section
      ONE_EMPTY_FUNCTION(0),                         // function section
      SECTION(Table,                                 // table section
              ENTRY_COUNT(1),                        // 1 table
              0x40,                                  // table 0: has initializer
              0x00,                                  // table 0: reserved byte
              kRefNullCode, 0,                       // table 0: type
              kNoMaximum, 10,                        // table 0: limits
              kExprRefFunc, 0, kExprEnd),            // table 0: initial value
      SECTION(Code, ENTRY_COUNT(1), NOP_BODY)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(ValueType::RefNull(0), result.value()->tables[0].type);
}

TEST_F(WasmModuleVerifyTest, NonNullableTable) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),  // type section
      ONE_EMPTY_FUNCTION(0),                         // function section
      SECTION(Table,                                 // table section
              ENTRY_COUNT(1),                        // 1 table
              0x40,                                  // table 0: has initializer
              0x00,                                  // table 0: reserved byte
              kRefCode, 0,                           // table 0: type
              kNoMaximum, 10,                        // table 0: limits
              kExprRefFunc, 0, kExprEnd),            // table 0: initial value
      SECTION(Code, ENTRY_COUNT(1), NOP_BODY)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
  EXPECT_EQ(ValueType::Ref(0), result.value()->tables[0].type);
}

TEST_F(WasmModuleVerifyTest, NonNullableTableNoInitializer) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_x(kI32Code)),
      SECTION(Table,                      // table section
              ENTRY_COUNT(2),             // 2 tables
              kRefCode, 0,                // table 0: type
              kNoMaximum, 10,             // table 0: limits
              kRefCode, 0,                // table 1: type
              kMemory64WithMaximum, 6)};  // table 1: limits

  EXPECT_FAILURE_WITH_MSG(
      data, "Table of non-defaultable table (ref 0) needs initial value");
}

TEST_F(WasmModuleVerifyTest, TieringCompilationHints) {
  WASM_FEATURE_SCOPE(compilation_hints);
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),
      FUNCTION_SECTION(3, 0, 0, 0),
      SECTION_COMPILATION_HINTS(BASELINE_TIER_BASELINE | TOP_TIER_BASELINE,
                                BASELINE_TIER_BASELINE | TOP_TIER_OPTIMIZED,
                                BASELINE_TIER_OPTIMIZED | TOP_TIER_OPTIMIZED),
      SECTION(Code, ENTRY_COUNT(3), NOP_BODY, NOP_BODY, NOP_BODY),
  };

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(3u, result.value()->compilation_hints.size());
  EXPECT_EQ(WasmCompilationHintStrategy::kDefault,
            result.value()->compilation_hints[0].strategy);
  EXPECT_EQ(WasmCompilationHintTier::kBaseline,
            result.value()->compilation_hints[0].baseline_tier);
  EXPECT_EQ(WasmCompilationHintTier::kBaseline,
            result.value()->compilation_hints[0].top_tier);
  EXPECT_EQ(WasmCompilationHintStrategy::kDefault,
            result.value()->compilation_hints[1].strategy);
  EXPECT_EQ(WasmCompilationHintTier::kBaseline,
            result.value()->compilation_hints[1].baseline_tier);
  EXPECT_EQ(WasmCompilationHintTier::kOptimized,
            result.value()->compilation_hints[1].top_tier);
  EXPECT_EQ(WasmCompilationHintStrategy::kDefault,
            result.value()->compilation_hints[2].strategy);
  EXPECT_EQ(WasmCompilationHintTier::kOptimized,
            result.value()->compilation_hints[2].baseline_tier);
  EXPECT_EQ(WasmCompilationHintTier::kOptimized,
            result.value()->compilation_hints[2].top_tier);
}

TEST_F(WasmModuleVerifyTest, BranchHinting) {
  WASM_FEATURE_SCOPE(branch_hinting);
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v), FUNCTION_SECTION(2, 0, 0),
      SECTION_BRANCH_HINTS(ENTRY_COUNT(2), 0 /*func_index*/, ENTRY_COUNT(1),
                           3 /* if offset*/, 1 /*reserved*/, 1 /*likely*/,
                           1 /*func_index*/, ENTRY_COUNT(1),
                           5 /* br_if offset*/, 1 /*reserved*/, 0 /*unlikely*/),
      SECTION(Code, ENTRY_COUNT(2),
              ADD_COUNT(0, /*no locals*/
                        WASM_IF(WASM_I32V_1(1), WASM_NOP), WASM_END),
              ADD_COUNT(0, /*no locals*/
                        WASM_BLOCK(WASM_BR_IF(0, WASM_I32V_1(1))), WASM_END))};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(2u, result.value()->branch_hints.size());
  EXPECT_EQ(WasmBranchHint::kLikely,
            result.value()->branch_hints[0].GetHintFor(3));
  EXPECT_EQ(WasmBranchHint::kUnlikely,
            result.value()->branch_hints[1].GetHintFor(5));
}

class WasmSignatureDecodeTest : public TestWithZone {
 public:
  WasmEnabledFeatures enabled_features_ = WasmEnabledFeatures::None();

  const FunctionSig* DecodeSig(base::Vector<const uint8_t> bytes) {
    Result<const FunctionSig*> res =
        DecodeWasmSignatureForTesting(enabled_features_, zone(), bytes);
    EXPECT_TRUE(res.ok()) << res.error().message() << " at offset "
                          << res.error().offset();
    return res.ok() ? res.value() : nullptr;
  }

  V8_NODISCARD testing::AssertionResult DecodeSigError(
      base::Vector<const uint8_t> bytes) {
    Result<const FunctionSig*> res =
        DecodeWasmSignatureForTesting(enabled_features_, zone(), bytes);
    if (res.ok()) {
      return testing::AssertionFailure() << "unexpected valid signature";
    }
    return testing::AssertionSuccess();
  }
};

TEST_F(WasmSignatureDecodeTest, Ok_v_v) {
  static const uint8_t data[] = {SIG_ENTRY_v_v};
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

  ASSERT_TRUE(sig != nullptr);
  EXPECT_EQ(0u, sig->parameter_count());
  EXPECT_EQ(0u, sig->return_count());
}

TEST_F(WasmSignatureDecodeTest, Ok_t_v) {
  WASM_FEATURE_SCOPE(stringref);
  WASM_FEATURE_SCOPE(exnref);
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueTypePair ret_type = kValueTypes[i];
    const uint8_t data[] = {SIG_ENTRY_x(ret_type.code)};
    const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

    SCOPED_TRACE("Return type " + ret_type.type.name());
    ASSERT_TRUE(sig != nullptr);
    EXPECT_EQ(0u, sig->parameter_count());
    EXPECT_EQ(1u, sig->return_count());
    EXPECT_EQ(ret_type.type, sig->GetReturn());
  }
}

TEST_F(WasmSignatureDecodeTest, Ok_v_t) {
  WASM_FEATURE_SCOPE(stringref);
  WASM_FEATURE_SCOPE(exnref);
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueTypePair param_type = kValueTypes[i];
    const uint8_t data[] = {SIG_ENTRY_v_x(param_type.code)};
    const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

    SCOPED_TRACE("Param type " + param_type.type.name());
    ASSERT_TRUE(sig != nullptr);
    EXPECT_EQ(1u, sig->parameter_count());
    EXPECT_EQ(0u, sig->return_count());
    EXPECT_EQ(param_type.type, sig->GetParam(0));
  }
}

TEST_F(WasmSignatureDecodeTest, Ok_t_t) {
  WASM_FEATURE_SCOPE(stringref);
  WASM_FEATURE_SCOPE(exnref);
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueTypePair ret_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueTypePair param_type = kValueTypes[j];
      const uint8_t data[] = {SIG_ENTRY_x_x(ret_type.code, param_type.code)};
      const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

      SCOPED_TRACE("Param type " + param_type.type.name());
      ASSERT_TRUE(sig != nullptr);
      EXPECT_EQ(1u, sig->parameter_count());
      EXPECT_EQ(1u, sig->return_count());
      EXPECT_EQ(param_type.type, sig->GetParam(0));
      EXPECT_EQ(ret_type.type, sig->GetReturn());
    }
  }
}

TEST_F(WasmSignatureDecodeTest, Ok_i_tt) {
  WASM_FEATURE_SCOPE(stringref);
  WASM_FEATURE_SCOPE(exnref);
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueTypePair p0_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueTypePair p1_type = kValueTypes[j];
      const uint8_t data[] = {
          SIG_ENTRY_x_xx(kI32Code, p0_type.code, p1_type.code)};
      const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

      SCOPED_TRACE("Signature i32(" + p0_type.type.name() + ", " +
                   p1_type.type.name() + ")");
      ASSERT_TRUE(sig != nullptr);
      EXPECT_EQ(2u, sig->parameter_count());
      EXPECT_EQ(1u, sig->return_count());
      EXPECT_EQ(p0_type.type, sig->GetParam(0));
      EXPECT_EQ(p1_type.type, sig->GetParam(1));
    }
  }
}

TEST_F(WasmSignatureDecodeTest, Ok_tt_tt) {
  WASM_FEATURE_SCOPE(stringref);
  WASM_FEATURE_SCOPE(exnref);
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueTypePair p0_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueTypePair p1_type = kValueTypes[j];
      const uint8_t data[] = {SIG_ENTRY_xx_xx(p0_type.code, p1_type.code,
                                              p0_type.code, p1_type.code)};
      const FunctionSig* sig = DecodeSig(base::ArrayVector(data));

      SCOPED_TRACE("p0 = " + p0_type.type.name() +
                   ", p1 = " + p1_type.type.name());
      ASSERT_TRUE(sig != nullptr);
      EXPECT_EQ(2u, sig->parameter_count());
      EXPECT_EQ(2u, sig->return_count());
      EXPECT_EQ(p0_type.type, sig->GetParam(0));
      EXPECT_EQ(p1_type.type, sig->GetParam(1));
      EXPECT_EQ(p0_type.type, sig->GetReturn(0));
      EXPECT_EQ(p1_type.type, sig->GetReturn(1));
    }
  }
}

TEST_F(WasmSignatureDecodeTest, Simd) {
  const uint8_t data[] = {SIG_ENTRY_x(kS128Code)};
  if (!CheckHardwareSupportsSimd()) {
    EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)))
        << "Type S128 should not be allowed on this hardware";
  } else {
    const FunctionSig* sig = DecodeSig(base::ArrayVector(data));
    ASSERT_TRUE(sig != nullptr);
    EXPECT_EQ(0u, sig->parameter_count());
    EXPECT_EQ(1u, sig->return_count());
    EXPECT_EQ(kWasmS128, sig->GetReturn());
  }
}

TEST_F(WasmSignatureDecodeTest, TooManyParams) {
  static const uint8_t data[] = {kWasmFunctionTypeCode,
                                 WASM_I32V_3(kV8MaxWasmFunctionParams + 1),
                                 kI32Code, 0};
  EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
}

TEST_F(WasmSignatureDecodeTest, TooManyReturns) {
  for (int i = 0; i < 2; i++) {
    uint8_t data[] = {kWasmFunctionTypeCode, 0,
                      WASM_I32V_3(kV8MaxWasmFunctionReturns + 1), kI32Code};
    EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
  }
}

TEST_F(WasmSignatureDecodeTest, Fail_off_end) {
  uint8_t data[256];
  for (int p = 0; p <= 255; p = p + 1 + p * 3) {
    for (int i = 0; i <= p; i++) data[i] = kI32Code;
    data[0] = static_cast<uint8_t>(p);

    for (int i = 0; i < p + 1; i++) {
      // Should fall off the end for all signatures.
      EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
    }
  }
}

TEST_F(WasmSignatureDecodeTest, Fail_invalid_type) {
  uint8_t kInvalidType = 76;
  for (size_t i = 0;; i++) {
    uint8_t data[] = {SIG_ENTRY_x_xx(kI32Code, kI32Code, kI32Code)};
    if (i >= arraysize(data)) break;
    data[i] = kInvalidType;
    EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
  }
}

TEST_F(WasmSignatureDecodeTest, Fail_invalid_ret_type1) {
  static const uint8_t data[] = {SIG_ENTRY_x_x(kVoidCode, kI32Code)};
  EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
}

TEST_F(WasmSignatureDecodeTest, Fail_invalid_param_type1) {
  static const uint8_t data[] = {SIG_ENTRY_x_x(kI32Code, kVoidCode)};
  EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
}

TEST_F(WasmSignatureDecodeTest, Fail_invalid_param_type2) {
  static const uint8_t data[] = {SIG_ENTRY_x_xx(kI32Code, kI32Code, kVoidCode)};
  EXPECT_TRUE(DecodeSigError(base::ArrayVector(data)));
}

class WasmFunctionVerifyTest : public TestWithIsolateAndZone {
 public:
  FunctionResult DecodeWasmFunction(
      ModuleWireBytes wire_bytes, const WasmModule* module,
      base::Vector<const uint8_t> function_bytes) {
    return DecodeWasmFunctionForTesting(WasmEnabledFeatures::All(), zone(),
                                        wire_bytes, module, function_bytes);
  }
};

TEST_F(WasmFunctionVerifyTest, Ok_v_v_empty) {
  static const uint8_t data[] = {
      SIG_ENTRY_v_v,  // signature entry
      4,              // locals
      3,
      kI32Code,  // --
      4,
      kI64Code,  // --
      5,
      kF32Code,  // --
      6,
      kF64Code,  // --
      kExprEnd   // body
  };

  WasmModule module;
  FunctionResult result =
      DecodeWasmFunction(ModuleWireBytes({}), &module, base::ArrayVector(data));
  EXPECT_OK(result);

  if (result.value() && result.ok()) {
    WasmFunction* function = result.value().get();
    EXPECT_EQ(0u, function->sig->parameter_count());
    EXPECT_EQ(0u, function->sig->return_count());
    EXPECT_EQ(COUNT_ARGS(SIG_ENTRY_v_v), function->code.offset());
    EXPECT_EQ(sizeof(data), function->code.end_offset());
    // TODO(titzer): verify encoding of local declarations
  }
}

TEST_F(WasmModuleVerifyTest, SectionWithoutNameLength) {
  const uint8_t data[] = {1};
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, EmptyCustomSectionIsInvalid) {
  // An empty custom section is invalid, because at least one byte for the
  // length of the custom section name is required.
  const uint8_t data[] = {
      0,  // unknown section code.
      0   // section length.
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, TheLoneliestOfValidModulesTheTrulyEmptyOne) {
  const uint8_t data[] = {
      0,  // unknown section code.
      1,  // section length, only one byte for the name length.
      0,  // string length of 0.
          // Empty section name, no content, nothing but sadness.
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, OnlyUnknownSectionEmpty) {
  const uint8_t data[] = {
      UNKNOWN_SECTION(0),
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, OnlyUnknownSectionNonEmpty) {
  const uint8_t data[] = {
      UNKNOWN_SECTION(5),
      0xFF,
      0xFF,
      0xFF,
      0xFF,
      0xFF,  // section data
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, SignatureFollowedByEmptyUnknownSection) {
  const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // -----------------------------------------------------------
      UNKNOWN_SECTION(0)};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, SignatureFollowedByUnknownSection) {
  const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // -----------------------------------------------------------
      UNKNOWN_SECTION(5),
      0xFF,
      0xFF,
      0xFF,
      0xFF,
      0xFF,
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, UnknownSectionOverflow) {
  static const uint8_t data[] = {
      UNKNOWN_SECTION(9),
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,  // 10 byte section
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, UnknownSectionUnderflow) {
  static const uint8_t data[] = {
      UNKNOWN_SECTION(333),
      1,
      2,
      3,
      4,  // 4 byte section
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, UnknownSectionSkipped) {
  static const uint8_t data[] = {
      UNKNOWN_SECTION(1),
      0,  // one byte section
      SECTION(Global, ENTRY_COUNT(1),
              kI32Code,                    // memory type
              0,                           // exported
              WASM_INIT_EXPR_I32V_1(33)),  // init
  };
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(1u, result.value()->globals.size());
  EXPECT_EQ(0u, result.value()->functions.size());
  EXPECT_EQ(0u, result.value()->data_segments.size());

  const WasmGlobal* global = &result.value()->globals.back();

  EXPECT_EQ(kWasmI32, global->type);
  EXPECT_EQ(0u, global->offset);
}

TEST_F(WasmModuleVerifyTest, ImportTable_empty) {
  static const uint8_t data[] = {SECTION(Type, ENTRY_COUNT(0)),
                                 SECTION(Import, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_nosigs1) {
  static const uint8_t data[] = {SECTION(Import, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_mutable_global) {
  {
    static const uint8_t data[] = {
        SECTION(Import,           // section header
                ENTRY_COUNT(1),   // number of imports
                ADD_COUNT('m'),   // module name
                ADD_COUNT('f'),   // global name
                kExternalGlobal,  // import kind
                kI32Code,         // type
                0),               // mutability
    };
    EXPECT_VERIFIES(data);
  }
  {
    static const uint8_t data[] = {
        SECTION(Import,           // section header
                ENTRY_COUNT(1),   // sig table
                ADD_COUNT('m'),   // module name
                ADD_COUNT('f'),   // global name
                kExternalGlobal,  // import kind
                kI32Code,         // type
                1),               // mutability
    };
    EXPECT_VERIFIES(data);
  }
}

TEST_F(WasmModuleVerifyTest, ImportTable_mutability_malformed) {
  static const uint8_t data[] = {
      SECTION(Import,
              ENTRY_COUNT(1),   // --
              ADD_COUNT('m'),   // module name
              ADD_COUNT('g'),   // global name
              kExternalGlobal,  // import kind
              kI32Code,         // type
              2),               // invalid mutability
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_nosigs2) {
  static const uint8_t data[] = {
      SECTION(Import, ENTRY_COUNT(1),  // sig table
              ADD_COUNT('m'),          // module name
              ADD_COUNT('f'),          // function name
              kExternalFunction,       // import kind
              SIG_INDEX(0)),           // sig index
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_invalid_sig) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(0)),   // --
      SECTION(Import, ENTRY_COUNT(1),  // --
              ADD_COUNT('m'),          // module name
              ADD_COUNT('f'),          // function name
              kExternalFunction,       // import kind
              SIG_INDEX(0)),           // sig index
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_one_sig) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      SECTION(Import,
              ENTRY_COUNT(1),     // --
              ADD_COUNT('m'),     // module name
              ADD_COUNT('f'),     // function name
              kExternalFunction,  // import kind
              SIG_INDEX(0)),      // sig index
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_invalid_module) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID,  // --
      SECTION(Import,                  // --
              ENTRY_COUNT(1),          // --
              NO_NAME,                 // module name
              ADD_COUNT('f'),          // function name
              kExternalFunction,       // import kind
              SIG_INDEX(0),            // sig index
              0),                      // auxiliary data
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ImportTable_off_end) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      SECTION(Import, ENTRY_COUNT(1),
              ADD_COUNT('m'),      // module name
              ADD_COUNT('f'),      // function name
              kExternalFunction),  // import kind
      SIG_INDEX(0),                // sig index (outside import section!)
  };

  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 3);
}

TEST_F(WasmModuleVerifyTest, ExportTable_empty1) {
  static const uint8_t data[] = {                                 // signatures
                                 TYPE_SECTION_ONE_SIG_VOID_VOID,  // --
                                 ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
                                 SECTION(Export, ENTRY_COUNT(0)),  // --
                                 ONE_EMPTY_BODY};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(1u, result.value()->functions.size());
  EXPECT_EQ(0u, result.value()->export_table.size());
}

TEST_F(WasmModuleVerifyTest, ExportTable_empty2) {
  static const uint8_t data[] = {SECTION(Type, ENTRY_COUNT(0)),
                                 SECTION(Export, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ExportTable_NoFunctions2) {
  static const uint8_t data[] = {SECTION(Export, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, ExportTableOne) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID, ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      SECTION(Export,
              ENTRY_COUNT(1),     // exports
              NO_NAME,            // --
              kExternalFunction,  // --
              FUNC_INDEX(0)),     // --
      ONE_EMPTY_BODY};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(1u, result.value()->functions.size());
  EXPECT_EQ(1u, result.value()->export_table.size());
}

TEST_F(WasmModuleVerifyTest, ExportNameWithInvalidStringLength) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID, ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      SECTION(Export,
              ENTRY_COUNT(1),     // exports
              U32V_1(84),         // invalid string length
              'e',                // --
              kExternalFunction,  // --
              FUNC_INDEX(0),      // --
              0, 0, 0)            // auxiliary data
  };

  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, ExportTableTwo) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID, ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      SECTION(Export,
              ENTRY_COUNT(2),                 // exports
              ADD_COUNT('n', 'a', 'm', 'e'),  // --
              kExternalFunction,              // --
              FUNC_INDEX(0),                  // --
              ADD_COUNT('n', 'o', 'm'),       // --
              kExternalFunction,              // --
              FUNC_INDEX(0)),                 // --
      ONE_EMPTY_BODY};

  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(1u, result.value()->functions.size());
  EXPECT_EQ(2u, result.value()->export_table.size());
}

TEST_F(WasmModuleVerifyTest, ExportTableThree) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID, THREE_EMPTY_FUNCTIONS(SIG_INDEX(0)),
      SECTION(Export,
              ENTRY_COUNT(3),  // exports
              ADD_COUNT('a'),  // --
              kExternalFunction,
              FUNC_INDEX(0),   // --
              ADD_COUNT('b'),  // --
              kExternalFunction,
              FUNC_INDEX(1),   // --
              ADD_COUNT('c'),  // --
              kExternalFunction,
              FUNC_INDEX(2)),  // --
      THREE_EMPTY_BODIES};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);

  EXPECT_EQ(3u, result.value()->functions.size());
  EXPECT_EQ(3u, result.value()->export_table.size());
}

TEST_F(WasmModuleVerifyTest, ExportTableThreeOne) {
  for (int i = 0; i < 6; i++) {
    const uint8_t data[] = {
        // signatures
        TYPE_SECTION_ONE_SIG_VOID_VOID, THREE_EMPTY_FUNCTIONS(SIG_INDEX(0)),
        SECTION(Export,
                ENTRY_COUNT(1),       // exports
                ADD_COUNT('e', 'x'),  // --
                kExternalFunction,
                FUNC_INDEX(i)),  // --
        THREE_EMPTY_BODIES};

    if (i < 3) {
      EXPECT_VERIFIES(data);
    } else {
      EXPECT_FAILURE(data);
    }
  }
}

TEST_F(WasmModuleVerifyTest, ExportTableOne_off_end) {
  static const uint8_t data[] = {
      // signatures
      TYPE_SECTION_ONE_SIG_VOID_VOID, ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      SECTION(Export,
              ENTRY_COUNT(1),  // exports
              NO_NAME,         // --
              kExternalFunction,
              FUNC_INDEX(0),  // --
              0, 0, 0)        // auxiliary data
  };

  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 3);
}

TEST_F(WasmModuleVerifyTest, Regression_648070) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(0)),         // --
      SECTION(Function, U32V_5(3500228624))  // function count = 3500228624
  };                                         // --
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, Regression_738097) {
  // The function body size caused an integer overflow in the module decoder.
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),  // --
      FUNCTION_SECTION(1, 0),          // --
      SECTION(Code,                    // --
              ENTRY_COUNT(1),          // --
              U32V_5(0xFFFFFFFF),      // function size,
              0)                       // No real body
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, FunctionBodySizeLimit) {
  const uint32_t delta = 3;
  for (uint32_t body_size = kV8MaxWasmFunctionSize - delta;
       body_size < kV8MaxWasmFunctionSize + delta; body_size++) {
    uint8_t data[] = {
        TYPE_SECTION(1, SIG_ENTRY_v_v),  // --
        FUNCTION_SECTION(1, 0),          // --
        kCodeSectionCode,                // code section
        U32V_5(1 + body_size + 5),       // section size
        1,                               // # functions
        U32V_5(body_size)                // body size
    };
    size_t total = sizeof(data) + body_size;
    uint8_t* buffer = reinterpret_cast<uint8_t*>(calloc(1, total));
    memcpy(buffer, data, sizeof(data));
    ModuleResult result = DecodeModule(base::VectorOf(buffer, total));
    if (body_size <= kV8MaxWasmFunctionSize) {
      EXPECT_TRUE(result.ok());
    } else {
      EXPECT_FALSE(result.ok());
    }
    free(buffer);
  }
}

TEST_F(WasmModuleVerifyTest, IllegalTypeCode) {
  static const uint8_t data[] = {TYPE_SECTION(1, SIG_ENTRY_v_x(0x41))};
  EXPECT_FAILURE_WITH_MSG(data, "invalid value type");
}

TEST_F(WasmModuleVerifyTest, FunctionBodies_empty) {
  static const uint8_t data[] = {
      EMPTY_TYPE_SECTION,            // --
      EMPTY_FUNCTION_SECTION,        // --
      EMPTY_FUNCTION_BODIES_SECTION  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, FunctionBodies_one_empty) {
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),  // --
      FUNCTION_SECTION(1, 0),          // --
      ONE_EMPTY_BODY                   // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, FunctionBodies_one_nop) {
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),          // --
      FUNCTION_SECTION(1, 0),                  // --
      SECTION(Code, ENTRY_COUNT(1), NOP_BODY)  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, FunctionBodies_count_mismatch1) {
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),  // --
      FUNCTION_SECTION(2, 0, 0),       // --
      ONE_EMPTY_BODY                   // --
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, FunctionBodies_count_mismatch2) {
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),                    // --
      FUNCTION_SECTION(1, 0),                            // --
      SECTION(Code, ENTRY_COUNT(2), NOP_BODY, NOP_BODY)  // --
  };
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, Names_empty) {
  static const uint8_t data[] = {EMPTY_TYPE_SECTION, EMPTY_FUNCTION_SECTION,
                                 EMPTY_FUNCTION_BODIES_SECTION,
                                 EMPTY_NAMES_SECTION};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, Names_one_empty) {
  // TODO(wasm): This test does not test anything (corrupt name section does not
  // fail validation).
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),                            // --
      FUNCTION_SECTION(1, 0),                                    // --
      ONE_EMPTY_BODY,                                            // --
      SECTION_NAMES(ENTRY_COUNT(1), FOO_STRING, NO_LOCAL_NAMES)  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, Names_two_empty) {
  // TODO(wasm): This test does not test anything (corrupt name section does not
  // fail validation).
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),             // --
      FUNCTION_SECTION(2, 0, 0),                  // --
      TWO_EMPTY_BODIES,                           // --
      SECTION_NAMES(ENTRY_COUNT(2),               // --
                    FOO_STRING, NO_LOCAL_NAMES,   // --
                    FOO_STRING, NO_LOCAL_NAMES),  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, Regression684855) {
  static const uint8_t data[] = {
      SECTION_NAMES(0xFB,  // functions count
                    0x27,  // |
                    0x00,  // function name length
                    0xFF,  // local names count
                    0xFF,  // |
                    0xFF,  // |
                    0xFF,  // |
                    0xFF,  // |
                    0xFF,  // error: "varint too large"
                    0xFF,  // |
                    0x00,  // --
                    0x00)  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, FunctionSectionWithoutCodeSection) {
  static const uint8_t data[] = {
      TYPE_SECTION(1, SIG_ENTRY_v_v),  // Type section.
      FUNCTION_SECTION(1, 0),          // Function section.
  };
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "function count is 1, but code section is absent");
}

TEST_F(WasmModuleVerifyTest, CodeSectionWithoutFunctionSection) {
  static const uint8_t data[] = {ONE_EMPTY_BODY};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "function body count 1 mismatch (0 expected)");
}

TEST_F(WasmModuleVerifyTest, EmptyFunctionSectionWithoutCodeSection) {
  static const uint8_t data[] = {SECTION(Function, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, DoubleNonEmptyFunctionSection) {
  // Regression test for https://crbug.com/1342274.
  static const uint8_t data[] = {TYPE_SECTION(1, SIG_ENTRY_v_v),  // --
                                 FUNCTION_SECTION(1, 0),          // --
                                 FUNCTION_SECTION(1, 0)};
  EXPECT_FAILURE(data);
}

TEST_F(WasmModuleVerifyTest, EmptyCodeSectionWithoutFunctionSection) {
  static const uint8_t data[] = {SECTION(Code, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

// TODO(manoskouk): Reintroduce tests deleted in
// https://chromium-review.googlesource.com/c/v8/v8/+/2972910 in some other
// form.

TEST_F(WasmModuleVerifyTest, Multiple_Named_Sections) {
  static const uint8_t data[] = {
      SECTION(Unknown, ADD_COUNT('X'), 17, 18),                    // --
      SECTION(Unknown, ADD_COUNT('f', 'o', 'o'), 5, 6, 7, 8, 9),   // --
      SECTION(Unknown, ADD_COUNT('o', 't', 'h', 'e', 'r'), 7, 8),  // --
  };
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, Section_Name_No_UTF8) {
  static const uint8_t data[] = {SECTION(Unknown, 1, 0xFF, 17, 18)};
  EXPECT_FAILURE(data);
}

class WasmModuleCustomSectionTest : public TestWithIsolateAndZone {
 public:
  void CheckSections(base::Vector<const uint8_t> wire_bytes,
                     const CustomSectionOffset* expected, size_t num_expected) {
    std::vector<CustomSectionOffset> custom_sections =
        DecodeCustomSections(wire_bytes);

    CHECK_EQ(num_expected, custom_sections.size());

    for (size_t i = 0; i < num_expected; i++) {
      EXPECT_EQ(expected[i].section.offset(),
                custom_sections[i].section.offset());
      EXPECT_EQ(expected[i].section.length(),
                custom_sections[i].section.length());
      EXPECT_EQ(expected[i].name.offset(), custom_sections[i].name.offset());
      EXPECT_EQ(expected[i].name.length(), custom_sections[i].name.length());
      EXPECT_EQ(expected[i].payload.offset(),
                custom_sections[i].payload.offset());
      EXPECT_EQ(expected[i].payload.length(),
                custom_sections[i].payload.length());
    }
  }
};

TEST_F(WasmModuleCustomSectionTest, ThreeUnknownSections) {
  static constexpr uint8_t data[] = {
      U32_LE(kWasmMagic),                                  // --
      U32_LE(kWasmVersion),                                // --
      SECTION(Unknown, 1, 'X', 17, 18),                    // --
      SECTION(Unknown, 3, 'f', 'o', 'o', 5, 6, 7, 8, 9),   // --
      SECTION(Unknown, 5, 'o', 't', 'h', 'e', 'r', 7, 8),  // --
  };

  static const CustomSectionOffset expected[] = {
      // section, name, payload
      {{10, 4}, {11, 1}, {12, 2}},  // --
      {{16, 9}, {17, 3}, {20, 5}},  // --
      {{27, 8}, {28, 5}, {33, 2}},  // --
  };

  CheckSections(base::ArrayVector(data), expected, arraysize(expected));
}

TEST_F(WasmModuleCustomSectionTest, TwoKnownTwoUnknownSections) {
  static const uint8_t data[] = {
      U32_LE(kWasmMagic),                                          // --
      U32_LE(kWasmVersion),                                        // --
      TYPE_SECTION(2, SIG_ENTRY_v_v, SIG_ENTRY_v_v),               // --
      SECTION(Unknown, ADD_COUNT('X'), 17, 18),                    // --
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),                            // --
      SECTION(Unknown, ADD_COUNT('o', 't', 'h', 'e', 'r'), 7, 8),  // --
  };

  static const CustomSectionOffset expected[] = {
      // section, name, payload
      {{19, 4}, {20, 1}, {21, 2}},  // --
      {{29, 8}, {30, 5}, {35, 2}},  // --
  };

  CheckSections(base::ArrayVector(data), expected, arraysize(expected));
}

TEST_F(WasmModuleVerifyTest, SourceMappingURLSection) {
  static const uint8_t data[] = {
      WASM_MODULE_HEADER,
      SECTION_SRC_MAP('s', 'r', 'c', '/', 'x', 'y', 'z', '.', 'c')};
  ModuleResult result = DecodeModuleNoHeader(base::ArrayVector(data));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(WasmDebugSymbols::Type::SourceMap,
            result.value()->debug_symbols.type);
  ModuleWireBytes wire_bytes(base::ArrayVector(data));
  WasmName external_url =
      wire_bytes.GetNameOrNull(result.value()->debug_symbols.external_url);
  EXPECT_EQ("src/xyz.c", std::string(external_url.data(), external_url.size()));
}

TEST_F(WasmModuleVerifyTest, BadSourceMappingURLSection) {
  static const uint8_t data[] = {
      WASM_MODULE_HEADER,
      SECTION_SRC_MAP('s', 'r', 'c', '/', 'x', 0xff, 'z', '.', 'c')};
  ModuleResult result = DecodeModuleNoHeader(base::ArrayVector(data));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(WasmDebugSymbols::Type::None, result.value()->debug_symbols.type);
  EXPECT_EQ(0u, result.value()->debug_symbols.external_url.length());
}

TEST_F(WasmModuleVerifyTest, MultipleSourceMappingURLSections) {
  static const uint8_t data[] = {WASM_MODULE_HEADER,
                                 SECTION_SRC_MAP('a', 'b', 'c'),
                                 SECTION_SRC_MAP('p', 'q', 'r')};
  ModuleResult result = DecodeModuleNoHeader(base::ArrayVector(data));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(WasmDebugSymbols::Type::SourceMap,
            result.value()->debug_symbols.type);
  ModuleWireBytes wire_bytes(base::ArrayVector(data));
  WasmName external_url =
      wire_bytes.GetNameOrNull(result.value()->debug_symbols.external_url);
  EXPECT_EQ("abc", std::string(external_url.data(), external_url.size()));
}

TEST_F(WasmModuleVerifyTest, MultipleNameSections) {
  static const uint8_t data[] = {
      SECTION_NAMES(0, ADD_COUNT(ADD_COUNT('a', 'b', 'c'))),
      SECTION_NAMES(0, ADD_COUNT(ADD_COUNT('p', 'q', 'r', 's')))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3u, result.value()->name.length());
}

TEST_F(WasmModuleVerifyTest, BadNameSection) {
  static const uint8_t data[] = {SECTION_NAMES(
      0, ADD_COUNT(ADD_COUNT('s', 'r', 'c', '/', 'x', 0xff, 'z', '.', 'c')))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0u, result.value()->name.length());
}

TEST_F(WasmModuleVerifyTest, PassiveDataSegment) {
  static const uint8_t data[] = {
      // memory declaration ----------------------------------------------------
      SECTION(Memory, ENTRY_COUNT(1), kNoMaximum, 1),
      // data segments  --------------------------------------------------------
      SECTION(Data, ENTRY_COUNT(1), PASSIVE, ADD_COUNT('h', 'i')),
  };
  EXPECT_VERIFIES(data);
  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 5);
}

TEST_F(WasmModuleVerifyTest, ActiveElementSegmentWithElements) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration -----------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // element segments  -----------------------------------------------------
      SECTION(Element, ENTRY_COUNT(1), ACTIVE_WITH_ELEMENTS, TABLE_INDEX0,
              WASM_INIT_EXPR_I32V_1(0), kFuncRefCode, U32V_1(3),
              REF_FUNC_ELEMENT(0), REF_FUNC_ELEMENT(0), REF_NULL_ELEMENT),
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_VERIFIES(data);
  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 5);
}

TEST_F(WasmModuleVerifyTest, Table64ActiveElementSegmentWithElements) {
  WASM_FEATURE_SCOPE(memory64);
  for (bool enable_table64 : {false, true}) {
    for (bool use_table64 : {false, true}) {
      uint8_t const_opcode = use_table64 ? kExprI64Const : kExprI32Const;
      const uint8_t data[] = {
          // sig#0 -------------------------------------------------------
          TYPE_SECTION_ONE_SIG_VOID_VOID,
          // funcs -------------------------------------------------------
          ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
          // table declaration -------------------------------------------
          SECTION(Table, ENTRY_COUNT(1), kFuncRefCode,
                  enable_table64 ? kMemory64WithMaximum : kWithMaximum, 20, 28),
          // element segments --------------------------------------------
          SECTION(Element, ENTRY_COUNT(1), ACTIVE_WITH_ELEMENTS, TABLE_INDEX0,
                  const_opcode, 0, kExprEnd, kFuncRefCode, U32V_1(3),
                  REF_FUNC_ELEMENT(0), REF_FUNC_ELEMENT(0), REF_NULL_ELEMENT),
          // code --------------------------------------------------------
          ONE_EMPTY_BODY};
      if (enable_table64 == use_table64) {
        EXPECT_VERIFIES(data);
      } else if (enable_table64) {
        EXPECT_FAILURE_WITH_MSG(data, "expected i64, got i32");
      } else {
        EXPECT_FAILURE_WITH_MSG(data, "expected i32, got i64");
      }
    }
  }
}

TEST_F(WasmModuleVerifyTest, PassiveElementSegment) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration -----------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // element segments  -----------------------------------------------------
      SECTION(Element, ENTRY_COUNT(1), PASSIVE_WITH_ELEMENTS, kFuncRefCode,
              U32V_1(3), REF_FUNC_ELEMENT(0), REF_FUNC_ELEMENT(0),
              REF_NULL_ELEMENT),
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_VERIFIES(data);
  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 5);
}

TEST_F(WasmModuleVerifyTest, PassiveElementSegmentExternRef) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration -----------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // element segments  -----------------------------------------------------
      SECTION(Element, ENTRY_COUNT(1), PASSIVE_WITH_ELEMENTS, kExternRefCode,
              U32V_1(0)),
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, PassiveElementSegmentWithIndices) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // table declaration -----------------------------------------------------
      SECTION(Table, ENTRY_COUNT(1), kFuncRefCode, kNoMaximum, 1),
      // element segments ------------------------------------------------------
      SECTION(Element, ENTRY_COUNT(1), PASSIVE, kExternalFunction,
              ENTRY_COUNT(3), U32V_1(0), U32V_1(0), U32V_1(0)),
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_VERIFIES(data);
  EXPECT_OFF_END_FAILURE(data, arraysize(data) - 5);
}

TEST_F(WasmModuleVerifyTest, DeclarativeElementSegmentFuncRef) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // element segments  -----------------------------------------------------
      SECTION(Element,                    // section name
              ENTRY_COUNT(1),             // entry count
              DECLARATIVE_WITH_ELEMENTS,  // flags
              kFuncRefCode,               // local type
              U32V_1(0)),                 // func ref count
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, DeclarativeElementSegmentWithInvalidIndex) {
  static const uint8_t data[] = {
      // sig#0 -----------------------------------------------------------------
      TYPE_SECTION_ONE_SIG_VOID_VOID,
      // funcs -----------------------------------------------------------------
      ONE_EMPTY_FUNCTION(SIG_INDEX(0)),
      // element segments  -----------------------------------------------------
      SECTION(Element,            // section name
              ENTRY_COUNT(1),     // entry count
              DECLARATIVE,        // flags
              kExternalFunction,  // type
              ENTRY_COUNT(2),     // func index count
              U32V_1(0),          // func index
              U32V_1(1)),         // func index
      // code ------------------------------------------------------------------
      ONE_EMPTY_BODY};
  EXPECT_FAILURE_WITH_MSG(data, "function index 1 out of bounds");
}

TEST_F(WasmModuleVerifyTest, DataCountSectionCorrectPlacement) {
  static const uint8_t data[] = {SECTION(Element, ENTRY_COUNT(0)),
                                 SECTION(DataCount, ENTRY_COUNT(0)),
                                 SECTION(Code, ENTRY_COUNT(0))};
  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, DataCountSectionAfterCode) {
  static const uint8_t data[] = {SECTION(Code, ENTRY_COUNT(0)),
                                 SECTION(DataCount, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result,
                "The DataCount section must appear before the Code section");
}

TEST_F(WasmModuleVerifyTest, DataCountSectionBeforeElement) {
  static const uint8_t data[] = {SECTION(DataCount, ENTRY_COUNT(0)),
                                 SECTION(Element, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "unexpected section <Element>");
}

TEST_F(WasmModuleVerifyTest, DataCountSectionAfterStartBeforeElement) {
  static_assert(kStartSectionCode + 1 == kElementSectionCode);
  static const uint8_t data[] = {
      // We need the start section for this test, but the start section must
      // reference a valid function, which requires the type and function
      // sections too.
      TYPE_SECTION(1, SIG_ENTRY_v_v),      // Type section.
      FUNCTION_SECTION(1, 0),              // Function section.
      SECTION(Start, U32V_1(0)),           // Start section.
      SECTION(DataCount, ENTRY_COUNT(0)),  // DataCount section.
      SECTION(Element, ENTRY_COUNT(0))     // Element section.
  };
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "unexpected section <Element>");
}

TEST_F(WasmModuleVerifyTest, MultipleDataCountSections) {
  static const uint8_t data[] = {SECTION(DataCount, ENTRY_COUNT(0)),
                                 SECTION(DataCount, ENTRY_COUNT(0))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "Multiple DataCount sections not allowed");
}

TEST_F(WasmModuleVerifyTest, DataCountSegmentCountMatch) {
  static const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kNoMaximum, 1),  // Memory section.
      SECTION(DataCount, ENTRY_COUNT(1)),              // DataCount section.
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,  // Data section.
              WASM_INIT_EXPR_I32V_1(12), ADD_COUNT('h', 'i'))};

  EXPECT_VERIFIES(data);
}

TEST_F(WasmModuleVerifyTest, DataCountSegmentCount_greater) {
  static const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kNoMaximum, 1),  // Memory section.
      SECTION(DataCount, ENTRY_COUNT(3)),              // DataCount section.
      SECTION(Data, ENTRY_COUNT(0))};                  // Data section.
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "data segments count 0 mismatch (3 expected)");
}

TEST_F(WasmModuleVerifyTest, DataCountSegmentCount_less) {
  static const uint8_t data[] = {
      SECTION(Memory, ENTRY_COUNT(1), kNoMaximum, 1),  // Memory section.
      SECTION(DataCount, ENTRY_COUNT(0)),              // DataCount section.
      SECTION(Data, ENTRY_COUNT(1), LINEAR_MEMORY_INDEX_0,  // Data section.
              WASM_INIT_EXPR_I32V_1(12), ADD_COUNT('a', 'b', 'c'))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "data segments count 1 mismatch (0 expected)");
}

TEST_F(WasmModuleVerifyTest, DataCountSegmentCount_omitted) {
  static const uint8_t data[] = {SECTION(Memory, ENTRY_COUNT(1), kNoMaximum, 1),
                                 SECTION(DataCount, ENTRY_COUNT(1))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "data segments count 0 mismatch (1 expected)");
}

TEST_F(WasmModuleVerifyTest, GcStructIdsPass) {
  static const uint8_t data[] = {SECTION(
      Type, ENTRY_COUNT(1),                         // One recursive group...
      kWasmRecursiveTypeGroupCode, ENTRY_COUNT(3),  // with three entries.
      WASM_STRUCT_DEF(FIELD_COUNT(3), STRUCT_FIELD(kI32Code, true),
                      STRUCT_FIELD(WASM_OPT_REF(0), true),
                      STRUCT_FIELD(WASM_OPT_REF(1), true)),
      WASM_STRUCT_DEF(FIELD_COUNT(2), STRUCT_FIELD(WASM_OPT_REF(0), true),
                      STRUCT_FIELD(WASM_OPT_REF(2), true)),
      WASM_ARRAY_DEF(WASM_OPT_REF(0), true))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, OutOfBoundsTypeInGlobal) {
  static const uint8_t data[] = {
      SECTION(Global, ENTRY_COUNT(1), kRefCode, 0, WASM_REF_NULL(0), kExprEnd)};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "Type index 0 is out of bounds");
}

TEST_F(WasmModuleVerifyTest, OutOfBoundsTypeInType) {
  static const uint8_t data[] = {SECTION(
      Type, ENTRY_COUNT(1),
      WASM_STRUCT_DEF(FIELD_COUNT(1),
                      STRUCT_FIELD(WASM_REF_TYPE(ValueType::Ref(1)), true)))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "Type index 1 is out of bounds");
}

TEST_F(WasmModuleVerifyTest, RecursiveTypeOutsideRecursiveGroup) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1),
              WASM_STRUCT_DEF(
                  FIELD_COUNT(1),
                  STRUCT_FIELD(WASM_REF_TYPE(ValueType::RefNull(0)), true)))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_OK(result);
}

TEST_F(WasmModuleVerifyTest, OutOfBoundsSupertype) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),
              kWasmSubtypeCode, ENTRY_COUNT(1), 1,
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "type 0: supertype 1 out of bounds");
}

TEST_F(WasmModuleVerifyTest, ForwardSupertypeSameType) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), kWasmRecursiveTypeGroupCode, ENTRY_COUNT(1),
              kWasmSubtypeCode, ENTRY_COUNT(1), 0,
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "type 0: forward-declared supertype 0");
}

TEST_F(WasmModuleVerifyTest, ForwardSupertypeSameRecGroup) {
  static const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1), kWasmRecursiveTypeGroupCode, ENTRY_COUNT(2),
              kWasmSubtypeCode, ENTRY_COUNT(1), 0,
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)),
              WASM_STRUCT_DEF(FIELD_COUNT(1), STRUCT_FIELD(kI32Code, true)))};
  ModuleResult result = DecodeModule(base::ArrayVector(data));
  EXPECT_NOT_OK(result, "type 0: forward-declared supertype 0");
}

TEST_F(WasmModuleVerifyTest, IllegalPackedFields) {
  static const uint8_t data[] = {
      SECTION(Global, ENTRY_COUNT(1), kI16Code, 0, WASM_INIT_EXPR_I32V_1(13))};

  ModuleResult result = DecodeModule(base::ArrayVector(data));

  EXPECT_NOT_OK(result, "invalid value type");
}

TEST_F(WasmModuleVerifyTest, Memory64DataSegment) {
  WASM_FEATURE_SCOPE(memory64);
  for (bool enable_memory64 : {false, true}) {
    for (bool use_memory64 : {false, true}) {
      uint8_t const_opcode = use_memory64 ? kExprI64Const : kExprI32Const;
      const uint8_t data[] = {
          SECTION(Memory, ENTRY_COUNT(1),
                  enable_memory64 ? kMemory64WithMaximum : kWithMaximum, 28,
                  28),
          SECTION(Data, ENTRY_COUNT(1), ACTIVE_NO_INDEX,  // -
                  const_opcode, 0, kExprEnd,              // dest addr
                  U32V_1(3),                              // source size
                  'a', 'b', 'c')                          // data bytes
      };

      if (enable_memory64 == use_memory64) {
        EXPECT_VERIFIES(data);
      } else if (enable_memory64) {
        EXPECT_FAILURE_WITH_MSG(data, "expected i64, got i32");
      } else {
        EXPECT_FAILURE_WITH_MSG(data, "expected i32, got i64");
      }
    }
  }
}

TEST_F(WasmModuleVerifyTest, InvalidSharedType) {
  // Fails if the feature is not enabled.
  const uint8_t data[] = {
      SECTION(Type, ENTRY_COUNT(1),
              WASM_STRUCT_DEF(FIELD_COUNT(1), kRefNullCode, kSharedFlagCode,
                              kAnyRefCode))};

  ModuleResult result = DecodeModule(base::ArrayVector(data));

  EXPECT_NOT_OK(
      result, "invalid heap type 0x65, enable with --experimental-wasm-shared");
}

TEST_F(WasmModuleVerifyTest, InvalidSharedGlobal) {
  // Fails if the feature is not enabled.
  const uint8_t data[] = {
      SECTION(Global, ENTRY_COUNT(1), kI32Code, 0b11, kExprI32Const, 0)};

  ModuleResult result = DecodeModule(base::ArrayVector(data));

  EXPECT_NOT_OK(
      result,
      "invalid global flags 0x3 (enable via --experimental-wasm-shared)");
}

#undef EXPECT_INIT_EXPR
#undef EXPECT_INIT_EXPR_FAIL
#undef WASM_INIT_EXPR_I32V_1
#undef WASM_INIT_EXPR_I32V_2
#undef WASM_INIT_EXPR_I32V_3
#undef WASM_INIT_EXPR_I32V_4
#undef WASM_INIT_EXPR_I32V_5
#undef WASM_INIT_EXPR_F32
#undef WASM_INIT_EXPR_I64
#undef WASM_INIT_EXPR_F64
#undef WASM_INIT_EXPR_EXTERN_REF_NULL
#undef WASM_INIT_EXPR_FUNC_REF_NULL
#undef WASM_INIT_EXPR_REF_FUNC
#undef WASM_INIT_EXPR_GLOBAL
#undef REF_NULL_ELEMENT
#undef REF_FUNC_ELEMENT
#undef EMPTY_BODY
#undef NOP_BODY
#undef SIG_ENTRY_i_i
#undef UNKNOWN_SECTION
#undef ADD_COUNT
#undef SECTION
#undef TYPE_SECTION
#undef FUNCTION_SECTION
#undef FOO_STRING
#undef NO_LOCAL_NAMES
#undef EMPTY_TYPE_SECTION
#undef EMPTY_FUNCTION_SECTION
#undef EMPTY_FUNCTION_BODIES_SECTION
#undef SECTION_NAMES
#undef EMPTY_NAMES_SECTION
#undef SECTION_SRC_MAP
#undef SECTION_COMPILATION_HINTS
#undef X1
#undef X2
#undef X3
#undef X4
#undef ONE_EMPTY_FUNCTION
#undef TWO_EMPTY_FUNCTIONS
#undef THREE_EMPTY_FUNCTIONS
#undef FOUR_EMPTY_FUNCTIONS
#undef ONE_EMPTY_BODY
#undef TWO_EMPTY_BODIES
#undef THREE_EMPTY_BODIES
#undef FOUR_EMPTY_BODIES
#undef TYPE_SECTION_ONE_SIG_VOID_VOID
#undef LINEAR_MEMORY_INDEX_0

#undef FIELD_COUNT
#undef STRUCT_FIELD
#undef WASM_REF
#undef WASM_OPT_REF
#undef WASM_STRUCT_DEF
#undef WASM_ARRAY_DEF
#undef WASM_FUNCTION_DEF
#undef EXCEPTION_ENTRY
#undef EXPECT_VERIFIES
#undef EXPECT_FAILURE_LEN
#undef EXPECT_FAILURE
#undef EXPECT_OFF_END_FAILURE
#undef EXPECT_OK
#undef EXPECT_NOT_OK

}  // namespace module_decoder_unittest
}  // namespace wasm
}  // namespace internal
}  // namespace v8
