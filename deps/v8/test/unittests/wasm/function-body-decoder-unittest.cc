// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/function-body-decoder.h"

#include "src/flags/flags.h"
#include "src/utils/ostreams.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/leb-helper.h"
#include "src/wasm/local-decl-encoder.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

namespace v8::internal::wasm {

#define B1(a) WASM_BLOCK(a)
#define B2(a, b) WASM_BLOCK(a, b)
#define B3(a, b, c) WASM_BLOCK(a, b, c)

#define WASM_IF_OP kExprIf, kVoidCode
#define WASM_LOOP_OP kExprLoop, kVoidCode

#define EXPECT_OK(result)                                        \
  do {                                                           \
    if (!result.ok()) {                                          \
      GTEST_NONFATAL_FAILURE_(result.error().message().c_str()); \
      return;                                                    \
    }                                                            \
  } while (false)

static const uint8_t kCodeGetLocal0[] = {kExprLocalGet, 0};
static const uint8_t kCodeGetLocal1[] = {kExprLocalGet, 1};
static const uint8_t kCodeSetLocal0[] = {WASM_LOCAL_SET(0, WASM_ZERO)};
static const uint8_t kCodeTeeLocal0[] = {WASM_LOCAL_TEE(0, WASM_ZERO)};

static const ValueType kValueTypes[] = {kWasmI32, kWasmI64, kWasmF32, kWasmF64,
                                        kWasmExternRef};
static const MachineType machineTypes[] = {
    MachineType::Int8(),   MachineType::Uint8(),  MachineType::Int16(),
    MachineType::Uint16(), MachineType::Int32(),  MachineType::Uint32(),
    MachineType::Int64(),  MachineType::Uint64(), MachineType::Float32(),
    MachineType::Float64()};

static const WasmOpcode kInt32BinopOpcodes[] = {
    kExprI32Add,  kExprI32Sub,  kExprI32Mul,  kExprI32DivS, kExprI32DivU,
    kExprI32RemS, kExprI32RemU, kExprI32And,  kExprI32Ior,  kExprI32Xor,
    kExprI32Shl,  kExprI32ShrU, kExprI32ShrS, kExprI32Eq,   kExprI32LtS,
    kExprI32LeS,  kExprI32LtU,  kExprI32LeU};

#define WASM_BRV_IF_ZERO(depth, val) \
  val, WASM_ZERO, kExprBrIf, static_cast<uint8_t>(depth)

constexpr size_t kMaxByteSizedLeb128 = 127;

using F = std::pair<ValueType, bool>;

// Used to construct fixed-size signatures: MakeSig::Returns(...).Params(...);
using MakeSig = FixedSizeSignature<ValueType>;

enum MemoryType { kMemory32, kMemory64 };

enum TableType { kTable32, kTable64 };

// A helper for tests that require a module environment for functions,
// globals, or memories.
class TestModuleBuilder {
 public:
  explicit TestModuleBuilder(ModuleOrigin origin = kWasmOrigin) : mod(origin) {
    mod.num_declared_functions = 1;
    mod.validated_functions = std::make_unique<std::atomic<uint8_t>[]>(1);
    // Asm.js functions are valid by design.
    if (is_asmjs_module(&mod)) mod.validated_functions[0] = 0xff;
  }
  uint8_t AddGlobal(ValueType type, bool mutability = true) {
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    mod.globals.push_back({type, mutability, {}, {0}, kIsShared, false, false});
    CHECK_LE(mod.globals.size(), kMaxByteSizedLeb128);
    return static_cast<uint8_t>(mod.globals.size() - 1);
  }
  uint8_t AddSignature(const FunctionSig* sig,
                       uint32_t supertype = kNoSuperType) {
    const bool is_final = true;
    const bool is_shared = false;
    mod.AddSignatureForTesting(sig, supertype, is_final, is_shared);
    CHECK_LE(mod.types.size(), kMaxByteSizedLeb128);
    GetTypeCanonicalizer()->AddRecursiveSingletonGroup(module());
    return static_cast<uint8_t>(mod.types.size() - 1);
  }
  uint8_t AddFunction(const FunctionSig* sig, bool declared = true) {
    uint8_t sig_index = AddSignature(sig);
    return AddFunctionImpl(sig, sig_index, declared);
  }
  uint8_t AddFunction(uint32_t sig_index, bool declared = true) {
    DCHECK(mod.has_signature(sig_index));
    return AddFunctionImpl(mod.types[sig_index].function_sig, sig_index,
                           declared);
  }
  uint8_t AddImport(const FunctionSig* sig) {
    uint8_t result = AddFunction(sig);
    mod.functions[result].imported = true;
    return result;
  }
  uint8_t AddException(WasmTagSig* sig) {
    mod.tags.emplace_back(sig, AddSignature(sig));
    CHECK_LE(mod.types.size(), kMaxByteSizedLeb128);
    return static_cast<uint8_t>(mod.tags.size() - 1);
  }

  uint8_t AddTable(ValueType type, uint32_t initial_size, bool has_maximum_size,
                   uint32_t maximum_size, TableType table_type = kTable32) {
    CHECK(type.is_object_reference());
    mod.tables.emplace_back();
    WasmTable& table = mod.tables.back();
    table.type = type;
    table.initial_size = initial_size;
    table.has_maximum_size = has_maximum_size;
    table.maximum_size = maximum_size;
    table.is_table64 = table_type == kTable64;
    return static_cast<uint8_t>(mod.tables.size() - 1);
  }

  uint8_t AddStruct(std::initializer_list<F> fields,
                    uint32_t supertype = kNoSuperType) {
    StructType::Builder type_builder(&mod.signature_zone,
                                     static_cast<uint32_t>(fields.size()));
    for (F field : fields) {
      type_builder.AddField(field.first, field.second);
    }
    const bool is_final = true;
    const bool is_shared = false;
    mod.AddStructTypeForTesting(type_builder.Build(), supertype, is_final,
                                is_shared);
    GetTypeCanonicalizer()->AddRecursiveSingletonGroup(module());
    return static_cast<uint8_t>(mod.types.size() - 1);
  }

  uint8_t AddArray(ValueType type, bool mutability) {
    ArrayType* array = mod.signature_zone.New<ArrayType>(type, mutability);
    const bool is_final = true;
    const bool is_shared = false;
    mod.AddArrayTypeForTesting(array, kNoSuperType, is_final, is_shared);
    GetTypeCanonicalizer()->AddRecursiveSingletonGroup(module());
    return static_cast<uint8_t>(mod.types.size() - 1);
  }

  uint8_t AddMemory(MemoryType mem_indextype = kMemory32) {
    mod.memories.push_back(WasmMemory{
        .initial_pages = 1,
        .maximum_pages = 100,
        .is_memory64 = mem_indextype == kMemory64,
    });
    CHECK_GE(kMaxUInt8, mod.memories.size());
    return static_cast<uint8_t>(mod.memories.size() - 1);
  }

  uint8_t AddTable(wasm::ValueType type, TableType table_indextype = kTable32) {
    mod.tables.push_back(WasmTable{
        .type = type,
        .is_table64 = table_indextype == kTable64,
    });
    CHECK_GE(kMaxUInt8, mod.tables.size());
    return static_cast<uint8_t>(mod.tables.size() - 1);
  }

  uint8_t AddPassiveElementSegment(wasm::ValueType type) {
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    mod.elem_segments.emplace_back(WasmElemSegment::kStatusPassive, kIsShared,
                                   type, WasmElemSegment::kExpressionElements,
                                   0, 0);
    return static_cast<uint8_t>(mod.elem_segments.size() - 1);
  }

  uint8_t AddDeclarativeElementSegment() {
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    mod.elem_segments.emplace_back(WasmElemSegment::kStatusDeclarative,
                                   kIsShared, kWasmFuncRef,
                                   WasmElemSegment::kExpressionElements, 0, 0);
    return static_cast<uint8_t>(mod.elem_segments.size() - 1);
  }

  // Set the number of data segments as declared by the DataCount section.
  void SetDataSegmentCount(uint32_t data_segment_count) {
    // The Data section occurs after the Code section, so we don't need to
    // update mod.data_segments, as it is always empty.
    mod.num_declared_data_segments = data_segment_count;
  }

  WasmModule* module() { return &mod; }

 private:
  uint8_t AddFunctionImpl(const FunctionSig* sig, uint32_t sig_index,
                          bool declared) {
    mod.functions.push_back(
        {sig,                                          // sig
         static_cast<uint32_t>(mod.functions.size()),  // func_index
         sig_index,                                    // sig_index
         {0, 0},                                       // code
         false,                                        // import
         false,                                        // export
         declared});                                   // declared
    CHECK_LE(mod.functions.size(), kMaxByteSizedLeb128);
    return static_cast<uint8_t>(mod.functions.size() - 1);
  }

  WasmModule mod;
};

template <class BaseTest>
class FunctionBodyDecoderTestBase : public WithZoneMixin<BaseTest> {
 public:
  using LocalsDecl = std::pair<uint32_t, ValueType>;
  // All features are disabled by default and must be activated with
  // a WASM_FEATURE_SCOPE in individual tests.
  WasmEnabledFeatures enabled_features_ = WasmEnabledFeatures::None();

  TestSignatures sigs;
  TestModuleBuilder builder;
  WasmModule* module = builder.module();
  LocalDeclEncoder local_decls{this->zone()};

  void AddLocals(ValueType type, uint32_t count) {
    local_decls.AddLocals(count, type);
  }

  enum AppendEnd : bool { kAppendEnd, kOmitEnd };

  base::Vector<const uint8_t> PrepareBytecode(base::Vector<const uint8_t> code,
                                              AppendEnd append_end) {
    size_t locals_size = local_decls.Size();
    size_t total_size =
        code.size() + locals_size + (append_end == kAppendEnd ? 1 : 0);
    uint8_t* buffer = this->zone()->template AllocateArray<uint8_t>(total_size);
    // Prepend the local decls to the code.
    local_decls.Emit(buffer);
    // Emit the code.
    if (code.size() > 0) {
      memcpy(buffer + locals_size, code.begin(), code.size());
    }
    if (append_end == kAppendEnd) {
      // Append an extra end opcode.
      buffer[total_size - 1] = kExprEnd;
    }

    return {buffer, total_size};
  }

  template <size_t N>
  base::Vector<const uint8_t> CodeToVector(const uint8_t (&code)[N]) {
    return base::ArrayVector(code);
  }

  base::Vector<const uint8_t> CodeToVector(
      const std::initializer_list<const uint8_t>& code) {
    return base::VectorOf(&*code.begin(), code.size());
  }

  base::Vector<const uint8_t> CodeToVector(base::Vector<const uint8_t> vec) {
    return vec;
  }

  // Prepends local variable declarations and renders nice error messages for
  // verification failures.
  template <typename Code = std::initializer_list<const uint8_t>>
  void Validate(bool expected_success, const FunctionSig* sig, Code&& raw_code,
                AppendEnd append_end = kAppendEnd,
                const char* message = nullptr) {
    base::Vector<const uint8_t> code =
        PrepareBytecode(CodeToVector(std::forward<Code>(raw_code)), append_end);

    // Validate the code.
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    FunctionBody body(sig, 0, code.begin(), code.end(), kIsShared);
    WasmDetectedFeatures unused_detected_features;
    DecodeResult result =
        ValidateFunctionBody(this->zone(), enabled_features_, module,
                             &unused_detected_features, body);

    std::ostringstream str;
    if (result.failed()) {
      str << "Verification failed: pc = +" << result.error().offset()
          << ", msg = " << result.error().message();
    } else {
      str << "Verification succeeded, expected failure";
    }
    EXPECT_EQ(result.ok(), expected_success) << str.str();
    if (result.failed() && message) {
      EXPECT_THAT(result.error().message(), ::testing::HasSubstr(message));
    }
  }

  template <typename Code = std::initializer_list<const uint8_t>>
  void ExpectValidates(const FunctionSig* sig, Code&& raw_code,
                       AppendEnd append_end = kAppendEnd,
                       const char* message = nullptr) {
    Validate(true, sig, std::forward<Code>(raw_code), append_end, message);
  }

  template <typename Code = std::initializer_list<const uint8_t>>
  void ExpectFailure(const FunctionSig* sig, Code&& raw_code,
                     AppendEnd append_end = kAppendEnd,
                     const char* message = nullptr) {
    Validate(false, sig, std::forward<Code>(raw_code), append_end, message);
  }

  void TestBinop(WasmOpcode opcode, const FunctionSig* success) {
    // op(local[0], local[1])
    uint8_t code[] = {WASM_BINOP(opcode, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))};
    ExpectValidates(success, code);

    // Try all combinations of return and parameter types.
    for (size_t i = 0; i < arraysize(kValueTypes); i++) {
      for (size_t j = 0; j < arraysize(kValueTypes); j++) {
        for (size_t k = 0; k < arraysize(kValueTypes); k++) {
          ValueType types[] = {kValueTypes[i], kValueTypes[j], kValueTypes[k]};
          if (types[0] != success->GetReturn(0) ||
              types[1] != success->GetParam(0) ||
              types[2] != success->GetParam(1)) {
            // Test signature mismatch.
            FunctionSig sig(1, 2, types);
            ExpectFailure(&sig, code);
          }
        }
      }
    }
  }

  void TestUnop(WasmOpcode opcode, const FunctionSig* success) {
    TestUnop(opcode, success->GetReturn(), success->GetParam(0));
  }

  void TestUnop(WasmOpcode opcode, ValueType ret_type, ValueType param_type) {
    // Return(op(local[0]))
    uint8_t code[] = {WASM_UNOP(opcode, WASM_LOCAL_GET(0))};
    {
      ValueType types[] = {ret_type, param_type};
      FunctionSig sig(1, 1, types);
      ExpectValidates(&sig, code);
    }

    // Try all combinations of return and parameter types.
    for (size_t i = 0; i < arraysize(kValueTypes); i++) {
      for (size_t j = 0; j < arraysize(kValueTypes); j++) {
        ValueType types[] = {kValueTypes[i], kValueTypes[j]};
        if (types[0] != ret_type || types[1] != param_type) {
          // Test signature mismatch.
          FunctionSig sig(1, 1, types);
          ExpectFailure(&sig, code);
        }
      }
    }
  }
};

using FunctionBodyDecoderTest = FunctionBodyDecoderTestBase<TestWithPlatform>;

TEST_F(FunctionBodyDecoderTest, Int32Const1) {
  uint8_t code[] = {kExprI32Const, 0};
  for (int i = -64; i <= 63; i++) {
    code[1] = static_cast<uint8_t>(i & 0x7F);
    ExpectValidates(sigs.i_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, RefFunc) {
  builder.AddFunction(sigs.v_ii());
  builder.AddFunction(sigs.ii_v());
  ExpectValidates(sigs.c_v(), {kExprRefFunc, 1});
}

TEST_F(FunctionBodyDecoderTest, EmptyFunction) {
  ExpectValidates(sigs.v_v(), {});
  ExpectFailure(sigs.i_i(), {});
}

TEST_F(FunctionBodyDecoderTest, IncompleteIf1) {
  uint8_t code[] = {kExprIf};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, Int32Const_fallthru) {
  ExpectValidates(sigs.i_i(), {WASM_I32V_1(0)});
}

TEST_F(FunctionBodyDecoderTest, Int32Const_fallthru2) {
  ExpectFailure(sigs.i_i(), {WASM_I32V_1(0), WASM_I32V_1(1)});
}

TEST_F(FunctionBodyDecoderTest, Int32Const) {
  const int kInc = 4498211;
  for (int32_t i = kMinInt; i < kMaxInt - kInc; i = i + kInc) {
    // TODO(binji): expand test for other sized int32s; 1 through 5 bytes.
    ExpectValidates(sigs.i_i(), {WASM_I32V(i)});
  }
}

TEST_F(FunctionBodyDecoderTest, Int64Const) {
  const int kInc = 4498211;
  for (int32_t i = kMinInt; i < kMaxInt - kInc; i = i + kInc) {
    ExpectValidates(sigs.l_l(),
                    {WASM_I64V((static_cast<uint64_t>(i) << 32) | i)});
  }
}

TEST_F(FunctionBodyDecoderTest, Float32Const) {
  uint8_t code[] = {kExprF32Const, 0, 0, 0, 0};
  Address ptr = reinterpret_cast<Address>(code + 1);
  for (int i = 0; i < 30; i++) {
    base::WriteLittleEndianValue<float>(ptr, i * -7.75f);
    ExpectValidates(sigs.f_ff(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, Float64Const) {
  uint8_t code[] = {kExprF64Const, 0, 0, 0, 0, 0, 0, 0, 0};
  Address ptr = reinterpret_cast<Address>(code + 1);
  for (int i = 0; i < 30; i++) {
    base::WriteLittleEndianValue<double>(ptr, i * 33.45);
    ExpectValidates(sigs.d_dd(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, Int32Const_off_end) {
  uint8_t code[] = {kExprI32Const, 0xAA, 0xBB, 0xCC, 0x44};

  for (size_t size = 1; size <= 4; ++size) {
    ExpectFailure(sigs.i_i(), base::VectorOf(code, size), kAppendEnd);
    // Should also fail without the trailing 'end' opcode.
    ExpectFailure(sigs.i_i(), base::VectorOf(code, size), kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, GetLocal0_param) {
  ExpectValidates(sigs.i_i(), kCodeGetLocal0);
}

TEST_F(FunctionBodyDecoderTest, GetLocal0_local) {
  AddLocals(kWasmI32, 1);
  ExpectValidates(sigs.i_v(), kCodeGetLocal0);
}

TEST_F(FunctionBodyDecoderTest, TooManyLocals) {
  AddLocals(kWasmI32, 4034986500);
  ExpectFailure(sigs.i_v(), kCodeGetLocal0);
}

TEST_F(FunctionBodyDecoderTest, GetLocal0_param_n) {
  for (const FunctionSig* sig : {sigs.i_i(), sigs.i_ii(), sigs.i_iii()}) {
    ExpectValidates(sig, kCodeGetLocal0);
  }
}

TEST_F(FunctionBodyDecoderTest, GetLocalN_local) {
  for (uint8_t i = 1; i < 8; i++) {
    AddLocals(kWasmI32, 1);
    for (uint8_t j = 0; j < i; j++) {
      ExpectValidates(sigs.i_v(), {kExprLocalGet, j});
    }
  }
}

TEST_F(FunctionBodyDecoderTest, GetLocal0_fail_no_params) {
  ExpectFailure(sigs.i_v(), kCodeGetLocal0);
}

TEST_F(FunctionBodyDecoderTest, GetLocal1_fail_no_locals) {
  ExpectFailure(sigs.i_i(), kCodeGetLocal1);
}

TEST_F(FunctionBodyDecoderTest, GetLocal_off_end) {
  ExpectFailure(sigs.i_i(), {kExprLocalGet});
}

TEST_F(FunctionBodyDecoderTest, NumLocalBelowLimit) {
  AddLocals(kWasmI32, kV8MaxWasmFunctionLocals - 1);
  ExpectValidates(sigs.v_v(), {WASM_NOP});
}

TEST_F(FunctionBodyDecoderTest, NumLocalAtLimit) {
  AddLocals(kWasmI32, kV8MaxWasmFunctionLocals);
  ExpectValidates(sigs.v_v(), {WASM_NOP});
}

TEST_F(FunctionBodyDecoderTest, NumLocalAboveLimit) {
  AddLocals(kWasmI32, kV8MaxWasmFunctionLocals + 1);
  ExpectFailure(sigs.v_v(), {WASM_NOP});
}

TEST_F(FunctionBodyDecoderTest, GetLocal_varint) {
  const int kMaxLocals = kV8MaxWasmFunctionLocals - 1;
  AddLocals(kWasmI32, kMaxLocals);

  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_1(66)});
  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_2(7777)});
  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_3(8888)});
  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_4(9999)});

  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_5(kMaxLocals - 1)});

  ExpectFailure(sigs.i_i(), {kExprLocalGet, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_4(kMaxLocals - 1)});
  ExpectValidates(sigs.i_i(), {kExprLocalGet, U32V_4(kMaxLocals)});
  ExpectFailure(sigs.i_i(), {kExprLocalGet, U32V_4(kMaxLocals + 1)});

  ExpectFailure(sigs.i_v(), {kExprLocalGet, U32V_4(kMaxLocals)});
  ExpectFailure(sigs.i_v(), {kExprLocalGet, U32V_4(kMaxLocals + 1)});
}

TEST_F(FunctionBodyDecoderTest, GetLocal_toomany) {
  AddLocals(kWasmI32, kV8MaxWasmFunctionLocals - 100);
  AddLocals(kWasmI32, 100);

  ExpectValidates(sigs.i_v(), {kExprLocalGet, U32V_1(66)});
  ExpectFailure(sigs.i_i(), {kExprLocalGet, U32V_1(66)});
}

TEST_F(FunctionBodyDecoderTest, Binops_off_end) {
  uint8_t code1[] = {0};  // [opcode]
  for (size_t i = 0; i < arraysize(kInt32BinopOpcodes); i++) {
    code1[0] = kInt32BinopOpcodes[i];
    ExpectFailure(sigs.i_i(), code1);
  }

  uint8_t code3[] = {kExprLocalGet, 0, 0};  // [expr] [opcode]
  for (size_t i = 0; i < arraysize(kInt32BinopOpcodes); i++) {
    code3[2] = kInt32BinopOpcodes[i];
    ExpectFailure(sigs.i_i(), code3);
  }

  uint8_t code4[] = {kExprLocalGet, 0, 0, 0};  // [expr] [opcode] [opcode]
  for (size_t i = 0; i < arraysize(kInt32BinopOpcodes); i++) {
    code4[2] = kInt32BinopOpcodes[i];
    code4[3] = kInt32BinopOpcodes[i];
    ExpectFailure(sigs.i_i(), code4);
  }
}

TEST_F(FunctionBodyDecoderTest, BinopsAcrossBlock1) {
  ExpectFailure(sigs.i_i(), {WASM_ZERO, kExprBlock, kI32Code, WASM_ZERO,
                             kExprI32Add, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, BinopsAcrossBlock2) {
  ExpectFailure(sigs.i_i(), {WASM_ZERO, WASM_ZERO, kExprBlock, kI32Code,
                             kExprI32Add, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, BinopsAcrossBlock3) {
  ExpectFailure(sigs.i_i(), {WASM_ZERO, WASM_ZERO, kExprIf, kI32Code,
                             kExprI32Add, kExprElse, kExprI32Add, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, Nop) {
  ExpectValidates(sigs.v_v(), {kExprNop});
}

TEST_F(FunctionBodyDecoderTest, SetLocal0_void) {
  ExpectFailure(sigs.i_i(), {WASM_LOCAL_SET(0, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, SetLocal0_param) {
  ExpectFailure(sigs.i_i(), kCodeSetLocal0);
  ExpectFailure(sigs.f_ff(), kCodeSetLocal0);
  ExpectFailure(sigs.d_dd(), kCodeSetLocal0);
}

TEST_F(FunctionBodyDecoderTest, TeeLocal0_param) {
  ExpectValidates(sigs.i_i(), kCodeTeeLocal0);
  ExpectFailure(sigs.f_ff(), kCodeTeeLocal0);
  ExpectFailure(sigs.d_dd(), kCodeTeeLocal0);
}

TEST_F(FunctionBodyDecoderTest, SetLocal0_local) {
  ExpectFailure(sigs.i_v(), kCodeSetLocal0);
  ExpectFailure(sigs.v_v(), kCodeSetLocal0);
  AddLocals(kWasmI32, 1);
  ExpectFailure(sigs.i_v(), kCodeSetLocal0);
  ExpectValidates(sigs.v_v(), kCodeSetLocal0);
}

TEST_F(FunctionBodyDecoderTest, TeeLocal0_local) {
  ExpectFailure(sigs.i_v(), kCodeTeeLocal0);
  AddLocals(kWasmI32, 1);
  ExpectValidates(sigs.i_v(), kCodeTeeLocal0);
}

TEST_F(FunctionBodyDecoderTest, TeeLocalN_local) {
  for (uint8_t i = 1; i < 8; i++) {
    AddLocals(kWasmI32, 1);
    for (uint8_t j = 0; j < i; j++) {
      ExpectFailure(sigs.v_v(), {WASM_LOCAL_TEE(j, WASM_I32V_1(i))});
      ExpectValidates(sigs.i_i(), {WASM_LOCAL_TEE(j, WASM_I32V_1(i))});
    }
  }
}

TEST_F(FunctionBodyDecoderTest, BlockN) {
  constexpr size_t kMaxSize = 200;
  uint8_t buffer[kMaxSize + 3];

  for (size_t i = 0; i <= kMaxSize; i++) {
    memset(buffer, kExprNop, sizeof(buffer));
    buffer[0] = kExprBlock;
    buffer[1] = kVoidCode;
    buffer[i + 2] = kExprEnd;
    ExpectValidates(sigs.v_i(), base::VectorOf(buffer, i + 3), kAppendEnd);
  }
}

#define WASM_EMPTY_BLOCK kExprBlock, kVoidCode, kExprEnd

TEST_F(FunctionBodyDecoderTest, Block0) {
  ExpectValidates(sigs.v_v(), {WASM_EMPTY_BLOCK});
  ExpectFailure(sigs.i_i(), {WASM_EMPTY_BLOCK});
}

TEST_F(FunctionBodyDecoderTest, Block0_fallthru1) {
  ExpectValidates(sigs.v_v(), {WASM_BLOCK(WASM_EMPTY_BLOCK)});
  ExpectFailure(sigs.i_i(), {WASM_BLOCK(WASM_EMPTY_BLOCK)});
}

TEST_F(FunctionBodyDecoderTest, Block0Block0) {
  ExpectValidates(sigs.v_v(), {WASM_EMPTY_BLOCK, WASM_EMPTY_BLOCK});
  ExpectFailure(sigs.i_i(), {WASM_EMPTY_BLOCK, WASM_EMPTY_BLOCK});
}

TEST_F(FunctionBodyDecoderTest, Block0_end) {
  ExpectFailure(sigs.v_v(), {WASM_EMPTY_BLOCK, kExprEnd});
}

#undef WASM_EMPTY_BLOCK

TEST_F(FunctionBodyDecoderTest, Block1) {
  uint8_t code[] = {WASM_BLOCK_I(WASM_LOCAL_GET(0))};
  ExpectValidates(sigs.i_i(), code);
  ExpectFailure(sigs.v_i(), code);
  ExpectFailure(sigs.d_dd(), code);
  ExpectFailure(sigs.i_f(), code);
  ExpectFailure(sigs.i_d(), code);
}

TEST_F(FunctionBodyDecoderTest, Block1_i) {
  uint8_t code[] = {WASM_BLOCK_I(WASM_ZERO)};
  ExpectValidates(sigs.i_i(), code);
  ExpectFailure(sigs.f_ff(), code);
  ExpectFailure(sigs.d_dd(), code);
  ExpectFailure(sigs.l_ll(), code);
}

TEST_F(FunctionBodyDecoderTest, Block1_f) {
  uint8_t code[] = {WASM_BLOCK_F(WASM_F32(0))};
  ExpectFailure(sigs.i_i(), code);
  ExpectValidates(sigs.f_ff(), code);
  ExpectFailure(sigs.d_dd(), code);
  ExpectFailure(sigs.l_ll(), code);
}

TEST_F(FunctionBodyDecoderTest, Block1_continue) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_BR(0))});
}

TEST_F(FunctionBodyDecoderTest, Block1_br) {
  ExpectValidates(sigs.v_v(), {B1(WASM_BR(0))});
  ExpectValidates(sigs.v_v(), {B1(WASM_BR(1))});
  ExpectFailure(sigs.v_v(), {B1(WASM_BR(2))});
}

TEST_F(FunctionBodyDecoderTest, Block2_br) {
  ExpectValidates(sigs.v_v(), {B2(WASM_NOP, WASM_BR(0))});
  ExpectValidates(sigs.v_v(), {B2(WASM_BR(0), WASM_NOP)});
  ExpectValidates(sigs.v_v(), {B2(WASM_BR(0), WASM_BR(0))});
}

TEST_F(FunctionBodyDecoderTest, Block2) {
  ExpectFailure(sigs.i_i(), {WASM_BLOCK(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_i(), {WASM_BLOCK_I(WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.i_i(), {WASM_BLOCK_I(WASM_NOP, WASM_ZERO)});
  ExpectValidates(sigs.i_i(), {WASM_BLOCK_I(WASM_ZERO, WASM_NOP)});
  ExpectFailure(sigs.i_i(), {WASM_BLOCK_I(WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Block2b) {
  uint8_t code[] = {WASM_BLOCK_I(WASM_LOCAL_SET(0, WASM_ZERO), WASM_ZERO)};
  ExpectValidates(sigs.i_i(), code);
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.f_ff(), code);
}

TEST_F(FunctionBodyDecoderTest, Block2_fallthru) {
  ExpectValidates(sigs.i_i(), {B2(WASM_LOCAL_SET(0, WASM_ZERO),
                                  WASM_LOCAL_SET(0, WASM_ZERO)),
                               WASM_I32V_1(23)});
}

TEST_F(FunctionBodyDecoderTest, Block3) {
  ExpectValidates(sigs.i_i(), {WASM_BLOCK_I(WASM_LOCAL_SET(0, WASM_ZERO),
                                            WASM_LOCAL_SET(0, WASM_ZERO),
                                            WASM_I32V_1(11))});
}

TEST_F(FunctionBodyDecoderTest, Block5) {
  ExpectFailure(sigs.v_i(), {WASM_BLOCK(WASM_ZERO)});

  ExpectFailure(sigs.v_i(), {WASM_BLOCK(WASM_ZERO, WASM_ZERO)});

  ExpectFailure(sigs.v_i(), {WASM_BLOCK(WASM_ZERO, WASM_ZERO, WASM_ZERO)});

  ExpectFailure(sigs.v_i(),
                {WASM_BLOCK(WASM_ZERO, WASM_ZERO, WASM_ZERO, WASM_ZERO)});

  ExpectFailure(sigs.v_i(), {WASM_BLOCK(WASM_ZERO, WASM_ZERO, WASM_ZERO,
                                        WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, BlockType) {
  ExpectValidates(sigs.i_i(), {WASM_BLOCK_I(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.l_l(), {WASM_BLOCK_L(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.f_f(), {WASM_BLOCK_F(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.d_d(), {WASM_BLOCK_D(WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, BlockType_fail) {
  ExpectFailure(sigs.i_i(), {WASM_BLOCK_L(WASM_I64V_1(0))}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.i_i(), {WASM_BLOCK_F(WASM_F32(0.0))}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.i_i(), {WASM_BLOCK_D(WASM_F64(1.1))}, kAppendEnd,
                "type error in fallthru[0]");

  ExpectFailure(sigs.l_l(), {WASM_BLOCK_I(WASM_ZERO)}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.l_l(), {WASM_BLOCK_F(WASM_F32(0.0))}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.l_l(), {WASM_BLOCK_D(WASM_F64(1.1))}, kAppendEnd,
                "type error in fallthru[0]");

  ExpectFailure(sigs.f_ff(), {WASM_BLOCK_I(WASM_ZERO)}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.f_ff(), {WASM_BLOCK_L(WASM_I64V_1(0))}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.f_ff(), {WASM_BLOCK_D(WASM_F64(1.1))}, kAppendEnd,
                "type error in fallthru[0]");

  ExpectFailure(sigs.d_dd(), {WASM_BLOCK_I(WASM_ZERO)}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.d_dd(), {WASM_BLOCK_L(WASM_I64V_1(0))}, kAppendEnd,
                "type error in fallthru[0]");
  ExpectFailure(sigs.d_dd(), {WASM_BLOCK_F(WASM_F32(0.0))}, kAppendEnd,
                "type error in fallthru[0]");
}

TEST_F(FunctionBodyDecoderTest, BlockF32) {
  static const uint8_t code[] = {WASM_BLOCK_F(kExprF32Const, 0, 0, 0, 0)};
  ExpectValidates(sigs.f_ff(), code);
  ExpectFailure(sigs.i_i(), code);
  ExpectFailure(sigs.d_dd(), code);
}

TEST_F(FunctionBodyDecoderTest, BlockN_off_end) {
  uint8_t code[] = {WASM_BLOCK(kExprNop, kExprNop, kExprNop, kExprNop)};
  ExpectValidates(sigs.v_v(), code);
  for (size_t i = 1; i < arraysize(code); i++) {
    ExpectFailure(sigs.v_v(), base::VectorOf(code, i), kAppendEnd);
    ExpectFailure(sigs.v_v(), base::VectorOf(code, i), kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, Block2_continue) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_NOP, WASM_BR(0))});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_NOP, WASM_BR(1))});
  ExpectFailure(sigs.v_v(), {WASM_LOOP(WASM_NOP, WASM_BR(2))});
}

TEST_F(FunctionBodyDecoderTest, Block3_continue) {
  ExpectValidates(sigs.v_v(), {B1(WASM_LOOP(WASM_NOP, WASM_BR(0)))});
  ExpectValidates(sigs.v_v(), {B1(WASM_LOOP(WASM_NOP, WASM_BR(1)))});
  ExpectValidates(sigs.v_v(), {B1(WASM_LOOP(WASM_NOP, WASM_BR(2)))});
  ExpectFailure(sigs.v_v(), {B1(WASM_LOOP(WASM_NOP, WASM_BR(3)))});
}

TEST_F(FunctionBodyDecoderTest, NestedBlock_return) {
  ExpectValidates(sigs.i_i(), {B1(B1(WASM_RETURN(WASM_ZERO))), WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, BlockBrBinop) {
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_AND(WASM_BLOCK_I(WASM_BRV(0, WASM_I32V_1(1))),
                                WASM_I32V_1(2))});
}

TEST_F(FunctionBodyDecoderTest, VoidBlockTypeVariants) {
  // Valid kVoidCode encoded in 2 bytes.
  ExpectValidates(sigs.v_v(), {kExprBlock, kVoidCode | 0x80, 0x7F, kExprEnd});
  // Invalid code, whose last 7 bits coincide with kVoidCode.
  ExpectFailure(sigs.v_v(), {kExprBlock, kVoidCode | 0x80, 0x45, kExprEnd},
                kAppendEnd, "invalid block type");
}

TEST_F(FunctionBodyDecoderTest, If_empty1) {
  ExpectValidates(sigs.v_v(), {WASM_ZERO, WASM_IF_OP, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, If_empty2) {
  ExpectValidates(sigs.v_v(), {WASM_ZERO, WASM_IF_OP, kExprElse, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, If_empty3) {
  ExpectValidates(sigs.v_v(),
                  {WASM_ZERO, WASM_IF_OP, WASM_NOP, kExprElse, kExprEnd});
  ExpectFailure(sigs.v_v(),
                {WASM_ZERO, WASM_IF_OP, WASM_ZERO, kExprElse, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, If_empty4) {
  ExpectValidates(sigs.v_v(),
                  {WASM_ZERO, WASM_IF_OP, kExprElse, WASM_NOP, kExprEnd});
  ExpectFailure(sigs.v_v(),
                {WASM_ZERO, WASM_IF_OP, kExprElse, WASM_ZERO, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, If_empty_stack) {
  uint8_t code[] = {kExprIf};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, If_incomplete1) {
  uint8_t code[] = {kExprI32Const, 0, kExprIf};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, If_incomplete2) {
  uint8_t code[] = {kExprI32Const, 0, kExprIf, kExprNop};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, If_else_else) {
  uint8_t code[] = {kExprI32Const, 0,         WASM_IF_OP,
                    kExprElse,     kExprElse, kExprEnd};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, IfEmpty) {
  ExpectValidates(sigs.v_i(), {kExprLocalGet, 0, WASM_IF_OP, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, IfSet) {
  ExpectValidates(sigs.v_i(),
                  {WASM_IF(WASM_LOCAL_GET(0), WASM_LOCAL_SET(0, WASM_ZERO))});
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_LOCAL_SET(0, WASM_ZERO),
                                WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, IfElseEmpty) {
  ExpectValidates(sigs.v_i(),
                  {WASM_LOCAL_GET(0), WASM_IF_OP, kExprElse, kExprEnd});
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, IfElseUnreachable1) {
  ExpectValidates(
      sigs.i_i(),
      {WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_UNREACHABLE, WASM_LOCAL_GET(0))});
  ExpectValidates(
      sigs.i_i(),
      {WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_UNREACHABLE)});
}

TEST_F(FunctionBodyDecoderTest, IfElseUnreachable2) {
  static const uint8_t code[] = {
      WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_UNREACHABLE, WASM_LOCAL_GET(0))};

  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType types[] = {kWasmI32, kValueTypes[i]};
    FunctionSig sig(1, 1, types);

    Validate(kValueTypes[i] == kWasmI32, &sig, code);
  }
}

TEST_F(FunctionBodyDecoderTest, OneArmedIfWithArity) {
  static const uint8_t code[] = {WASM_ZERO, kExprIf, kI32Code, WASM_ONE,
                                 kExprEnd};
  ExpectFailure(sigs.i_v(), code, kAppendEnd,
                "start-arity and end-arity of one-armed if must match");
}

TEST_F(FunctionBodyDecoderTest, IfBreak) {
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0), WASM_BR(0))});
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0), WASM_BR(1))});
  ExpectFailure(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0), WASM_BR(2))});
}

TEST_F(FunctionBodyDecoderTest, IfElseBreak) {
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_BR(0))});
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_BR(1))});
  ExpectFailure(sigs.v_i(),
                {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_BR(2))});
}

TEST_F(FunctionBodyDecoderTest, Block_else) {
  uint8_t code[] = {kExprI32Const, 0, kExprBlock, kExprElse, kExprEnd};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, IfNop) {
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0), WASM_NOP)});
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, If_end) {
  ExpectValidates(sigs.v_i(), {kExprLocalGet, 0, WASM_IF_OP, kExprEnd});
  ExpectFailure(sigs.v_i(), {kExprLocalGet, 0, WASM_IF_OP, kExprEnd, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, If_falloff1) {
  ExpectFailure(sigs.v_i(), {kExprLocalGet, 0, kExprIf});
  ExpectFailure(sigs.v_i(), {kExprLocalGet, 0, WASM_IF_OP});
  ExpectFailure(sigs.v_i(),
                {kExprLocalGet, 0, WASM_IF_OP, kExprNop, kExprElse});
}

TEST_F(FunctionBodyDecoderTest, IfElseNop) {
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_LOCAL_SET(0, WASM_ZERO),
                                WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, IfBlock1) {
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0),
                                B1(WASM_LOCAL_SET(0, WASM_ZERO)), WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, IfBlock1b) {
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0),
                                       B1(WASM_LOCAL_SET(0, WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, IfBlock2a) {
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0),
                                       B2(WASM_LOCAL_SET(0, WASM_ZERO),
                                          WASM_LOCAL_SET(0, WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, IfBlock2b) {
  ExpectValidates(sigs.v_i(), {WASM_IF_ELSE(WASM_LOCAL_GET(0),
                                            B2(WASM_LOCAL_SET(0, WASM_ZERO),
                                               WASM_LOCAL_SET(0, WASM_ZERO)),
                                            WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, IfElseSet) {
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_LOCAL_SET(0, WASM_ZERO),
                                WASM_LOCAL_SET(0, WASM_I32V_1(1)))});
}

TEST_F(FunctionBodyDecoderTest, Loop0) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP_OP, kExprEnd});
}

TEST_F(FunctionBodyDecoderTest, Loop1) {
  static const uint8_t code[] = {WASM_LOOP(WASM_LOCAL_SET(0, WASM_ZERO))};
  ExpectValidates(sigs.v_i(), code);
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.f_ff(), code);
}

TEST_F(FunctionBodyDecoderTest, Loop2) {
  ExpectValidates(sigs.v_i(), {WASM_LOOP(WASM_LOCAL_SET(0, WASM_ZERO),
                                         WASM_LOCAL_SET(0, WASM_ZERO))});
}

TEST_F(FunctionBodyDecoderTest, Loop1_continue) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_BR(0))});
}

TEST_F(FunctionBodyDecoderTest, Loop1_break) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_BR(1))});
}

TEST_F(FunctionBodyDecoderTest, Loop2_continue) {
  ExpectValidates(sigs.v_i(),
                  {WASM_LOOP(WASM_LOCAL_SET(0, WASM_ZERO), WASM_BR(0))});
}

TEST_F(FunctionBodyDecoderTest, Loop2_break) {
  ExpectValidates(sigs.v_i(),
                  {WASM_LOOP(WASM_LOCAL_SET(0, WASM_ZERO), WASM_BR(1))});
}

TEST_F(FunctionBodyDecoderTest, InfiniteLoop1) {
  ExpectValidates(sigs.i_i(), {WASM_LOOP(WASM_BR(0)), WASM_ZERO});
  ExpectValidates(sigs.i_i(), {WASM_LOOP(WASM_BR(0)), WASM_ZERO});
  ExpectValidates(sigs.i_i(), {WASM_LOOP_I(WASM_BRV(1, WASM_ZERO))});
}

TEST_F(FunctionBodyDecoderTest, InfiniteLoop2) {
  ExpectFailure(sigs.i_i(), {WASM_LOOP(WASM_BR(0), WASM_ZERO), WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, Loop2_unreachable) {
  ExpectValidates(sigs.i_i(), {WASM_LOOP_I(WASM_BR(0), WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, LoopType) {
  ExpectValidates(sigs.i_i(), {WASM_LOOP_I(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.l_l(), {WASM_LOOP_L(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.f_f(), {WASM_LOOP_F(WASM_LOCAL_GET(0))});
  ExpectValidates(sigs.d_d(), {WASM_LOOP_D(WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, LoopType_void) {
  ExpectFailure(sigs.v_v(), {WASM_LOOP_I(WASM_ZERO)});
  ExpectFailure(sigs.v_v(), {WASM_LOOP_L(WASM_I64V_1(0))});
  ExpectFailure(sigs.v_v(), {WASM_LOOP_F(WASM_F32(0.0))});
  ExpectFailure(sigs.v_v(), {WASM_LOOP_D(WASM_F64(1.1))});
}

TEST_F(FunctionBodyDecoderTest, LoopType_fail) {
  ExpectFailure(sigs.i_i(), {WASM_LOOP_L(WASM_I64V_1(0))});
  ExpectFailure(sigs.i_i(), {WASM_LOOP_F(WASM_F32(0.0))});
  ExpectFailure(sigs.i_i(), {WASM_LOOP_D(WASM_F64(1.1))});

  ExpectFailure(sigs.l_l(), {WASM_LOOP_I(WASM_ZERO)});
  ExpectFailure(sigs.l_l(), {WASM_LOOP_F(WASM_F32(0.0))});
  ExpectFailure(sigs.l_l(), {WASM_LOOP_D(WASM_F64(1.1))});

  ExpectFailure(sigs.f_ff(), {WASM_LOOP_I(WASM_ZERO)});
  ExpectFailure(sigs.f_ff(), {WASM_LOOP_L(WASM_I64V_1(0))});
  ExpectFailure(sigs.f_ff(), {WASM_LOOP_D(WASM_F64(1.1))});

  ExpectFailure(sigs.d_dd(), {WASM_LOOP_I(WASM_ZERO)});
  ExpectFailure(sigs.d_dd(), {WASM_LOOP_L(WASM_I64V_1(0))});
  ExpectFailure(sigs.d_dd(), {WASM_LOOP_F(WASM_F32(0.0))});
}

TEST_F(FunctionBodyDecoderTest, ReturnVoid1) {
  static const uint8_t code[] = {kExprNop};
  ExpectValidates(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
  ExpectFailure(sigs.i_f(), code);
}

TEST_F(FunctionBodyDecoderTest, ReturnVoid2) {
  static const uint8_t code[] = {WASM_BLOCK(WASM_BR(0))};
  ExpectValidates(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
  ExpectFailure(sigs.i_f(), code);
}

TEST_F(FunctionBodyDecoderTest, ReturnVoid3) {
  ExpectFailure(sigs.v_v(), {kExprI32Const, 0});
  ExpectFailure(sigs.v_v(), {kExprI64Const, 0});
  ExpectFailure(sigs.v_v(), {kExprF32Const, 0, 0, 0, 0});
  ExpectFailure(sigs.v_v(), {kExprF64Const, 0, 0, 0, 0, 0, 0, 0, 0});
  ExpectFailure(sigs.v_v(), {kExprRefNull});
  ExpectFailure(sigs.v_v(), {kExprRefFunc, 0});

  ExpectFailure(sigs.v_i(), {kExprLocalGet, 0});
}

TEST_F(FunctionBodyDecoderTest, Unreachable1) {
  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE});
  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE, WASM_UNREACHABLE});
  ExpectValidates(sigs.i_i(), {WASM_UNREACHABLE, WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, Unreachable2) {
  ExpectFailure(sigs.v_v(), {B2(WASM_UNREACHABLE, WASM_ZERO)});
  ExpectFailure(sigs.v_v(), {B2(WASM_BR(0), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, UnreachableLoop1) {
  ExpectFailure(sigs.v_v(), {WASM_LOOP(WASM_UNREACHABLE, WASM_ZERO)});
  ExpectFailure(sigs.v_v(), {WASM_LOOP(WASM_BR(0), WASM_ZERO)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_UNREACHABLE, WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_BR(0), WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, Unreachable_binop1) {
  ExpectValidates(sigs.i_i(), {WASM_I32_AND(WASM_ZERO, WASM_UNREACHABLE)});
  ExpectValidates(sigs.i_i(), {WASM_I32_AND(WASM_UNREACHABLE, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Unreachable_binop2) {
  ExpectValidates(sigs.i_i(), {WASM_I32_AND(WASM_F32(0.0), WASM_UNREACHABLE)});
  ExpectFailure(sigs.i_i(), {WASM_I32_AND(WASM_UNREACHABLE, WASM_F32(0.0))});
}

TEST_F(FunctionBodyDecoderTest, Unreachable_select1) {
  ExpectValidates(sigs.i_i(),
                  {WASM_SELECT(WASM_UNREACHABLE, WASM_ZERO, WASM_ZERO)});
  ExpectValidates(sigs.i_i(),
                  {WASM_SELECT(WASM_ZERO, WASM_UNREACHABLE, WASM_ZERO)});
  ExpectValidates(sigs.i_i(),
                  {WASM_SELECT(WASM_ZERO, WASM_ZERO, WASM_UNREACHABLE)});
}

TEST_F(FunctionBodyDecoderTest, Unreachable_select2) {
  ExpectValidates(sigs.i_i(),
                  {WASM_SELECT(WASM_F32(0.0), WASM_UNREACHABLE, WASM_ZERO)});
  ExpectFailure(sigs.i_i(),
                {WASM_SELECT(WASM_UNREACHABLE, WASM_F32(0.0), WASM_ZERO)});
  ExpectFailure(sigs.i_i(),
                {WASM_SELECT(WASM_UNREACHABLE, WASM_ZERO, WASM_F32(0.0))});
}

TEST_F(FunctionBodyDecoderTest, UnreachableRefTypes) {
  uint8_t sig_index = builder.AddSignature(sigs.i_ii());
  uint8_t function_index = builder.AddFunction(sig_index);
  uint8_t struct_index =
      builder.AddStruct({F(kWasmI32, true), F(kWasmI64, true)});
  uint8_t array_index = builder.AddArray(kWasmI32, true);

  ValueType struct_type = ValueType::Ref(struct_index);
  ValueType struct_type_null = ValueType::RefNull(struct_index);
  FunctionSig sig_v_s(0, 1, &struct_type);
  uint8_t struct_consumer = builder.AddFunction(&sig_v_s);
  uint8_t struct_consumer2 = builder.AddFunction(
      FunctionSig::Build(zone(), {kWasmI32}, {struct_type, struct_type}));

  ExpectValidates(sigs.i_v(), {WASM_UNREACHABLE, kExprRefIsNull});
  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE, kExprRefAsNonNull, kExprDrop});

  ExpectValidates(sigs.i_v(), {WASM_UNREACHABLE, kExprCallRef, sig_index});
  ExpectValidates(sigs.i_v(), {WASM_UNREACHABLE, WASM_REF_FUNC(function_index),
                               kExprCallRef, sig_index});
  ExpectValidates(sigs.i_v(),
                  {WASM_UNREACHABLE, kExprReturnCallRef, sig_index});

  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprStructNew), struct_index,
                   kExprCallFunction, struct_consumer});
  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_I64V(42), WASM_GC_OP(kExprStructNew),
                   struct_index, kExprCallFunction, struct_consumer});
  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprStructNewDefault),
                   struct_index, kExprDrop});
  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprStructNewDefault),
                   struct_index, kExprCallFunction, struct_consumer});

  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE, WASM_GC_OP(kExprArrayNew),
                               array_index, kExprDrop});
  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_I32V(42), WASM_GC_OP(kExprArrayNew),
                   array_index, kExprDrop});
  ExpectValidates(sigs.v_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprArrayNewDefault),
                   array_index, kExprDrop});

  ExpectValidates(sigs.i_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprRefTest), struct_index});
  ExpectValidates(sigs.i_v(),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprRefTest), kEqRefCode});

  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE, WASM_GC_OP(kExprRefCast),
                               struct_index, kExprDrop});

  ExpectValidates(sigs.v_v(), {WASM_UNREACHABLE, kExprBrOnNull, 0, WASM_DROP});

  ExpectValidates(&sig_v_s, {WASM_UNREACHABLE, WASM_LOCAL_GET(0), kExprBrOnNull,
                             0, kExprCallFunction, struct_consumer});

  ExpectValidates(FunctionSig::Build(zone(), {struct_type}, {}),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprRefCast), struct_index});

  ExpectValidates(FunctionSig::Build(zone(), {kWasmStructRef}, {}),
                  {WASM_UNREACHABLE, WASM_GC_OP(kExprRefCast), kStructRefCode});

  ExpectValidates(FunctionSig::Build(zone(), {}, {struct_type_null}),
                  {WASM_UNREACHABLE, WASM_LOCAL_GET(0), kExprBrOnNull, 0,
                   kExprCallFunction, struct_consumer});

  ExpectFailure(
      sigs.v_v(), {WASM_UNREACHABLE, WASM_I32V(42), kExprBrOnNull, 0},
      kAppendEnd,
      "br_on_null[0] expected object reference, found i32.const of type i32");

  // This tests for a bug where {TypeCheckStackAgainstMerge} did not insert
  // unreachable values into the stack correctly.
  ExpectValidates(FunctionSig::Build(zone(), {kWasmI32}, {struct_type_null}),
                  {WASM_BLOCK_R(struct_type_null, kExprUnreachable,   // --
                                kExprLocalGet, 0, kExprRefAsNonNull,  // --
                                kExprLocalGet, 0, kExprBrOnNull, 0,   // --
                                kExprCallFunction, struct_consumer2,  // --
                                kExprBr, 1),
                   kExprDrop, WASM_I32V(1)});
}

TEST_F(FunctionBodyDecoderTest, If1) {
  ExpectValidates(sigs.i_i(), {WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_I32V_1(9),
                                              WASM_I32V_1(8))});
  ExpectValidates(sigs.i_i(), {WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_I32V_1(9),
                                              WASM_LOCAL_GET(0))});
  ExpectValidates(
      sigs.i_i(),
      {WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_I32V_1(8))});
}

TEST_F(FunctionBodyDecoderTest, If_off_end) {
  static const uint8_t kCode[] = {
      WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))};
  for (size_t len = 3; len < arraysize(kCode); len++) {
    ExpectFailure(sigs.i_i(), base::VectorOf(kCode, len), kAppendEnd);
    ExpectFailure(sigs.i_i(), base::VectorOf(kCode, len), kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, If_type1) {
  // float|double ? 1 : 2
  static const uint8_t kCode[] = {
      WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_I32V_1(0), WASM_I32V_1(2))};
  ExpectValidates(sigs.i_i(), kCode);
  ExpectFailure(sigs.i_f(), kCode);
  ExpectFailure(sigs.i_d(), kCode);
}

TEST_F(FunctionBodyDecoderTest, If_type2) {
  // 1 ? float|double : 2
  static const uint8_t kCode[] = {
      WASM_IF_ELSE_I(WASM_I32V_1(1), WASM_LOCAL_GET(0), WASM_I32V_1(1))};
  ExpectValidates(sigs.i_i(), kCode);
  ExpectFailure(sigs.i_f(), kCode);
  ExpectFailure(sigs.i_d(), kCode);
}

TEST_F(FunctionBodyDecoderTest, If_type3) {
  // stmt ? 0 : 1
  static const uint8_t kCode[] = {
      WASM_IF_ELSE_I(WASM_NOP, WASM_I32V_1(0), WASM_I32V_1(1))};
  ExpectFailure(sigs.i_i(), kCode);
  ExpectFailure(sigs.i_f(), kCode);
  ExpectFailure(sigs.i_d(), kCode);
}

TEST_F(FunctionBodyDecoderTest, If_type4) {
  // 0 ? stmt : 1
  static const uint8_t kCode[] = {
      WASM_IF_ELSE_I(WASM_LOCAL_GET(0), WASM_NOP, WASM_I32V_1(1))};
  ExpectFailure(sigs.i_i(), kCode);
  ExpectFailure(sigs.i_f(), kCode);
  ExpectFailure(sigs.i_d(), kCode);
}

TEST_F(FunctionBodyDecoderTest, If_type5) {
  // 0 ? 1 : stmt
  static const uint8_t kCode[] = {
      WASM_IF_ELSE_I(WASM_ZERO, WASM_I32V_1(1), WASM_NOP)};
  ExpectFailure(sigs.i_i(), kCode);
  ExpectFailure(sigs.i_f(), kCode);
  ExpectFailure(sigs.i_d(), kCode);
}

TEST_F(FunctionBodyDecoderTest, Int64Local_param) {
  ExpectValidates(sigs.l_l(), kCodeGetLocal0);
}

TEST_F(FunctionBodyDecoderTest, Int64Locals) {
  for (uint8_t i = 1; i < 8; i++) {
    AddLocals(kWasmI64, 1);
    for (uint8_t j = 0; j < i; j++) {
      ExpectValidates(sigs.l_v(), {WASM_LOCAL_GET(j)});
    }
  }
}

TEST_F(FunctionBodyDecoderTest, Int32Binops) {
  TestBinop(kExprI32Add, sigs.i_ii());
  TestBinop(kExprI32Sub, sigs.i_ii());
  TestBinop(kExprI32Mul, sigs.i_ii());
  TestBinop(kExprI32DivS, sigs.i_ii());
  TestBinop(kExprI32DivU, sigs.i_ii());
  TestBinop(kExprI32RemS, sigs.i_ii());
  TestBinop(kExprI32RemU, sigs.i_ii());
  TestBinop(kExprI32And, sigs.i_ii());
  TestBinop(kExprI32Ior, sigs.i_ii());
  TestBinop(kExprI32Xor, sigs.i_ii());
  TestBinop(kExprI32Shl, sigs.i_ii());
  TestBinop(kExprI32ShrU, sigs.i_ii());
  TestBinop(kExprI32ShrS, sigs.i_ii());
  TestBinop(kExprI32Eq, sigs.i_ii());
  TestBinop(kExprI32LtS, sigs.i_ii());
  TestBinop(kExprI32LeS, sigs.i_ii());
  TestBinop(kExprI32LtU, sigs.i_ii());
  TestBinop(kExprI32LeU, sigs.i_ii());
}

TEST_F(FunctionBodyDecoderTest, DoubleBinops) {
  TestBinop(kExprF64Add, sigs.d_dd());
  TestBinop(kExprF64Sub, sigs.d_dd());
  TestBinop(kExprF64Mul, sigs.d_dd());
  TestBinop(kExprF64Div, sigs.d_dd());

  TestBinop(kExprF64Eq, sigs.i_dd());
  TestBinop(kExprF64Lt, sigs.i_dd());
  TestBinop(kExprF64Le, sigs.i_dd());
}

TEST_F(FunctionBodyDecoderTest, FloatBinops) {
  TestBinop(kExprF32Add, sigs.f_ff());
  TestBinop(kExprF32Sub, sigs.f_ff());
  TestBinop(kExprF32Mul, sigs.f_ff());
  TestBinop(kExprF32Div, sigs.f_ff());

  TestBinop(kExprF32Eq, sigs.i_ff());
  TestBinop(kExprF32Lt, sigs.i_ff());
  TestBinop(kExprF32Le, sigs.i_ff());
}

TEST_F(FunctionBodyDecoderTest, TypeConversions) {
  TestUnop(kExprI32SConvertF32, kWasmI32, kWasmF32);
  TestUnop(kExprI32SConvertF64, kWasmI32, kWasmF64);
  TestUnop(kExprI32UConvertF32, kWasmI32, kWasmF32);
  TestUnop(kExprI32UConvertF64, kWasmI32, kWasmF64);
  TestUnop(kExprF64SConvertI32, kWasmF64, kWasmI32);
  TestUnop(kExprF64UConvertI32, kWasmF64, kWasmI32);
  TestUnop(kExprF64ConvertF32, kWasmF64, kWasmF32);
  TestUnop(kExprF32SConvertI32, kWasmF32, kWasmI32);
  TestUnop(kExprF32UConvertI32, kWasmF32, kWasmI32);
  TestUnop(kExprF32ConvertF64, kWasmF32, kWasmF64);
}

TEST_F(FunctionBodyDecoderTest, MacrosVoid) {
  builder.AddMemory();
  ExpectValidates(sigs.v_i(), {WASM_LOCAL_SET(0, WASM_I32V_3(87348))});
  ExpectValidates(
      sigs.v_i(),
      {WASM_STORE_MEM(MachineType::Int32(), WASM_I32V_1(24), WASM_I32V_1(40))});
  ExpectValidates(sigs.v_i(), {WASM_IF(WASM_LOCAL_GET(0), WASM_NOP)});
  ExpectValidates(sigs.v_i(),
                  {WASM_IF_ELSE(WASM_LOCAL_GET(0), WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_NOP});
  ExpectValidates(sigs.v_v(), {B1(WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_BR(0))});
}

TEST_F(FunctionBodyDecoderTest, MacrosContinue) {
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_CONTINUE(0))});
}

TEST_F(FunctionBodyDecoderTest, MacrosVariadic) {
  ExpectValidates(sigs.v_v(), {B2(WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.v_v(), {B3(WASM_NOP, WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.v_v(), {WASM_LOOP(WASM_NOP, WASM_NOP, WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, MacrosNestedBlocks) {
  ExpectValidates(sigs.v_v(), {B2(WASM_NOP, B2(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.v_v(), {B3(WASM_NOP,                   // --
                                  B2(WASM_NOP, WASM_NOP),     // --
                                  B2(WASM_NOP, WASM_NOP))});  // --
  ExpectValidates(sigs.v_v(), {B1(B1(B2(WASM_NOP, WASM_NOP)))});
}

TEST_F(FunctionBodyDecoderTest, MultipleReturn) {
  static ValueType kIntTypes5[] = {kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                                   kWasmI32};
  FunctionSig sig_ii_v(2, 0, kIntTypes5);
  ExpectValidates(&sig_ii_v, {WASM_RETURN(WASM_ZERO, WASM_ONE)});
  ExpectFailure(&sig_ii_v, {WASM_RETURN(WASM_ZERO)});

  FunctionSig sig_iii_v(3, 0, kIntTypes5);
  ExpectValidates(&sig_iii_v,
                  {WASM_RETURN(WASM_ZERO, WASM_ONE, WASM_I32V_1(44))});
  ExpectFailure(&sig_iii_v, {WASM_RETURN(WASM_ZERO, WASM_ONE)});
}

TEST_F(FunctionBodyDecoderTest, MultipleReturn_fallthru) {
  static ValueType kIntTypes5[] = {kWasmI32, kWasmI32, kWasmI32, kWasmI32,
                                   kWasmI32};
  FunctionSig sig_ii_v(2, 0, kIntTypes5);

  ExpectValidates(&sig_ii_v, {WASM_ZERO, WASM_ONE});
  ExpectFailure(&sig_ii_v, {WASM_ZERO});

  FunctionSig sig_iii_v(3, 0, kIntTypes5);
  ExpectValidates(&sig_iii_v, {WASM_ZERO, WASM_ONE, WASM_I32V_1(44)});
  ExpectFailure(&sig_iii_v, {WASM_ZERO, WASM_ONE});
}

TEST_F(FunctionBodyDecoderTest, MacrosInt32) {
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_I32V_1(12))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_I32V_1(13))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_I32V_1(14))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_DIVS(WASM_LOCAL_GET(0), WASM_I32V_1(15))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_DIVU(WASM_LOCAL_GET(0), WASM_I32V_1(16))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_REMS(WASM_LOCAL_GET(0), WASM_I32V_1(17))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_REMU(WASM_LOCAL_GET(0), WASM_I32V_1(18))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_AND(WASM_LOCAL_GET(0), WASM_I32V_1(19))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_IOR(WASM_LOCAL_GET(0), WASM_I32V_1(20))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_XOR(WASM_LOCAL_GET(0), WASM_I32V_1(21))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_SHL(WASM_LOCAL_GET(0), WASM_I32V_1(22))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_SHR(WASM_LOCAL_GET(0), WASM_I32V_1(23))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_SAR(WASM_LOCAL_GET(0), WASM_I32V_1(24))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_ROR(WASM_LOCAL_GET(0), WASM_I32V_1(24))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_ROL(WASM_LOCAL_GET(0), WASM_I32V_1(24))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_EQ(WASM_LOCAL_GET(0), WASM_I32V_1(25))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_NE(WASM_LOCAL_GET(0), WASM_I32V_1(25))});

  ExpectValidates(sigs.i_i(),
                  {WASM_I32_LTS(WASM_LOCAL_GET(0), WASM_I32V_1(26))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_LES(WASM_LOCAL_GET(0), WASM_I32V_1(27))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_LTU(WASM_LOCAL_GET(0), WASM_I32V_1(28))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_LEU(WASM_LOCAL_GET(0), WASM_I32V_1(29))});

  ExpectValidates(sigs.i_i(),
                  {WASM_I32_GTS(WASM_LOCAL_GET(0), WASM_I32V_1(26))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_GES(WASM_LOCAL_GET(0), WASM_I32V_1(27))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_GTU(WASM_LOCAL_GET(0), WASM_I32V_1(28))});
  ExpectValidates(sigs.i_i(),
                  {WASM_I32_GEU(WASM_LOCAL_GET(0), WASM_I32V_1(29))});
}

TEST_F(FunctionBodyDecoderTest, MacrosInt64) {
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_ADD(WASM_LOCAL_GET(0), WASM_I64V_1(12))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_SUB(WASM_LOCAL_GET(0), WASM_I64V_1(13))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_MUL(WASM_LOCAL_GET(0), WASM_I64V_1(14))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_DIVS(WASM_LOCAL_GET(0), WASM_I64V_1(15))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_DIVU(WASM_LOCAL_GET(0), WASM_I64V_1(16))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_REMS(WASM_LOCAL_GET(0), WASM_I64V_1(17))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_REMU(WASM_LOCAL_GET(0), WASM_I64V_1(18))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_AND(WASM_LOCAL_GET(0), WASM_I64V_1(19))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_IOR(WASM_LOCAL_GET(0), WASM_I64V_1(20))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_XOR(WASM_LOCAL_GET(0), WASM_I64V_1(21))});

  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_SHL(WASM_LOCAL_GET(0), WASM_I64V_1(22))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_SHR(WASM_LOCAL_GET(0), WASM_I64V_1(23))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_SAR(WASM_LOCAL_GET(0), WASM_I64V_1(24))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_ROR(WASM_LOCAL_GET(0), WASM_I64V_1(24))});
  ExpectValidates(sigs.l_ll(),
                  {WASM_I64_ROL(WASM_LOCAL_GET(0), WASM_I64V_1(24))});

  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_LTS(WASM_LOCAL_GET(0), WASM_I64V_1(26))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_LES(WASM_LOCAL_GET(0), WASM_I64V_1(27))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_LTU(WASM_LOCAL_GET(0), WASM_I64V_1(28))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_LEU(WASM_LOCAL_GET(0), WASM_I64V_1(29))});

  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_GTS(WASM_LOCAL_GET(0), WASM_I64V_1(26))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_GES(WASM_LOCAL_GET(0), WASM_I64V_1(27))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_GTU(WASM_LOCAL_GET(0), WASM_I64V_1(28))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_GEU(WASM_LOCAL_GET(0), WASM_I64V_1(29))});

  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_EQ(WASM_LOCAL_GET(0), WASM_I64V_1(25))});
  ExpectValidates(sigs.i_ll(),
                  {WASM_I64_NE(WASM_LOCAL_GET(0), WASM_I64V_1(25))});
}

TEST_F(FunctionBodyDecoderTest, AllSimpleExpressions) {
// Test all simple expressions which are described by a signature.
#define DECODE_TEST(name, opcode, sig, ...)                       \
  {                                                               \
    const FunctionSig* sig = WasmOpcodes::Signature(kExpr##name); \
    if (sig->parameter_count() == 1) {                            \
      TestUnop(kExpr##name, sig);                                 \
    } else {                                                      \
      TestBinop(kExpr##name, sig);                                \
    }                                                             \
  }

  FOREACH_SIMPLE_OPCODE(DECODE_TEST);

#undef DECODE_TEST
}

TEST_F(FunctionBodyDecoderTest, MemorySize) {
  builder.AddMemory();
  uint8_t code[] = {kExprMemorySize, 0};
  ExpectValidates(sigs.i_i(), code);
  ExpectFailure(sigs.f_ff(), code);
}

TEST_F(FunctionBodyDecoderTest, LoadMemOffset) {
  builder.AddMemory();
  for (int offset = 0; offset < 128; offset += 7) {
    uint8_t code[] = {kExprI32Const, 0, kExprI32LoadMem, ZERO_ALIGNMENT,
                      static_cast<uint8_t>(offset)};
    ExpectValidates(sigs.i_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, LoadMemAlignment) {
  builder.AddMemory();
  struct {
    WasmOpcode instruction;
    uint32_t maximum_aligment;
  } values[] = {
      {kExprI32LoadMem8U, 0},   // --
      {kExprI32LoadMem8S, 0},   // --
      {kExprI32LoadMem16U, 1},  // --
      {kExprI32LoadMem16S, 1},  // --
      {kExprI64LoadMem8U, 0},   // --
      {kExprI64LoadMem8S, 0},   // --
      {kExprI64LoadMem16U, 1},  // --
      {kExprI64LoadMem16S, 1},  // --
      {kExprI64LoadMem32U, 2},  // --
      {kExprI64LoadMem32S, 2},  // --
      {kExprI32LoadMem, 2},     // --
      {kExprI64LoadMem, 3},     // --
      {kExprF32LoadMem, 2},     // --
      {kExprF64LoadMem, 3},     // --
  };

  for (size_t i = 0; i < arraysize(values); i++) {
    for (uint8_t alignment = 0; alignment <= 4; alignment++) {
      uint8_t code[] = {WASM_ZERO, static_cast<uint8_t>(values[i].instruction),
                        alignment, ZERO_OFFSET, WASM_DROP};
      Validate(alignment <= values[i].maximum_aligment, sigs.v_i(), code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, StoreMemOffset) {
  builder.AddMemory();
  for (uint8_t offset = 0; offset < 128; offset += 7) {
    uint8_t code[] = {WASM_STORE_MEM_OFFSET(MachineType::Int32(), offset,
                                            WASM_ZERO, WASM_ZERO)};
    ExpectValidates(sigs.v_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, StoreMemOffset_void) {
  builder.AddMemory();
  ExpectFailure(sigs.i_i(), {WASM_STORE_MEM_OFFSET(MachineType::Int32(), 0,
                                                   WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, LoadMemOffset_varint) {
  builder.AddMemory();
  ExpectValidates(sigs.i_i(),
                  {WASM_ZERO, kExprI32LoadMem, ZERO_ALIGNMENT, U32V_1(0x45)});
  ExpectValidates(sigs.i_i(),
                  {WASM_ZERO, kExprI32LoadMem, ZERO_ALIGNMENT, U32V_2(0x3999)});
  ExpectValidates(sigs.i_i(), {WASM_ZERO, kExprI32LoadMem, ZERO_ALIGNMENT,
                               U32V_3(0x344445)});
  ExpectValidates(sigs.i_i(), {WASM_ZERO, kExprI32LoadMem, ZERO_ALIGNMENT,
                               U32V_4(0x36666667)});
}

TEST_F(FunctionBodyDecoderTest, StoreMemOffset_varint) {
  builder.AddMemory();
  ExpectValidates(sigs.v_i(), {WASM_ZERO, WASM_ZERO, kExprI32StoreMem,
                               ZERO_ALIGNMENT, U32V_1(0x33)});
  ExpectValidates(sigs.v_i(), {WASM_ZERO, WASM_ZERO, kExprI32StoreMem,
                               ZERO_ALIGNMENT, U32V_2(0x1111)});
  ExpectValidates(sigs.v_i(), {WASM_ZERO, WASM_ZERO, kExprI32StoreMem,
                               ZERO_ALIGNMENT, U32V_3(0x222222)});
  ExpectValidates(sigs.v_i(), {WASM_ZERO, WASM_ZERO, kExprI32StoreMem,
                               ZERO_ALIGNMENT, U32V_4(0x44444444)});
}

TEST_F(FunctionBodyDecoderTest, AllLoadMemCombinations) {
  builder.AddMemory();
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType local_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(machineTypes); j++) {
      MachineType mem_type = machineTypes[j];
      uint8_t code[] = {WASM_LOAD_MEM(mem_type, WASM_ZERO)};
      FunctionSig sig(1, 0, &local_type);
      Validate(local_type == ValueType::For(mem_type), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, AllStoreMemCombinations) {
  builder.AddMemory();
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType local_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(machineTypes); j++) {
      MachineType mem_type = machineTypes[j];
      uint8_t code[] = {WASM_STORE_MEM(mem_type, WASM_ZERO, WASM_LOCAL_GET(0))};
      FunctionSig sig(0, 1, &local_type);
      Validate(local_type == ValueType::For(mem_type), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, SimpleCalls) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_v());
  builder.AddFunction(sigs.i_i());
  builder.AddFunction(sigs.i_ii());

  ExpectValidates(sig, {WASM_CALL_FUNCTION0(0)});
  ExpectValidates(sig, {WASM_CALL_FUNCTION(1, WASM_I32V_1(27))});
  ExpectValidates(sig,
                  {WASM_CALL_FUNCTION(2, WASM_I32V_1(37), WASM_I32V_2(77))});
}

TEST_F(FunctionBodyDecoderTest, CallsWithTooFewArguments) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_i());
  builder.AddFunction(sigs.i_ii());
  builder.AddFunction(sigs.f_ff());

  ExpectFailure(sig, {WASM_CALL_FUNCTION0(0)});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(1, WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(2, WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, CallsWithMismatchedSigs2) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_i());

  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_I64V_1(17))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_F32(17.1))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_F64(17.1))});
}

TEST_F(FunctionBodyDecoderTest, CallsWithMismatchedSigs3) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_f());

  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_I32V_1(17))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_I64V_1(27))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(0, WASM_F64(37.2))});

  builder.AddFunction(sigs.i_d());

  ExpectFailure(sig, {WASM_CALL_FUNCTION(1, WASM_I32V_1(16))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(1, WASM_I64V_1(16))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(1, WASM_F32(17.6))});
}

TEST_F(FunctionBodyDecoderTest, SimpleReturnCalls) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_v());
  builder.AddFunction(sigs.i_i());
  builder.AddFunction(sigs.i_ii());

  ExpectValidates(sig, {WASM_RETURN_CALL_FUNCTION0(0)});
  ExpectValidates(sig, {WASM_RETURN_CALL_FUNCTION(1, WASM_I32V_1(27))});
  ExpectValidates(
      sig, {WASM_RETURN_CALL_FUNCTION(2, WASM_I32V_1(37), WASM_I32V_2(77))});
}

TEST_F(FunctionBodyDecoderTest, ReturnCallsWithTooFewArguments) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_i());
  builder.AddFunction(sigs.i_ii());
  builder.AddFunction(sigs.f_ff());

  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION0(0)});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(1, WASM_ZERO)});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(2, WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, ReturnCallWithSubtype) {
  auto sig = MakeSig::Returns(kWasmAnyRef);
  auto callee_sig = MakeSig::Returns(kWasmAnyRef.AsNonNull());
  builder.AddFunction(&callee_sig);

  ExpectValidates(&sig, {WASM_RETURN_CALL_FUNCTION0(0)});
}

TEST_F(FunctionBodyDecoderTest, ReturnCallsWithMismatchedSigs) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddFunction(sigs.i_f());
  builder.AddFunction(sigs.f_f());

  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(0, WASM_I32V_1(17))});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(0, WASM_I64V_1(27))});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(0, WASM_F64(37.2))});

  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(1, WASM_F64(37.2))});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(1, WASM_F32(37.2))});
  ExpectFailure(sig, {WASM_RETURN_CALL_FUNCTION(1, WASM_I32V_1(17))});
}

TEST_F(FunctionBodyDecoderTest, SimpleIndirectReturnCalls) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, true, 30);

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  ExpectValidates(sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_ZERO)});
  ExpectValidates(
      sig, {WASM_RETURN_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO)});
  ExpectValidates(sig, {WASM_RETURN_CALL_INDIRECT(sig2, WASM_I32V_1(32),
                                                  WASM_I32V_2(72), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectReturnCallsOutOfBounds) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, false, 20);

  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(0, WASM_ZERO)});
  builder.AddSignature(sigs.i_v());
  ExpectValidates(sig, {WASM_RETURN_CALL_INDIRECT(0, WASM_ZERO)});

  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(1, WASM_I32V_1(22), WASM_ZERO)});
  builder.AddSignature(sigs.i_i());
  ExpectValidates(sig,
                  {WASM_RETURN_CALL_INDIRECT(1, WASM_I32V_1(27), WASM_ZERO)});

  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(2, WASM_I32V_1(27), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectReturnCallsWithMismatchedSigs3) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(wasm::kWasmVoid);

  uint8_t sig0 = builder.AddSignature(sigs.i_f());

  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig0, WASM_I32V_1(17), WASM_ZERO)});
  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig0, WASM_I64V_1(27), WASM_ZERO)});
  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig0, WASM_F64(37.2), WASM_ZERO)});

  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_I32V_1(17))});
  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_I64V_1(27))});
  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_F64(37.2))});

  uint8_t sig1 = builder.AddFunction(sigs.i_d());

  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig1, WASM_I32V_1(16), WASM_ZERO)});
  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig1, WASM_I64V_1(16), WASM_ZERO)});
  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig1, WASM_F32(17.6), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectReturnCallsWithoutTableCrash) {
  const FunctionSig* sig = sigs.i_i();

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_ZERO)});
  ExpectFailure(sig,
                {WASM_RETURN_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO)});
  ExpectFailure(sig, {WASM_RETURN_CALL_INDIRECT(sig2, WASM_I32V_1(32),
                                                WASM_I32V_2(72), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IncompleteIndirectReturnCall) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(wasm::kWasmVoid);

  static uint8_t code[] = {kExprReturnCallIndirect};
  ExpectFailure(sig, base::ArrayVector(code), kOmitEnd);
}

TEST_F(FunctionBodyDecoderTest, MultiReturn) {
  ValueType storage[] = {kWasmI32, kWasmI32};
  FunctionSig sig_ii_v(2, 0, storage);
  FunctionSig sig_v_ii(0, 2, storage);

  builder.AddFunction(&sig_v_ii);
  builder.AddFunction(&sig_ii_v);

  ExpectValidates(&sig_ii_v, {WASM_CALL_FUNCTION0(1)});
  ExpectValidates(sigs.v_v(), {WASM_CALL_FUNCTION0(1), WASM_DROP, WASM_DROP});
  ExpectValidates(sigs.v_v(), {WASM_CALL_FUNCTION0(1), kExprCallFunction, 0});
}

TEST_F(FunctionBodyDecoderTest, MultiReturnType) {
  for (size_t a = 0; a < arraysize(kValueTypes); a++) {
    for (size_t b = 0; b < arraysize(kValueTypes); b++) {
      for (size_t c = 0; c < arraysize(kValueTypes); c++) {
        for (size_t d = 0; d < arraysize(kValueTypes); d++) {
          ValueType storage_ab[] = {kValueTypes[a], kValueTypes[b]};
          FunctionSig sig_ab_v(2, 0, storage_ab);
          ValueType storage_cd[] = {kValueTypes[c], kValueTypes[d]};
          FunctionSig sig_cd_v(2, 0, storage_cd);

          TestModuleBuilder builder;
          module = builder.module();
          builder.AddFunction(&sig_cd_v);

          ExpectValidates(&sig_cd_v, {WASM_CALL_FUNCTION0(0)});

          if (IsSubtypeOf(kValueTypes[c], kValueTypes[a], module) &&
              IsSubtypeOf(kValueTypes[d], kValueTypes[b], module)) {
            ExpectValidates(&sig_ab_v, {WASM_CALL_FUNCTION0(0)});
          } else {
            ExpectFailure(&sig_ab_v, {WASM_CALL_FUNCTION0(0)});
          }
        }
      }
    }
  }
}

TEST_F(FunctionBodyDecoderTest, SimpleIndirectCalls) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, false, 20);

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  ExpectValidates(sig, {WASM_CALL_INDIRECT(sig0, WASM_ZERO)});
  ExpectValidates(sig, {WASM_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO)});
  ExpectValidates(sig, {WASM_CALL_INDIRECT(sig2, WASM_I32V_1(32),
                                           WASM_I32V_2(72), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectCallsOutOfBounds) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, false, 20);

  ExpectFailure(sig, {WASM_CALL_INDIRECT(0, WASM_ZERO)});
  builder.AddSignature(sigs.i_v());
  ExpectValidates(sig, {WASM_CALL_INDIRECT(0, WASM_ZERO)});

  ExpectFailure(sig, {WASM_CALL_INDIRECT(1, WASM_I32V_1(22), WASM_ZERO)});
  builder.AddSignature(sigs.i_i());
  ExpectValidates(sig, {WASM_CALL_INDIRECT(1, WASM_I32V_1(27), WASM_ZERO)});

  ExpectFailure(sig, {WASM_CALL_INDIRECT(2, WASM_I32V_1(27), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectCallsWithMismatchedSigs1) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(wasm::kWasmVoid);

  uint8_t sig0 = builder.AddSignature(sigs.i_f());

  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_I32V_1(17), WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_I64V_1(27), WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_F64(37.2), WASM_ZERO)});

  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_I32V_1(17))});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_I64V_1(27))});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_F64(37.2))});

  uint8_t sig1 = builder.AddFunction(sigs.i_d());

  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig1, WASM_I32V_1(16), WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig1, WASM_I64V_1(16), WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig1, WASM_F32(17.6), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IndirectCallsWithMismatchedSigs2) {
  uint8_t table_type_index = builder.AddSignature(sigs.i_i());
  uint8_t table_index = builder.AddTable(ValueType::RefNull(table_type_index));

  ExpectValidates(sigs.i_v(),
                  {WASM_CALL_INDIRECT_TABLE(table_index, table_type_index,
                                            WASM_I32V_1(42), WASM_ZERO)});

  uint8_t wrong_type_index = builder.AddSignature(sigs.i_ii());
  // Note: this would trap at runtime, but does validate.
  ExpectValidates(
      sigs.i_v(),
      {WASM_CALL_INDIRECT_TABLE(table_index, wrong_type_index, WASM_I32V_1(41),
                                WASM_I32V_1(42), WASM_ZERO)});

  uint8_t non_function_table_index = builder.AddTable(kWasmExternRef);
  ExpectFailure(
      sigs.i_v(),
      {WASM_CALL_INDIRECT_TABLE(non_function_table_index, table_type_index,
                                WASM_I32V_1(42), WASM_ZERO)},
      kAppendEnd,
      "call_indirect: immediate table #1 is not of a function type");
}

TEST_F(FunctionBodyDecoderTest, TablesWithFunctionSubtyping) {
  uint8_t empty_struct = builder.AddStruct({});
  uint8_t super_struct = builder.AddStruct({F(kWasmI32, true)}, empty_struct);
  uint8_t sub_struct =
      builder.AddStruct({F(kWasmI32, true), F(kWasmF64, true)}, super_struct);

  uint8_t table_supertype = builder.AddSignature(
      FunctionSig::Build(zone(), {ValueType::RefNull(empty_struct)},
                         {ValueType::RefNull(sub_struct)}));
  uint8_t table_type = builder.AddSignature(
      FunctionSig::Build(zone(), {ValueType::RefNull(super_struct)},
                         {ValueType::RefNull(sub_struct)}),
      table_supertype);
  auto function_sig =
      FunctionSig::Build(zone(), {ValueType::RefNull(sub_struct)},
                         {ValueType::RefNull(super_struct)});
  uint8_t function_type = builder.AddSignature(function_sig, table_type);

  uint8_t function = builder.AddFunction(function_type);

  uint8_t table = builder.AddTable(ValueType::RefNull(table_type));

  // We can call-indirect from a typed function table with an immediate type
  // that is a subtype of the table type.
  ExpectValidates(
      FunctionSig::Build(zone(), {ValueType::RefNull(sub_struct)}, {}),
      {WASM_CALL_INDIRECT_TABLE(table, function_type,
                                WASM_STRUCT_NEW_DEFAULT(super_struct),
                                WASM_ZERO)});

  // table.set's subtyping works as expected.
  ExpectValidates(sigs.v_i(), {WASM_TABLE_SET(0, WASM_LOCAL_GET(0),
                                              WASM_REF_FUNC(function))});
  // table.get's subtyping works as expected.
  ExpectValidates(
      FunctionSig::Build(zone(), {ValueType::RefNull(table_supertype)},
                         {kWasmI32}),
      {WASM_TABLE_GET(0, WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, IndirectCallsWithoutTableCrash) {
  const FunctionSig* sig = sigs.i_i();

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig0, WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO)});
  ExpectFailure(sig, {WASM_CALL_INDIRECT(sig2, WASM_I32V_1(32), WASM_I32V_2(72),
                                         WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, IncompleteIndirectCall) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(wasm::kWasmVoid);

  static uint8_t code[] = {kExprCallIndirect};
  ExpectFailure(sig, base::ArrayVector(code), kOmitEnd);
}

TEST_F(FunctionBodyDecoderTest, IncompleteStore) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddMemory();
  builder.AddTable(wasm::kWasmVoid);

  static uint8_t code[] = {kExprI32StoreMem};
  ExpectFailure(sig, base::ArrayVector(code), kOmitEnd);
}

TEST_F(FunctionBodyDecoderTest, IncompleteI8x16Shuffle) {
  const FunctionSig* sig = sigs.i_i();
  builder.AddMemory();
  builder.AddTable(wasm::kWasmVoid);

  static uint8_t code[] = {kSimdPrefix,
                           static_cast<uint8_t>(kExprI8x16Shuffle & 0xff)};
  ExpectFailure(sig, base::ArrayVector(code), kOmitEnd);
}

TEST_F(FunctionBodyDecoderTest, SimpleImportCalls) {
  const FunctionSig* sig = sigs.i_i();

  uint8_t f0 = builder.AddImport(sigs.i_v());
  uint8_t f1 = builder.AddImport(sigs.i_i());
  uint8_t f2 = builder.AddImport(sigs.i_ii());

  ExpectValidates(sig, {WASM_CALL_FUNCTION0(f0)});
  ExpectValidates(sig, {WASM_CALL_FUNCTION(f1, WASM_I32V_1(22))});
  ExpectValidates(sig,
                  {WASM_CALL_FUNCTION(f2, WASM_I32V_1(32), WASM_I32V_2(72))});
}

TEST_F(FunctionBodyDecoderTest, ImportCallsWithMismatchedSigs3) {
  const FunctionSig* sig = sigs.i_i();

  uint8_t f0 = builder.AddImport(sigs.i_f());

  ExpectFailure(sig, {WASM_CALL_FUNCTION0(f0)});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f0, WASM_I32V_1(17))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f0, WASM_I64V_1(27))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f0, WASM_F64(37.2))});

  uint8_t f1 = builder.AddImport(sigs.i_d());

  ExpectFailure(sig, {WASM_CALL_FUNCTION0(f1)});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f1, WASM_I32V_1(16))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f1, WASM_I64V_1(16))});
  ExpectFailure(sig, {WASM_CALL_FUNCTION(f1, WASM_F32(17.6))});
}

TEST_F(FunctionBodyDecoderTest, Int32Globals) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddGlobal(kWasmI32);

  ExpectValidates(sig, {WASM_GLOBAL_GET(0)});
  ExpectFailure(sig, {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0))});
  ExpectValidates(sig, {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, ImmutableGlobal) {
  const FunctionSig* sig = sigs.v_v();

  uint32_t g0 = builder.AddGlobal(kWasmI32, true);
  uint32_t g1 = builder.AddGlobal(kWasmI32, false);

  ExpectValidates(sig, {WASM_GLOBAL_SET(g0, WASM_ZERO)});
  ExpectFailure(sig, {WASM_GLOBAL_SET(g1, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Int32Globals_fail) {
  const FunctionSig* sig = sigs.i_i();

  builder.AddGlobal(kWasmI64);
  builder.AddGlobal(kWasmI64);
  builder.AddGlobal(kWasmF32);
  builder.AddGlobal(kWasmF64);

  ExpectFailure(sig, {WASM_GLOBAL_GET(0)});
  ExpectFailure(sig, {WASM_GLOBAL_GET(1)});
  ExpectFailure(sig, {WASM_GLOBAL_GET(2)});
  ExpectFailure(sig, {WASM_GLOBAL_GET(3)});

  ExpectFailure(sig, {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_ZERO});
  ExpectFailure(sig, {WASM_GLOBAL_SET(1, WASM_LOCAL_GET(0)), WASM_ZERO});
  ExpectFailure(sig, {WASM_GLOBAL_SET(2, WASM_LOCAL_GET(0)), WASM_ZERO});
  ExpectFailure(sig, {WASM_GLOBAL_SET(3, WASM_LOCAL_GET(0)), WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, Int64Globals) {
  const FunctionSig* sig = sigs.l_l();

  builder.AddGlobal(kWasmI64);
  builder.AddGlobal(kWasmI64);

  ExpectValidates(sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(sig, {WASM_GLOBAL_GET(1)});

  ExpectValidates(sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
  ExpectValidates(sig,
                  {WASM_GLOBAL_SET(1, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, Float32Globals) {
  const FunctionSig* sig = sigs.f_ff();

  builder.AddGlobal(kWasmF32);

  ExpectValidates(sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, Float64Globals) {
  const FunctionSig* sig = sigs.d_dd();

  builder.AddGlobal(kWasmF64);

  ExpectValidates(sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, NullRefGlobals) {
  ValueType nullRefs[] = {kWasmNullRef, kWasmNullRef, kWasmNullRef};
  FunctionSig sig(1, 2, nullRefs);
  builder.AddGlobal(kWasmNullRef);
  ExpectValidates(&sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(&sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
  ExpectValidates(
      &sig, {WASM_GLOBAL_SET(0, WASM_REF_NULL(kNoneCode)), WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, NullExternRefGlobals) {
  ValueType nullExternRefs[] = {kWasmNullExternRef, kWasmNullExternRef,
                                kWasmNullExternRef};
  FunctionSig sig(1, 2, nullExternRefs);
  builder.AddGlobal(kWasmNullExternRef);
  ExpectValidates(&sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(&sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
  ExpectValidates(&sig, {WASM_GLOBAL_SET(0, WASM_REF_NULL(kNoExternCode)),
                         WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, NullFuncRefGlobals) {
  ValueType nullFuncRefs[] = {kWasmNullFuncRef, kWasmNullFuncRef,
                              kWasmNullFuncRef};
  FunctionSig sig(1, 2, nullFuncRefs);
  builder.AddGlobal(kWasmNullFuncRef);
  ExpectValidates(&sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(&sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
  ExpectValidates(&sig, {WASM_GLOBAL_SET(0, WASM_REF_NULL(kNoFuncCode)),
                         WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, NullExnRefGlobals) {
  WASM_FEATURE_SCOPE(exnref);
  ValueType nullFuncRefs[] = {kWasmNullExnRef, kWasmNullExnRef,
                              kWasmNullExnRef};
  FunctionSig sig(1, 2, nullFuncRefs);
  builder.AddGlobal(kWasmNullExnRef);
  ExpectValidates(&sig, {WASM_GLOBAL_GET(0)});
  ExpectValidates(&sig,
                  {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0)), WASM_LOCAL_GET(0)});
  ExpectValidates(
      &sig, {WASM_GLOBAL_SET(0, WASM_REF_NULL(kNoExnCode)), WASM_LOCAL_GET(0)});
}

TEST_F(FunctionBodyDecoderTest, AllGetGlobalCombinations) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType local_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType global_type = kValueTypes[j];
      FunctionSig sig(1, 0, &local_type);
      TestModuleBuilder builder;
      module = builder.module();
      builder.AddGlobal(global_type);
      Validate(IsSubtypeOf(global_type, local_type, module), &sig,
               {WASM_GLOBAL_GET(0)});
    }
  }
}

TEST_F(FunctionBodyDecoderTest, AllSetGlobalCombinations) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType local_type = kValueTypes[i];
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType global_type = kValueTypes[j];
      FunctionSig sig(0, 1, &local_type);
      TestModuleBuilder builder;
      module = builder.module();
      builder.AddGlobal(global_type);
      Validate(IsSubtypeOf(local_type, global_type, module), &sig,
               {WASM_GLOBAL_SET(0, WASM_LOCAL_GET(0))});
    }
  }
}

TEST_F(FunctionBodyDecoderTest, TableSet) {
  uint8_t tab_type = builder.AddSignature(sigs.i_i());
  uint8_t tab_ref1 = builder.AddTable(kWasmExternRef, 10, true, 20);
  uint8_t tab_func1 = builder.AddTable(kWasmFuncRef, 20, true, 30);
  uint8_t tab_func2 = builder.AddTable(kWasmFuncRef, 10, false, 20);
  uint8_t tab_ref2 = builder.AddTable(kWasmExternRef, 10, false, 20);
  uint8_t tab_typed_func =
      builder.AddTable(ValueType::RefNull(tab_type), 10, false, 20);

  ValueType sig_types[]{kWasmExternRef, kWasmFuncRef, kWasmI32,
                        ValueType::Ref(tab_type)};
  FunctionSig sig(0, 4, sig_types);
  uint8_t local_ref = 0;
  uint8_t local_func = 1;
  uint8_t local_int = 2;
  uint8_t local_typed_func = 3;

  ExpectValidates(&sig, {WASM_TABLE_SET(tab_ref1, WASM_I32V(6),
                                        WASM_LOCAL_GET(local_ref))});
  ExpectValidates(&sig, {WASM_TABLE_SET(tab_func1, WASM_I32V(5),
                                        WASM_LOCAL_GET(local_func))});
  ExpectValidates(&sig, {WASM_TABLE_SET(tab_func2, WASM_I32V(7),
                                        WASM_LOCAL_GET(local_func))});
  ExpectValidates(&sig, {WASM_TABLE_SET(tab_ref2, WASM_I32V(8),
                                        WASM_LOCAL_GET(local_ref))});
  ExpectValidates(&sig, {WASM_TABLE_SET(tab_typed_func, WASM_I32V(8),
                                        WASM_LOCAL_GET(local_typed_func))});
  ExpectValidates(&sig, {WASM_TABLE_SET(tab_func1, WASM_I32V(8),
                                        WASM_LOCAL_GET(local_typed_func))});

  // Only values of the correct type can be set to a table.
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_ref1, WASM_I32V(4),
                                      WASM_LOCAL_GET(local_func))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_func1, WASM_I32V(9),
                                      WASM_LOCAL_GET(local_ref))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_func2, WASM_I32V(3),
                                      WASM_LOCAL_GET(local_ref))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_ref2, WASM_I32V(2),
                                      WASM_LOCAL_GET(local_func))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_ref1, WASM_I32V(9),
                                      WASM_LOCAL_GET(local_int))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_func1, WASM_I32V(3),
                                      WASM_LOCAL_GET(local_int))});
  ExpectFailure(&sig, {WASM_TABLE_SET(tab_typed_func, WASM_I32V(3),
                                      WASM_LOCAL_GET(local_func))});

  // Out-of-bounds table index should fail.
  uint8_t oob_tab = 37;
  ExpectFailure(
      &sig, {WASM_TABLE_SET(oob_tab, WASM_I32V(9), WASM_LOCAL_GET(local_ref))});
  ExpectFailure(&sig, {WASM_TABLE_SET(oob_tab, WASM_I32V(3),
                                      WASM_LOCAL_GET(local_func))});
}

TEST_F(FunctionBodyDecoderTest, TableGet) {
  uint8_t tab_type = builder.AddSignature(sigs.i_i());
  uint8_t tab_ref1 = builder.AddTable(kWasmExternRef, 10, true, 20);
  uint8_t tab_func1 = builder.AddTable(kWasmFuncRef, 20, true, 30);
  uint8_t tab_func2 = builder.AddTable(kWasmFuncRef, 10, false, 20);
  uint8_t tab_ref2 = builder.AddTable(kWasmExternRef, 10, false, 20);
  uint8_t tab_typed_func =
      builder.AddTable(ValueType::RefNull(tab_type), 10, false, 20);

  ValueType sig_types[]{kWasmExternRef, kWasmFuncRef, kWasmI32,
                        ValueType::RefNull(tab_type)};
  FunctionSig sig(0, 4, sig_types);
  uint8_t local_ref = 0;
  uint8_t local_func = 1;
  uint8_t local_int = 2;
  uint8_t local_typed_func = 3;

  ExpectValidates(
      &sig,
      {WASM_LOCAL_SET(local_ref, WASM_TABLE_GET(tab_ref1, WASM_I32V(6)))});
  ExpectValidates(
      &sig,
      {WASM_LOCAL_SET(local_ref, WASM_TABLE_GET(tab_ref2, WASM_I32V(8)))});
  ExpectValidates(
      &sig,
      {WASM_LOCAL_SET(local_func, WASM_TABLE_GET(tab_func1, WASM_I32V(5)))});
  ExpectValidates(
      &sig,
      {WASM_LOCAL_SET(local_func, WASM_TABLE_GET(tab_func2, WASM_I32V(7)))});
  ExpectValidates(
      &sig, {WASM_LOCAL_SET(local_ref, WASM_SEQ(WASM_I32V(6), kExprTableGet,
                                                U32V_2(tab_ref1)))});
  ExpectValidates(
      &sig, {WASM_LOCAL_SET(local_func,
                            WASM_TABLE_GET(tab_typed_func, WASM_I32V(7)))});
  ExpectValidates(
      &sig, {WASM_LOCAL_SET(local_typed_func,
                            WASM_TABLE_GET(tab_typed_func, WASM_I32V(7)))});

  // We cannot store references as any other type.
  ExpectFailure(&sig, {WASM_LOCAL_SET(local_func,
                                      WASM_TABLE_GET(tab_ref1, WASM_I32V(4)))});
  ExpectFailure(&sig, {WASM_LOCAL_SET(
                          local_ref, WASM_TABLE_GET(tab_func1, WASM_I32V(9)))});
  ExpectFailure(&sig, {WASM_LOCAL_SET(
                          local_ref, WASM_TABLE_GET(tab_func2, WASM_I32V(3)))});
  ExpectFailure(&sig, {WASM_LOCAL_SET(local_func,
                                      WASM_TABLE_GET(tab_ref2, WASM_I32V(2)))});

  ExpectFailure(&sig, {WASM_LOCAL_SET(local_int,
                                      WASM_TABLE_GET(tab_ref1, WASM_I32V(9)))});
  ExpectFailure(&sig, {WASM_LOCAL_SET(
                          local_int, WASM_TABLE_GET(tab_func1, WASM_I32V(3)))});
  ExpectFailure(&sig,
                {WASM_LOCAL_SET(local_typed_func,
                                WASM_TABLE_GET(tab_func1, WASM_I32V(3)))});

  // Out-of-bounds table index should fail.
  uint8_t oob_tab = 37;
  ExpectFailure(
      &sig, {WASM_LOCAL_SET(local_ref, WASM_TABLE_GET(oob_tab, WASM_I32V(9)))});
  ExpectFailure(&sig, {WASM_LOCAL_SET(local_func,
                                      WASM_TABLE_GET(oob_tab, WASM_I32V(3)))});
}

TEST_F(FunctionBodyDecoderTest, MultiTableCallIndirect) {
  uint8_t tab_ref = builder.AddTable(kWasmExternRef, 10, true, 20);
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, 20, true, 30);

  ValueType sig_types[]{kWasmExternRef, kWasmFuncRef, kWasmI32};
  FunctionSig sig(0, 3, sig_types);
  uint8_t sig_index = builder.AddSignature(sigs.i_v());

  // We can store funcref values as externref, but not the other way around.
  ExpectValidates(sigs.i_v(),
                  {kExprI32Const, 0, kExprCallIndirect, sig_index, tab_func});

  ExpectFailure(sigs.i_v(),
                {kExprI32Const, 0, kExprCallIndirect, sig_index, tab_ref});
}

TEST_F(FunctionBodyDecoderTest, WasmMemoryGrow) {
  builder.AddMemory();

  uint8_t code[] = {WASM_LOCAL_GET(0), kExprMemoryGrow, 0};
  ExpectValidates(sigs.i_i(), code);
  ExpectFailure(sigs.i_d(), code);
}

TEST_F(FunctionBodyDecoderTest, BreakEnd) {
  ExpectValidates(
      sigs.i_i(),
      {WASM_BLOCK_I(WASM_I32_ADD(WASM_BRV(0, WASM_ZERO), WASM_ZERO))});
  ExpectValidates(
      sigs.i_i(),
      {WASM_BLOCK_I(WASM_I32_ADD(WASM_ZERO, WASM_BRV(0, WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, BreakIfBinop) {
  ExpectValidates(sigs.i_i(),
                  {WASM_BLOCK_I(WASM_I32_ADD(
                      WASM_BRV_IF(0, WASM_ZERO, WASM_ZERO), WASM_ZERO))});
  ExpectValidates(sigs.i_i(),
                  {WASM_BLOCK_I(WASM_I32_ADD(
                      WASM_ZERO, WASM_BRV_IF(0, WASM_ZERO, WASM_ZERO)))});
  ExpectValidates(
      sigs.f_ff(),
      {WASM_BLOCK_F(WASM_F32_ABS(WASM_BRV_IF(0, WASM_F32(0.0f), WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, BreakIfBinop_fail) {
  ExpectFailure(
      sigs.f_ff(),
      {WASM_BLOCK_F(WASM_F32_ABS(WASM_BRV_IF(0, WASM_ZERO, WASM_ZERO)))});
  ExpectFailure(
      sigs.i_i(),
      {WASM_BLOCK_I(WASM_F32_ABS(WASM_BRV_IF(0, WASM_F32(0.0f), WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, BreakIfUnrNarrow) {
  ExpectFailure(
      sigs.f_ff(),
      {WASM_BLOCK_I(WASM_BRV_IF(0, WASM_UNREACHABLE, WASM_UNREACHABLE),
                    WASM_RETURN0),
       WASM_F32(0.0)});
}

TEST_F(FunctionBodyDecoderTest, BreakNesting1) {
  for (int i = 0; i < 5; i++) {
    // (block[2] (loop[2] (if (get p) break[N]) (set p 1)) p)
    uint8_t code[] = {WASM_BLOCK_I(
        WASM_LOOP(WASM_IF(WASM_LOCAL_GET(0), WASM_BRV(i + 1, WASM_ZERO)),
                  WASM_LOCAL_SET(0, WASM_I32V_1(1))),
        WASM_ZERO)};
    Validate(i < 3, sigs.i_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, BreakNesting2) {
  for (int i = 0; i < 7; i++) {
    uint8_t code[] = {B1(WASM_LOOP(WASM_IF(WASM_ZERO, WASM_BR(i)), WASM_NOP))};
    Validate(i <= 3, sigs.v_v(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, BreakNesting3) {
  for (int i = 0; i < 7; i++) {
    // (block[1] (loop[1] (block[1] (if 0 break[N])
    uint8_t code[] = {
        WASM_BLOCK(WASM_LOOP(B1(WASM_IF(WASM_ZERO, WASM_BR(i + 1)))))};
    Validate(i < 4, sigs.v_v(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, BreaksWithMultipleTypes) {
  ExpectFailure(sigs.i_i(),
                {B2(WASM_BRV_IF_ZERO(0, WASM_I32V_1(7)), WASM_F32(7.7))});

  ExpectFailure(sigs.i_i(), {B2(WASM_BRV_IF_ZERO(0, WASM_I32V_1(7)),
                                WASM_BRV_IF_ZERO(0, WASM_F32(7.7)))});
  ExpectFailure(sigs.i_i(), {B3(WASM_BRV_IF_ZERO(0, WASM_I32V_1(8)),
                                WASM_BRV_IF_ZERO(0, WASM_I32V_1(0)),
                                WASM_BRV_IF_ZERO(0, WASM_F32(7.7)))});
  ExpectFailure(sigs.i_i(), {B3(WASM_BRV_IF_ZERO(0, WASM_I32V_1(9)),
                                WASM_BRV_IF_ZERO(0, WASM_F32(7.7)),
                                WASM_BRV_IF_ZERO(0, WASM_I32V_1(11)))});
}

TEST_F(FunctionBodyDecoderTest, BreakNesting_6_levels) {
  for (int mask = 0; mask < 64; mask++) {
    for (int i = 0; i < 14; i++) {
      uint8_t code[] = {WASM_BLOCK(WASM_BLOCK(
          WASM_BLOCK(WASM_BLOCK(WASM_BLOCK(WASM_BLOCK(WASM_BR(i)))))))};

      int depth = 6;
      int m = mask;
      for (size_t pos = 0; pos < sizeof(code) - 1; pos++) {
        if (code[pos] != kExprBlock) continue;
        if (m & 1) {
          code[pos] = kExprLoop;
          code[pos + 1] = kVoidCode;
        }
        m >>= 1;
      }

      Validate(i <= depth, sigs.v_v(), code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, Break_TypeCheck) {
  for (const FunctionSig* sig :
       {sigs.i_i(), sigs.l_l(), sigs.f_ff(), sigs.d_dd()}) {
    // unify X and X => OK
    uint8_t code[] = {WASM_BLOCK_T(
        sig->GetReturn(), WASM_IF(WASM_ZERO, WASM_BRV(0, WASM_LOCAL_GET(0))),
        WASM_LOCAL_GET(0))};
    ExpectValidates(sig, code);
  }

  // unify i32 and f32 => fail
  ExpectFailure(sigs.i_i(),
                {WASM_BLOCK_I(WASM_IF(WASM_ZERO, WASM_BRV(0, WASM_ZERO)),
                              WASM_F32(1.2))});

  // unify f64 and f64 => OK
  ExpectValidates(
      sigs.d_dd(),
      {WASM_BLOCK_D(WASM_IF(WASM_ZERO, WASM_BRV(0, WASM_LOCAL_GET(0))),
                    WASM_F64(1.2))});
}

TEST_F(FunctionBodyDecoderTest, Break_TypeCheckAll1) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType storage[] = {kValueTypes[i], kValueTypes[i], kValueTypes[j]};
      FunctionSig sig(1, 2, storage);
      uint8_t code[] = {WASM_BLOCK_T(
          sig.GetReturn(), WASM_IF(WASM_ZERO, WASM_BRV(0, WASM_LOCAL_GET(0))),
          WASM_LOCAL_GET(1))};

      Validate(IsSubtypeOf(kValueTypes[j], kValueTypes[i], module), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, Break_TypeCheckAll2) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType storage[] = {kValueTypes[i], kValueTypes[i], kValueTypes[j]};
      FunctionSig sig(1, 2, storage);
      uint8_t code[] = {WASM_IF_ELSE_T(sig.GetReturn(0), WASM_ZERO,
                                       WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)),
                                       WASM_LOCAL_GET(1))};

      Validate(IsSubtypeOf(kValueTypes[j], kValueTypes[i], module), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, Break_TypeCheckAll3) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType storage[] = {kValueTypes[i], kValueTypes[i], kValueTypes[j]};
      FunctionSig sig(1, 2, storage);
      uint8_t code[] = {WASM_IF_ELSE_T(sig.GetReturn(), WASM_ZERO,
                                       WASM_LOCAL_GET(1),
                                       WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))};

      Validate(IsSubtypeOf(kValueTypes[j], kValueTypes[i], module), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, Break_Unify) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < arraysize(kValueTypes); i++) {
      ValueType type = kValueTypes[i];
      ValueType storage[] = {kWasmI32, kWasmI32, type};
      FunctionSig sig(1, 2, storage);

      uint8_t code1[] = {WASM_BLOCK_T(
          type, WASM_IF(WASM_ZERO, WASM_BRV(1, WASM_LOCAL_GET(which))),
          WASM_LOCAL_GET(which ^ 1))};

      Validate(type == kWasmI32, &sig, code1);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, BreakIf_cond_type) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType types[] = {kValueTypes[i], kValueTypes[i], kValueTypes[j]};
      FunctionSig sig(1, 2, types);
      uint8_t code[] = {WASM_BLOCK_T(
          types[0], WASM_BRV_IF(0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)))};

      Validate(types[2] == kWasmI32, &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, BreakIf_val_type) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    for (size_t j = 0; j < arraysize(kValueTypes); j++) {
      ValueType types[] = {kValueTypes[i], kValueTypes[i], kValueTypes[j],
                           kWasmI32};
      FunctionSig sig(1, 3, types);
      uint8_t code[] = {WASM_BLOCK_T(
          types[1], WASM_BRV_IF(0, WASM_LOCAL_GET(1), WASM_LOCAL_GET(2)),
          WASM_DROP, WASM_LOCAL_GET(0))};

      Validate(IsSubtypeOf(kValueTypes[j], kValueTypes[i], module), &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, BreakIf_Unify) {
  for (int which = 0; which < 2; which++) {
    for (size_t i = 0; i < arraysize(kValueTypes); i++) {
      ValueType type = kValueTypes[i];
      ValueType storage[] = {kWasmI32, kWasmI32, type};
      FunctionSig sig(1, 2, storage);
      uint8_t code[] = {WASM_BLOCK_I(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(which)),
                                     WASM_DROP, WASM_LOCAL_GET(which ^ 1))};

      Validate(type == kWasmI32, &sig, code);
    }
  }
}

TEST_F(FunctionBodyDecoderTest, BrTable0) {
  ExpectFailure(sigs.v_v(), {kExprBrTable, 0, BR_TARGET(0)});
}

TEST_F(FunctionBodyDecoderTest, BrTable0b) {
  static uint8_t code[] = {kExprI32Const, 11, kExprBrTable, 0, BR_TARGET(0)};
  ExpectValidates(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, BrTable0c) {
  static uint8_t code[] = {kExprI32Const, 11, kExprBrTable, 0, BR_TARGET(1)};
  ExpectFailure(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
}

TEST_F(FunctionBodyDecoderTest, BrTable1a) {
  ExpectValidates(sigs.v_v(),
                  {B1(WASM_BR_TABLE(WASM_I32V_2(67), 0, BR_TARGET(0)))});
}

TEST_F(FunctionBodyDecoderTest, BrTable1b) {
  static uint8_t code[] = {B1(WASM_BR_TABLE(WASM_ZERO, 0, BR_TARGET(0)))};
  ExpectValidates(sigs.v_v(), code);
  ExpectFailure(sigs.i_i(), code);
  ExpectFailure(sigs.f_ff(), code);
  ExpectFailure(sigs.d_dd(), code);
}

TEST_F(FunctionBodyDecoderTest, BrTable2a) {
  ExpectValidates(
      sigs.v_v(),
      {B1(WASM_BR_TABLE(WASM_I32V_2(67), 1, BR_TARGET(0), BR_TARGET(0)))});
}

TEST_F(FunctionBodyDecoderTest, BrTable2b) {
  ExpectValidates(sigs.v_v(),
                  {WASM_BLOCK(WASM_BLOCK(WASM_BR_TABLE(
                      WASM_I32V_2(67), 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTableSubtyping) {
  uint8_t supertype1 = builder.AddStruct({F(kWasmI8, true)});
  uint8_t supertype2 =
      builder.AddStruct({F(kWasmI8, true), F(kWasmI16, false)}, supertype1);
  uint8_t subtype = builder.AddStruct(
      {F(kWasmI8, true), F(kWasmI16, false), F(kWasmI32, true)}, supertype2);
  ExpectValidates(
      sigs.v_v(),
      {WASM_BLOCK_R(wasm::ValueType::Ref(supertype1),
                    WASM_BLOCK_R(wasm::ValueType::Ref(supertype2),
                                 WASM_STRUCT_NEW(subtype, WASM_I32V(10),
                                                 WASM_I32V(20), WASM_I32V(30)),
                                 WASM_BR_TABLE(WASM_I32V(5), 1, BR_TARGET(0),
                                               BR_TARGET(1))),
                    WASM_UNREACHABLE),
       WASM_DROP});
}

TEST_F(FunctionBodyDecoderTest, BrTable_off_end) {
  static uint8_t code[] = {
      B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 0, BR_TARGET(0)))};
  for (size_t len = 1; len < sizeof(code); len++) {
    ExpectFailure(sigs.i_i(), base::VectorOf(code, len), kAppendEnd);
    ExpectFailure(sigs.i_i(), base::VectorOf(code, len), kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, BrTable_invalid_br1) {
  for (int depth = 0; depth < 4; depth++) {
    uint8_t code[] = {
        B1(WASM_BR_TABLE(WASM_LOCAL_GET(0), 0, BR_TARGET(depth)))};
    Validate(depth <= 1, sigs.v_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, BrTable_invalid_br2) {
  for (int depth = 0; depth < 7; depth++) {
    uint8_t code[] = {
        WASM_LOOP(WASM_BR_TABLE(WASM_LOCAL_GET(0), 0, BR_TARGET(depth)))};
    Validate(depth < 2, sigs.v_i(), code);
  }
}

TEST_F(FunctionBodyDecoderTest, BrTable_arity_mismatch1) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK(WASM_BLOCK_I(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_arity_mismatch2) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK_I(WASM_BLOCK(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_arity_mismatch_loop1) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_LOOP(WASM_BLOCK_I(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_arity_mismatch_loop2) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK_I(WASM_LOOP(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_loop_block) {
  ExpectValidates(
      sigs.v_v(),
      {WASM_LOOP(WASM_BLOCK(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_block_loop) {
  ExpectValidates(
      sigs.v_v(),
      {WASM_LOOP(WASM_BLOCK(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_type_mismatch1) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK_I(WASM_BLOCK_F(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_type_mismatch2) {
  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK_F(WASM_BLOCK_I(
          WASM_ONE, WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrTable_type_mismatch_unreachable) {
  ExpectFailure(sigs.v_v(),
                {WASM_BLOCK_F(WASM_BLOCK_I(
                    WASM_UNREACHABLE,
                    WASM_BR_TABLE(WASM_ONE, 1, BR_TARGET(0), BR_TARGET(1))))});
}

TEST_F(FunctionBodyDecoderTest, BrUnreachable1) {
  ExpectValidates(sigs.v_i(),
                  {WASM_LOCAL_GET(0), kExprBrTable, 0, BR_TARGET(0)});
}

TEST_F(FunctionBodyDecoderTest, BrUnreachable2) {
  ExpectValidates(sigs.v_i(),
                  {WASM_LOCAL_GET(0), kExprBrTable, 0, BR_TARGET(0), WASM_NOP});
  ExpectFailure(sigs.v_i(),
                {WASM_LOCAL_GET(0), kExprBrTable, 0, BR_TARGET(0), WASM_ZERO});
}

TEST_F(FunctionBodyDecoderTest, Brv1) {
  ExpectValidates(sigs.i_i(), {WASM_BLOCK_I(WASM_BRV(0, WASM_ZERO))});
  ExpectValidates(sigs.i_i(),
                  {WASM_BLOCK_I(WASM_LOOP_I(WASM_BRV(2, WASM_ZERO)))});
}

TEST_F(FunctionBodyDecoderTest, Brv1_type) {
  ExpectValidates(sigs.i_ii(), {WASM_BLOCK_I(WASM_BRV(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.l_ll(), {WASM_BLOCK_L(WASM_BRV(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.f_ff(), {WASM_BLOCK_F(WASM_BRV(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.d_dd(), {WASM_BLOCK_D(WASM_BRV(0, WASM_LOCAL_GET(0)))});
}

TEST_F(FunctionBodyDecoderTest, Brv1_type_n) {
  ExpectFailure(sigs.i_f(), {WASM_BLOCK_I(WASM_BRV(0, WASM_LOCAL_GET(0)))});
  ExpectFailure(sigs.i_d(), {WASM_BLOCK_I(WASM_BRV(0, WASM_LOCAL_GET(0)))});
}

TEST_F(FunctionBodyDecoderTest, BrvIf1) {
  ExpectValidates(sigs.i_v(), {WASM_BLOCK_I(WASM_BRV_IF_ZERO(0, WASM_ZERO))});
}

TEST_F(FunctionBodyDecoderTest, BrvIf1_type) {
  ExpectValidates(sigs.i_i(),
                  {WASM_BLOCK_I(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.l_l(),
                  {WASM_BLOCK_L(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.f_ff(),
                  {WASM_BLOCK_F(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
  ExpectValidates(sigs.d_dd(),
                  {WASM_BLOCK_D(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
}

TEST_F(FunctionBodyDecoderTest, BrvIf1_type_n) {
  ExpectFailure(sigs.i_f(),
                {WASM_BLOCK_I(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
  ExpectFailure(sigs.i_d(),
                {WASM_BLOCK_I(WASM_BRV_IF_ZERO(0, WASM_LOCAL_GET(0)))});
}

TEST_F(FunctionBodyDecoderTest, Select) {
  ExpectValidates(sigs.i_i(), {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                           WASM_ZERO)});
  ExpectValidates(sigs.f_ff(),
                  {WASM_SELECT(WASM_F32(0.0), WASM_F32(0.0), WASM_ZERO)});
  ExpectValidates(sigs.d_dd(),
                  {WASM_SELECT(WASM_F64(0.0), WASM_F64(0.0), WASM_ZERO)});
  ExpectValidates(sigs.l_l(),
                  {WASM_SELECT(WASM_I64V_1(0), WASM_I64V_1(0), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Select_needs_value_type) {
  ExpectFailure(sigs.a_a(),
                {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_ZERO)});
  ExpectFailure(sigs.c_c(),
                {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Select_fail1) {
  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_F32(0.0), WASM_LOCAL_GET(0),
                                         WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_LOCAL_GET(0), WASM_F32(0.0),
                                         WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                         WASM_F32(0.0))});
}

TEST_F(FunctionBodyDecoderTest, Select_fail2) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType type = kValueTypes[i];
    if (type == kWasmI32) continue;
    // Select without specified type is only allowed for number types.
    if (type == kWasmExternRef) continue;

    ValueType types[] = {type, kWasmI32, type};
    FunctionSig sig(1, 2, types);

    ExpectValidates(&sig, {WASM_SELECT(WASM_LOCAL_GET(1), WASM_LOCAL_GET(1),
                                       WASM_LOCAL_GET(0))});

    ExpectFailure(&sig, {WASM_SELECT(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0),
                                     WASM_LOCAL_GET(0))});

    ExpectFailure(&sig, {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                     WASM_LOCAL_GET(0))});

    ExpectFailure(&sig, {WASM_SELECT(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     WASM_LOCAL_GET(1))});
  }
}

TEST_F(FunctionBodyDecoderTest, Select_TypeCheck) {
  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_F32(9.9), WASM_LOCAL_GET(0),
                                         WASM_LOCAL_GET(0))});

  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_LOCAL_GET(0), WASM_F64(0.25),
                                         WASM_LOCAL_GET(0))});

  ExpectFailure(sigs.i_i(), {WASM_SELECT(WASM_F32(9.9), WASM_LOCAL_GET(0),
                                         WASM_I64V_1(0))});
}

TEST_F(FunctionBodyDecoderTest, SelectWithType) {
  ExpectValidates(sigs.i_i(), {WASM_SELECT_I(WASM_LOCAL_GET(0),
                                             WASM_LOCAL_GET(0), WASM_ZERO)});
  ExpectValidates(sigs.f_ff(),
                  {WASM_SELECT_F(WASM_F32(0.0), WASM_F32(0.0), WASM_ZERO)});
  ExpectValidates(sigs.d_dd(),
                  {WASM_SELECT_D(WASM_F64(0.0), WASM_F64(0.0), WASM_ZERO)});
  ExpectValidates(sigs.l_l(),
                  {WASM_SELECT_L(WASM_I64V_1(0), WASM_I64V_1(0), WASM_ZERO)});
  ExpectValidates(sigs.a_a(),
                  {WASM_SELECT_R(WASM_REF_NULL(kExternRefCode),
                                 WASM_REF_NULL(kExternRefCode), WASM_ZERO)});
  ExpectValidates(sigs.c_c(),
                  {WASM_SELECT_A(WASM_REF_NULL(kFuncRefCode),
                                 WASM_REF_NULL(kFuncRefCode), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, SelectWithType_fail) {
  ExpectFailure(sigs.i_i(), {WASM_SELECT_F(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                           WASM_ZERO)});
  ExpectFailure(sigs.f_ff(),
                {WASM_SELECT_D(WASM_F32(0.0), WASM_F32(0.0), WASM_ZERO)});
  ExpectFailure(sigs.d_dd(),
                {WASM_SELECT_L(WASM_F64(0.0), WASM_F64(0.0), WASM_ZERO)});
  ExpectFailure(sigs.l_l(),
                {WASM_SELECT_I(WASM_I64V_1(0), WASM_I64V_1(0), WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, Throw) {
  uint8_t ex1 = builder.AddException(sigs.v_v());
  uint8_t ex2 = builder.AddException(sigs.v_i());
  uint8_t ex3 = builder.AddException(sigs.v_ii());
  ExpectValidates(sigs.v_v(), {kExprThrow, ex1});
  ExpectValidates(sigs.v_v(), {WASM_I32V(0), kExprThrow, ex2});
  ExpectFailure(sigs.v_v(), {WASM_F32(0.0), kExprThrow, ex2});
  ExpectValidates(sigs.v_v(), {WASM_I32V(0), WASM_I32V(0), kExprThrow, ex3});
  ExpectFailure(sigs.v_v(), {WASM_F32(0.0), WASM_I32V(0), kExprThrow, ex3});
  ExpectFailure(sigs.v_v(), {kExprThrow, 99});
}

TEST_F(FunctionBodyDecoderTest, ThrowUnreachable) {
  uint8_t ex1 = builder.AddException(sigs.v_v());
  uint8_t ex2 = builder.AddException(sigs.v_i());
  ExpectValidates(sigs.i_i(), {WASM_LOCAL_GET(0), kExprThrow, ex1, WASM_NOP});
  ExpectValidates(sigs.v_i(), {WASM_LOCAL_GET(0), kExprThrow, ex2, WASM_NOP});
  ExpectValidates(sigs.i_i(), {WASM_LOCAL_GET(0), kExprThrow, ex1, WASM_ZERO});
  ExpectFailure(sigs.v_i(), {WASM_LOCAL_GET(0), kExprThrow, ex2, WASM_ZERO});
  ExpectFailure(sigs.i_i(),
                {WASM_LOCAL_GET(0), kExprThrow, ex1, WASM_F32(0.0)});
  ExpectFailure(sigs.v_i(),
                {WASM_LOCAL_GET(0), kExprThrow, ex2, WASM_F32(0.0)});
}

#define WASM_TRY_OP kExprTry, kVoidCode

TEST_F(FunctionBodyDecoderTest, TryCatch) {
  WASM_FEATURE_SCOPE(legacy_eh);
  uint8_t ex = builder.AddException(sigs.v_v());
  ExpectValidates(sigs.v_v(), {WASM_TRY_OP, kExprCatch, ex, kExprEnd});
  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_OP, kExprCatch, ex, kExprCatchAll, kExprEnd});
  ExpectValidates(sigs.v_v(), {WASM_TRY_OP, kExprEnd}, kAppendEnd);
  ExpectFailure(sigs.v_v(),
                {WASM_TRY_OP, kExprCatchAll, kExprCatch, ex, kExprEnd},
                kAppendEnd, "catch after catch-all for try");
  ExpectFailure(sigs.v_v(),
                {WASM_TRY_OP, kExprCatchAll, kExprCatchAll, kExprEnd},
                kAppendEnd, "catch-all already present for try");
  ExpectFailure(sigs.v_v(), {kExprCatch, ex, kExprEnd}, kAppendEnd,
                "catch does not match a try");
}

TEST_F(FunctionBodyDecoderTest, Rethrow) {
  WASM_FEATURE_SCOPE(legacy_eh);

  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_OP, kExprCatchAll, kExprRethrow, 0, kExprEnd});
  ExpectFailure(sigs.v_v(),
                {WASM_TRY_OP, kExprRethrow, 0, kExprCatch, kExprEnd},
                kAppendEnd, "rethrow not targeting catch or catch-all");
  ExpectFailure(sigs.v_v(), {WASM_BLOCK(kExprRethrow, 0)}, kAppendEnd,
                "rethrow not targeting catch or catch-all");
  ExpectFailure(sigs.v_v(), {kExprRethrow, 0}, kAppendEnd,
                "rethrow not targeting catch or catch-all");
}

TEST_F(FunctionBodyDecoderTest, TryDelegate) {
  WASM_FEATURE_SCOPE(legacy_eh);
  uint8_t ex = builder.AddException(sigs.v_v());

  ExpectValidates(sigs.v_v(), {WASM_TRY_OP,
                               WASM_TRY_DELEGATE(WASM_STMTS(kExprThrow, ex), 0),
                               kExprCatch, ex, kExprEnd});
  ExpectValidates(
      sigs.v_v(),
      {WASM_BLOCK(WASM_TRY_OP, WASM_TRY_DELEGATE(WASM_STMTS(kExprThrow, ex), 2),
                  kExprCatch, ex, kExprEnd)});
  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_OP, kExprCatch, ex,
                   WASM_TRY_DELEGATE(WASM_STMTS(kExprThrow, ex), 0), kExprEnd},
                  kAppendEnd);
  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_OP,
                   WASM_BLOCK(WASM_TRY_DELEGATE(WASM_STMTS(kExprThrow, ex), 0)),
                   kExprCatch, ex, kExprEnd},
                  kAppendEnd);

  ExpectFailure(
      sigs.v_v(),
      {WASM_BLOCK(WASM_TRY_OP, WASM_TRY_DELEGATE(WASM_STMTS(kExprThrow, ex), 3),
                  kExprCatch, ex, kExprEnd)},
      kAppendEnd, "invalid branch depth: 3");
  ExpectFailure(
      sigs.v_v(),
      {WASM_TRY_OP, WASM_TRY_OP, kExprCatch, ex, kExprDelegate, 0, kExprEnd},
      kAppendEnd, "delegate does not match a try");
  ExpectFailure(
      sigs.v_v(),
      {WASM_TRY_OP, WASM_TRY_OP, kExprCatchAll, kExprDelegate, 1, kExprEnd},
      kAppendEnd, "delegate does not match a try");
}

#undef WASM_TRY_OP

#define WASM_TRY_TABLE_OP kExprTryTable, kVoidCode

TEST_F(FunctionBodyDecoderTest, ThrowRef) {
  WASM_FEATURE_SCOPE(exnref);
  ExpectValidates(sigs.v_v(), {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP,
                               U32V_1(1), CatchKind::kCatchAllRef, 0, kExprEnd,
                               kExprBr, 1, kExprEnd, kExprThrowRef});
  ExpectFailure(sigs.v_v(),
                {WASM_REF_NULL(WASM_HEAP_TYPE(HeapType(HeapType::kExtern))),
                 kExprThrowRef},
                kAppendEnd,
                "invalid type for throw_ref: expected exnref, found externref");
}

TEST_F(FunctionBodyDecoderTest, TryTable) {
  WASM_FEATURE_SCOPE(exnref);
  uint8_t ex = builder.AddException(sigs.v_v());
  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_TABLE_OP, U32V_1(1), CatchKind::kCatch, ex,
                   U32V_1(0), kExprEnd},
                  kAppendEnd);
  ExpectValidates(sigs.v_v(),
                  {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP, U32V_1(1),
                   CatchKind::kCatchRef, ex, U32V_1(0), kExprEnd,
                   kExprUnreachable, kExprEnd, kExprDrop},
                  kAppendEnd);
  ExpectValidates(sigs.v_v(),
                  {WASM_TRY_TABLE_OP, U32V_1(1), CatchKind::kCatchAll,
                   U32V_1(0), kExprEnd, kExprUnreachable},
                  kAppendEnd);
  ExpectValidates(sigs.v_v(),
                  {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP, U32V_1(1),
                   CatchKind::kCatchAllRef, U32V_1(0), kExprEnd,
                   kExprUnreachable, kExprEnd, kExprDrop},
                  kAppendEnd);
  // All catch kinds at the same time.
  ExpectValidates(
      sigs.v_v(),
      {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP, U32V_1(4), CatchKind::kCatch,
       ex, U32V_1(1), CatchKind::kCatchRef, ex, U32V_1(0), CatchKind::kCatchAll,
       U32V_1(1), CatchKind::kCatchAllRef, U32V_1(0), kExprEnd,
       kExprUnreachable, kExprEnd, kExprDrop},
      kAppendEnd);
  // // Duplicate catch-all.
  ExpectValidates(
      sigs.v_v(),
      {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP, U32V_1(4),
       CatchKind::kCatchAll, U32V_1(1), CatchKind::kCatchAll, U32V_1(1),
       CatchKind::kCatchAllRef, U32V_1(0), CatchKind::kCatchAllRef, U32V_1(0),
       kExprEnd, kExprUnreachable, kExprEnd, kExprDrop},
      kAppendEnd);
  // // Catch-all before catch.
  ExpectValidates(
      sigs.v_v(),
      {WASM_TRY_TABLE_OP, U32V_1(2), CatchKind::kCatchAll, U32V_1(0),
       CatchKind::kCatch, ex, U32V_1(0), kExprEnd, kExprUnreachable},
      kAppendEnd);

  constexpr uint8_t kInvalidCatchKind = kLastCatchKind + 1;
  ExpectFailure(sigs.v_v(),
                {WASM_TRY_TABLE_OP, U32V_1(1), kInvalidCatchKind, ex, U32V_1(0),
                 kExprEnd},
                kAppendEnd, "invalid catch kind in try table");
  // Branching to an exnref block with ref-less catch.
  ExpectFailure(sigs.v_v(),
                {kExprBlock, kExnRefCode, WASM_TRY_TABLE_OP, U32V_1(1), kCatch,
                 ex, U32V_1(0), kExprEnd, kExprUnreachable, kExprEnd},
                kAppendEnd,
                "catch kind generates 0 operands, target block expects 1");
  // Branching to a void block with catch-ref.
  ExpectFailure(sigs.v_v(),
                {kExprBlock, kVoidCode, WASM_TRY_TABLE_OP, U32V_1(1), kCatchRef,
                 ex, U32V_1(0), kExprEnd, kExprUnreachable, kExprEnd},
                kAppendEnd,
                "catch kind generates 1 operand, target block expects 0");
}

TEST_F(FunctionBodyDecoderTest, MultiValBlock1) {
  uint8_t sig0 = builder.AddSignature(sigs.ii_v());
  ExpectValidates(
      sigs.i_ii(),
      {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprI32Add});
  ExpectFailure(sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_NOP), kExprI32Add},
                kAppendEnd,
                "expected 2 elements on the stack for fallthru, found 0");
  ExpectFailure(
      sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0)), kExprI32Add},
      kAppendEnd, "expected 2 elements on the stack for fallthru, found 1");
  ExpectFailure(sigs.i_ii(),
                {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                              WASM_LOCAL_GET(0)),
                 kExprI32Add},
                kAppendEnd,
                "expected 2 elements on the stack for fallthru, found 3");
  ExpectFailure(
      sigs.i_ii(),
      {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprF32Add},
      kAppendEnd, "f32.add[0] expected type f32, found block of type i32");

  uint8_t sig1 = builder.AddSignature(sigs.v_i());
  ExpectFailure(
      sigs.v_i(),
      {WASM_LOCAL_GET(0), WASM_BLOCK(WASM_BLOCK_X(sig1, WASM_UNREACHABLE))},
      kAppendEnd,
      "not enough arguments on the stack for block (need 1, got 0)");
}

TEST_F(FunctionBodyDecoderTest, MultiValBlock2) {
  uint8_t sig0 = builder.AddSignature(sigs.ii_v());
  ExpectValidates(sigs.i_ii(),
                  {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                   WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_NOP),
                              WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0)),
                              WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(),
                {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                              WASM_LOCAL_GET(0)),
                 WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(),
                {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                 WASM_F32_ADD(WASM_NOP, WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, MultiValBlockBr) {
  uint8_t sig0 = builder.AddSignature(sigs.ii_v());
  ExpectFailure(sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0), WASM_BR(0)),
                              kExprI32Add});
  ExpectValidates(sigs.i_ii(), {WASM_BLOCK_X(sig0, WASM_LOCAL_GET(0),
                                             WASM_LOCAL_GET(1), WASM_BR(0)),
                                kExprI32Add});
}

TEST_F(FunctionBodyDecoderTest, MultiValLoop1) {
  uint8_t sig0 = builder.AddSignature(sigs.ii_v());
  ExpectValidates(
      sigs.i_ii(),
      {WASM_LOOP_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprI32Add});
  ExpectFailure(sigs.i_ii(), {WASM_LOOP_X(sig0, WASM_NOP), kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_LOOP_X(sig0, WASM_LOCAL_GET(0)), kExprI32Add});
  ExpectFailure(sigs.i_ii(), {WASM_LOOP_X(sig0, WASM_LOCAL_GET(0),
                                          WASM_LOCAL_GET(1), WASM_LOCAL_GET(0)),
                              kExprI32Add});
  ExpectFailure(
      sigs.i_ii(),
      {WASM_LOOP_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprF32Add});
}

TEST_F(FunctionBodyDecoderTest, MultiValIf) {
  uint8_t sig0 = builder.AddSignature(sigs.ii_v());
  ExpectValidates(
      sigs.i_ii(),
      {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                      WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                      WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))),
       kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0), WASM_NOP, WASM_NOP),
                 kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0), WASM_NOP,
                                WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))),
                 kExprI32Add});
  ExpectFailure(
      sigs.i_ii(),
      {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                      WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_NOP),
       kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                WASM_LOCAL_GET(1)),
                 kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))),
                 kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                                WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                                WASM_LOCAL_GET(1)),
                 kExprI32Add});
  ExpectFailure(
      sigs.i_ii(),
      {WASM_IF_ELSE_X(
           sig0, WASM_LOCAL_GET(0),
           WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0), WASM_LOCAL_GET(0)),
           WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))),
       kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                                WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                         WASM_LOCAL_GET(0)),
                                WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))),
                 kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                                WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                                WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(1),
                                         WASM_LOCAL_GET(1))),
                 kExprI32Add});
  ExpectFailure(sigs.i_ii(),
                {WASM_IF_ELSE_X(sig0, WASM_LOCAL_GET(0),
                                WASM_SEQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)),
                                WASM_SEQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))),
                 kExprF32Add});
}

TEST_F(FunctionBodyDecoderTest, BlockParam) {
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());
  ExpectValidates(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_BLOCK_X(sig1, WASM_LOCAL_GET(1),
                                       WASM_I32_ADD(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                   WASM_BLOCK_X(sig2, WASM_I32_ADD(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.i_ii(), {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                WASM_BLOCK_X(sig1, WASM_NOP),
                                WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(),
                {WASM_BLOCK_X(sig1, WASM_NOP), WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_ii(), {WASM_BLOCK_X(sig1, WASM_LOCAL_GET(0)),
                              WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_BLOCK_X(sig2, WASM_I32_ADD(WASM_NOP, WASM_NOP)),
       WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_ii(),
                {WASM_LOCAL_GET(0), WASM_BLOCK_X(sig1, WASM_F32_NEG(WASM_NOP)),
                 WASM_RETURN(WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, LoopParam) {
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());
  ExpectValidates(sigs.i_ii(), {WASM_LOCAL_GET(0),
                                WASM_LOOP_X(sig1, WASM_LOCAL_GET(1),
                                            WASM_I32_ADD(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                   WASM_LOOP_X(sig2, WASM_I32_ADD(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.i_ii(), {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                WASM_LOOP_X(sig1, WASM_NOP),
                                WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectFailure(sigs.i_ii(),
                {WASM_LOOP_X(sig1, WASM_NOP), WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_ii(), {WASM_LOOP_X(sig1, WASM_LOCAL_GET(0)),
                              WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_LOOP_X(sig2, WASM_I32_ADD(WASM_NOP, WASM_NOP)),
       WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_ii(),
                {WASM_LOCAL_GET(0), WASM_LOOP_X(sig1, WASM_F32_NEG(WASM_NOP)),
                 WASM_RETURN(WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, LoopParamBr) {
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0), WASM_LOOP_X(sig1, WASM_BR(0))});
  ExpectValidates(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_LOOP_X(sig1, WASM_BRV(0, WASM_LOCAL_GET(1)))});
  ExpectValidates(sigs.i_ii(), {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                WASM_LOOP_X(sig2, WASM_BR(0))});
  ExpectValidates(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_LOOP_X(sig1, WASM_BLOCK_X(sig1, WASM_BR(1)))});
  ExpectFailure(sigs.i_ii(),
                {WASM_LOCAL_GET(0), WASM_LOOP_X(sig1, WASM_BLOCK(WASM_BR(1))),
                 WASM_RETURN(WASM_LOCAL_GET(0))});
  ExpectFailure(sigs.i_ii(), {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                              WASM_LOOP_X(sig2, WASM_BLOCK_X(sig1, WASM_BR(1))),
                              WASM_RETURN(WASM_LOCAL_GET(0))});
}

TEST_F(FunctionBodyDecoderTest, IfParam) {
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0),
                   WASM_IF_X(sig1, WASM_LOCAL_GET(0),
                             WASM_I32_ADD(WASM_NOP, WASM_LOCAL_GET(1)))});
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0),
                   WASM_IF_ELSE_X(sig1, WASM_LOCAL_GET(0),
                                  WASM_I32_ADD(WASM_NOP, WASM_LOCAL_GET(1)),
                                  WASM_I32_EQZ(WASM_NOP))});
  ExpectValidates(
      sigs.i_ii(),
      {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
       WASM_IF_ELSE_X(sig2, WASM_LOCAL_GET(0), WASM_I32_ADD(WASM_NOP, WASM_NOP),
                      WASM_I32_MUL(WASM_NOP, WASM_NOP))});
  ExpectValidates(sigs.i_ii(), {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                WASM_IF_X(sig1, WASM_LOCAL_GET(0), WASM_NOP),
                                WASM_I32_ADD(WASM_NOP, WASM_NOP)});
  ExpectValidates(sigs.i_ii(),
                  {WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                   WASM_IF_ELSE_X(sig1, WASM_LOCAL_GET(0), WASM_NOP,
                                  WASM_I32_EQZ(WASM_NOP)),
                   WASM_I32_ADD(WASM_NOP, WASM_NOP)});
}

TEST_F(FunctionBodyDecoderTest, Regression709741) {
  AddLocals(kWasmI32, kV8MaxWasmFunctionLocals - 1);
  ExpectValidates(sigs.v_v(), {WASM_NOP});
  uint8_t code[] = {WASM_NOP, WASM_END};

  for (size_t i = 0; i < arraysize(code); ++i) {
    constexpr bool kIsShared = false;
    FunctionBody body(sigs.v_v(), 0, code, code + i, kIsShared);
    WasmDetectedFeatures unused_detected_features;
    DecodeResult result =
        ValidateFunctionBody(this->zone(), WasmEnabledFeatures::All(), module,
                             &unused_detected_features, body);
    if (result.ok()) {
      std::ostringstream str;
      str << "Expected verification to fail";
    }
  }
}

TEST_F(FunctionBodyDecoderTest, MemoryInit) {
  builder.AddMemory();
  builder.SetDataSegmentCount(1);

  ExpectValidates(sigs.v_v(),
                  {WASM_MEMORY_INIT(0, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectFailure(sigs.v_v(),
                {WASM_TABLE_INIT(0, 1, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, MemoryInitInvalid) {
  builder.AddMemory();
  builder.SetDataSegmentCount(1);

  uint8_t code[] = {WASM_MEMORY_INIT(0, WASM_ZERO, WASM_ZERO, WASM_ZERO),
                    WASM_END};
  for (size_t i = 0; i <= arraysize(code); ++i) {
    Validate(i == arraysize(code), sigs.v_v(), base::VectorOf(code, i),
             kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, DataDrop) {
  builder.AddMemory();
  builder.SetDataSegmentCount(1);

  ExpectValidates(sigs.v_v(), {WASM_DATA_DROP(0)});
  ExpectFailure(sigs.v_v(), {WASM_DATA_DROP(1)});
}

TEST_F(FunctionBodyDecoderTest, DataSegmentIndexUnsigned) {
  builder.AddMemory();
  builder.SetDataSegmentCount(65);

  // Make sure that the index is interpreted as an unsigned number; 64 is
  // interpreted as -64 when decoded as a signed LEB.
  ExpectValidates(sigs.v_v(),
                  {WASM_MEMORY_INIT(64, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectValidates(sigs.v_v(), {WASM_DATA_DROP(64)});
}

TEST_F(FunctionBodyDecoderTest, MemoryCopy) {
  builder.AddMemory();

  ExpectValidates(sigs.v_v(),
                  {WASM_MEMORY0_COPY(WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, MemoryFill) {
  builder.AddMemory();

  ExpectValidates(sigs.v_v(),
                  {WASM_MEMORY_FILL(WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, BulkMemoryOpsWithoutMemory) {
  ExpectFailure(sigs.v_v(),
                {WASM_MEMORY_INIT(0, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectFailure(sigs.v_v(),
                {WASM_MEMORY0_COPY(WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectFailure(sigs.v_v(),
                {WASM_MEMORY_FILL(WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableInit) {
  builder.AddTable(wasm::kWasmFuncRef);
  builder.AddPassiveElementSegment(wasm::kWasmFuncRef);

  ExpectValidates(sigs.v_v(),
                  {WASM_TABLE_INIT(0, 0, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectFailure(sigs.v_v(),
                {WASM_TABLE_INIT(0, 1, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableInitWrongType) {
  uint32_t table_index = builder.AddTable(wasm::kWasmFuncRef);
  uint32_t element_index =
      builder.AddPassiveElementSegment(wasm::kWasmExternRef);
  ExpectFailure(sigs.v_v(), {WASM_TABLE_INIT(table_index, element_index,
                                             WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableInitInvalid) {
  builder.AddTable(wasm::kWasmFuncRef);
  builder.AddPassiveElementSegment(wasm::kWasmFuncRef);

  uint8_t code[] = {WASM_TABLE_INIT(0, 0, WASM_ZERO, WASM_ZERO, WASM_ZERO),
                    WASM_END};
  for (size_t i = 0; i <= arraysize(code); ++i) {
    Validate(i == arraysize(code), sigs.v_v(), base::VectorOf(code, i),
             kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, ElemDrop) {
  builder.AddTable(wasm::kWasmFuncRef);
  builder.AddPassiveElementSegment(wasm::kWasmFuncRef);

  ExpectValidates(sigs.v_v(), {WASM_ELEM_DROP(0)});
  ExpectFailure(sigs.v_v(), {WASM_ELEM_DROP(1)});
}

TEST_F(FunctionBodyDecoderTest, TableInitDeclarativeElem) {
  builder.AddTable(wasm::kWasmFuncRef);
  builder.AddDeclarativeElementSegment();
  uint8_t code[] = {WASM_TABLE_INIT(0, 0, WASM_ZERO, WASM_ZERO, WASM_ZERO),
                    WASM_END};
  for (size_t i = 0; i <= arraysize(code); ++i) {
    Validate(i == arraysize(code), sigs.v_v(), base::VectorOf(code, i),
             kOmitEnd);
  }
}

TEST_F(FunctionBodyDecoderTest, DeclarativeElemDrop) {
  builder.AddTable(wasm::kWasmFuncRef);
  builder.AddDeclarativeElementSegment();
  ExpectValidates(sigs.v_v(), {WASM_ELEM_DROP(0)});
  ExpectFailure(sigs.v_v(), {WASM_ELEM_DROP(1)});
}

TEST_F(FunctionBodyDecoderTest, RefFuncDeclared) {
  uint8_t function_index = builder.AddFunction(sigs.v_i());
  ExpectValidates(sigs.c_v(), {WASM_REF_FUNC(function_index)});
}

TEST_F(FunctionBodyDecoderTest, RefFuncUndeclared) {
  uint8_t function_index = builder.AddFunction(sigs.v_i(), false);
  ExpectFailure(sigs.c_v(), {WASM_REF_FUNC(function_index)});
}

TEST_F(FunctionBodyDecoderTest, ElemSegmentIndexUnsigned) {
  builder.AddTable(wasm::kWasmFuncRef);
  for (int i = 0; i < 65; ++i) {
    builder.AddPassiveElementSegment(wasm::kWasmFuncRef);
  }

  // Make sure that the index is interpreted as an unsigned number; 64 is
  // interpreted as -64 when decoded as a signed LEB.
  ExpectValidates(sigs.v_v(),
                  {WASM_TABLE_INIT(0, 64, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectValidates(sigs.v_v(), {WASM_ELEM_DROP(64)});
}

TEST_F(FunctionBodyDecoderTest, TableCopy) {
  uint8_t table_index = builder.AddTable(wasm::kWasmVoid);

  ExpectValidates(sigs.v_v(),
                  {WASM_TABLE_COPY(table_index, table_index, WASM_ZERO,
                                   WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableCopyWrongType) {
  uint8_t dst_table_index = builder.AddTable(wasm::kWasmFuncRef);
  uint8_t src_table_index = builder.AddTable(wasm::kWasmExternRef);
  ExpectFailure(sigs.v_v(), {WASM_TABLE_COPY(dst_table_index, src_table_index,
                                             WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableGrow) {
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, 10, true, 20);
  uint8_t tab_ref = builder.AddTable(kWasmExternRef, 10, true, 20);

  ExpectValidates(
      sigs.i_c(),
      {WASM_TABLE_GROW(tab_func, WASM_REF_NULL(kFuncRefCode), WASM_ONE)});
  ExpectValidates(
      sigs.i_a(),
      {WASM_TABLE_GROW(tab_ref, WASM_REF_NULL(kExternRefCode), WASM_ONE)});
  // FuncRef table cannot be initialized with an ExternRef value.
  ExpectFailure(sigs.i_a(),
                {WASM_TABLE_GROW(tab_func, WASM_LOCAL_GET(0), WASM_ONE)});
  // ExternRef table cannot be initialized with a FuncRef value.
  ExpectFailure(sigs.i_c(),
                {WASM_TABLE_GROW(tab_ref, WASM_LOCAL_GET(0), WASM_ONE)});
  // Check that the table index gets verified.
  ExpectFailure(
      sigs.i_a(),
      {WASM_TABLE_GROW(tab_ref + 2, WASM_REF_NULL(kExternRefCode), WASM_ONE)});
}

TEST_F(FunctionBodyDecoderTest, TableSize) {
  int tab = builder.AddTable(kWasmFuncRef, 10, true, 20);
  ExpectValidates(sigs.i_v(), {WASM_TABLE_SIZE(tab)});
  ExpectFailure(sigs.i_v(), {WASM_TABLE_SIZE(tab + 2)});
}

TEST_F(FunctionBodyDecoderTest, TableFill) {
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, 10, true, 20);
  uint8_t tab_ref = builder.AddTable(kWasmExternRef, 10, true, 20);
  ExpectValidates(sigs.v_c(),
                  {WASM_TABLE_FILL(tab_func, WASM_ONE,
                                   WASM_REF_NULL(kFuncRefCode), WASM_ONE)});
  ExpectValidates(sigs.v_a(),
                  {WASM_TABLE_FILL(tab_ref, WASM_ONE,
                                   WASM_REF_NULL(kExternRefCode), WASM_ONE)});
  // FuncRef table cannot be initialized with an ExternRef value.
  ExpectFailure(sigs.v_a(), {WASM_TABLE_FILL(tab_func, WASM_ONE,
                                             WASM_LOCAL_GET(0), WASM_ONE)});
  // ExternRef table cannot be initialized with a FuncRef value.
  ExpectFailure(sigs.v_c(), {WASM_TABLE_FILL(tab_ref, WASM_ONE,
                                             WASM_LOCAL_GET(0), WASM_ONE)});
  // Check that the table index gets verified.
  ExpectFailure(sigs.v_a(),
                {WASM_TABLE_FILL(tab_ref + 2, WASM_ONE,
                                 WASM_REF_NULL(kExternRefCode), WASM_ONE)});
}

TEST_F(FunctionBodyDecoderTest, TableOpsWithoutTable) {
  ExpectFailure(sigs.i_v(),
                {WASM_TABLE_GROW(0, WASM_REF_NULL(kExternRefCode), WASM_ONE)});
  ExpectFailure(sigs.i_v(), {WASM_TABLE_SIZE(0)});
  ExpectFailure(
      sigs.i_a(),
      {WASM_TABLE_FILL(0, WASM_ONE, WASM_REF_NULL(kExternRefCode), WASM_ONE)});
  builder.AddPassiveElementSegment(wasm::kWasmFuncRef);
  ExpectFailure(sigs.v_v(),
                {WASM_TABLE_INIT(0, 0, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
  ExpectFailure(sigs.v_v(),
                {WASM_TABLE_COPY(0, 0, WASM_ZERO, WASM_ZERO, WASM_ZERO)});
}

TEST_F(FunctionBodyDecoderTest, TableCopyMultiTable) {
  {
    TestModuleBuilder builder;
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddPassiveElementSegment(wasm::kWasmFuncRef);
    module = builder.module();
    // We added one table, therefore table.copy on table 0 should work.
    int table_src = 0;
    int table_dst = 0;
    ExpectValidates(sigs.v_v(),
                    {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO, WASM_ZERO,
                                     WASM_ZERO)});
    // There is only one table, so table.copy on table 1 should fail.
    table_src = 0;
    table_dst = 1;
    ExpectFailure(sigs.v_v(), {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO,
                                               WASM_ZERO, WASM_ZERO)});
    table_src = 1;
    table_dst = 0;
    ExpectFailure(sigs.v_v(), {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO,
                                               WASM_ZERO, WASM_ZERO)});
  }
  {
    TestModuleBuilder builder;
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddPassiveElementSegment(wasm::kWasmFuncRef);
    module = builder.module();
    // We added two tables, therefore table.copy on table 0 should work.
    int table_src = 0;
    int table_dst = 0;
    ExpectValidates(sigs.v_v(),
                    {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO, WASM_ZERO,
                                     WASM_ZERO)});
    // Also table.copy on table 1 should work now.
    table_src = 1;
    table_dst = 0;
    ExpectValidates(sigs.v_v(),
                    {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO, WASM_ZERO,
                                     WASM_ZERO)});
    table_src = 0;
    table_dst = 1;
    ExpectValidates(sigs.v_v(),
                    {WASM_TABLE_COPY(table_dst, table_src, WASM_ZERO, WASM_ZERO,
                                     WASM_ZERO)});
  }
}

TEST_F(FunctionBodyDecoderTest, TableInitMultiTable) {
  {
    TestModuleBuilder builder;
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddPassiveElementSegment(wasm::kWasmExternRef);
    module = builder.module();
    // We added one table, therefore table.init on table 0 should work.
    int table_index = 0;
    ExpectValidates(sigs.v_v(), {WASM_TABLE_INIT(table_index, 0, WASM_ZERO,
                                                 WASM_ZERO, WASM_ZERO)});
    // There is only one table, so table.init on table 1 should fail.
    table_index = 1;
    ExpectFailure(sigs.v_v(), {WASM_TABLE_INIT(table_index, 0, WASM_ZERO,
                                               WASM_ZERO, WASM_ZERO)});
  }
  {
    TestModuleBuilder builder;
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddTable(kWasmExternRef, 10, true, 20);
    builder.AddPassiveElementSegment(wasm::kWasmExternRef);
    module = builder.module();
    // We added two tables, therefore table.init on table 0 should work.
    int table_index = 0;
    ExpectValidates(sigs.v_v(), {WASM_TABLE_INIT(table_index, 0, WASM_ZERO,
                                                 WASM_ZERO, WASM_ZERO)});
    // Also table.init on table 1 should work now.
    table_index = 1;
    ExpectValidates(sigs.v_v(), {WASM_TABLE_INIT(table_index, 0, WASM_ZERO,
                                                 WASM_ZERO, WASM_ZERO)});
  }
}

TEST_F(FunctionBodyDecoderTest, UnpackPackedTypes) {
  {
    TestModuleBuilder builder;
    uint8_t type_index =
        builder.AddStruct({F(kWasmI8, true), F(kWasmI16, false)});
    module = builder.module();
    ExpectValidates(sigs.v_v(),
                    {WASM_STRUCT_SET(type_index, 0,
                                     WASM_STRUCT_NEW(type_index, WASM_I32V(1),
                                                     WASM_I32V(42)),
                                     WASM_I32V(-1))});
  }
  {
    TestModuleBuilder builder;
    uint8_t type_index = builder.AddArray(kWasmI8, true);
    module = builder.module();
    ExpectValidates(
        sigs.v_v(),
        {WASM_ARRAY_SET(type_index,
                        WASM_ARRAY_NEW(type_index, WASM_I32V(10), WASM_I32V(5)),
                        WASM_I32V(3), WASM_I32V(12345678))});
  }
}

ValueType ref(uint32_t type_index) { return ValueType::Ref(type_index); }
ValueType refNull(uint32_t type_index) {
  return ValueType::RefNull(type_index);
}

TEST_F(FunctionBodyDecoderTest, StructOrArrayNewDefault) {
  TestModuleBuilder builder;
  uint8_t struct_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t struct_non_def_index =
      builder.AddStruct({F(ref(struct_index), true)});
  uint8_t struct_immutable_index = builder.AddStruct({F(kWasmI32, false)});
  uint8_t array_index = builder.AddArray(kWasmI32, true);
  uint8_t array_non_def_index = builder.AddArray(ref(array_index), true);
  uint8_t array_immutable_index = builder.AddArray(kWasmI32, false);

  module = builder.module();

  ExpectValidates(sigs.v_v(),
                  {WASM_STRUCT_NEW_DEFAULT(struct_index), WASM_DROP});
  ExpectFailure(sigs.v_v(),
                {WASM_STRUCT_NEW_DEFAULT(struct_non_def_index), WASM_DROP},
                kAppendEnd,
                "struct.new_default: struct type 1 has field 0 of "
                "non-defaultable type (ref 0)");
  ExpectValidates(sigs.v_v(),
                  {WASM_STRUCT_NEW_DEFAULT(struct_immutable_index), WASM_DROP});
  ExpectValidates(
      sigs.v_v(),
      {WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(3)), WASM_DROP});
  ExpectFailure(
      sigs.v_v(),
      {WASM_ARRAY_NEW_DEFAULT(array_non_def_index, WASM_I32V(3)), WASM_DROP},
      kAppendEnd,
      "array.new_default: array type 4 has non-defaultable element type (ref "
      "3)");
  ExpectValidates(
      sigs.v_v(),
      {WASM_ARRAY_NEW_DEFAULT(array_immutable_index, WASM_I32V(3)), WASM_DROP});
}

TEST_F(FunctionBodyDecoderTest, DefaultableLocal) {
  AddLocals(kWasmExternRef, 1);
  ExpectValidates(sigs.v_v(), {});
}

TEST_F(FunctionBodyDecoderTest, NonDefaultableLocals) {
  WASM_FEATURE_SCOPE(legacy_eh);
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  ValueType rep = ref(struct_type_index);
  FunctionSig sig(0, 1, &rep);
  AddLocals(rep, 2);
  uint8_t ex = builder.AddException(sigs.v_v());
  // Declaring non-defaultable locals is fine.
  ExpectValidates(&sig, {});
  // Loading from an uninitialized non-defaultable local fails.
  ExpectFailure(&sig, {WASM_LOCAL_GET(1), WASM_DROP}, kAppendEnd,
                "uninitialized non-defaultable local: 1");
  // Loading from an initialized local is fine.
  ExpectValidates(&sig, {WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                         WASM_LOCAL_GET(1), WASM_DROP});
  ExpectValidates(&sig, {WASM_LOCAL_TEE(1, WASM_LOCAL_GET(0)),
                         WASM_LOCAL_GET(1), WASM_DROP, WASM_DROP});
  // Non-nullable locals must be initialized with non-null values.
  ExpectFailure(&sig, {WASM_LOCAL_SET(1, WASM_REF_NULL(struct_type_index))},
                kAppendEnd,
                "expected type (ref 0), found ref.null of type (ref null 0)");
  // Initialization is propagated into inner blocks.
  ExpectValidates(
      &sig,
      {WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
       WASM_BLOCK(WASM_LOCAL_GET(1), WASM_DROP),
       WASM_LOOP(WASM_LOCAL_GET(1), WASM_DROP),
       WASM_IF_ELSE(WASM_ZERO, WASM_SEQ(WASM_LOCAL_GET(1), WASM_DROP),
                    WASM_SEQ(WASM_LOCAL_GET(1), WASM_DROP)),
       kExprTry, kVoidCode, WASM_LOCAL_GET(1), WASM_DROP, kExprCatch, ex,
       WASM_LOCAL_GET(1), WASM_DROP, kExprEnd, WASM_LOCAL_GET(1), WASM_DROP});
  // Initialization is forgotten at the end of a block.
  ExpectFailure(&sig,
                {WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                 WASM_BLOCK(WASM_LOCAL_SET(2, WASM_LOCAL_GET(0))),
                 WASM_LOCAL_GET(1), WASM_DROP,   // OK
                 WASM_LOCAL_GET(2), WASM_DROP},  // Error
                kAppendEnd, "uninitialized non-defaultable local: 2");
  // Initialization is forgotten at the end of if/else, even if both
  // branches initialized the local.
  ExpectFailure(&sig,
                {WASM_IF_ELSE(WASM_ZERO, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                              WASM_LOCAL_SET(1, WASM_LOCAL_GET(0))),
                 WASM_LOCAL_GET(1), WASM_DROP},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  // Initialization does not carry from the "then" branch to the "else" branch.
  ExpectFailure(&sig,
                {WASM_IF_ELSE(WASM_ONE, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                              WASM_SEQ(WASM_LOCAL_GET(1), WASM_DROP))},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  // Initialization is forgotten at the end of a loop.
  ExpectFailure(&sig,
                {WASM_LOOP(WASM_LOCAL_SET(1, WASM_LOCAL_GET(0))),
                 WASM_LOCAL_GET(1), WASM_DROP},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  // Initialization is forgotten at the end of a try, with or without catch.
  ExpectFailure(&sig,
                {kExprTry, kVoidCode, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                 kExprEnd, WASM_LOCAL_GET(1), WASM_DROP},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  ExpectFailure(&sig,
                {kExprTry, kVoidCode, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                 kExprCatch, ex, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)), kExprEnd,
                 WASM_LOCAL_GET(1), WASM_DROP},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  ExpectFailure(&sig,
                {kExprTry, kVoidCode, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                 kExprCatchAll, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)), kExprEnd,
                 WASM_LOCAL_GET(1), WASM_DROP},
                kAppendEnd, "uninitialized non-defaultable local: 1");
  // Initialization does not carry from a "try" block to its "catch" block.
  ExpectFailure(&sig,
                {kExprTry, kVoidCode, WASM_LOCAL_SET(1, WASM_LOCAL_GET(0)),
                 kExprCatch, ex, WASM_LOCAL_GET(1), WASM_DROP, kExprEnd},
                kAppendEnd, "uninitialized non-defaultable local: 1");
}

TEST_F(FunctionBodyDecoderTest, RefEq) {
  WASM_FEATURE_SCOPE(exnref);

  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  ValueType eqref_subtypes[] = {kWasmEqRef,
                                kWasmI31Ref,
                                kWasmI31Ref.AsNonNull(),
                                kWasmEqRef.AsNonNull(),
                                kWasmStructRef,
                                kWasmArrayRef,
                                refNull(HeapType::kEqShared),
                                refNull(HeapType::kI31Shared),
                                ref(HeapType::kStructShared),
                                ref(HeapType::kArrayShared),
                                ref(struct_type_index),
                                refNull(struct_type_index)};
  ValueType non_eqref_subtypes[] = {kWasmI32,
                                    kWasmI64,
                                    kWasmF32,
                                    kWasmF64,
                                    kWasmS128,
                                    kWasmFuncRef,
                                    kWasmExternRef,
                                    kWasmAnyRef,
                                    kWasmExnRef,
                                    ref(HeapType::kExtern),
                                    ref(HeapType::kAny),
                                    ref(HeapType::kFunc),
                                    ref(HeapType::kExn),
                                    refNull(HeapType::kExternShared),
                                    refNull(HeapType::kAnyShared),
                                    refNull(HeapType::kFuncShared),
                                    refNull(HeapType::kExnShared)};

  for (ValueType type1 : eqref_subtypes) {
    for (ValueType type2 : eqref_subtypes) {
      ValueType reps[] = {kWasmI32, type1, type2};
      FunctionSig sig(1, 2, reps);
      ExpectValidates(&sig,
                      {WASM_REF_EQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))});
    }
  }

  for (ValueType type1 : eqref_subtypes) {
    for (ValueType type2 : non_eqref_subtypes) {
      ValueType reps[] = {kWasmI32, type1, type2};
      FunctionSig sig(1, 2, reps);
      ExpectFailure(&sig, {WASM_REF_EQ(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1))},
                    kAppendEnd,
                    "expected either eqref or (ref null shared eq), found "
                    "local.get of type");
      ExpectFailure(&sig, {WASM_REF_EQ(WASM_LOCAL_GET(1), WASM_LOCAL_GET(0))},
                    kAppendEnd,
                    "expected either eqref or (ref null shared eq), found "
                    "local.get of type");
    }
  }
}

TEST_F(FunctionBodyDecoderTest, RefAsNonNull) {
  WASM_FEATURE_SCOPE(exnref);

  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint32_t heap_types[] = {
      struct_type_index, array_type_index,  HeapType::kExn, HeapType::kFunc,
      HeapType::kEq,     HeapType::kExtern, HeapType::kAny, HeapType::kI31};

  ValueType non_compatible_types[] = {kWasmI32, kWasmI64, kWasmF32, kWasmF64,
                                      kWasmS128};

  // It works with nullable types.
  for (uint32_t heap_type : heap_types) {
    ValueType reprs[] = {ValueType::Ref(heap_type),
                         ValueType::RefNull(heap_type)};
    FunctionSig sig(1, 1, reprs);
    ExpectValidates(&sig, {WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0))});
  }

  // It works with non-nullable types.
  for (uint32_t heap_type : heap_types) {
    ValueType reprs[] = {ValueType::Ref(heap_type), ValueType::Ref(heap_type)};
    FunctionSig sig(1, 1, reprs);
    ExpectValidates(&sig, {WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0))});
  }

  // It fails with other types.
  for (ValueType type : non_compatible_types) {
    FunctionSig sig(0, 1, &type);
    ExpectFailure(
        &sig, {WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)), kExprDrop}, kAppendEnd,
        "ref.as_non_null[0] expected reference type, found local.get of type");
  }
}

TEST_F(FunctionBodyDecoderTest, RefNull) {
  WASM_FEATURE_SCOPE(exnref);

  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint32_t type_reprs[] = {
      struct_type_index, array_type_index, HeapType::kExn,
      HeapType::kFunc,   HeapType::kEq,    HeapType::kExtern,
      HeapType::kAny,    HeapType::kI31,   HeapType::kNone};
  // It works with heap types.
  for (uint32_t type_repr : type_reprs) {
    const ValueType type = ValueType::RefNull(type_repr);
    const FunctionSig sig(1, 0, &type);
    ExpectValidates(&sig, {WASM_REF_NULL(WASM_HEAP_TYPE(HeapType(type_repr)))});
  }
  // It fails for undeclared types.
  ExpectFailure(sigs.v_v(), {WASM_REF_NULL(42), kExprDrop}, kAppendEnd,
                "Type index 42 is out of bounds");
}

TEST_F(FunctionBodyDecoderTest, RefIsNull) {
  ExpectValidates(sigs.i_i(),
                  {WASM_REF_IS_NULL(WASM_REF_NULL(kExternRefCode))});
  ExpectFailure(
      sigs.i_i(), {WASM_REF_IS_NULL(WASM_LOCAL_GET(0))}, kAppendEnd,
      "ref.is_null[0] expected reference type, found local.get of type i32");

  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint32_t heap_types[] = {
      struct_type_index, array_type_index, HeapType::kFunc, HeapType::kEq,
      HeapType::kExtern, HeapType::kAny,   HeapType::kI31};

  for (uint32_t heap_type : heap_types) {
    const ValueType types[] = {kWasmI32, ValueType::RefNull(heap_type)};
    const FunctionSig sig(1, 1, types);
    // It works for nullable references.
    ExpectValidates(&sig, {WASM_REF_IS_NULL(WASM_LOCAL_GET(0))});
    // It works for non-nullable references.
    ExpectValidates(
        &sig, {WASM_REF_IS_NULL(WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)))});
  }

  // It fails if the argument type is not a reference type.
  ExpectFailure(
      sigs.v_v(), {WASM_REF_IS_NULL(WASM_I32V(0)), kExprDrop}, kAppendEnd,
      "ref.is_null[0] expected reference type, found i32.const of type i32");
}

TEST_F(FunctionBodyDecoderTest, BrOnNull) {
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint32_t type_reprs[] = {
      struct_type_index, array_type_index, HeapType::kFunc, HeapType::kEq,
      HeapType::kExtern, HeapType::kAny,   HeapType::kI31,  HeapType::kNone};

  for (uint32_t type_repr : type_reprs) {
    const ValueType reps[] = {ValueType::Ref(type_repr),
                              ValueType::RefNull(type_repr)};
    const FunctionSig sig(1, 1, reps);
    ExpectValidates(
        &sig, {WASM_BLOCK_R(reps[0], WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)),
                            WASM_BR_ON_NULL(0, WASM_LOCAL_GET(0)), WASM_I32V(0),
                            kExprSelectWithType, 1, WASM_REF_TYPE(reps[0]))});
    // Should have block return value on stack before calling br_on_null.
    ExpectFailure(&sig,
                  {WASM_BLOCK_R(reps[0], WASM_BR_ON_NULL(0, WASM_LOCAL_GET(0)),
                                WASM_I32V(0), kExprSelectWithType, 1,
                                WASM_REF_TYPE(reps[0]))},
                  kAppendEnd,
                  "expected 1 elements on the stack for branch, found 0");
  }
}

TEST_F(FunctionBodyDecoderTest, BrOnNonNull) {
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint32_t type_reprs[] = {
      struct_type_index, array_type_index, HeapType::kFunc, HeapType::kEq,
      HeapType::kExtern, HeapType::kAny,   HeapType::kI31};

  for (uint32_t type_repr : type_reprs) {
    const ValueType reps[] = {ValueType::Ref(type_repr),
                              ValueType::RefNull(type_repr)};
    const FunctionSig sig(1, 1, reps);
    ExpectValidates(
        &sig,
        {WASM_BLOCK_R(reps[0], WASM_BR_ON_NON_NULL(0, WASM_LOCAL_GET(0)),
                      WASM_RETURN(WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0))))});

    // Wrong branch type.
    ExpectFailure(
        &sig,
        {WASM_BLOCK_I(WASM_BR_ON_NON_NULL(0, WASM_LOCAL_GET(0)),
                      WASM_RETURN(WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0))))},
        kAppendEnd,
        ("type error in branch[0] (expected i32, got " + reps[0].name() + ")")
            .c_str());

    // br_on_non_null does not leave a value on the stack.
    ExpectFailure(&sig, {WASM_BR_ON_NON_NULL(0, WASM_LOCAL_GET(0))}, kAppendEnd,
                  "expected 1 elements on the stack for fallthru, found 0");
  }
}

TEST_F(FunctionBodyDecoderTest, GCStruct) {
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  uint8_t array_type_index = builder.AddArray(kWasmI32, true);
  uint8_t immutable_struct_type_index = builder.AddStruct({F(kWasmI32, false)});
  uint8_t field_index = 0;

  ValueType struct_type = ValueType::Ref(struct_type_index);
  ValueType reps_i_r[] = {kWasmI32, struct_type};
  ValueType reps_f_r[] = {kWasmF32, struct_type};
  const FunctionSig sig_i_r(1, 1, reps_i_r);
  const FunctionSig sig_v_r(0, 1, &struct_type);
  const FunctionSig sig_r_v(1, 0, &struct_type);
  const FunctionSig sig_f_r(1, 1, reps_f_r);

  /** struct.new **/
  ExpectValidates(&sig_r_v, {WASM_STRUCT_NEW(struct_type_index, WASM_I32V(0))});
  // Too few arguments.
  ExpectFailure(&sig_r_v, {WASM_GC_OP(kExprStructNew), struct_type_index},
                kAppendEnd,
                "not enough arguments on the stack for struct.new "
                "(need 1, got 0)");
  // Too many arguments.
  ExpectFailure(
      &sig_r_v,
      {WASM_STRUCT_NEW(struct_type_index, WASM_I32V(0), WASM_I32V(1))},
      kAppendEnd, "expected 1 elements on the stack for fallthru, found 2");
  // Mistyped arguments.
  ExpectFailure(&sig_v_r,
                {WASM_STRUCT_NEW(struct_type_index, WASM_LOCAL_GET(0))},
                kAppendEnd,
                "struct.new[0] expected type i32, found local.get of "
                "type (ref 0)");
  // Wrongly typed index.
  ExpectFailure(sigs.v_v(),
                {WASM_STRUCT_NEW(array_type_index, WASM_I32V(0)), kExprDrop},
                kAppendEnd, "invalid struct index: 1");
  // Out-of-bounds index.
  ExpectFailure(sigs.v_v(), {WASM_STRUCT_NEW(42, WASM_I32V(0)), kExprDrop},
                kAppendEnd, "invalid struct index: 42");

  /** struct.get **/
  ExpectValidates(&sig_i_r, {WASM_STRUCT_GET(struct_type_index, field_index,
                                             WASM_LOCAL_GET(0))});
  // With non-nullable struct.
  ExpectValidates(&sig_i_r,
                  {WASM_STRUCT_GET(struct_type_index, field_index,
                                   WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)))});
  // Wrong index.
  ExpectFailure(
      &sig_v_r,
      {WASM_STRUCT_GET(struct_type_index, field_index + 1, WASM_LOCAL_GET(0)),
       kExprDrop},
      kAppendEnd, "invalid field index: 1");
  // Mistyped expected type.
  ExpectFailure(
      &sig_f_r,
      {WASM_STRUCT_GET(struct_type_index, field_index, WASM_LOCAL_GET(0))},
      kAppendEnd, "type error in fallthru[0] (expected f32, got i32)");

  /** struct.set **/
  ExpectValidates(&sig_v_r, {WASM_STRUCT_SET(struct_type_index, field_index,
                                             WASM_LOCAL_GET(0), WASM_I32V(0))});
  // Non-nullable struct.
  ExpectValidates(
      &sig_v_r,
      {WASM_STRUCT_SET(struct_type_index, field_index,
                       WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)), WASM_I32V(0))});
  // Wrong index.
  ExpectFailure(&sig_v_r,
                {WASM_STRUCT_SET(struct_type_index, field_index + 1,
                                 WASM_LOCAL_GET(0), WASM_I32V(0))},
                kAppendEnd, "invalid field index: 1");
  // Mistyped input.
  ExpectFailure(&sig_v_r,
                {WASM_STRUCT_SET(struct_type_index, field_index,
                                 WASM_LOCAL_GET(0), WASM_I64V(0))},
                kAppendEnd,
                "struct.set[1] expected type i32, found i64.const of type i64");
  // Expecting output.
  ExpectFailure(&sig_i_r,
                {WASM_STRUCT_SET(struct_type_index, field_index,
                                 WASM_LOCAL_GET(0), WASM_I32V(0))},
                kAppendEnd,
                "expected 1 elements on the stack for fallthru, found 0");
  // Setting immutable field.
  ExpectFailure(sigs.v_v(),
                {WASM_STRUCT_SET(
                    immutable_struct_type_index, field_index,
                    WASM_STRUCT_NEW(immutable_struct_type_index, WASM_I32V(42)),
                    WASM_I32V(0))},
                kAppendEnd, "struct.set: Field 0 of type 2 is immutable.");

  // struct.get_s/u fail
  ExpectFailure(
      &sig_i_r,
      {WASM_STRUCT_GET_S(struct_type_index, field_index, WASM_LOCAL_GET(0))},
      kAppendEnd,
      "struct.get_s: Immediate field 0 of type 0 has non-packed type i32. Use "
      "struct.get instead.");

  ExpectFailure(
      &sig_i_r,
      {WASM_STRUCT_GET_U(struct_type_index, field_index, WASM_LOCAL_GET(0))},
      kAppendEnd,
      "struct.get_u: Immediate field 0 of type 0 has non-packed type i32. Use "
      "struct.get instead.");
}

TEST_F(FunctionBodyDecoderTest, GCArray) {
  uint8_t array_type_index = builder.AddArray(kWasmFuncRef, true);
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, false)});
  uint8_t immutable_array_type_index = builder.AddArray(kWasmI32, false);

  ValueType array_type = ValueType::Ref(array_type_index);
  ValueType immutable_array_type = ValueType::Ref(immutable_array_type_index);
  ValueType reps_c_r[] = {kWasmFuncRef, array_type};
  ValueType reps_f_r[] = {kWasmF32, array_type};
  ValueType reps_i_r[] = {kWasmI32, array_type};
  ValueType reps_i_a[] = {kWasmI32, kWasmArrayRef};
  ValueType reps_i_s[] = {kWasmI32, ValueType::Ref(struct_type_index)};
  const FunctionSig sig_c_r(1, 1, reps_c_r);
  const FunctionSig sig_v_r(0, 1, &array_type);
  const FunctionSig sig_v_r2(0, 1, &immutable_array_type);
  const FunctionSig sig_r_v(1, 0, &array_type);
  const FunctionSig sig_f_r(1, 1, reps_f_r);
  const FunctionSig sig_v_cr(0, 2, reps_c_r);
  const FunctionSig sig_i_r(1, 1, reps_i_r);
  const FunctionSig sig_i_a(1, 1, reps_i_a);
  const FunctionSig sig_i_s(1, 1, reps_i_s);

  /** array.new **/
  ExpectValidates(&sig_r_v,
                  {WASM_ARRAY_NEW(array_type_index, WASM_REF_NULL(kFuncRefCode),
                                  WASM_I32V(10))});
  // Too few arguments.
  ExpectFailure(&sig_r_v,
                {WASM_I32V(10), WASM_GC_OP(kExprArrayNew), array_type_index},
                kAppendEnd,
                "not enough arguments on the stack for array.new "
                "(need 2, got 1)");
  // Mistyped initializer.
  ExpectFailure(&sig_r_v,
                {WASM_ARRAY_NEW(array_type_index, WASM_REF_NULL(kExternRefCode),
                                WASM_I32V(10))},
                kAppendEnd,
                "array.new[0] expected type funcref, found ref.null "
                "of type externref");
  // Mistyped length.
  ExpectFailure(&sig_r_v,
                {WASM_ARRAY_NEW(array_type_index, WASM_REF_NULL(kFuncRefCode),
                                WASM_I64V(5))},
                kAppendEnd,
                "array.new[1] expected type i32, found i64.const of type i64");
  // Wrong type index.
  ExpectFailure(sigs.v_v(),
                {WASM_ARRAY_NEW(struct_type_index, WASM_REF_NULL(kFuncRefCode),
                                WASM_I32V(10)),
                 kExprDrop},
                kAppendEnd, "invalid array index: 1");

  /** array.get **/
  ExpectValidates(&sig_c_r, {WASM_ARRAY_GET(array_type_index, WASM_LOCAL_GET(0),
                                            WASM_I32V(5))});
  // With non-nullable array type.
  ExpectValidates(
      &sig_c_r,
      {WASM_ARRAY_GET(array_type_index, WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)),
                      WASM_I32V(5))});
  // Wrongly typed index.
  ExpectFailure(
      &sig_v_r,
      {WASM_ARRAY_GET(array_type_index, WASM_LOCAL_GET(0), WASM_I64V(5)),
       kExprDrop},
      kAppendEnd,
      "array.get[1] expected type i32, found i64.const of type i64");
  // Mistyped expected type.
  ExpectFailure(
      &sig_f_r,
      {WASM_ARRAY_GET(array_type_index, WASM_LOCAL_GET(0), WASM_I32V(5))},
      kAppendEnd, "type error in fallthru[0] (expected f32, got funcref)");

  // array.get_s/u fail.
  ExpectFailure(
      &sig_c_r,
      {WASM_ARRAY_GET_S(array_type_index, WASM_LOCAL_GET(0), WASM_I32V(5))},
      kAppendEnd,
      "array.get_s: Immediate array type 0 has non-packed type funcref. Use "
      "array.get instead.");
  ExpectFailure(
      &sig_c_r,
      {WASM_ARRAY_GET_U(array_type_index, WASM_LOCAL_GET(0), WASM_I32V(5))},
      kAppendEnd,
      "array.get_u: Immediate array type 0 has non-packed type funcref. Use "
      "array.get instead.");

  /** array.set **/
  ExpectValidates(&sig_v_r,
                  {WASM_ARRAY_SET(array_type_index, WASM_LOCAL_GET(0),
                                  WASM_I32V(42), WASM_REF_NULL(kFuncRefCode))});
  // With non-nullable array type.
  ExpectValidates(
      &sig_v_cr,
      {WASM_ARRAY_SET(array_type_index, WASM_LOCAL_GET(1), WASM_I32V(42),
                      WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(0)))});
  // Non-array type index.
  ExpectFailure(&sig_v_cr,
                {WASM_ARRAY_SET(struct_type_index, WASM_LOCAL_GET(1),
                                WASM_I32V(42), WASM_LOCAL_GET(0))},
                kAppendEnd, "invalid array index: 1");
  // Wrongly typed index.
  ExpectFailure(&sig_v_cr,
                {WASM_ARRAY_SET(array_type_index, WASM_LOCAL_GET(1),
                                WASM_I64V(42), WASM_LOCAL_GET(0))},
                kAppendEnd,
                "array.set[1] expected type i32, found i64.const of type i64");
  // Wrongly typed value.
  ExpectFailure(
      &sig_v_cr,
      {WASM_ARRAY_SET(array_type_index, WASM_LOCAL_GET(1), WASM_I32V(42),
                      WASM_I64V(0))},
      kAppendEnd,
      "array.set[2] expected type funcref, found i64.const of type i64");

  /** array.len **/
  // Works both with conrete array types and arrayref.
  ExpectValidates(&sig_i_r, {WASM_ARRAY_LEN(WASM_LOCAL_GET(0))});
  ExpectValidates(&sig_i_a, {WASM_ARRAY_LEN(WASM_LOCAL_GET(0))});
  // Wrong return type.
  ExpectFailure(&sig_f_r, {WASM_ARRAY_LEN(WASM_LOCAL_GET(0))}, kAppendEnd,
                "type error in fallthru[0] (expected f32, got i32)");
  // Non-array argument.
  ExpectFailure(
      &sig_i_s, {WASM_ARRAY_LEN(WASM_LOCAL_GET(0))}, kAppendEnd,
      "array.len[0] expected type arrayref, found local.get of type (ref 1)");

  // Immutable array.
  // Allocating and reading is OK:
  ExpectValidates(
      sigs.i_v(),
      {WASM_ARRAY_GET(
          immutable_array_type_index,
          WASM_ARRAY_NEW_FIXED(immutable_array_type_index, 1, WASM_I32V(42)),
          WASM_I32V(0))});
  // Writing fails:
  ExpectFailure(&sig_v_r2,
                {WASM_ARRAY_SET(immutable_array_type_index, WASM_LOCAL_GET(0),
                                WASM_I32V(0), WASM_I32V(42))},
                kAppendEnd, "array.set: immediate array type 2 is immutable");
}

TEST_F(FunctionBodyDecoderTest, PackedFields) {
  uint8_t array_type_index = builder.AddArray(kWasmI8, true);
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI16, true)});
  uint8_t field_index = 0;

  // *.new with packed fields works.
  ExpectValidates(sigs.v_v(),
                  {WASM_ARRAY_NEW(array_type_index, WASM_I32V(0), WASM_I32V(5)),
                   kExprDrop});
  ExpectValidates(
      sigs.v_v(),
      {WASM_STRUCT_NEW(struct_type_index, WASM_I32V(42)), kExprDrop});
  // It can't unpack types other that i32.
  ExpectFailure(
      sigs.v_v(),
      {WASM_ARRAY_NEW(array_type_index, WASM_I64V(0), WASM_I32V(5)), kExprDrop},
      kAppendEnd,
      "array.new[0] expected type i32, found i64.const of type i64");
  ExpectFailure(sigs.v_v(),
                {WASM_STRUCT_NEW(struct_type_index, WASM_I64V(42)), kExprDrop},
                kAppendEnd,
                "struct.new[0] expected type i32, found i64.const of type i64");

  // *.set with packed fields works.
  ExpectValidates(sigs.v_v(), {WASM_ARRAY_SET(array_type_index,
                                              WASM_REF_NULL(array_type_index),
                                              WASM_I32V(0), WASM_I32V(5))});
  ExpectValidates(sigs.v_v(), {WASM_STRUCT_SET(struct_type_index, field_index,
                                               WASM_REF_NULL(struct_type_index),
                                               WASM_I32V(42))});
  // It can't unpack into types other that i32.
  ExpectFailure(
      sigs.v_v(),
      {WASM_ARRAY_SET(array_type_index, WASM_REF_NULL(array_type_index),
                      WASM_I32V(0), WASM_I64V(5))},
      kAppendEnd,
      "array.set[2] expected type i32, found i64.const of type i64");
  ExpectFailure(
      sigs.v_v(),
      {WASM_STRUCT_NEW(struct_type_index, field_index,
                       WASM_REF_NULL(struct_type_index), WASM_I64V(42))},
      kAppendEnd,
      "struct.new[0] expected type i32, found i64.const of type i64");

  // *.get_s/u works.
  ExpectValidates(sigs.i_v(), {WASM_ARRAY_GET_S(array_type_index,
                                                WASM_REF_NULL(array_type_index),
                                                WASM_I32V(0))});
  ExpectValidates(sigs.i_v(), {WASM_ARRAY_GET_U(array_type_index,
                                                WASM_REF_NULL(array_type_index),
                                                WASM_I32V(0))});
  ExpectValidates(sigs.i_v(),
                  {WASM_STRUCT_GET_S(struct_type_index, field_index,
                                     WASM_REF_NULL(struct_type_index))});
  ExpectValidates(sigs.i_v(),
                  {WASM_STRUCT_GET_U(struct_type_index, field_index,
                                     WASM_REF_NULL(struct_type_index))});

  // *.get fails.
  ExpectFailure(sigs.i_v(),
                {WASM_ARRAY_GET(array_type_index,
                                WASM_REF_NULL(array_type_index), WASM_I32V(0))},
                kAppendEnd,
                "array.get: Immediate array type 0 has packed type i8. Use "
                "array.get_s or array.get_u instead.");
  ExpectFailure(sigs.i_v(),
                {WASM_STRUCT_GET(struct_type_index, field_index,
                                 WASM_REF_NULL(struct_type_index))},
                kAppendEnd,
                "struct.get: Immediate field 0 of type 1 has packed type i16. "
                "Use struct.get_s or struct.get_u instead.");
}

TEST_F(FunctionBodyDecoderTest, PackedTypesAsLocals) {
  AddLocals(kWasmI8, 1);
  ExpectFailure(sigs.v_v(), {}, kAppendEnd, "invalid value type");
}

TEST_F(FunctionBodyDecoderTest, RefTestCast) {
  WASM_FEATURE_SCOPE(exnref);

  HeapType::Representation array_heap =
      static_cast<HeapType::Representation>(builder.AddArray(kWasmI8, true));
  HeapType::Representation super_struct_heap =
      static_cast<HeapType::Representation>(
          builder.AddStruct({F(kWasmI16, true)}));

  HeapType::Representation sub_struct_heap =
      static_cast<HeapType::Representation>(
          builder.AddStruct({F(kWasmI16, true), F(kWasmI32, false)}));

  HeapType::Representation func_heap_1 =
      static_cast<HeapType::Representation>(builder.AddSignature(sigs.i_i()));

  HeapType::Representation func_heap_2 =
      static_cast<HeapType::Representation>(builder.AddSignature(sigs.i_v()));

  std::tuple<HeapType::Representation, HeapType::Representation, bool> tests[] =
      {
          std::make_tuple(HeapType::kArray, array_heap, true),
          std::make_tuple(HeapType::kStruct, super_struct_heap, true),
          std::make_tuple(HeapType::kFunc, func_heap_1, true),
          std::make_tuple(func_heap_1, func_heap_1, true),
          std::make_tuple(func_heap_1, func_heap_2, true),
          std::make_tuple(super_struct_heap, sub_struct_heap, true),
          std::make_tuple(array_heap, sub_struct_heap, true),
          std::make_tuple(super_struct_heap, func_heap_1, false),
          std::make_tuple(HeapType::kEq, super_struct_heap, true),
          std::make_tuple(HeapType::kExtern, func_heap_1, false),
          std::make_tuple(HeapType::kAny, array_heap, true),
          std::make_tuple(HeapType::kI31, array_heap, true),
          std::make_tuple(HeapType::kNone, array_heap, true),
          std::make_tuple(HeapType::kNone, func_heap_1, false),
          std::make_tuple(HeapType::kExn, HeapType::kExtern, false),
          std::make_tuple(HeapType::kExn, HeapType::kAny, false),
      };

  for (auto test : tests) {
    HeapType from_heap = HeapType(std::get<0>(test));
    HeapType to_heap = HeapType(std::get<1>(test));
    bool should_pass = std::get<2>(test);
    SCOPED_TRACE("from_heap = " + from_heap.name() +
                 ", to_heap = " + to_heap.name());

    ValueType test_reps[] = {kWasmI32, ValueType::RefNull(from_heap)};
    FunctionSig test_sig(1, 1, test_reps);

    ValueType cast_reps[] = {ValueType::RefNull(to_heap),
                             ValueType::RefNull(from_heap)};
    FunctionSig cast_sig(1, 1, cast_reps);

    if (should_pass) {
      ExpectValidates(&test_sig, {WASM_REF_TEST(WASM_LOCAL_GET(0),
                                                WASM_HEAP_TYPE(to_heap))});
      ExpectValidates(&cast_sig, {WASM_REF_CAST(WASM_LOCAL_GET(0),
                                                WASM_HEAP_TYPE(to_heap))});
      ExpectValidates(&test_sig, {WASM_REF_TEST_NULL(WASM_LOCAL_GET(0),
                                                     WASM_HEAP_TYPE(to_heap))});
      ExpectValidates(&cast_sig, {WASM_REF_CAST_NULL(WASM_LOCAL_GET(0),
                                                     WASM_HEAP_TYPE(to_heap))});
    } else {
      std::string error_message =
          "local.get of type " + cast_reps[1].name() +
          " has to be in the same reference type hierarchy as (ref " +
          to_heap.name() + ")";
      ExpectFailure(&test_sig,
                    {WASM_REF_TEST(WASM_LOCAL_GET(0), WASM_HEAP_TYPE(to_heap))},
                    kAppendEnd,
                    ("Invalid types for ref.test: " + error_message).c_str());
      ExpectFailure(&cast_sig,
                    {WASM_REF_CAST(WASM_LOCAL_GET(0), WASM_HEAP_TYPE(to_heap))},
                    kAppendEnd,
                    ("Invalid types for ref.cast: " + error_message).c_str());
      ExpectFailure(
          &test_sig,
          {WASM_REF_TEST_NULL(WASM_LOCAL_GET(0), WASM_HEAP_TYPE(to_heap))},
          kAppendEnd,
          ("Invalid types for ref.test null: " + error_message).c_str());
      ExpectFailure(
          &cast_sig,
          {WASM_REF_CAST_NULL(WASM_LOCAL_GET(0), WASM_HEAP_TYPE(to_heap))},
          kAppendEnd,
          ("Invalid types for ref.cast null: " + error_message).c_str());
    }
  }

  // Trivial type error.
  ExpectFailure(sigs.v_v(),
                {WASM_REF_TEST(WASM_I32V(1), array_heap), kExprDrop},
                kAppendEnd,
                "Invalid types for ref.test: i32.const of type i32 has to be "
                "in the same reference type hierarchy as (ref 0)");
  ExpectFailure(sigs.v_v(),
                {WASM_REF_CAST(WASM_I32V(1), array_heap), kExprDrop},
                kAppendEnd,
                "Invalid types for ref.cast: i32.const of type i32 has to be "
                "in the same reference type hierarchy as (ref 0)");
}

TEST_F(FunctionBodyDecoderTest, BrOnCastOrCastFail) {
  uint8_t super_struct = builder.AddStruct({F(kWasmI16, true)});
  uint8_t sub_struct =
      builder.AddStruct({F(kWasmI16, true), F(kWasmI32, false)}, super_struct);

  ValueType supertype = ValueType::RefNull(super_struct);
  ValueType subtype = ValueType::RefNull(sub_struct);

  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmI32, subtype}, {supertype}),
      {WASM_I32V(42), WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST(0, super_struct, sub_struct), WASM_GC_OP(kExprRefCast),
       sub_struct});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmI32, subtype}, {supertype}),
      {WASM_I32V(42), WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST(0, super_struct, sub_struct), WASM_GC_OP(kExprRefCast),
       sub_struct});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmI32, supertype}, {supertype}),
      {WASM_I32V(42), WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL(0, super_struct, sub_struct)});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmI32, supertype}, {supertype}),
      {WASM_I32V(42), WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL_NULL(0, super_struct, sub_struct)});

  // Wrong branch type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {}, {supertype}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, super_struct, sub_struct),
       WASM_UNREACHABLE},
      kAppendEnd, "br_on_cast must target a branch of arity at least 1");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {supertype}),
      {WASM_I32V(42), WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL(0, super_struct, sub_struct)},
      kAppendEnd,
      "type error in branch[0] (expected (ref null 1), got (ref null 0))");
  ExpectFailure(FunctionSig::Build(this->zone(), {subtype}, {supertype}),
                {WASM_I32V(42), WASM_LOCAL_GET(0),
                 WASM_BR_ON_CAST_FAIL_NULL(0, super_struct, sub_struct)},
                kAppendEnd,
                "type error in branch[0] (expected (ref null 1), got (ref 0))");

  // Wrong fallthrough type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {supertype}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, super_struct, sub_struct)},
      kAppendEnd,
      "type error in fallthru[0] (expected (ref null 1), got (ref null 0))");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {supertype}),
      {WASM_BLOCK_I(WASM_LOCAL_GET(0),
                    WASM_BR_ON_CAST_FAIL(0, super_struct, sub_struct))},
      kAppendEnd, "type error in branch[0] (expected i32, got (ref null 0))");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {supertype}),
      {WASM_BLOCK_I(WASM_LOCAL_GET(0),
                    WASM_BR_ON_CAST_FAIL_NULL(0, super_struct, sub_struct))},
      kAppendEnd, "type error in branch[0] (expected i32, got (ref 0))");

  // Wrong argument type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, super_struct, sub_struct),
       WASM_GC_OP(kExprRefCast), sub_struct},
      kAppendEnd,
      "br_on_cast[0] expected type (ref null 0), found local.get of type "
      "externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_NULL(0, super_struct, sub_struct),
       WASM_GC_OP(kExprRefCast), sub_struct},
      kAppendEnd,
      "br_on_cast[0] expected type (ref null 0), found local.get of type "
      "externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_FAIL(0, super_struct, sub_struct)},
      kAppendEnd,
      "br_on_cast_fail[0] expected type (ref null 0), found local.get of type "
      "externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL_NULL(0, super_struct, sub_struct)},
      kAppendEnd,
      "br_on_cast_fail[0] expected type (ref null 0), found local.get of "
      "type externref");

  // Wrong immediate type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kExternRefCode, sub_struct),
       WASM_GC_OP(kExprRefCast), sub_struct},
      kAppendEnd,
      "invalid types for br_on_cast: (ref 1) is not a subtype of externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {subtype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_NULL(0, kExternRefCode, sub_struct),
       WASM_GC_OP(kExprRefCast), sub_struct},
      kAppendEnd,
      "invalid types for br_on_cast: (ref null 1) is not a subtype of "
      "externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_FAIL(0, kExternRefCode, sub_struct)},
      kAppendEnd,
      "invalid types for br_on_cast_fail: (ref 1) is not a subtype of "
      "externref");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {supertype}, {kWasmExternRef}),
      {WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL_NULL(0, kExternRefCode, sub_struct)},
      kAppendEnd,
      "invalid types for br_on_cast_fail: (ref null 1) is not a subtype "
      "of externref");
}

TEST_F(FunctionBodyDecoderTest, BrOnAbstractType) {
  ValueType kNonNullableFunc = ValueType::Ref(HeapType::kFunc);

  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmStructRef}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kAnyRefCode, kStructRefCode),
       WASM_GC_OP(kExprRefCast), kStructRefCode});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmAnyRef}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0),
       WASM_BR_ON_CAST_FAIL(0, kAnyRefCode, kStructRefCode)});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmI31Ref}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kAnyRefCode, kI31RefCode),
       WASM_GC_OP(kExprRefCast), kI31RefCode});
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmAnyRef}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_FAIL(0, kAnyRefCode, kI31RefCode)});

  // Wrong branch type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kAnyRefCode, kStructRefCode),
       WASM_UNREACHABLE},
      kAppendEnd, "br_on_cast must target a branch of arity at least 1");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kNonNullableFunc}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST_FAIL(0, kAnyRefCode, kStructRefCode)},
      kAppendEnd, "type error in branch[0] (expected (ref func), got anyref)");

  // Wrong fallthrough type.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kWasmStructRef}, {kWasmAnyRef}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kAnyRefCode, kStructRefCode)},
      kAppendEnd, "type error in fallthru[0] (expected structref, got anyref)");
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kWasmAnyRef}, {kWasmAnyRef}),
      {WASM_BLOCK_I(WASM_LOCAL_GET(0),
                    WASM_BR_ON_CAST_FAIL(0, kAnyRefCode, kStructRefCode))},
      kAppendEnd, "type error in branch[0] (expected i32, got anyref)");

  // Argument type error.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kWasmI31Ref}, {kWasmI32}),
      {WASM_LOCAL_GET(0), WASM_BR_ON_CAST(0, kAnyRefCode, kI31RefCode),
       WASM_GC_OP(kExprRefCast), kI31RefCode},
      kAppendEnd,
      "br_on_cast[0] expected type anyref, found local.get of type i32");
}

TEST_F(FunctionBodyDecoderTest, BrWithBottom) {
  // Merging an unsatisfiable non-nullable (ref none) into a target that
  // expects a non-null struct is OK.
  ExpectValidates(
      FunctionSig::Build(this->zone(), {ValueType::Ref(HeapType::kStruct)},
                         {ValueType::Ref(HeapType::kStruct)}),
      {WASM_BR_ON_NON_NULL(0, WASM_REF_NULL(ValueTypeCode::kNoneCode)),
       WASM_LOCAL_GET(0)});
  // Merging the same value into a target that expects a value outside
  // the "anyref" hierarchy is invalid...
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kWasmFuncRef}, {kWasmFuncRef}),
      {WASM_BR_ON_NON_NULL(0, WASM_REF_NULL(ValueTypeCode::kNoneCode)),
       WASM_LOCAL_GET(0)},
      kAppendEnd, "type error in branch[0] (expected funcref, got (ref none))");
  // ...because it would have to be a (ref nofunc) in that case.
  ExpectValidates(
      FunctionSig::Build(this->zone(), {kWasmFuncRef}, {kWasmFuncRef}),
      {WASM_BR_ON_NON_NULL(0, WASM_REF_NULL(ValueTypeCode::kNoFuncCode)),
       WASM_LOCAL_GET(0)});
  // (ref nofunc) in turn doesn't match anyref.
  ExpectFailure(
      FunctionSig::Build(this->zone(), {kWasmAnyRef}, {kWasmAnyRef}),
      {WASM_BR_ON_NON_NULL(0, WASM_REF_NULL(ValueTypeCode::kNoFuncCode)),
       WASM_LOCAL_GET(0)},
      kAppendEnd,
      "type error in branch[0] (expected anyref, got (ref nofunc))");
}

TEST_F(FunctionBodyDecoderTest, LocalTeeTyping) {
  uint8_t array_type = builder.AddArray(kWasmI8, true);

  ValueType types[] = {ValueType::Ref(array_type)};
  FunctionSig sig(1, 0, types);

  AddLocals(ValueType::RefNull(array_type), 1);

  ExpectFailure(
      &sig,
      {WASM_LOCAL_TEE(0, WASM_ARRAY_NEW_DEFAULT(array_type, WASM_I32V(5)))},
      kAppendEnd, "expected (ref 0), got (ref null 0)");
}

TEST_F(FunctionBodyDecoderTest, MergeNullableTypes) {
  uint8_t struct_type_index = builder.AddStruct({F(kWasmI32, true)});
  ValueType struct_type = refNull(struct_type_index);
  FunctionSig loop_sig(0, 1, &struct_type);
  uint8_t loop_sig_index = builder.AddSignature(&loop_sig);
  // Verifies that when a loop consuming a nullable type is entered with a
  // statically known non-null value on the stack, its {start_merge_} can
  // consume null values later.
  // Regression test for crbug.com/1234453.
  ExpectValidates(sigs.v_v(),
                  {WASM_GC_OP(kExprStructNewDefault), struct_type_index,
                   WASM_LOOP_X(loop_sig_index, kExprDrop, kExprRefNull,
                               struct_type_index, kExprBr, 0)});
}

// This tests that num_locals_ in decoder remains consistent, even if we fail
// mid-DecodeLocals().
TEST_F(FunctionBodyDecoderTest, Regress_1154439) {
  AddLocals(kWasmI32, 1);
  AddLocals(kWasmI64, 1000000);
  ExpectFailure(sigs.v_v(), {}, kAppendEnd, "local count too large");
}

TEST_F(FunctionBodyDecoderTest, DropOnEmptyStack) {
  // Valid code:
  ExpectValidates(sigs.v_v(), {kExprI32Const, 1, kExprDrop}, kAppendEnd);
  // Invalid code (dropping from empty stack):
  ExpectFailure(sigs.v_v(), {kExprDrop}, kAppendEnd,
                "not enough arguments on the stack for drop");
  // Valid code (dropping from empty stack in unreachable code):
  ExpectValidates(sigs.v_v(), {kExprUnreachable, kExprDrop}, kAppendEnd);
}

TEST_F(FunctionBodyDecoderTest, AnyConvertExtern) {
  ExpectValidates(FunctionSig::Build(zone(), {kWasmAnyRef}, {}),
                  {WASM_GC_ANY_CONVERT_EXTERN(WASM_REF_NULL(kNoExternCode))});
  ExpectValidates(FunctionSig::Build(zone(), {kWasmAnyRef}, {kWasmExternRef}),
                  {WASM_GC_ANY_CONVERT_EXTERN(WASM_LOCAL_GET(0))});
  ExpectValidates(
      FunctionSig::Build(zone(), {kWasmAnyRef}, {kWasmExternRef.AsNonNull()}),
      {WASM_GC_ANY_CONVERT_EXTERN(WASM_LOCAL_GET(0))});
  ExpectFailure(FunctionSig::Build(zone(), {kWasmAnyRef}, {}),
                {WASM_GC_ANY_CONVERT_EXTERN(kExprNop)}, kAppendEnd,
                "not enough arguments on the stack for any.convert_extern "
                "(need 1, got 0)");
  ExpectFailure(
      FunctionSig::Build(zone(), {kWasmAnyRef.AsNonNull()}, {kWasmExternRef}),
      {WASM_GC_ANY_CONVERT_EXTERN(WASM_LOCAL_GET(0))}, kAppendEnd,
      "type error in fallthru[0] (expected (ref any), got anyref)");
  ExpectFailure(FunctionSig::Build(zone(), {kWasmAnyRef}, {kWasmAnyRef}),
                {WASM_GC_ANY_CONVERT_EXTERN(WASM_LOCAL_GET(0))}, kAppendEnd,
                "any.convert_extern[0] expected type externref, found "
                "local.get of type anyref");
}

TEST_F(FunctionBodyDecoderTest, ExternConvertAny) {
  ExpectValidates(FunctionSig::Build(zone(), {kWasmExternRef}, {}),
                  {WASM_GC_EXTERN_CONVERT_ANY(WASM_REF_NULL(kNoneCode))});
  ExpectValidates(FunctionSig::Build(zone(), {kWasmExternRef}, {kWasmAnyRef}),
                  {WASM_GC_EXTERN_CONVERT_ANY(WASM_LOCAL_GET(0))});
  ExpectValidates(
      FunctionSig::Build(zone(), {kWasmExternRef}, {kWasmAnyRef.AsNonNull()}),
      {WASM_GC_EXTERN_CONVERT_ANY(WASM_LOCAL_GET(0))});
  ExpectFailure(FunctionSig::Build(zone(), {kWasmExternRef}, {}),
                {WASM_GC_EXTERN_CONVERT_ANY(kExprNop)}, kAppendEnd,
                "not enough arguments on the stack for extern.convert_any "
                "(need 1, got 0)");
  ExpectFailure(
      FunctionSig::Build(zone(), {kWasmExternRef.AsNonNull()}, {kWasmAnyRef}),
      {WASM_GC_EXTERN_CONVERT_ANY(WASM_LOCAL_GET(0))}, kAppendEnd,
      "type error in fallthru[0] (expected (ref extern), got externref)");
  ExpectFailure(FunctionSig::Build(zone(), {kWasmExternRef}, {kWasmExternRef}),
                {WASM_GC_EXTERN_CONVERT_ANY(WASM_LOCAL_GET(0))}, kAppendEnd,
                "extern.convert_any[0] expected type anyref, found "
                "local.get of type externref");
}

class BranchTableIteratorTest : public TestWithZone {
 public:
  BranchTableIteratorTest() : TestWithZone() {}
  void CheckBrTableSize(const uint8_t* start, const uint8_t* end) {
    Decoder decoder(start, end);
    BranchTableImmediate operand(&decoder, start + 1, Decoder::kFullValidation);
    BranchTableIterator<Decoder::FullValidationTag> iterator(&decoder, operand);
    EXPECT_EQ(end - start - 1u, iterator.length());
    EXPECT_OK(decoder);
  }
  void CheckBrTableError(const uint8_t* start, const uint8_t* end) {
    Decoder decoder(start, end);
    BranchTableImmediate operand(&decoder, start + 1, Decoder::kFullValidation);
    BranchTableIterator<Decoder::FullValidationTag> iterator(&decoder, operand);
    iterator.length();
    EXPECT_FALSE(decoder.ok());
  }
};

#define CHECK_BR_TABLE_LENGTH(...)                       \
  {                                                      \
    static uint8_t code[] = {kExprBrTable, __VA_ARGS__}; \
    CheckBrTableSize(code, code + sizeof(code));         \
  }

#define CHECK_BR_TABLE_ERROR(...)                        \
  {                                                      \
    static uint8_t code[] = {kExprBrTable, __VA_ARGS__}; \
    CheckBrTableError(code, code + sizeof(code));        \
  }

TEST_F(BranchTableIteratorTest, count0) {
  CHECK_BR_TABLE_LENGTH(0, U32V_1(1));
  CHECK_BR_TABLE_LENGTH(0, U32V_2(200));
  CHECK_BR_TABLE_LENGTH(0, U32V_3(30000));
  CHECK_BR_TABLE_LENGTH(0, U32V_4(400000));

  CHECK_BR_TABLE_LENGTH(0, U32V_1(2));
  CHECK_BR_TABLE_LENGTH(0, U32V_2(300));
  CHECK_BR_TABLE_LENGTH(0, U32V_3(40000));
  CHECK_BR_TABLE_LENGTH(0, U32V_4(500000));
}

TEST_F(BranchTableIteratorTest, count1) {
  CHECK_BR_TABLE_LENGTH(1, U32V_1(1), U32V_1(6));
  CHECK_BR_TABLE_LENGTH(1, U32V_2(200), U32V_1(8));
  CHECK_BR_TABLE_LENGTH(1, U32V_3(30000), U32V_1(9));
  CHECK_BR_TABLE_LENGTH(1, U32V_4(400000), U32V_1(11));

  CHECK_BR_TABLE_LENGTH(1, U32V_1(2), U32V_2(6));
  CHECK_BR_TABLE_LENGTH(1, U32V_2(300), U32V_2(7));
  CHECK_BR_TABLE_LENGTH(1, U32V_3(40000), U32V_2(8));
  CHECK_BR_TABLE_LENGTH(1, U32V_4(500000), U32V_2(9));
}

TEST_F(BranchTableIteratorTest, error0) {
  CHECK_BR_TABLE_ERROR(0);
  CHECK_BR_TABLE_ERROR(1, U32V_1(33));
}

#undef CHECK_BR_TABLE_LENGTH
#undef CHECK_BR_TABLE_ERROR

struct PrintOpcodes {
  const uint8_t* start;
  const uint8_t* end;
};
std::ostream& operator<<(std::ostream& out, const PrintOpcodes& range) {
  out << "First opcode: \""
      << WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(*range.start))
      << "\"\nall bytes: [";
  for (const uint8_t* b = range.start; b < range.end; ++b) {
    out << (b == range.start ? "" : ", ") << uint32_t{*b} << "/"
        << AsHex(*b, 2, true);
  }
  return out << "]";
}

class WasmOpcodeLengthTest : public TestWithZone {
 public:
  WasmOpcodeLengthTest() : TestWithZone() {}

  template <typename... Bytes>
  void ExpectLength(unsigned expected, Bytes... bytes) {
    const uint8_t code[] = {
        static_cast<uint8_t>(bytes)..., 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(expected, OpcodeLength(code, code + sizeof(code)))
        << PrintOpcodes{code, code + sizeof...(bytes)};
  }

  // Helper to check for prefixed opcodes, which can have multiple bytes.
  void ExpectLengthPrefixed(unsigned operands, WasmOpcode opcode) {
    uint8_t prefix = WasmOpcodes::ExtractPrefix(opcode);
    DCHECK(WasmOpcodes::IsPrefixOpcode(static_cast<WasmOpcode>(prefix)));
    uint16_t index = ExtractPrefixedOpcodeBytes(opcode);
    uint8_t encoded[2] = {0, 0};
    uint8_t* p = encoded;
    unsigned len = static_cast<unsigned>(LEBHelper::sizeof_u32v(index));
    DCHECK_GE(2, len);
    LEBHelper::write_u32v(&p, index);
    // length of index, + number of operands + prefix bye
    ExpectLength(len + operands + 1, prefix, encoded[0], encoded[1]);
  }

  template <typename... Bytes>
  void ExpectFailure(Bytes... bytes) {
    const uint8_t code[] = {
        static_cast<uint8_t>(bytes)..., 0, 0, 0, 0, 0, 0, 0, 0};
    WasmDetectedFeatures detected_features;
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    WasmDecoder<Decoder::FullValidationTag> decoder(
        this->zone(), nullptr, WasmEnabledFeatures::None(), &detected_features,
        nullptr, kIsShared, code, code + sizeof(code), 0);
    WasmDecoder<Decoder::FullValidationTag>::OpcodeLength(&decoder, code);
    EXPECT_TRUE(decoder.failed());
  }

  void ExpectNonFailure(WasmOpcode opcode) {
    uint8_t maybe_prefix = WasmOpcodes::ExtractPrefix(opcode);
    uint8_t bytes[32]{0};
    if (WasmOpcodes::IsPrefixOpcode(static_cast<WasmOpcode>(maybe_prefix))) {
      bytes[0] = maybe_prefix;
      uint16_t index = ExtractPrefixedOpcodeBytes(opcode);
      uint8_t* p = &bytes[1];
      LEBHelper::write_u32v(&p, index);
    } else {
      DCHECK_LE(static_cast<uint32_t>(opcode), 0xFF);
      bytes[0] = static_cast<uint8_t>(opcode);
      // Special case: select_with_type insists on a {1} immediate.
      if (opcode == kExprSelectWithType) {
        bytes[1] = 1;
        bytes[2] = kAnyRefCode;
      }
    }
    WasmDetectedFeatures detected;
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    WasmDecoder<Decoder::BooleanValidationTag> decoder(
        this->zone(), nullptr, WasmEnabledFeatures::All(), &detected, nullptr,
        kIsShared, bytes, bytes + sizeof(bytes), 0);
    WasmDecoder<Decoder::BooleanValidationTag>::OpcodeLength(&decoder, bytes);
    EXPECT_TRUE(decoder.ok())
        << opcode << " aka " << WasmOpcodes::OpcodeName(opcode) << ": "
        << decoder.error().message();
  }
};

TEST_F(WasmOpcodeLengthTest, AllOpcodesImplemented) {
  // Make sure we didn't forget to add an opcode to the switch statements
  // in the {OpcodeLength} function. Due to nested handling of prefixed
  // opcodes, and the desire to support invalid modules, we can't rely on
  // compiler warnings for non-exhaustive cases lists here.
#define OPCODE(opcode, ...) ExpectNonFailure(kExpr##opcode);
  FOREACH_OPCODE(OPCODE)
#undef OPCODE
}

TEST_F(WasmOpcodeLengthTest, Statements) {
  ExpectLength(1, kExprNop);
  ExpectLength(1, kExprElse);
  ExpectLength(1, kExprEnd);
  ExpectLength(1, kExprSelect);
  ExpectLength(2, kExprCatch);
  ExpectLength(2, kExprDelegate);
  ExpectLength(2, kExprRethrow);
  ExpectLength(2, kExprBr);
  ExpectLength(2, kExprBrIf);
  ExpectLength(2, kExprThrow);
  ExpectLength(2, kExprBlock, kI32Code);
  ExpectLength(2, kExprLoop, kI32Code);
  ExpectLength(2, kExprIf, kI32Code);
  ExpectLength(2, kExprTry, kI32Code);
}

TEST_F(WasmOpcodeLengthTest, MiscExpressions) {
  ExpectLength(5, kExprF32Const);
  ExpectLength(9, kExprF64Const);
  ExpectLength(2, kExprRefNull);
  ExpectLength(2, kExprLocalGet);
  ExpectLength(2, kExprLocalSet);
  ExpectLength(2, kExprGlobalGet);
  ExpectLength(2, kExprGlobalSet);
  ExpectLength(2, kExprCallFunction);
  ExpectLength(3, kExprCallIndirect);
  ExpectLength(3, kExprSelectWithType, 1, kI32Code);
}

TEST_F(WasmOpcodeLengthTest, I32Const) {
  ExpectLength(2, kExprI32Const, U32V_1(1));
  ExpectLength(3, kExprI32Const, U32V_2(999));
  ExpectLength(4, kExprI32Const, U32V_3(9999));
  ExpectLength(5, kExprI32Const, U32V_4(999999));
  ExpectLength(6, kExprI32Const, U32V_5(99999999));
}

TEST_F(WasmOpcodeLengthTest, I64Const) {
  ExpectLength(2, kExprI64Const, U32V_1(1));
  ExpectLength(3, kExprI64Const, U32V_2(99));
  ExpectLength(4, kExprI64Const, U32V_3(9999));
  ExpectLength(5, kExprI64Const, U32V_4(99999));
  ExpectLength(6, kExprI64Const, U32V_5(9999999));
  ExpectLength(7, WASM_I64V_6(777777));
  ExpectLength(8, WASM_I64V_7(7777777));
  ExpectLength(9, WASM_I64V_8(77777777));
  ExpectLength(10, WASM_I64V_9(777777777));
}

TEST_F(WasmOpcodeLengthTest, VariableLength) {
  ExpectLength(2, kExprGlobalGet, U32V_1(1));
  ExpectLength(3, kExprGlobalGet, U32V_2(33));
  ExpectLength(4, kExprGlobalGet, U32V_3(44));
  ExpectLength(5, kExprGlobalGet, U32V_4(66));
  ExpectLength(6, kExprGlobalGet, U32V_5(77));

  ExpectLength(2, kExprRefFunc, U32V_1(1));
  ExpectLength(3, kExprRefFunc, U32V_2(33));
  ExpectLength(4, kExprRefFunc, U32V_3(44));
  ExpectLength(5, kExprRefFunc, U32V_4(66));
  ExpectLength(6, kExprRefFunc, U32V_5(77));

  ExpectLength(2, kExprTableGet, U32V_1(1));
  ExpectLength(3, kExprTableGet, U32V_2(33));
  ExpectLength(4, kExprTableGet, U32V_3(44));
  ExpectLength(5, kExprTableGet, U32V_4(66));
  ExpectLength(6, kExprTableGet, U32V_5(77));

  ExpectLength(2, kExprTableSet, U32V_1(1));
  ExpectLength(3, kExprTableSet, U32V_2(33));
  ExpectLength(4, kExprTableSet, U32V_3(44));
  ExpectLength(5, kExprTableSet, U32V_4(66));
  ExpectLength(6, kExprTableSet, U32V_5(77));

  ExpectLength(3, kExprCallIndirect, U32V_1(1), U32V_1(1));
  ExpectLength(4, kExprCallIndirect, U32V_1(1), U32V_2(33));
  ExpectLength(5, kExprCallIndirect, U32V_1(1), U32V_3(44));
  ExpectLength(6, kExprCallIndirect, U32V_1(1), U32V_4(66));
  ExpectLength(7, kExprCallIndirect, U32V_1(1), U32V_5(77));
}

TEST_F(WasmOpcodeLengthTest, LoadsAndStores) {
  ExpectLength(3, kExprI32LoadMem8S);
  ExpectLength(3, kExprI32LoadMem8U);
  ExpectLength(3, kExprI32LoadMem16S);
  ExpectLength(3, kExprI32LoadMem16U);
  ExpectLength(3, kExprI32LoadMem);
  ExpectLength(3, kExprI64LoadMem8S);
  ExpectLength(3, kExprI64LoadMem8U);
  ExpectLength(3, kExprI64LoadMem16S);
  ExpectLength(3, kExprI64LoadMem16U);
  ExpectLength(3, kExprI64LoadMem32S);
  ExpectLength(3, kExprI64LoadMem32U);
  ExpectLength(3, kExprI64LoadMem);
  ExpectLength(3, kExprF32LoadMem);
  ExpectLength(3, kExprF64LoadMem);

  ExpectLength(3, kExprI32StoreMem8);
  ExpectLength(3, kExprI32StoreMem16);
  ExpectLength(3, kExprI32StoreMem);
  ExpectLength(3, kExprI64StoreMem8);
  ExpectLength(3, kExprI64StoreMem16);
  ExpectLength(3, kExprI64StoreMem32);
  ExpectLength(3, kExprI64StoreMem);
  ExpectLength(3, kExprF32StoreMem);
  ExpectLength(3, kExprF64StoreMem);
}

TEST_F(WasmOpcodeLengthTest, MiscMemExpressions) {
  ExpectLength(2, kExprMemorySize);
  ExpectLength(2, kExprMemoryGrow);
}

TEST_F(WasmOpcodeLengthTest, SimpleExpressions) {
#define SIMPLE_OPCODE(name, byte, ...) byte,
  static constexpr uint8_t kSimpleOpcodes[] = {
      FOREACH_SIMPLE_OPCODE(SIMPLE_OPCODE)};
#undef SIMPLE_OPCODE
  for (uint8_t simple_opcode : kSimpleOpcodes) {
    ExpectLength(1, simple_opcode);
  }
}

TEST_F(WasmOpcodeLengthTest, SimdExpressions) {
#define TEST_SIMD(name, ...) ExpectLengthPrefixed(0, kExpr##name);
  FOREACH_SIMD_0_OPERAND_OPCODE(TEST_SIMD)
#undef TEST_SIMD
#define TEST_SIMD(name, ...) ExpectLengthPrefixed(1, kExpr##name);
  FOREACH_SIMD_1_OPERAND_OPCODE(TEST_SIMD)
#undef TEST_SIMD
  ExpectLengthPrefixed(16, kExprI8x16Shuffle);
  // test for bad simd opcode, 0xFF is encoded in two bytes.
  ExpectLength(3, kSimdPrefix, 0xFF, 0x1);
}

TEST_F(WasmOpcodeLengthTest, IllegalRefIndices) {
  ExpectFailure(kExprBlock, kRefNullCode, U32V_3(kV8MaxWasmTypes + 1));
  ExpectFailure(kExprBlock, kRefNullCode, U32V_4(0x01000000));
}

TEST_F(WasmOpcodeLengthTest, GCOpcodes) {
  // br_on_cast[_fail]: prefix + opcode + flags + br_depth + source_type +
  //                    target_type
  ExpectLength(6, 0xfb, kExprBrOnCast & 0xFF);
  ExpectLength(6, 0xfb, kExprBrOnCastFail & 0xFF);

  // struct.new, with leb immediate operand.
  ExpectLength(3, 0xfb, 0x07, 0x42);
  ExpectLength(4, 0xfb, 0x07, 0x80, 0x00);

  // string.new_utf8 with $mem=0.
  ExpectLength(4, 0xfb, 0x80, 0x01, 0x00);

  // string.as_wtf8.
  ExpectLength(3, 0xfb, 0x90, 0x01);
}

TEST_F(WasmOpcodeLengthTest, PrefixedOpcodesLEB) {
  // kExprI8x16Splat with a 3-byte LEB-encoded opcode.
  ExpectLength(4, 0xfd, 0x8f, 0x80, 0x00);

  // kExprI32SConvertSatF32 with a 4-byte LEB-encoded opcode.
  ExpectLength(5, 0xfc, 0x80, 0x80, 0x80, 0x00);

  // kExprAtomicNotify with a 2-byte LEB-encoded opcode, and 2 i32 imm for
  // memarg.
  ExpectLength(5, 0xfe, 0x80, 0x00, 0x00, 0x00);
}

class TypeReaderTest : public TestWithZone {
 public:
  HeapType DecodeHeapType(const uint8_t* start, const uint8_t* end) {
    Decoder decoder(start, end);
    auto [heap_type, length] =
        value_type_reader::read_heap_type<Decoder::FullValidationTag>(
            &decoder, start, enabled_features_);
    return heap_type;
  }

  // This variable is modified by WASM_FEATURE_SCOPE.
  WasmEnabledFeatures enabled_features_;
};

TEST_F(TypeReaderTest, HeapTypeDecodingTest) {
  HeapType heap_func = HeapType(HeapType::kFunc);
  HeapType heap_bottom = HeapType(HeapType::kBottom);

  // 1- to 5-byte representation of kFuncRefCode.
  {
    const uint8_t data[] = {kFuncRefCode};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_TRUE(result == heap_func);
  }
  {
    const uint8_t data[] = {kFuncRefCode | 0x80, 0x7F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_func);
  }
  {
    const uint8_t data[] = {kFuncRefCode | 0x80, 0xFF, 0x7F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_func);
  }
  {
    const uint8_t data[] = {kFuncRefCode | 0x80, 0xFF, 0xFF, 0x7F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_func);
  }
  {
    const uint8_t data[] = {kFuncRefCode | 0x80, 0xFF, 0xFF, 0xFF, 0x7F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_func);
  }

  {
    // Some negative number.
    const uint8_t data[] = {0xB4, 0x7F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_bottom);
  }

  {
    // This differs from kFuncRefCode by one bit outside the 1-byte LEB128
    // range. This should therefore NOT be decoded as HeapType::kFunc and
    // instead fail.
    const uint8_t data[] = {kFuncRefCode | 0x80, 0x6F};
    HeapType result = DecodeHeapType(data, data + sizeof(data));
    EXPECT_EQ(result, heap_bottom);
  }
}

class LocalDeclDecoderTest : public TestWithZone {
 public:
  WasmEnabledFeatures enabled_features_;

  size_t ExpectRun(ValueType* local_types, size_t pos, ValueType expected,
                   size_t count) {
    for (size_t i = 0; i < count; i++) {
      EXPECT_EQ(expected, local_types[pos++]);
    }
    return pos;
  }

  bool DecodeLocalDecls(BodyLocalDecls* decls, const uint8_t* start,
                        const uint8_t* end) {
    WasmModule module;
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    return ValidateAndDecodeLocalDeclsForTesting(
        enabled_features_, decls, &module, kIsShared, start, end, zone());
  }
};

TEST_F(LocalDeclDecoderTest, EmptyLocals) {
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, nullptr, nullptr);
  EXPECT_FALSE(result);
}

TEST_F(LocalDeclDecoderTest, NoLocals) {
  static const uint8_t data[] = {0};
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
  EXPECT_TRUE(result);
  EXPECT_EQ(0u, decls.num_locals);
}

TEST_F(LocalDeclDecoderTest, WrongLocalDeclsCount1) {
  static const uint8_t data[] = {1};
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
  EXPECT_FALSE(result);
}

TEST_F(LocalDeclDecoderTest, WrongLocalDeclsCount2) {
  static const uint8_t data[] = {
      2, 1, static_cast<uint8_t>(kWasmI32.value_type_code())};
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
  EXPECT_FALSE(result);
}

TEST_F(LocalDeclDecoderTest, OneLocal) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType type = kValueTypes[i];
    const uint8_t data[] = {1, 1, static_cast<uint8_t>(type.value_type_code())};
    BodyLocalDecls decls;
    bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
    EXPECT_TRUE(result);
    EXPECT_EQ(1u, decls.num_locals);

    EXPECT_EQ(type, decls.local_types[0]);
  }
}

TEST_F(LocalDeclDecoderTest, FiveLocals) {
  for (size_t i = 0; i < arraysize(kValueTypes); i++) {
    ValueType type = kValueTypes[i];
    const uint8_t data[] = {1, 5, static_cast<uint8_t>(type.value_type_code())};
    BodyLocalDecls decls;
    bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
    EXPECT_TRUE(result);
    EXPECT_EQ(sizeof(data), decls.encoded_size);
    EXPECT_EQ(5u, decls.num_locals);
    ExpectRun(decls.local_types, 0, type, 5);
  }
}

TEST_F(LocalDeclDecoderTest, MixedLocals) {
  for (uint8_t a = 0; a < 3; a++) {
    for (uint8_t b = 0; b < 3; b++) {
      for (uint8_t c = 0; c < 3; c++) {
        for (uint8_t d = 0; d < 3; d++) {
          const uint8_t data[] = {4, a,        kI32Code, b,       kI64Code,
                                  c, kF32Code, d,        kF64Code};
          BodyLocalDecls decls;
          bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
          EXPECT_TRUE(result);
          EXPECT_EQ(sizeof(data), decls.encoded_size);
          EXPECT_EQ(static_cast<uint32_t>(a + b + c + d), decls.num_locals);

          size_t pos = 0;
          pos = ExpectRun(decls.local_types, pos, kWasmI32, a);
          pos = ExpectRun(decls.local_types, pos, kWasmI64, b);
          pos = ExpectRun(decls.local_types, pos, kWasmF32, c);
          pos = ExpectRun(decls.local_types, pos, kWasmF64, d);
        }
      }
    }
  }
}

TEST_F(LocalDeclDecoderTest, UseEncoder) {
  const uint8_t* data = nullptr;
  const uint8_t* end = nullptr;
  LocalDeclEncoder local_decls(zone());

  local_decls.AddLocals(5, kWasmF32);
  local_decls.AddLocals(1337, kWasmI32);
  local_decls.AddLocals(212, kWasmI64);
  local_decls.Prepend(zone(), &data, &end);

  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, end);
  EXPECT_TRUE(result);
  EXPECT_EQ(5u + 1337u + 212u, decls.num_locals);

  size_t pos = 0;
  pos = ExpectRun(decls.local_types, pos, kWasmF32, 5);
  pos = ExpectRun(decls.local_types, pos, kWasmI32, 1337);
  pos = ExpectRun(decls.local_types, pos, kWasmI64, 212);
}

TEST_F(LocalDeclDecoderTest, ExnRef) {
  WASM_FEATURE_SCOPE(exnref);
  const uint8_t data[] = {1, 1,
                          static_cast<uint8_t>(kWasmExnRef.value_type_code())};
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, data + sizeof(data));
  EXPECT_TRUE(result);
  EXPECT_EQ(1u, decls.num_locals);
  EXPECT_EQ(kWasmExnRef, decls.local_types[0]);
}

TEST_F(LocalDeclDecoderTest, InvalidTypeIndex) {
  const uint8_t* data = nullptr;
  const uint8_t* end = nullptr;
  LocalDeclEncoder local_decls(zone());

  local_decls.AddLocals(1, ValueType::RefNull(0));
  BodyLocalDecls decls;
  bool result = DecodeLocalDecls(&decls, data, end);
  EXPECT_FALSE(result);
}

class BytecodeIteratorTest : public TestWithZone {};

TEST_F(BytecodeIteratorTest, SimpleForeach) {
  uint8_t code[] = {WASM_IF_ELSE(WASM_ZERO, WASM_ZERO, WASM_ZERO)};
  BytecodeIterator iter(code, code + sizeof(code));
  WasmOpcode expected[] = {kExprI32Const, kExprIf,       kExprI32Const,
                           kExprElse,     kExprI32Const, kExprEnd};
  size_t pos = 0;
  for (WasmOpcode opcode : iter.opcodes()) {
    if (pos >= arraysize(expected)) {
      EXPECT_TRUE(false);
      break;
    }
    EXPECT_EQ(expected[pos++], opcode);
  }
  EXPECT_EQ(arraysize(expected), pos);
}

TEST_F(BytecodeIteratorTest, ForeachTwice) {
  uint8_t code[] = {WASM_IF_ELSE(WASM_ZERO, WASM_ZERO, WASM_ZERO)};
  BytecodeIterator iter(code, code + sizeof(code));
  int count = 0;

  count = 0;
  for (WasmOpcode opcode : iter.opcodes()) {
    USE(opcode);
    count++;
  }
  EXPECT_EQ(6, count);

  count = 0;
  for (WasmOpcode opcode : iter.opcodes()) {
    USE(opcode);
    count++;
  }
  EXPECT_EQ(6, count);
}

TEST_F(BytecodeIteratorTest, ForeachOffset) {
  uint8_t code[] = {WASM_IF_ELSE(WASM_ZERO, WASM_ZERO, WASM_ZERO)};
  BytecodeIterator iter(code, code + sizeof(code));
  int count = 0;

  count = 0;
  for (auto offset : iter.offsets()) {
    USE(offset);
    count++;
  }
  EXPECT_EQ(6, count);

  count = 0;
  for (auto offset : iter.offsets()) {
    USE(offset);
    count++;
  }
  EXPECT_EQ(6, count);
}

TEST_F(BytecodeIteratorTest, WithLocalDecls) {
  uint8_t code[] = {1, 1, kI32Code, WASM_I32V_1(9), WASM_I32V_1(11)};
  BodyLocalDecls decls;
  BytecodeIterator iter(code, code + sizeof(code), &decls, zone());

  EXPECT_EQ(3u, decls.encoded_size);
  EXPECT_EQ(3u, iter.pc_offset());
  EXPECT_TRUE(iter.has_next());
  EXPECT_EQ(kExprI32Const, iter.current());
  iter.next();
  EXPECT_TRUE(iter.has_next());
  EXPECT_EQ(kExprI32Const, iter.current());
  iter.next();
  EXPECT_FALSE(iter.has_next());
}

/*******************************************************************************
 * Memory64 tests.
 ******************************************************************************/

class FunctionBodyDecoderTestOnBothMemoryTypes
    : public FunctionBodyDecoderTestBase<
          WithDefaultPlatformMixin<::testing::TestWithParam<MemoryType>>> {
 public:
  FunctionBodyDecoderTestOnBothMemoryTypes() {
    if (is_memory64()) enabled_features_.Add(WasmEnabledFeature::memory64);
  }

  bool is_memory64() const { return GetParam() == kMemory64; }
};

std::string PrintMemoryType(::testing::TestParamInfo<MemoryType> info) {
  switch (info.param) {
    case kMemory32:
      return "kMemory32";
    case kMemory64:
      return "kMemory64";
  }
  UNREACHABLE();
}

INSTANTIATE_TEST_SUITE_P(MemoryTypes, FunctionBodyDecoderTestOnBothMemoryTypes,
                         ::testing::Values(kMemory32, kMemory64),
                         PrintMemoryType);

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, IndexTypes) {
  builder.AddMemory(GetParam());
  Validate(!is_memory64(), sigs.i_v(),
           {WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO)});
  Validate(is_memory64(), sigs.i_v(),
           {WASM_LOAD_MEM(MachineType::Int32(), WASM_ZERO64)});
  Validate(!is_memory64(), sigs.v_v(),
           {WASM_STORE_MEM(MachineType::Int32(), WASM_ZERO, WASM_ZERO)});
  Validate(is_memory64(), sigs.v_v(),
           {WASM_STORE_MEM(MachineType::Int32(), WASM_ZERO64, WASM_ZERO)});
}

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, 64BitOffsetOnMemory32) {
  // Check that with memory64 enabled, the offset is always decoded as u64, even
  // if the memory is declared as 32-bit memory.
  builder.AddMemory(kMemory32);
  // Offset is zero encoded in 5 bytes (always works).
  Validate(true, sigs.i_v(),
           {WASM_LOAD_MEM_OFFSET(MachineType::Int32(), U64V_5(0), WASM_ZERO)});
  // Offset is zero encoded in 6 bytes (works if memory64 is enabled).
  Validate(is_memory64(), sigs.i_v(),
           {WASM_LOAD_MEM_OFFSET(MachineType::Int32(), U64V_6(0), WASM_ZERO)});
  // Same with store.
  Validate(true, sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(), U64V_5(0), WASM_ZERO,
                                  WASM_ZERO)});
  Validate(is_memory64(), sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(), U64V_6(0), WASM_ZERO,
                                  WASM_ZERO)});
  // Offset is 2^32+2 (fails validation on memory32).
  Validate(false, sigs.i_v(),
           {WASM_LOAD_MEM_OFFSET(MachineType::Int32(),
                                 U64V_6((uint64_t{1} << 32) + 2), WASM_ZERO)});
  Validate(false, sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(),
                                  U64V_6((uint64_t{1} << 32) + 2), WASM_ZERO,
                                  WASM_ZERO)});
}

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, 64BitOffsetOnMemory64) {
  // Same as above, but on a 64-bit memory.
  builder.AddMemory(kMemory64);
  // Offset is zero encoded in 5 bytes.
  Validate(
      true, sigs.i_v(),
      {WASM_LOAD_MEM_OFFSET(MachineType::Int32(), U64V_5(0), WASM_ZERO64)});
  // Offset is zero encoded in 6 bytes (works if memory64 is enabled).
  Validate(
      is_memory64(), sigs.i_v(),
      {WASM_LOAD_MEM_OFFSET(MachineType::Int32(), U64V_6(0), WASM_ZERO64)});
  // Same with store.
  Validate(true, sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(), U64V_5(0), WASM_ZERO64,
                                  WASM_ZERO)});
  Validate(is_memory64(), sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(), U64V_6(0), WASM_ZERO64,
                                  WASM_ZERO)});
  // Offset is 2^32+2 (validates on memory64).
  Validate(
      is_memory64(), sigs.i_v(),
      {WASM_LOAD_MEM_OFFSET(MachineType::Int32(),
                            U64V_6((uint64_t{1} << 32) + 2), WASM_ZERO64)});
  Validate(is_memory64(), sigs.v_v(),
           {WASM_STORE_MEM_OFFSET(MachineType::Int32(),
                                  U64V_6((uint64_t{1} << 32) + 2), WASM_ZERO64,
                                  WASM_ZERO)});
}

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, MemorySize) {
  builder.AddMemory(GetParam());
  // memory.size returns i32 on memory32.
  Validate(!is_memory64(), sigs.v_v(),
           {WASM_MEMORY_SIZE, kExprI32Eqz, kExprDrop});
  // memory.size returns i64 on memory64.
  Validate(is_memory64(), sigs.v_v(),
           {WASM_MEMORY_SIZE, kExprI64Eqz, kExprDrop});
}

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, MemoryGrow) {
  builder.AddMemory(GetParam());
  // memory.grow is i32->i32 memory32.
  Validate(!is_memory64(), sigs.i_i(), {WASM_MEMORY_GROW(WASM_LOCAL_GET(0))});
  // memory.grow is i64->i64 memory32.
  Validate(is_memory64(), sigs.l_l(), {WASM_MEMORY_GROW(WASM_LOCAL_GET(0))});
  // any other combination always fails.
  auto sig_l_i = MakeSig::Returns(kWasmI64).Params(kWasmI32);
  ExpectFailure(&sig_l_i, {WASM_MEMORY_GROW(WASM_LOCAL_GET(0))});
  auto sig_i_l = MakeSig::Returns(kWasmI32).Params(kWasmI64);
  ExpectFailure(&sig_i_l, {WASM_MEMORY_GROW(WASM_LOCAL_GET(0))});
}

TEST_P(FunctionBodyDecoderTestOnBothMemoryTypes, CopyDifferentMemTypes) {
  MemoryType mem_type = GetParam();
  MemoryType other_mem_type = mem_type == kMemory32 ? kMemory64 : kMemory32;
  uint8_t memory0 = builder.AddMemory(mem_type);
  uint8_t memory1 = builder.AddMemory(other_mem_type);

  // Copy from memory0 to memory1 with types i32/i64/i32. Valid if memory0 is
  // 64-bit.
  Validate(
      mem_type == kMemory64, sigs.v_v(),
      {WASM_MEMORY_COPY(memory1, memory0, WASM_ZERO, WASM_ZERO64, WASM_ZERO)},
      kAppendEnd);
  // Copy from memory0 to memory1 with types i64/i32/i32. Valid if memory0 is
  // 32-bit.
  Validate(
      mem_type == kMemory32, sigs.v_v(),
      {WASM_MEMORY_COPY(memory1, memory0, WASM_ZERO64, WASM_ZERO, WASM_ZERO)},
      kAppendEnd);
  // Passing the size as i64 is always invalid because one memory is always
  // 32-bit.
  ExpectFailure(
      sigs.v_v(),
      {WASM_MEMORY_COPY(memory1, memory0, WASM_ZERO, WASM_ZERO64, WASM_ZERO64)},
      kAppendEnd,
      mem_type == kMemory32
          ? "memory.copy[0] expected type i64, found i32.const of type i32"
          : "memory.copy[2] expected type i32, found i64.const of type i64");
  ExpectFailure(
      sigs.v_v(),
      {WASM_MEMORY_COPY(memory1, memory0, WASM_ZERO64, WASM_ZERO, WASM_ZERO64)},
      kAppendEnd,
      mem_type == kMemory32
          ? "memory.copy[2] expected type i32, found i64.const of type i64"
          : "memory.copy[0] expected type i32, found i64.const of type i64");
}

/*******************************************************************************
 * Multi-memory tests.
 ******************************************************************************/

TEST_F(FunctionBodyDecoderTest, ExtendedMemoryAccessImmediate) {
  builder.AddMemory();
  // The memory index can be encoded in a separate field, after a 0x40
  // alignment. For now, only memory index 0 is allowed.
  ExpectValidates(sigs.i_v(), {WASM_ZERO, kExprI32LoadMem, 0x40 /* alignment */,
                               0 /* memory index */, 0 /* offset */});
  // The memory index is LEB-encoded, so index 0 can also be store in 5 bytes.
  ExpectValidates(sigs.i_v(), {WASM_ZERO, kExprI32LoadMem, 0x40 /* alignment */,
                               U32V_5(0) /* memory index */, 0 /* offset */});
  // Memory index 1 is invalid.
  ExpectFailure(sigs.i_v(), {WASM_ZERO, kExprI32LoadMem, 0x40 /* alignment */,
                             1 /* memory index */, 0 /* offset */});
  // Add another memory; memory index 1 should be valid then.
  builder.AddMemory();
  ExpectValidates(sigs.i_v(), {WASM_ZERO, kExprI32LoadMem, 0x40 /* alignment */,
                               1 /* memory index */, 0 /* offset */});
  // Memory index 2 is still invalid.
  ExpectFailure(sigs.i_v(), {WASM_ZERO, kExprI32LoadMem, 0x40 /* alignment */,
                             2 /* memory index */, 0 /* offset */});
}

/*******************************************************************************
 * Table64.
 ******************************************************************************/

class FunctionBodyDecoderTestTable64
    : public FunctionBodyDecoderTestBase<
          WithDefaultPlatformMixin<::testing::TestWithParam<TableType>>> {
 public:
  FunctionBodyDecoderTestTable64() {
    if (is_table64()) enabled_features_.Add(WasmEnabledFeature::memory64);
  }

  bool is_table64() const { return GetParam() == kTable64; }
};

std::string PrintTableType(::testing::TestParamInfo<TableType> info) {
  switch (info.param) {
    case kTable32:
      return "kTable32";
    case kTable64:
      return "kTable64";
  }
  UNREACHABLE();
}

INSTANTIATE_TEST_SUITE_P(Table64Tests, FunctionBodyDecoderTestTable64,
                         ::testing::Values(kTable32, kTable64), PrintTableType);

TEST_P(FunctionBodyDecoderTestTable64, Table64Set) {
  TableType table_type = GetParam();
  uint8_t tab_ref1 = builder.AddTable(kWasmExternRef, 10, true, 20, table_type);
  uint8_t tab_func1 = builder.AddTable(kWasmFuncRef, 20, true, 30, table_type);

  ValueType sig_types[]{kWasmExternRef, kWasmFuncRef};
  FunctionSig sig(0, 2, sig_types);
  uint8_t local_ref = 0;
  uint8_t local_func = 1;

  Validate(is_table64(), &sig,
           {WASM_TABLE_SET(tab_ref1, WASM_I64V(6), WASM_LOCAL_GET(local_ref))});
  Validate(
      is_table64(), &sig,
      {WASM_TABLE_SET(tab_func1, WASM_I64V(7), WASM_LOCAL_GET(local_func))});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Get) {
  TableType table_type = GetParam();
  uint8_t tab_ref1 = builder.AddTable(kWasmExternRef, 10, true, 20, table_type);
  uint8_t tab_func1 = builder.AddTable(kWasmFuncRef, 20, true, 30, table_type);

  ValueType sig_types[]{kWasmExternRef, kWasmFuncRef};
  FunctionSig sig(0, 2, sig_types);
  uint8_t local_ref = 0;
  uint8_t local_func = 1;

  Validate(is_table64(), &sig,
           {WASM_LOCAL_SET(local_ref, WASM_TABLE_GET(tab_ref1, WASM_I64V(6)))});
  Validate(
      is_table64(), &sig,
      {WASM_LOCAL_SET(local_func, WASM_TABLE_GET(tab_func1, WASM_I64V(5)))});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64CallIndirect) {
  TableType table_type = GetParam();
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, false, 20, table_type);

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  Validate(is_table64(), sig, {WASM_CALL_INDIRECT(sig0, WASM_ZERO64)});
  Validate(is_table64(), sig,
           {WASM_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO64)});
  Validate(is_table64(), sig,
           {WASM_CALL_INDIRECT(sig2, WASM_I32V_1(32), WASM_I32V_2(72),
                               WASM_ZERO64)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64ReturnCallIndirect) {
  TableType table_type = GetParam();
  const FunctionSig* sig = sigs.i_i();
  builder.AddTable(kWasmFuncRef, 20, true, 30, table_type);

  uint8_t sig0 = builder.AddSignature(sigs.i_v());
  uint8_t sig1 = builder.AddSignature(sigs.i_i());
  uint8_t sig2 = builder.AddSignature(sigs.i_ii());

  Validate(is_table64(), sig, {WASM_RETURN_CALL_INDIRECT(sig0, WASM_ZERO64)});
  Validate(is_table64(), sig,
           {WASM_RETURN_CALL_INDIRECT(sig1, WASM_I32V_1(22), WASM_ZERO64)});
  Validate(is_table64(), sig,
           {WASM_RETURN_CALL_INDIRECT(sig2, WASM_I32V_1(32), WASM_I32V_2(72),
                                      WASM_ZERO64)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Grow) {
  TableType table_type = GetParam();
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, 10, true, 20, table_type);
  uint8_t tab_ref = builder.AddTable(kWasmExternRef, 10, true, 20, table_type);

  Validate(
      is_table64(), sigs.l_c(),
      {WASM_TABLE_GROW(tab_func, WASM_REF_NULL(kFuncRefCode), WASM_ONE64)});
  Validate(
      is_table64(), sigs.l_a(),
      {WASM_TABLE_GROW(tab_ref, WASM_REF_NULL(kExternRefCode), WASM_ONE64)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Size) {
  TableType table_type = GetParam();
  int tab = builder.AddTable(kWasmFuncRef, 10, true, 20, table_type);
  Validate(is_table64(), sigs.l_v(), {WASM_TABLE_SIZE(tab)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Fill) {
  TableType table_type = GetParam();
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, 10, true, 20, table_type);
  uint8_t tab_ref = builder.AddTable(kWasmExternRef, 10, true, 20, table_type);
  Validate(is_table64(), sigs.v_c(),
           {WASM_TABLE_FILL(tab_func, WASM_ONE64, WASM_REF_NULL(kFuncRefCode),
                            WASM_ONE64)});
  Validate(is_table64(), sigs.v_a(),
           {WASM_TABLE_FILL(tab_ref, WASM_ONE64, WASM_REF_NULL(kExternRefCode),
                            WASM_ONE64)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Init) {
  TableType table_type = GetParam();
  uint8_t tab_func = builder.AddTable(kWasmFuncRef, table_type);
  uint8_t elem_seg = builder.AddPassiveElementSegment(wasm::kWasmFuncRef);

  Validate(
      is_table64(), sigs.v_v(),
      {WASM_TABLE_INIT(tab_func, elem_seg, WASM_ZERO64, WASM_ZERO, WASM_ZERO)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64Copy) {
  TableType table_type = GetParam();
  uint8_t table = builder.AddTable(wasm::kWasmVoid, table_type);

  Validate(
      is_table64(), sigs.v_v(),
      {WASM_TABLE_COPY(table, table, WASM_ZERO64, WASM_ZERO64, WASM_ZERO64)});
}

TEST_P(FunctionBodyDecoderTestTable64, Table64CopyDifferentTypes) {
  TableType table_type = GetParam();
  TableType other_table_type = table_type == kTable32 ? kTable64 : kTable32;
  uint8_t table = builder.AddTable(wasm::kWasmVoid, table_type);
  uint8_t other_table = builder.AddTable(wasm::kWasmVoid, other_table_type);

  // Copy from `table` to `other_table` with types i32/i64/i32. Valid if `table`
  // is table64 (and hence `other_table` is table32).
  Validate(
      table_type == kTable64, sigs.v_v(),
      {WASM_TABLE_COPY(other_table, table, WASM_ZERO, WASM_ZERO64, WASM_ZERO)},
      kAppendEnd);
  // Copy from `table` to `other_table` with types i64/i32/i32. Valid if `table`
  // is table32 (and hence `other_table` is table64).
  Validate(
      table_type == kTable32, sigs.v_v(),
      {WASM_TABLE_COPY(other_table, table, WASM_ZERO64, WASM_ZERO, WASM_ZERO)},
      kAppendEnd);
  // Passing the size as i64 is always invalid because one table is always 32
  // bit.
  ExpectFailure(
      sigs.v_v(),
      {WASM_TABLE_COPY(other_table, table, WASM_ZERO, WASM_ZERO64,
                       WASM_ZERO64)},
      kAppendEnd,
      table_type == kTable64
          ? "table.copy[2] expected type i32, found i64.const of type i64"
          : "table.copy[0] expected type i64, found i32.const of type i32");
  ExpectFailure(
      sigs.v_v(),
      {WASM_TABLE_COPY(other_table, table, WASM_ZERO64, WASM_ZERO,
                       WASM_ZERO64)},
      kAppendEnd,
      table_type == kTable32
          ? "table.copy[2] expected type i32, found i64.const of type i64"
          : "table.copy[0] expected type i32, found i64.const of type i64");
}

#undef B1
#undef B2
#undef B3
#undef WASM_IF_OP
#undef WASM_LOOP_OP
#undef WASM_BRV_IF_ZERO
#undef EXPECT_OK

}  // namespace v8::internal::wasm
