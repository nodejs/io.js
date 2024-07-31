// Copyright 2014 the V8 project authors. All rights reserved. Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include "src/base/memory.h"
#include "src/codegen/external-reference.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/value-helper.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-external-refs.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOp(ExternalReference ref,
                                     void (*wrapper)(Address),
                                     Iterable inputs) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  constexpr size_t kBufferSize = std::max(sizeof(InType), sizeof(OutType));
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m.ExternalConstant(ref);
  m.CallCFunction(
      function, MachineType::Pointer(),
      std::make_pair(MachineType::Pointer(), m.PointerConstant(buffer)));
  m.Return(m.Int32Constant(4356));

  for (InType input : inputs) {
    WriteUnalignedValue<InType>(buffer_addr, input);

    CHECK_EQ(4356, m.Call());
    OutType output = ReadUnalignedValue<OutType>(buffer_addr);

    WriteUnalignedValue<InType>(buffer_addr, input);
    wrapper(buffer_addr);
    OutType expected_output = ReadUnalignedValue<OutType>(buffer_addr);

    CHECK_EQ(expected_output, output);
  }
}

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOpWithOutputAndReturn(
    ExternalReference ref, int32_t (*wrapper)(Address), Iterable inputs) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  constexpr size_t kBufferSize = std::max(sizeof(InType), sizeof(OutType));
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m.PointerConstant(buffer))));

  for (InType input : inputs) {
    WriteUnalignedValue<InType>(buffer_addr, input);

    int32_t ret = m.Call();
    OutType output = ReadUnalignedValue<OutType>(buffer_addr);

    WriteUnalignedValue<InType>(buffer_addr, input);
    int32_t expected_ret = wrapper(buffer_addr);
    OutType expected_output = ReadUnalignedValue<OutType>(buffer_addr);

    CHECK_EQ(expected_ret, ret);
    CHECK_EQ(expected_output, output);
  }
}

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOpWithReturn(ExternalReference ref,
                                               OutType (*wrapper)(InType),
                                               Iterable inputs) {
  static_assert(std::is_same_v<uint32_t, InType> ||
                std::is_same_v<uint64_t, InType>);
  MachineType input_type = std::is_same_v<uint32_t, InType>
                               ? MachineType::Uint32()
                               : MachineType::Uint64();
  BufferedRawMachineAssemblerTester<uint32_t> m(input_type);
  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(function, MachineType::Int32(),
                           {{input_type, m.Parameter(0)}}));

  for (InType input : inputs) {
    OutType ret = m.Call(input);
    OutType expected_ret = wrapper(input);
    CHECK_EQ(expected_ret, ret);
  }
}

template <typename Type>
bool isnan(Type value) {
  return false;
}
template <>
bool isnan<float>(float value) {
  return std::isnan(value);
}
template <>
bool isnan<double>(double value) {
  return std::isnan(value);
}

template <typename Type, typename Iterable>
void TestExternalReference_UnOp(ExternalReference ref, void (*wrapper)(Address),
                                Iterable inputs) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  constexpr size_t kBufferSize = sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m.ExternalConstant(ref);
  m.CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m.PointerConstant(buffer)));
  m.Return(m.Int32Constant(4356));

  for (Type input : inputs) {
    WriteUnalignedValue<Type>(buffer_addr, input);
    CHECK_EQ(4356, m.Call());
    Type output = ReadUnalignedValue<Type>(buffer_addr);

    WriteUnalignedValue<Type>(buffer_addr, input);
    wrapper(buffer_addr);
    Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

    if (isnan(expected_output) && isnan(output)) continue;
    CHECK_EQ(expected_output, output);
  }
}

template <typename Type, typename Iterable>
void TestExternalReference_BinOp(ExternalReference ref,
                                 void (*wrapper)(Address), Iterable inputs) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  constexpr size_t kBufferSize = 2 * sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m.ExternalConstant(ref);
  m.CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m.PointerConstant(buffer)));
  m.Return(m.Int32Constant(4356));

  for (Type input1 : inputs) {
    for (Type input2 : inputs) {
      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      CHECK_EQ(4356, m.Call());
      Type output = ReadUnalignedValue<Type>(buffer_addr);

      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      wrapper(buffer_addr);
      Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

      if (isnan(expected_output) && isnan(output)) continue;
      CHECK_EQ(expected_output, output);
    }
  }
}

template <typename Type, typename Iterable>
void TestExternalReference_BinOpWithReturn(ExternalReference ref,
                                           int32_t (*wrapper)(Address),
                                           Iterable inputs) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  constexpr size_t kBufferSize = 2 * sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m.PointerConstant(buffer))));

  for (Type input1 : inputs) {
    for (Type input2 : inputs) {
      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      int32_t ret = m.Call();
      Type output = ReadUnalignedValue<Type>(buffer_addr);

      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      int32_t expected_ret = wrapper(buffer_addr);
      Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

      CHECK_EQ(expected_ret, ret);
      if (isnan(expected_output) && isnan(output)) continue;
      CHECK_EQ(expected_output, output);
    }
  }
}

#if V8_ENABLE_WEBASSEMBLY
TEST(RunCallF32Trunc) {
  ExternalReference ref = ExternalReference::wasm_f32_trunc();
  TestExternalReference_UnOp<float>(ref, wasm::f32_trunc_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32Floor) {
  ExternalReference ref = ExternalReference::wasm_f32_floor();
  TestExternalReference_UnOp<float>(ref, wasm::f32_floor_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32Ceil) {
  ExternalReference ref = ExternalReference::wasm_f32_ceil();
  TestExternalReference_UnOp<float>(ref, wasm::f32_ceil_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32RoundTiesEven) {
  ExternalReference ref = ExternalReference::wasm_f32_nearest_int();
  TestExternalReference_UnOp<float>(ref, wasm::f32_nearest_int_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF64Trunc) {
  ExternalReference ref = ExternalReference::wasm_f64_trunc();
  TestExternalReference_UnOp<double>(ref, wasm::f64_trunc_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64Floor) {
  ExternalReference ref = ExternalReference::wasm_f64_floor();
  TestExternalReference_UnOp<double>(ref, wasm::f64_floor_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64Ceil) {
  ExternalReference ref = ExternalReference::wasm_f64_ceil();
  TestExternalReference_UnOp<double>(ref, wasm::f64_ceil_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64RoundTiesEven) {
  ExternalReference ref = ExternalReference::wasm_f64_nearest_int();
  TestExternalReference_UnOp<double>(ref, wasm::f64_nearest_int_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallInt64ToFloat32) {
  ExternalReference ref = ExternalReference::wasm_int64_to_float32();
  TestExternalReference_ConvertOp<int64_t, float>(
      ref, wasm::int64_to_float32_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallUint64ToFloat32) {
  ExternalReference ref = ExternalReference::wasm_uint64_to_float32();
  TestExternalReference_ConvertOp<uint64_t, float>(
      ref, wasm::uint64_to_float32_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallInt64ToFloat64) {
  ExternalReference ref = ExternalReference::wasm_int64_to_float64();
  TestExternalReference_ConvertOp<int64_t, double>(
      ref, wasm::int64_to_float64_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallUint64ToFloat64) {
  ExternalReference ref = ExternalReference::wasm_uint64_to_float64();
  TestExternalReference_ConvertOp<uint64_t, double>(
      ref, wasm::uint64_to_float64_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallFloat32ToInt64) {
  ExternalReference ref = ExternalReference::wasm_float32_to_int64();
  TestExternalReference_ConvertOpWithOutputAndReturn<float, int64_t>(
      ref, wasm::float32_to_int64_wrapper, ValueHelper::float32_vector());
}

TEST(RunCallFloat32ToUint64) {
  ExternalReference ref = ExternalReference::wasm_float32_to_uint64();
  TestExternalReference_ConvertOpWithOutputAndReturn<float, uint64_t>(
      ref, wasm::float32_to_uint64_wrapper, ValueHelper::float32_vector());
}

TEST(RunCallFloat64ToInt64) {
  ExternalReference ref = ExternalReference::wasm_float64_to_int64();
  TestExternalReference_ConvertOpWithOutputAndReturn<double, int64_t>(
      ref, wasm::float64_to_int64_wrapper, ValueHelper::float64_vector());
}

TEST(RunCallFloat64ToUint64) {
  ExternalReference ref = ExternalReference::wasm_float64_to_uint64();
  TestExternalReference_ConvertOpWithOutputAndReturn<double, uint64_t>(
      ref, wasm::float64_to_uint64_wrapper, ValueHelper::float64_vector());
}

TEST(RunCallInt64Div) {
  ExternalReference ref = ExternalReference::wasm_int64_div();
  TestExternalReference_BinOpWithReturn<int64_t>(ref, wasm::int64_div_wrapper,
                                                 ValueHelper::int64_vector());
}

TEST(RunCallInt64Mod) {
  ExternalReference ref = ExternalReference::wasm_int64_mod();
  TestExternalReference_BinOpWithReturn<int64_t>(ref, wasm::int64_mod_wrapper,
                                                 ValueHelper::int64_vector());
}

TEST(RunCallUint64Div) {
  ExternalReference ref = ExternalReference::wasm_uint64_div();
  TestExternalReference_BinOpWithReturn<uint64_t>(ref, wasm::uint64_div_wrapper,
                                                  ValueHelper::uint64_vector());
}

TEST(RunCallUint64Mod) {
  ExternalReference ref = ExternalReference::wasm_uint64_mod();
  TestExternalReference_BinOpWithReturn<uint64_t>(ref, wasm::uint64_mod_wrapper,
                                                  ValueHelper::uint64_vector());
}

TEST(RunCallWord32Ctz) {
  ExternalReference ref = ExternalReference::wasm_word32_ctz();
  TestExternalReference_ConvertOpWithReturn<uint32_t, uint32_t>(
      ref, base::bits::CountTrailingZeros, ValueHelper::int32_vector());
}

TEST(RunCallWord64Ctz) {
  // Word64 operations are not supported on 32 bit.
  if (kSystemPointerSize == 4) return;
  ExternalReference ref = ExternalReference::wasm_word64_ctz();
  TestExternalReference_ConvertOpWithReturn<uint64_t, uint32_t>(
      ref, base::bits::CountTrailingZeros, ValueHelper::int64_vector());
}

TEST(RunCallWord32Popcnt) {
  ExternalReference ref = ExternalReference::wasm_word32_popcnt();
  TestExternalReference_ConvertOpWithReturn<uint32_t, uint32_t>(
      ref, base::bits::CountPopulation, ValueHelper::int32_vector());
}

TEST(RunCallWord64Popcnt) {
  // Word64 operations are not supported on 32 bit.
  if (kSystemPointerSize == 4) return;
  ExternalReference ref = ExternalReference::wasm_word64_popcnt();
  TestExternalReference_ConvertOpWithReturn<uint64_t, uint32_t>(
      ref, base::bits::CountPopulation, ValueHelper::int64_vector());
}

TEST(RunCallFloat64Pow) {
  ExternalReference ref = ExternalReference::wasm_float64_pow();
  TestExternalReference_BinOp<double>(ref, wasm::float64_pow_wrapper,
                                      ValueHelper::float64_vector());
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename T>
MachineType MachineTypeForCType() {
  return MachineType::AnyTagged();
}

template <>
MachineType MachineTypeForCType<int64_t>() {
  return MachineType::Int64();
}

template <>
MachineType MachineTypeForCType<int32_t>() {
  return MachineType::Int32();
}

template <>
MachineType MachineTypeForCType<double>() {
  return MachineType::Float64();
}

#define SIGNATURE_TYPES(TYPE, IDX, VALUE) MachineTypeForCType<TYPE>()

#define PARAM_PAIRS(TYPE, IDX, VALUE) \
  std::make_pair(MachineTypeForCType<TYPE>(), m.Parameter(IDX))

#define CALL_ARGS(TYPE, IDX, VALUE) static_cast<TYPE>(VALUE)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
union Int64OrDoubleUnion {
  int64_t int64_t_value;
  double double_value;
};

#define CHECK_ARG_I(TYPE, IDX, VALUE) \
  (result = result && (arg##IDX.TYPE##_value == VALUE))

#define ReturnType v8::AnyCType
MachineType machine_type = MachineType::Int64();

#define CHECK_RESULT(CALL, EXPECT) \
  v8::AnyCType ret = CALL;         \
  CHECK_EQ(ret.int64_value, EXPECT);

#define IF_SIMULATOR_ADD_SIGNATURE                                     \
  EncodedCSignature sig = m.call_descriptor()->ToEncodedCSignature();  \
  m.main_isolate()->simulator_data()->AddSignatureForTargetForTesting( \
      func_address, sig);
#else  // def V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
#define IF_SIMULATOR_ADD_SIGNATURE

#ifdef V8_TARGET_ARCH_64_BIT
#define ReturnType int64_t
MachineType machine_type = MachineType::Int64();
#else  // V8_TARGET_ARCH_64_BIT
#define ReturnType int32_t
MachineType machine_type = MachineType::Int32();
#endif  // V8_TARGET_ARCH_64_BIT

#define CHECK_ARG_I(TYPE, IDX, VALUE) (result = result && (arg##IDX == VALUE))

#define CHECK_RESULT(CALL, EXPECT) \
  int64_t ret = CALL;              \
  CHECK_EQ(ret, EXPECT);

#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

#define SIGNATURE_TEST(NAME, SIGNATURE, FUNC)                                  \
  TEST(NAME) {                                                                 \
    RawMachineAssemblerTester<ReturnType> m(SIGNATURE(SIGNATURE_TYPES));       \
                                                                               \
    Address func_address = FUNCTION_ADDR(&FUNC);                               \
    ExternalReference::Type func_type = ExternalReference::FAST_C_CALL;        \
    ApiFunction func(func_address);                                            \
    ExternalReference ref = ExternalReference::Create(&func, func_type);       \
                                                                               \
    IF_SIMULATOR_ADD_SIGNATURE                                                 \
                                                                               \
    Node* function = m.ExternalConstant(ref);                                  \
    m.Return(m.CallCFunction(function, machine_type, SIGNATURE(PARAM_PAIRS))); \
                                                                               \
    CHECK_RESULT(m.Call(SIGNATURE(CALL_ARGS)), 42);                            \
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
#define SIGNATURE_ONLY_INT(V)                                                 \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9)

Int64OrDoubleUnion func_only_int(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9) {
#elif defined(V8_TARGET_ARCH_64_BIT)
#define SIGNATURE_ONLY_INT(V)                                                 \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9)

ReturnType func_only_int(int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3,
                         int64_t arg4, int64_t arg5, int64_t arg6, int64_t arg7,
                         int64_t arg8, int64_t arg9) {
#else  // defined(V8_TARGET_ARCH_64_BIT)
#define SIGNATURE_ONLY_INT(V)                                                 \
  V(int32_t, 0, 0), V(int32_t, 1, 1), V(int32_t, 2, 2), V(int32_t, 3, 3),     \
      V(int32_t, 4, 4), V(int32_t, 5, 5), V(int32_t, 6, 6), V(int32_t, 7, 7), \
      V(int32_t, 8, 8), V(int32_t, 9, 9)

ReturnType func_only_int(int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3,
                         int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
                         int32_t arg8, int32_t arg9) {
#endif
  bool result = true;
  SIGNATURE_ONLY_INT(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithSignatureOnlyInt, SIGNATURE_ONLY_INT, func_only_int)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
#define SIGNATURE_ONLY_INT_20(V)                                              \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9), V(int64_t, 10, 10),                 \
      V(int64_t, 11, 11), V(int64_t, 12, 12), V(int64_t, 13, 13),             \
      V(int64_t, 14, 14), V(int64_t, 15, 15), V(int64_t, 16, 16),             \
      V(int64_t, 17, 17), V(int64_t, 18, 18), V(int64_t, 19, 19)

Int64OrDoubleUnion func_only_int_20(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#elif defined(V8_TARGET_ARCH_64_BIT)
#define SIGNATURE_ONLY_INT_20(V)                                              \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9), V(int64_t, 10, 10),                 \
      V(int64_t, 11, 11), V(int64_t, 12, 12), V(int64_t, 13, 13),             \
      V(int64_t, 14, 14), V(int64_t, 15, 15), V(int64_t, 16, 16),             \
      V(int64_t, 17, 17), V(int64_t, 18, 18), V(int64_t, 19, 19)

ReturnType func_only_int_20(int64_t arg0, int64_t arg1, int64_t arg2,
                            int64_t arg3, int64_t arg4, int64_t arg5,
                            int64_t arg6, int64_t arg7, int64_t arg8,
                            int64_t arg9, int64_t arg10, int64_t arg11,
                            int64_t arg12, int64_t arg13, int64_t arg14,
                            int64_t arg15, int64_t arg16, int64_t arg17,
                            int64_t arg18, int64_t arg19) {
#else  // defined(V8_TARGET_ARCH_64_BIT)
#define SIGNATURE_ONLY_INT_20(V)                                              \
  V(int32_t, 0, 0), V(int32_t, 1, 1), V(int32_t, 2, 2), V(int32_t, 3, 3),     \
      V(int32_t, 4, 4), V(int32_t, 5, 5), V(int32_t, 6, 6), V(int32_t, 7, 7), \
      V(int32_t, 8, 8), V(int32_t, 9, 9), V(int32_t, 10, 10),                 \
      V(int32_t, 11, 11), V(int32_t, 12, 12), V(int32_t, 13, 13),             \
      V(int32_t, 14, 14), V(int32_t, 15, 15), V(int32_t, 16, 16),             \
      V(int32_t, 17, 17), V(int32_t, 18, 18), V(int32_t, 19, 19)

ReturnType func_only_int_20(int32_t arg0, int32_t arg1, int32_t arg2,
                            int32_t arg3, int32_t arg4, int32_t arg5,
                            int32_t arg6, int32_t arg7, int32_t arg8,
                            int32_t arg9, int32_t arg10, int32_t arg11,
                            int32_t arg12, int32_t arg13, int32_t arg14,
                            int32_t arg15, int32_t arg16, int32_t arg17,
                            int32_t arg18, int32_t arg19) {
#endif
  bool result = true;
  SIGNATURE_ONLY_INT_20(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithSignatureOnlyInt20, SIGNATURE_ONLY_INT_20,
               func_only_int_20)

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

#define MIXED_SIGNATURE_SIMPLE(V) \
  V(int64_t, 0, 0), V(double, 1, 1.5), V(int64_t, 2, 2)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion test_api_func_simple(Int64OrDoubleUnion arg0,
                                        Int64OrDoubleUnion arg1,
                                        Int64OrDoubleUnion arg2) {
#else
ReturnType test_api_func_simple(int64_t arg0, double arg1, int64_t arg2) {
#endif
  bool result = true;
  MIXED_SIGNATURE_SIMPLE(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignatureSimple, MIXED_SIGNATURE_SIMPLE,
               test_api_func_simple)

#define MIXED_SIGNATURE(V)                                                  \
  V(int64_t, 0, 0), V(double, 1, 1.5), V(int64_t, 2, 2), V(double, 3, 3.5), \
      V(int64_t, 4, 4), V(double, 5, 5.5), V(int64_t, 6, 6),                \
      V(double, 7, 7.5), V(int64_t, 8, 8), V(double, 9, 9.5),               \
      V(int64_t, 10, 10)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion test_api_func(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10) {
#else
ReturnType test_api_func(int64_t arg0, double arg1, int64_t arg2, double arg3,
                         int64_t arg4, double arg5, int64_t arg6, double arg7,
                         int64_t arg8, double arg9, int64_t arg10) {
#endif
  bool result = true;
  MIXED_SIGNATURE(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignature, MIXED_SIGNATURE, test_api_func)

#define MIXED_SIGNATURE_DOUBLE_INT(V)                                         \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5),                \
      V(int64_t, 10, 10), V(int64_t, 11, 11), V(int64_t, 12, 12),             \
      V(int64_t, 13, 13), V(int64_t, 14, 14), V(int64_t, 15, 15),             \
      V(int64_t, 16, 16), V(int64_t, 17, 17), V(int64_t, 18, 18),             \
      V(int64_t, 19, 19)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_mixed_double_int(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#else
ReturnType func_mixed_double_int(double arg0, double arg1, double arg2,
                                 double arg3, double arg4, double arg5,
                                 double arg6, double arg7, double arg8,
                                 double arg9, int64_t arg10, int64_t arg11,
                                 int64_t arg12, int64_t arg13, int64_t arg14,
                                 int64_t arg15, int64_t arg16, int64_t arg17,
                                 int64_t arg18, int64_t arg19) {
#endif
  bool result = true;
  MIXED_SIGNATURE_DOUBLE_INT(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignatureDoubleInt, MIXED_SIGNATURE_DOUBLE_INT,
               func_mixed_double_int)

#define MIXED_SIGNATURE_DOUBLE_INT_ALT(V)                                     \
  V(double, 0, 0.5)                                                           \
  , V(int64_t, 1, 1), V(double, 2, 2.5), V(int64_t, 3, 3), V(double, 4, 4.5), \
      V(int64_t, 5, 5), V(double, 6, 6.5), V(int64_t, 7, 7),                  \
      V(double, 8, 8.5), V(int64_t, 9, 9), V(double, 10, 10.5),               \
      V(int64_t, 11, 11), V(double, 12, 12.5), V(int64_t, 13, 13),            \
      V(double, 14, 14.5), V(int64_t, 15, 15), V(double, 16, 16.5),           \
      V(int64_t, 17, 17), V(double, 18, 18.5), V(int64_t, 19, 19)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_mixed_double_int_alt(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#else
ReturnType func_mixed_double_int_alt(double arg0, int64_t arg1, double arg2,
                                     int64_t arg3, double arg4, int64_t arg5,
                                     double arg6, int64_t arg7, double arg8,
                                     int64_t arg9, double arg10, int64_t arg11,
                                     double arg12, int64_t arg13, double arg14,
                                     int64_t arg15, double arg16, int64_t arg17,
                                     double arg18, int64_t arg19) {
#endif
  bool result = true;
  MIXED_SIGNATURE_DOUBLE_INT_ALT(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignatureDoubleIntAlt,
               MIXED_SIGNATURE_DOUBLE_INT_ALT, func_mixed_double_int_alt)

#define MIXED_SIGNATURE_INT_DOUBLE(V)                                         \
  V(int64_t, 0, 0), V(int64_t, 1, 1), V(int64_t, 2, 2), V(int64_t, 3, 3),     \
      V(int64_t, 4, 4), V(int64_t, 5, 5), V(int64_t, 6, 6), V(int64_t, 7, 7), \
      V(int64_t, 8, 8), V(int64_t, 9, 9), V(double, 10, 10.5),                \
      V(double, 11, 11.5), V(double, 12, 12.5), V(double, 13, 13.5),          \
      V(double, 14, 14.5), V(double, 15, 15.5), V(double, 16, 16.5),          \
      V(double, 17, 17.5), V(double, 18, 18.5), V(double, 19, 19.5)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_mixed_int_double(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#else
ReturnType func_mixed_int_double(int64_t arg0, int64_t arg1, int64_t arg2,
                                 int64_t arg3, int64_t arg4, int64_t arg5,
                                 int64_t arg6, int64_t arg7, int64_t arg8,
                                 int64_t arg9, double arg10, double arg11,
                                 double arg12, double arg13, double arg14,
                                 double arg15, double arg16, double arg17,
                                 double arg18, double arg19) {
#endif
  bool result = true;
  MIXED_SIGNATURE_INT_DOUBLE(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignatureIntDouble, MIXED_SIGNATURE_INT_DOUBLE,
               func_mixed_int_double)

#define MIXED_SIGNATURE_INT_DOUBLE_ALT(V)                                   \
  V(int64_t, 0, 0), V(double, 1, 1.5), V(int64_t, 2, 2), V(double, 3, 3.5), \
      V(int64_t, 4, 4), V(double, 5, 5.5), V(int64_t, 6, 6),                \
      V(double, 7, 7.5), V(int64_t, 8, 8), V(double, 9, 9.5),               \
      V(int64_t, 10, 10), V(double, 11, 11.5), V(int64_t, 12, 12),          \
      V(double, 13, 13.5), V(int64_t, 14, 14), V(double, 15, 15.5),         \
      V(int64_t, 16, 16), V(double, 17, 17.5), V(int64_t, 18, 18),          \
      V(double, 19, 19.5)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_mixed_int_double_alt(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#else
ReturnType func_mixed_int_double_alt(int64_t arg0, double arg1, int64_t arg2,
                                     double arg3, int64_t arg4, double arg5,
                                     int64_t arg6, double arg7, int64_t arg8,
                                     double arg9, int64_t arg10, double arg11,
                                     int64_t arg12, double arg13, int64_t arg14,
                                     double arg15, int64_t arg16, double arg17,
                                     int64_t arg18, double arg19) {
#endif
  bool result = true;
  MIXED_SIGNATURE_INT_DOUBLE_ALT(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithMixedSignatureIntDoubleAlt,
               MIXED_SIGNATURE_INT_DOUBLE_ALT, func_mixed_int_double_alt)

#define SIGNATURE_ONLY_DOUBLE(V)                                              \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_only_double(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9) {
#else
ReturnType func_only_double(double arg0, double arg1, double arg2, double arg3,
                            double arg4, double arg5, double arg6, double arg7,
                            double arg8, double arg9) {
#endif
  bool result = true;
  SIGNATURE_ONLY_DOUBLE(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithSignatureOnlyDouble, SIGNATURE_ONLY_DOUBLE,
               func_only_double)

#define SIGNATURE_ONLY_DOUBLE_20(V)                                           \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5),                \
      V(double, 10, 10.5), V(double, 11, 11.5), V(double, 12, 12.5),          \
      V(double, 13, 13.5), V(double, 14, 14.5), V(double, 15, 15.5),          \
      V(double, 16, 16.5), V(double, 17, 17.5), V(double, 18, 18.5),          \
      V(double, 19, 19.5)

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
Int64OrDoubleUnion func_only_double_20(
    Int64OrDoubleUnion arg0, Int64OrDoubleUnion arg1, Int64OrDoubleUnion arg2,
    Int64OrDoubleUnion arg3, Int64OrDoubleUnion arg4, Int64OrDoubleUnion arg5,
    Int64OrDoubleUnion arg6, Int64OrDoubleUnion arg7, Int64OrDoubleUnion arg8,
    Int64OrDoubleUnion arg9, Int64OrDoubleUnion arg10, Int64OrDoubleUnion arg11,
    Int64OrDoubleUnion arg12, Int64OrDoubleUnion arg13,
    Int64OrDoubleUnion arg14, Int64OrDoubleUnion arg15,
    Int64OrDoubleUnion arg16, Int64OrDoubleUnion arg17,
    Int64OrDoubleUnion arg18, Int64OrDoubleUnion arg19) {
#else
ReturnType func_only_double_20(double arg0, double arg1, double arg2,
                               double arg3, double arg4, double arg5,
                               double arg6, double arg7, double arg8,
                               double arg9, double arg10, double arg11,
                               double arg12, double arg13, double arg14,
                               double arg15, double arg16, double arg17,
                               double arg18, double arg19) {
#endif
  bool result = true;
  SIGNATURE_ONLY_DOUBLE_20(CHECK_ARG_I);
  CHECK(result);

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  Int64OrDoubleUnion ret;
  ret.int64_t_value = 42;
  return ret;
#else
  return 42;
#endif
}

SIGNATURE_TEST(RunCallWithSignatureOnlyDouble20, SIGNATURE_ONLY_DOUBLE_20,
               func_only_double_20)

#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
