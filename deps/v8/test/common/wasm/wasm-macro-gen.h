// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_MACRO_GEN_H_
#define V8_WASM_MACRO_GEN_H_

#include "src/wasm/wasm-opcodes.h"

#define U32_LE(v)                                          \
  static_cast<uint8_t>(v), static_cast<uint8_t>((v) >> 8), \
      static_cast<uint8_t>((v) >> 16), static_cast<uint8_t>((v) >> 24)

#define U16_LE(v) static_cast<uint8_t>(v), static_cast<uint8_t>((v) >> 8)

#define WASM_MODULE_HEADER U32_LE(kWasmMagic), U32_LE(kWasmVersion)

#define SIG_INDEX(v) U32V_1(v)
#define FUNC_INDEX(v) U32V_1(v)
#define EXCEPTION_INDEX(v) U32V_1(v)
#define NO_NAME U32V_1(0)
#define ENTRY_COUNT(v) U32V_1(v)

// Segment flags
#define ACTIVE_NO_INDEX 0
#define PASSIVE 1
#define ACTIVE_WITH_INDEX 2
#define DECLARATIVE 3
#define PASSIVE_WITH_ELEMENTS 5
#define ACTIVE_WITH_ELEMENTS 6
#define DECLARATIVE_WITH_ELEMENTS 7

// The table index field in an element segment was repurposed as a flags field.
// To specify a table index, we have to set the flag value to 2, followed by
// the table index.
#define TABLE_INDEX0 static_cast<uint8_t>(ACTIVE_NO_INDEX)
#define TABLE_INDEX(v) static_cast<uint8_t>(ACTIVE_WITH_INDEX), U32V_1(v)

#define ZERO_ALIGNMENT 0
#define ZERO_OFFSET 0

#define BR_TARGET(v) U32V_1(v)

#define MASK_7 ((1 << 7) - 1)
#define MASK_14 ((1 << 14) - 1)
#define MASK_21 ((1 << 21) - 1)
#define MASK_28 ((1 << 28) - 1)

#define U32V_1(x) static_cast<uint8_t>((x)&MASK_7)
#define U32V_2(x)                            \
  static_cast<uint8_t>(((x)&MASK_7) | 0x80), \
      static_cast<uint8_t>(((x) >> 7) & MASK_7)
#define U32V_3(x)                                         \
  static_cast<uint8_t>((((x)) & MASK_7) | 0x80),          \
      static_cast<uint8_t>((((x) >> 7) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((x) >> 14) & MASK_7)
#define U32V_4(x)                                          \
  static_cast<uint8_t>(((x)&MASK_7) | 0x80),               \
      static_cast<uint8_t>((((x) >> 7) & MASK_7) | 0x80),  \
      static_cast<uint8_t>((((x) >> 14) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((x) >> 21) & MASK_7)
#define U32V_5(x)                                          \
  static_cast<uint8_t>(((x)&MASK_7) | 0x80),               \
      static_cast<uint8_t>((((x) >> 7) & MASK_7) | 0x80),  \
      static_cast<uint8_t>((((x) >> 14) & MASK_7) | 0x80), \
      static_cast<uint8_t>((((x) >> 21) & MASK_7) | 0x80), \
      static_cast<uint8_t>((((x) >> 28) & MASK_7))

#define U64V_1(x) U32V_1(static_cast<uint32_t>(x))
#define U64V_2(x) U32V_2(static_cast<uint32_t>(x))
#define U64V_3(x) U32V_3(static_cast<uint32_t>(x))
#define U64V_4(x) U32V_4(static_cast<uint32_t>(x))
#define U64V_5(x)                                                  \
  static_cast<uint8_t>((uint64_t{x} & MASK_7) | 0x80),             \
      static_cast<uint8_t>(((uint64_t{x} >> 7) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((uint64_t{x} >> 14) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 21) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 28) & MASK_7))
#define U64V_6(x)                                                  \
  static_cast<uint8_t>((uint64_t{x} & MASK_7) | 0x80),             \
      static_cast<uint8_t>(((uint64_t{x} >> 7) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((uint64_t{x} >> 14) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 21) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 28) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 35) & MASK_7))
#define U64V_10(x)                                                 \
  static_cast<uint8_t>((uint64_t{x} & MASK_7) | 0x80),             \
      static_cast<uint8_t>(((uint64_t{x} >> 7) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((uint64_t{x} >> 14) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 21) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 28) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 35) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 42) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 49) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((uint64_t{x} >> 56) & MASK_7) | 0x80), \
      static_cast<uint8_t>((uint64_t{x} >> 63) & MASK_7)

// Convenience macros for building Wasm bytecode directly into a byte array.

//------------------------------------------------------------------------------
// Control.
//------------------------------------------------------------------------------
#define WASM_NOP kExprNop
#define WASM_END kExprEnd

#define ARITY_0 0
#define ARITY_1 1
#define ARITY_2 2
#define DEPTH_0 0
#define DEPTH_1 1
#define DEPTH_2 2

#define WASM_HEAP_TYPE(heap_type) \
  static_cast<uint8_t>((heap_type).code() & 0x7f)

#define WASM_REF_TYPE(type)                        \
  (type).kind() == kRef ? kRefCode : kRefNullCode, \
      WASM_HEAP_TYPE((type).heap_type())

#define WASM_BLOCK(...) kExprBlock, kVoidCode, __VA_ARGS__, kExprEnd
#define WASM_BLOCK_I(...) kExprBlock, kI32Code, __VA_ARGS__, kExprEnd
#define WASM_BLOCK_L(...) kExprBlock, kI64Code, __VA_ARGS__, kExprEnd
#define WASM_BLOCK_F(...) kExprBlock, kF32Code, __VA_ARGS__, kExprEnd
#define WASM_BLOCK_D(...) kExprBlock, kF64Code, __VA_ARGS__, kExprEnd
#define WASM_BLOCK_T(t, ...) \
  kExprBlock, static_cast<uint8_t>((t).value_type_code()), __VA_ARGS__, kExprEnd

#define WASM_BLOCK_R(type, ...) \
  kExprBlock, WASM_REF_TYPE(type), __VA_ARGS__, kExprEnd

#define WASM_BLOCK_X(index, ...) \
  kExprBlock, static_cast<uint8_t>(index), __VA_ARGS__, kExprEnd

#define WASM_INFINITE_LOOP kExprLoop, kVoidCode, kExprBr, DEPTH_0, kExprEnd

#define WASM_LOOP(...) kExprLoop, kVoidCode, __VA_ARGS__, kExprEnd
#define WASM_LOOP_I(...) kExprLoop, kI32Code, __VA_ARGS__, kExprEnd
#define WASM_LOOP_L(...) kExprLoop, kI64Code, __VA_ARGS__, kExprEnd
#define WASM_LOOP_F(...) kExprLoop, kF32Code, __VA_ARGS__, kExprEnd
#define WASM_LOOP_D(...) kExprLoop, kF64Code, __VA_ARGS__, kExprEnd

#define WASM_LOOP_T(t, ...) \
  kExprLoop, static_cast<uint8_t>((t).value_type_code()), __VA_ARGS__, kExprEnd

#define WASM_LOOP_R(t, ...) kExprLoop, TYPE_IMM(t), __VA_ARGS__, kExprEnd

#define WASM_LOOP_X(index, ...) \
  kExprLoop, static_cast<uint8_t>(index), __VA_ARGS__, kExprEnd

#define WASM_IF(cond, ...) cond, kExprIf, kVoidCode, __VA_ARGS__, kExprEnd

#define WASM_IF_T(t, cond, ...)                                            \
  cond, kExprIf, static_cast<uint8_t>((t).value_type_code()), __VA_ARGS__, \
      kExprEnd

#define WASM_IF_R(t, cond, ...) \
  cond, kExprIf, TYPE_IMM(t), __VA_ARGS__, kExprEnd

#define WASM_IF_X(index, cond, ...) \
  cond, kExprIf, static_cast<uint8_t>(index), __VA_ARGS__, kExprEnd

#define WASM_IF_ELSE(cond, tstmt, fstmt) \
  cond, kExprIf, kVoidCode, tstmt, kExprElse, fstmt, kExprEnd

#define WASM_IF_ELSE_I(cond, tstmt, fstmt) \
  cond, kExprIf, kI32Code, tstmt, kExprElse, fstmt, kExprEnd
#define WASM_IF_ELSE_L(cond, tstmt, fstmt) \
  cond, kExprIf, kI64Code, tstmt, kExprElse, fstmt, kExprEnd
#define WASM_IF_ELSE_F(cond, tstmt, fstmt) \
  cond, kExprIf, kF32Code, tstmt, kExprElse, fstmt, kExprEnd
#define WASM_IF_ELSE_D(cond, tstmt, fstmt) \
  cond, kExprIf, kF64Code, tstmt, kExprElse, fstmt, kExprEnd

#define WASM_IF_ELSE_T(t, cond, tstmt, fstmt)                        \
  cond, kExprIf, static_cast<uint8_t>((t).value_type_code()), tstmt, \
      kExprElse, fstmt, kExprEnd

#define WASM_IF_ELSE_R(t, cond, tstmt, fstmt) \
  cond, kExprIf, WASM_REF_TYPE(t), tstmt, kExprElse, fstmt, kExprEnd

#define WASM_IF_ELSE_X(index, cond, tstmt, fstmt) \
  cond, kExprIf, static_cast<uint8_t>(index), tstmt, kExprElse, fstmt, kExprEnd

#define WASM_TRY_T(t, trystmt) \
  kExprTry, static_cast<uint8_t>((t).value_type_code()), trystmt, kExprEnd
#define WASM_TRY_CATCH_T(t, trystmt, catchstmt, except)                       \
  kExprTry, static_cast<uint8_t>((t).value_type_code()), trystmt, kExprCatch, \
      except, catchstmt, kExprEnd
#define WASM_TRY_CATCH_CATCH_T(t, trystmt, except1, catchstmt1, except2,      \
                               catchstmt2)                                    \
  kExprTry, static_cast<uint8_t>((t).value_type_code()), trystmt, kExprCatch, \
      except1, catchstmt1, kExprCatch, except2, catchstmt2, kExprEnd
#define WASM_TRY_CATCH_R(t, trystmt, catchstmt) \
  kExprTry, WASM_REF_TYPE(t), trystmt, kExprCatch, catchstmt, kExprEnd
#define WASM_TRY_CATCH_ALL_T(t, trystmt, catchstmt)               \
  kExprTry, static_cast<uint8_t>((t).value_type_code()), trystmt, \
      kExprCatchAll, catchstmt, kExprEnd
#define WASM_TRY_DELEGATE(trystmt, depth) \
  kExprTry, kVoidCode, trystmt, kExprDelegate, depth
#define WASM_TRY_DELEGATE_T(t, trystmt, depth)                    \
  kExprTry, static_cast<uint8_t>((t).value_type_code()), trystmt, \
      kExprDelegate, depth

#define WASM_SELECT(tval, fval, cond) tval, fval, cond, kExprSelect
#define WASM_SELECT_I(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kI32Code
#define WASM_SELECT_L(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kI64Code
#define WASM_SELECT_F(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kF32Code
#define WASM_SELECT_D(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kF64Code
#define WASM_SELECT_R(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kExternRefCode
#define WASM_SELECT_A(tval, fval, cond) \
  tval, fval, cond, kExprSelectWithType, U32V_1(1), kFuncRefCode

#define WASM_BR(depth) kExprBr, static_cast<uint8_t>(depth)
#define WASM_BR_IF(depth, cond) cond, kExprBrIf, static_cast<uint8_t>(depth)
#define WASM_BR_IFD(depth, val, cond) \
  val, cond, kExprBrIf, static_cast<uint8_t>(depth), kExprDrop
#define WASM_CONTINUE(depth) kExprBr, static_cast<uint8_t>(depth)
#define WASM_UNREACHABLE kExprUnreachable
#define WASM_RETURN(...) __VA_ARGS__, kExprReturn
#define WASM_RETURN0 kExprReturn

#define WASM_BR_TABLE(key, count, ...) \
  key, kExprBrTable, U32V_1(count), __VA_ARGS__

#define WASM_THROW(index) kExprThrow, static_cast<uint8_t>(index)

//------------------------------------------------------------------------------
// Misc expressions.
//------------------------------------------------------------------------------
#define WASM_STMTS(...) __VA_ARGS__
#define WASM_ZERO WASM_I32V_1(0)
#define WASM_ONE WASM_I32V_1(1)
#define WASM_ZERO64 WASM_I64V_1(0)
#define WASM_ONE64 WASM_I64V_1(1)

#define I32V_MIN(length) -(1 << (6 + (7 * ((length)-1))))
#define I32V_MAX(length) ((1 << (6 + (7 * ((length)-1)))) - 1)
#define I64V_MIN(length) -(1LL << (6 + (7 * ((length)-1))))
#define I64V_MAX(length) ((1LL << (6 + 7 * ((length)-1))) - 1)

#define I32V_IN_RANGE(value, length) \
  ((value) >= I32V_MIN(length) && (value) <= I32V_MAX(length))
#define I64V_IN_RANGE(value, length) \
  ((value) >= I64V_MIN(length) && (value) <= I64V_MAX(length))

#define WASM_NO_LOCALS 0

//------------------------------------------------------------------------------
// Helpers for encoding sections and other fields with length prefix.
//------------------------------------------------------------------------------

template <typename... Args>
std::integral_constant<size_t, sizeof...(Args)> CountArgsHelper(Args...);
#define COUNT_ARGS(...) (decltype(CountArgsHelper(__VA_ARGS__))::value)

template <size_t num>
struct CheckLEB1 : std::integral_constant<size_t, num> {
  static_assert(num <= I32V_MAX(1), "LEB range check");
};
#define CHECK_LEB1(num) CheckLEB1<num>::value

#define ADD_COUNT(...) CHECK_LEB1(COUNT_ARGS(__VA_ARGS__)), __VA_ARGS__

#define SECTION(name, ...) k##name##SectionCode, ADD_COUNT(__VA_ARGS__)

namespace v8 {
namespace internal {
namespace wasm {

inline void CheckI32v(int32_t value, int length) {
  DCHECK(length >= 1 && length <= 5);
  DCHECK(length == 5 || I32V_IN_RANGE(value, length));
}

inline void CheckI64v(int64_t value, int length) {
  DCHECK(length >= 1 && length <= 10);
  DCHECK(length == 10 || I64V_IN_RANGE(value, length));
}

inline WasmOpcode LoadStoreOpcodeOf(MachineType type, bool store) {
  switch (type.representation()) {
    case MachineRepresentation::kWord8:
      return store ? kExprI32StoreMem8
                   : type.IsSigned() ? kExprI32LoadMem8S : kExprI32LoadMem8U;
    case MachineRepresentation::kWord16:
      return store ? kExprI32StoreMem16
                   : type.IsSigned() ? kExprI32LoadMem16S : kExprI32LoadMem16U;
    case MachineRepresentation::kWord32:
      return store ? kExprI32StoreMem : kExprI32LoadMem;
    case MachineRepresentation::kWord64:
      return store ? kExprI64StoreMem : kExprI64LoadMem;
    case MachineRepresentation::kFloat32:
      return store ? kExprF32StoreMem : kExprF32LoadMem;
    case MachineRepresentation::kFloat64:
      return store ? kExprF64StoreMem : kExprF64LoadMem;
    case MachineRepresentation::kSimd128:
      return store ? kExprS128StoreMem : kExprS128LoadMem;
    default:
      UNREACHABLE();
  }
}

// See comment on {WasmOpcode} for the encoding.
// This method handles opcodes with decoded length up to 3 bytes. Update if we
// exceed that opcode length.
inline uint16_t ExtractPrefixedOpcodeBytes(WasmOpcode opcode) {
  return (opcode > 0xffff) ? opcode & 0x0fff : opcode & 0xff;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

//------------------------------------------------------------------------------
// Int32 Const operations
//------------------------------------------------------------------------------
#define WASM_I32V(val) WASM_I32V_5(val)

#define WASM_I32V_1(val)                                    \
  static_cast<uint8_t>(CheckI32v((val), 1), kExprI32Const), \
      U32V_1(static_cast<int32_t>(val))
#define WASM_I32V_2(val)                                    \
  static_cast<uint8_t>(CheckI32v((val), 2), kExprI32Const), \
      U32V_2(static_cast<int32_t>(val))
#define WASM_I32V_3(val)                                    \
  static_cast<uint8_t>(CheckI32v((val), 3), kExprI32Const), \
      U32V_3(static_cast<int32_t>(val))
#define WASM_I32V_4(val)                                    \
  static_cast<uint8_t>(CheckI32v((val), 4), kExprI32Const), \
      U32V_4(static_cast<int32_t>(val))
#define WASM_I32V_5(val)                                    \
  static_cast<uint8_t>(CheckI32v((val), 5), kExprI32Const), \
      U32V_5(static_cast<int32_t>(val))

//------------------------------------------------------------------------------
// Int64 Const operations
//------------------------------------------------------------------------------
#define WASM_I64V(val)                                                    \
  kExprI64Const,                                                          \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 35) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 42) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 49) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 56) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 63) & MASK_7)

#define WASM_I64V_1(val)                                        \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 1), \
                       kExprI64Const),                          \
      static_cast<uint8_t>(static_cast<int64_t>(val) & MASK_7)
#define WASM_I64V_2(val)                                                 \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 2),          \
                       kExprI64Const),                                   \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80), \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 7) & MASK_7)
#define WASM_I64V_3(val)                                                 \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 3),          \
                       kExprI64Const),                                   \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80), \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) | \
                           0x80),                                        \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 14) & MASK_7)
#define WASM_I64V_4(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 4),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 21) & MASK_7)
#define WASM_I64V_5(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 5),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 28) & MASK_7)
#define WASM_I64V_6(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 6),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 35) & MASK_7)
#define WASM_I64V_7(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 7),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 35) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 42) & MASK_7)
#define WASM_I64V_8(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 8),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 35) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 42) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 49) & MASK_7)
#define WASM_I64V_9(val)                                                  \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 9),           \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 35) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 42) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 49) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 56) & MASK_7)
#define WASM_I64V_10(val)                                                 \
  static_cast<uint8_t>(CheckI64v(static_cast<int64_t>(val), 10),          \
                       kExprI64Const),                                    \
      static_cast<uint8_t>((static_cast<int64_t>(val) & MASK_7) | 0x80),  \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 7) & MASK_7) |  \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 14) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 21) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 28) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 35) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 42) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 49) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>(((static_cast<int64_t>(val) >> 56) & MASK_7) | \
                           0x80),                                         \
      static_cast<uint8_t>((static_cast<int64_t>(val) >> 63) & MASK_7)

#define WASM_F32(val)                                                         \
  kExprF32Const,                                                              \
      static_cast<uint8_t>(base::bit_cast<int32_t>(static_cast<float>(val))), \
      static_cast<uint8_t>(                                                   \
          base::bit_cast<uint32_t>(static_cast<float>(val)) >> 8),            \
      static_cast<uint8_t>(                                                   \
          base::bit_cast<uint32_t>(static_cast<float>(val)) >> 16),           \
      static_cast<uint8_t>(                                                   \
          base::bit_cast<uint32_t>(static_cast<float>(val)) >> 24)
#define WASM_F64(val)                                                \
  kExprF64Const,                                                     \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val))),       \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 8),  \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 16), \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 24), \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 32), \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 40), \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 48), \
      static_cast<uint8_t>(                                          \
          base::bit_cast<uint64_t>(static_cast<double>(val)) >> 56)

#define WASM_LOCAL_GET(index) kExprLocalGet, static_cast<uint8_t>(index)
#define WASM_LOCAL_SET(index, val) \
  val, kExprLocalSet, static_cast<uint8_t>(index)
#define WASM_LOCAL_TEE(index, val) \
  val, kExprLocalTee, static_cast<uint8_t>(index)
#define WASM_DROP kExprDrop
#define WASM_GLOBAL_GET(index) kExprGlobalGet, static_cast<uint8_t>(index)
#define WASM_GLOBAL_SET(index, val) \
  val, kExprGlobalSet, static_cast<uint8_t>(index)
#define WASM_TABLE_GET(table_index, index) \
  index, kExprTableGet, static_cast<uint8_t>(table_index)
#define WASM_TABLE_SET(table_index, index, val) \
  index, val, kExprTableSet, static_cast<uint8_t>(table_index)
#define WASM_LOAD_MEM(type, index)                             \
  index,                                                       \
      static_cast<uint8_t>(                                    \
          v8::internal::wasm::LoadStoreOpcodeOf(type, false)), \
      ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_STORE_MEM(type, index, val)                                       \
  index, val,                                                                  \
      static_cast<uint8_t>(v8::internal::wasm::LoadStoreOpcodeOf(type, true)), \
      ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_LOAD_MEM_OFFSET(type, offset, index)              \
  index,                                                       \
      static_cast<uint8_t>(                                    \
          v8::internal::wasm::LoadStoreOpcodeOf(type, false)), \
      ZERO_ALIGNMENT, offset
#define WASM_STORE_MEM_OFFSET(type, offset, index, val)                        \
  index, val,                                                                  \
      static_cast<uint8_t>(v8::internal::wasm::LoadStoreOpcodeOf(type, true)), \
      ZERO_ALIGNMENT, offset
#define WASM_LOAD_MEM_ALIGNMENT(type, index, alignment)        \
  index,                                                       \
      static_cast<uint8_t>(                                    \
          v8::internal::wasm::LoadStoreOpcodeOf(type, false)), \
      alignment, ZERO_OFFSET
#define WASM_STORE_MEM_ALIGNMENT(type, index, alignment, val)                  \
  index, val,                                                                  \
      static_cast<uint8_t>(v8::internal::wasm::LoadStoreOpcodeOf(type, true)), \
      alignment, ZERO_OFFSET
#define WASM_F16_LOAD_MEM(index) \
  index, WASM_NUMERIC_OP(kExprF32LoadMemF16), ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_F16_STORE_MEM(index, val) \
  index, val, WASM_NUMERIC_OP(kExprF32StoreMemF16), ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_RETHROW(index) kExprRethrow, static_cast<uint8_t>(index)

#define WASM_CALL_FUNCTION0(index) \
  kExprCallFunction, static_cast<uint8_t>(index)
#define WASM_CALL_FUNCTION(index, ...) \
  __VA_ARGS__, kExprCallFunction, static_cast<uint8_t>(index)

#define WASM_RETURN_CALL_FUNCTION0(index) \
  kExprReturnCall, static_cast<uint8_t>(index)
#define WASM_RETURN_CALL_FUNCTION(index, ...) \
  __VA_ARGS__, kExprReturnCall, static_cast<uint8_t>(index)

#define TABLE_ZERO 0

//------------------------------------------------------------------------------
// Heap-allocated object operations.
//------------------------------------------------------------------------------
#define WASM_GC_OP(op) kGCPrefix, static_cast<uint8_t>(op)
#define WASM_STRUCT_NEW(index, ...) \
  __VA_ARGS__, WASM_GC_OP(kExprStructNew), static_cast<uint8_t>(index)
#define WASM_STRUCT_NEW_DEFAULT(index) \
  WASM_GC_OP(kExprStructNewDefault), static_cast<uint8_t>(index)
#define WASM_STRUCT_GET(typeidx, fieldidx, struct_obj)                   \
  struct_obj, WASM_GC_OP(kExprStructGet), static_cast<uint8_t>(typeidx), \
      static_cast<uint8_t>(fieldidx)
#define WASM_STRUCT_GET_S(typeidx, fieldidx, struct_obj)                  \
  struct_obj, WASM_GC_OP(kExprStructGetS), static_cast<uint8_t>(typeidx), \
      static_cast<uint8_t>(fieldidx)
#define WASM_STRUCT_GET_U(typeidx, fieldidx, struct_obj)                  \
  struct_obj, WASM_GC_OP(kExprStructGetU), static_cast<uint8_t>(typeidx), \
      static_cast<uint8_t>(fieldidx)
#define WASM_STRUCT_SET(typeidx, fieldidx, struct_obj, value) \
  struct_obj, value, WASM_GC_OP(kExprStructSet),              \
      static_cast<uint8_t>(typeidx), static_cast<uint8_t>(fieldidx)
#define WASM_REF_NULL(type_encoding) kExprRefNull, type_encoding
#define WASM_REF_FUNC(index) kExprRefFunc, index
#define WASM_REF_IS_NULL(val) val, kExprRefIsNull
#define WASM_REF_AS_NON_NULL(val) val, kExprRefAsNonNull
#define WASM_REF_EQ(lhs, rhs) lhs, rhs, kExprRefEq
#define WASM_REF_TEST(ref, typeidx) \
  ref, WASM_GC_OP(kExprRefTest), static_cast<uint8_t>(typeidx)
#define WASM_REF_TEST_NULL(ref, typeidx) \
  ref, WASM_GC_OP(kExprRefTestNull), static_cast<uint8_t>(typeidx)
#define WASM_REF_CAST(ref, typeidx) \
  ref, WASM_GC_OP(kExprRefCast), static_cast<uint8_t>(typeidx)
#define WASM_REF_CAST_NULL(ref, typeidx) \
  ref, WASM_GC_OP(kExprRefCastNull), static_cast<uint8_t>(typeidx)
// Takes a reference value from the value stack to allow sequences of
// conditional branches.
#define WASM_BR_ON_CAST(depth, sourcetype, targettype)               \
  WASM_GC_OP(kExprBrOnCast),                                         \
      static_cast<uint8_t>(0b01), /*source is nullable*/             \
      static_cast<uint8_t>(depth), static_cast<uint8_t>(sourcetype), \
      static_cast<uint8_t>(targettype)
#define WASM_BR_ON_CAST_NULL(depth, sourcetype, targettype)          \
  WASM_GC_OP(kExprBrOnCast),                                         \
      static_cast<uint8_t>(0b11) /*source & target nullable*/,       \
      static_cast<uint8_t>(depth), static_cast<uint8_t>(sourcetype), \
      static_cast<uint8_t>(targettype)
#define WASM_BR_ON_CAST_FAIL(depth, sourcetype, targettype)          \
  WASM_GC_OP(kExprBrOnCastFail),                                     \
      static_cast<uint8_t>(0b01), /*source is nullable*/             \
      static_cast<uint8_t>(depth), static_cast<uint8_t>(sourcetype), \
      static_cast<uint8_t>(targettype)
#define WASM_BR_ON_CAST_FAIL_NULL(depth, sourcetype, targettype)     \
  WASM_GC_OP(kExprBrOnCastFail),                                     \
      static_cast<uint8_t>(0b11), /*source, target nullable*/        \
      static_cast<uint8_t>(depth), static_cast<uint8_t>(sourcetype), \
      static_cast<uint8_t>(targettype)

#define WASM_GC_ANY_CONVERT_EXTERN(extern) \
  extern, WASM_GC_OP(kExprAnyConvertExtern)
#define WASM_GC_EXTERN_CONVERT_ANY(ref) ref, WASM_GC_OP(kExprExternConvertAny)

#define WASM_ARRAY_NEW(index, default_value, length) \
  default_value, length, WASM_GC_OP(kExprArrayNew), static_cast<uint8_t>(index)
#define WASM_ARRAY_NEW_DEFAULT(index, length) \
  length, WASM_GC_OP(kExprArrayNewDefault), static_cast<uint8_t>(index)
#define WASM_ARRAY_GET(typeidx, array, index) \
  array, index, WASM_GC_OP(kExprArrayGet), static_cast<uint8_t>(typeidx)
#define WASM_ARRAY_GET_U(typeidx, array, index) \
  array, index, WASM_GC_OP(kExprArrayGetU), static_cast<uint8_t>(typeidx)
#define WASM_ARRAY_GET_S(typeidx, array, index) \
  array, index, WASM_GC_OP(kExprArrayGetS), static_cast<uint8_t>(typeidx)
#define WASM_ARRAY_SET(typeidx, array, index, value) \
  array, index, value, WASM_GC_OP(kExprArraySet), static_cast<uint8_t>(typeidx)
#define WASM_ARRAY_LEN(array) array, WASM_GC_OP(kExprArrayLen)
#define WASM_ARRAY_COPY(dst_idx, src_idx, dst_array, dst_index, src_array, \
                        src_index, length)                                 \
  dst_array, dst_index, src_array, src_index, length,                      \
      WASM_GC_OP(kExprArrayCopy), static_cast<uint8_t>(dst_idx),           \
      static_cast<uint8_t>(src_idx)
#define WASM_ARRAY_NEW_FIXED(index, length, ...)                            \
  __VA_ARGS__, WASM_GC_OP(kExprArrayNewFixed), static_cast<uint8_t>(index), \
      static_cast<uint8_t>(length)

#define WASM_REF_I31(val) val, WASM_GC_OP(kExprRefI31)
#define WASM_I31_GET_S(val) val, WASM_GC_OP(kExprI31GetS)
#define WASM_I31_GET_U(val) val, WASM_GC_OP(kExprI31GetU)

#define WASM_BR_ON_NULL(depth, ref_object) \
  ref_object, kExprBrOnNull, static_cast<uint8_t>(depth)

#define WASM_BR_ON_NON_NULL(depth, ref_object) \
  ref_object, kExprBrOnNonNull, static_cast<uint8_t>(depth)

// Pass: sig_index, ...args, func_index
#define WASM_CALL_INDIRECT(sig_index, ...) \
  __VA_ARGS__, kExprCallIndirect, static_cast<uint8_t>(sig_index), TABLE_ZERO
#define WASM_CALL_INDIRECT_TABLE(table, sig_index, ...)            \
  __VA_ARGS__, kExprCallIndirect, static_cast<uint8_t>(sig_index), \
      static_cast<uint8_t>(table)
#define WASM_RETURN_CALL_INDIRECT(sig_index, ...)                        \
  __VA_ARGS__, kExprReturnCallIndirect, static_cast<uint8_t>(sig_index), \
      TABLE_ZERO

#define WASM_CALL_REF(func_ref, sig_index, ...) \
  __VA_ARGS__, func_ref, kExprCallRef, sig_index

#define WASM_RETURN_CALL_REF(func_ref, sig_index, ...) \
  __VA_ARGS__, func_ref, kExprReturnCallRef, sig_index

#define WASM_NOT(x) x, kExprI32Eqz
#define WASM_SEQ(...) __VA_ARGS__

//------------------------------------------------------------------------------
// Constructs that are composed of multiple bytecodes.
//------------------------------------------------------------------------------
#define WASM_WHILE(x, y)                                                      \
  kExprLoop, kVoidCode, x, kExprIf, kVoidCode, y, kExprBr, DEPTH_1, kExprEnd, \
      kExprEnd
#define WASM_INC_LOCAL(index)                                                \
  kExprLocalGet, static_cast<uint8_t>(index), kExprI32Const, 1, kExprI32Add, \
      kExprLocalTee, static_cast<uint8_t>(index)
#define WASM_INC_LOCAL_BYV(index, count)                       \
  kExprLocalGet, static_cast<uint8_t>(index), kExprI32Const,   \
      static_cast<uint8_t>(count), kExprI32Add, kExprLocalTee, \
      static_cast<uint8_t>(index)
#define WASM_INC_LOCAL_BY(index, count)                        \
  kExprLocalGet, static_cast<uint8_t>(index), kExprI32Const,   \
      static_cast<uint8_t>(count), kExprI32Add, kExprLocalSet, \
      static_cast<uint8_t>(index)
#define WASM_UNOP(opcode, x) x, static_cast<uint8_t>(opcode)
#define WASM_BINOP(opcode, x, y) x, y, static_cast<uint8_t>(opcode)

//------------------------------------------------------------------------------
// Int32 operations
//------------------------------------------------------------------------------
#define WASM_I32_ADD(x, y) x, y, kExprI32Add
#define WASM_I32_SUB(x, y) x, y, kExprI32Sub
#define WASM_I32_MUL(x, y) x, y, kExprI32Mul
#define WASM_I32_DIVS(x, y) x, y, kExprI32DivS
#define WASM_I32_DIVU(x, y) x, y, kExprI32DivU
#define WASM_I32_REMS(x, y) x, y, kExprI32RemS
#define WASM_I32_REMU(x, y) x, y, kExprI32RemU
#define WASM_I32_AND(x, y) x, y, kExprI32And
#define WASM_I32_IOR(x, y) x, y, kExprI32Ior
#define WASM_I32_XOR(x, y) x, y, kExprI32Xor
#define WASM_I32_SHL(x, y) x, y, kExprI32Shl
#define WASM_I32_SHR(x, y) x, y, kExprI32ShrU
#define WASM_I32_SAR(x, y) x, y, kExprI32ShrS
#define WASM_I32_ROR(x, y) x, y, kExprI32Ror
#define WASM_I32_ROL(x, y) x, y, kExprI32Rol
#define WASM_I32_EQ(x, y) x, y, kExprI32Eq
#define WASM_I32_NE(x, y) x, y, kExprI32Ne
#define WASM_I32_LTS(x, y) x, y, kExprI32LtS
#define WASM_I32_LES(x, y) x, y, kExprI32LeS
#define WASM_I32_LTU(x, y) x, y, kExprI32LtU
#define WASM_I32_LEU(x, y) x, y, kExprI32LeU
#define WASM_I32_GTS(x, y) x, y, kExprI32GtS
#define WASM_I32_GES(x, y) x, y, kExprI32GeS
#define WASM_I32_GTU(x, y) x, y, kExprI32GtU
#define WASM_I32_GEU(x, y) x, y, kExprI32GeU
#define WASM_I32_CLZ(x) x, kExprI32Clz
#define WASM_I32_CTZ(x) x, kExprI32Ctz
#define WASM_I32_POPCNT(x) x, kExprI32Popcnt
#define WASM_I32_EQZ(x) x, kExprI32Eqz

//------------------------------------------------------------------------------
// Asmjs Int32 operations
//------------------------------------------------------------------------------
#define WASM_I32_ASMJS_DIVS(x, y) x, y, kExprI32AsmjsDivS
#define WASM_I32_ASMJS_REMS(x, y) x, y, kExprI32AsmjsRemS
#define WASM_I32_ASMJS_DIVU(x, y) x, y, kExprI32AsmjsDivU
#define WASM_I32_ASMJS_REMU(x, y) x, y, kExprI32AsmjsRemU

//------------------------------------------------------------------------------
// Int64 operations
//------------------------------------------------------------------------------
#define WASM_I64_ADD(x, y) x, y, kExprI64Add
#define WASM_I64_SUB(x, y) x, y, kExprI64Sub
#define WASM_I64_MUL(x, y) x, y, kExprI64Mul
#define WASM_I64_DIVS(x, y) x, y, kExprI64DivS
#define WASM_I64_DIVU(x, y) x, y, kExprI64DivU
#define WASM_I64_REMS(x, y) x, y, kExprI64RemS
#define WASM_I64_REMU(x, y) x, y, kExprI64RemU
#define WASM_I64_AND(x, y) x, y, kExprI64And
#define WASM_I64_IOR(x, y) x, y, kExprI64Ior
#define WASM_I64_XOR(x, y) x, y, kExprI64Xor
#define WASM_I64_SHL(x, y) x, y, kExprI64Shl
#define WASM_I64_SHR(x, y) x, y, kExprI64ShrU
#define WASM_I64_SAR(x, y) x, y, kExprI64ShrS
#define WASM_I64_ROR(x, y) x, y, kExprI64Ror
#define WASM_I64_ROL(x, y) x, y, kExprI64Rol
#define WASM_I64_EQ(x, y) x, y, kExprI64Eq
#define WASM_I64_NE(x, y) x, y, kExprI64Ne
#define WASM_I64_LTS(x, y) x, y, kExprI64LtS
#define WASM_I64_LES(x, y) x, y, kExprI64LeS
#define WASM_I64_LTU(x, y) x, y, kExprI64LtU
#define WASM_I64_LEU(x, y) x, y, kExprI64LeU
#define WASM_I64_GTS(x, y) x, y, kExprI64GtS
#define WASM_I64_GES(x, y) x, y, kExprI64GeS
#define WASM_I64_GTU(x, y) x, y, kExprI64GtU
#define WASM_I64_GEU(x, y) x, y, kExprI64GeU
#define WASM_I64_CLZ(x) x, kExprI64Clz
#define WASM_I64_CTZ(x) x, kExprI64Ctz
#define WASM_I64_POPCNT(x) x, kExprI64Popcnt
#define WASM_I64_EQZ(x) x, kExprI64Eqz

//------------------------------------------------------------------------------
// Float32 operations
//------------------------------------------------------------------------------
#define WASM_F32_ADD(x, y) x, y, kExprF32Add
#define WASM_F32_SUB(x, y) x, y, kExprF32Sub
#define WASM_F32_MUL(x, y) x, y, kExprF32Mul
#define WASM_F32_DIV(x, y) x, y, kExprF32Div
#define WASM_F32_MIN(x, y) x, y, kExprF32Min
#define WASM_F32_MAX(x, y) x, y, kExprF32Max
#define WASM_F32_ABS(x) x, kExprF32Abs
#define WASM_F32_NEG(x) x, kExprF32Neg
#define WASM_F32_COPYSIGN(x, y) x, y, kExprF32CopySign
#define WASM_F32_CEIL(x) x, kExprF32Ceil
#define WASM_F32_FLOOR(x) x, kExprF32Floor
#define WASM_F32_TRUNC(x) x, kExprF32Trunc
#define WASM_F32_NEARESTINT(x) x, kExprF32NearestInt
#define WASM_F32_SQRT(x) x, kExprF32Sqrt
#define WASM_F32_EQ(x, y) x, y, kExprF32Eq
#define WASM_F32_NE(x, y) x, y, kExprF32Ne
#define WASM_F32_LT(x, y) x, y, kExprF32Lt
#define WASM_F32_LE(x, y) x, y, kExprF32Le
#define WASM_F32_GT(x, y) x, y, kExprF32Gt
#define WASM_F32_GE(x, y) x, y, kExprF32Ge

//------------------------------------------------------------------------------
// Float64 operations
//------------------------------------------------------------------------------
#define WASM_F64_ADD(x, y) x, y, kExprF64Add
#define WASM_F64_SUB(x, y) x, y, kExprF64Sub
#define WASM_F64_MUL(x, y) x, y, kExprF64Mul
#define WASM_F64_DIV(x, y) x, y, kExprF64Div
#define WASM_F64_MIN(x, y) x, y, kExprF64Min
#define WASM_F64_MAX(x, y) x, y, kExprF64Max
#define WASM_F64_ABS(x) x, kExprF64Abs
#define WASM_F64_NEG(x) x, kExprF64Neg
#define WASM_F64_COPYSIGN(x, y) x, y, kExprF64CopySign
#define WASM_F64_CEIL(x) x, kExprF64Ceil
#define WASM_F64_FLOOR(x) x, kExprF64Floor
#define WASM_F64_TRUNC(x) x, kExprF64Trunc
#define WASM_F64_NEARESTINT(x) x, kExprF64NearestInt
#define WASM_F64_SQRT(x) x, kExprF64Sqrt
#define WASM_F64_EQ(x, y) x, y, kExprF64Eq
#define WASM_F64_NE(x, y) x, y, kExprF64Ne
#define WASM_F64_LT(x, y) x, y, kExprF64Lt
#define WASM_F64_LE(x, y) x, y, kExprF64Le
#define WASM_F64_GT(x, y) x, y, kExprF64Gt
#define WASM_F64_GE(x, y) x, y, kExprF64Ge

//------------------------------------------------------------------------------
// Type conversions.
//------------------------------------------------------------------------------
#define WASM_I32_SCONVERT_F32(x) x, kExprI32SConvertF32
#define WASM_I32_SCONVERT_F64(x) x, kExprI32SConvertF64
#define WASM_I32_UCONVERT_F32(x) x, kExprI32UConvertF32
#define WASM_I32_UCONVERT_F64(x) x, kExprI32UConvertF64
#define WASM_I32_CONVERT_I64(x) x, kExprI32ConvertI64
#define WASM_I64_SCONVERT_F32(x) x, kExprI64SConvertF32
#define WASM_I64_SCONVERT_F64(x) x, kExprI64SConvertF64
#define WASM_I64_UCONVERT_F32(x) x, kExprI64UConvertF32
#define WASM_I64_UCONVERT_F64(x) x, kExprI64UConvertF64
#define WASM_I64_SCONVERT_I32(x) x, kExprI64SConvertI32
#define WASM_I64_UCONVERT_I32(x) x, kExprI64UConvertI32
#define WASM_F32_SCONVERT_I32(x) x, kExprF32SConvertI32
#define WASM_F32_UCONVERT_I32(x) x, kExprF32UConvertI32
#define WASM_F32_SCONVERT_I64(x) x, kExprF32SConvertI64
#define WASM_F32_UCONVERT_I64(x) x, kExprF32UConvertI64
#define WASM_F32_CONVERT_F64(x) x, kExprF32ConvertF64
#define WASM_F32_REINTERPRET_I32(x) x, kExprF32ReinterpretI32
#define WASM_F64_SCONVERT_I32(x) x, kExprF64SConvertI32
#define WASM_F64_UCONVERT_I32(x) x, kExprF64UConvertI32
#define WASM_F64_SCONVERT_I64(x) x, kExprF64SConvertI64
#define WASM_F64_UCONVERT_I64(x) x, kExprF64UConvertI64
#define WASM_F64_CONVERT_F32(x) x, kExprF64ConvertF32
#define WASM_F64_REINTERPRET_I64(x) x, kExprF64ReinterpretI64
#define WASM_I32_REINTERPRET_F32(x) x, kExprI32ReinterpretF32
#define WASM_I64_REINTERPRET_F64(x) x, kExprI64ReinterpretF64

//------------------------------------------------------------------------------
// Numeric operations
//------------------------------------------------------------------------------
#define WASM_NUMERIC_OP(op) kNumericPrefix, static_cast<uint8_t>(op)
#define WASM_I32_SCONVERT_SAT_F32(x) x, WASM_NUMERIC_OP(kExprI32SConvertSatF32)
#define WASM_I32_UCONVERT_SAT_F32(x) x, WASM_NUMERIC_OP(kExprI32UConvertSatF32)
#define WASM_I32_SCONVERT_SAT_F64(x) x, WASM_NUMERIC_OP(kExprI32SConvertSatF64)
#define WASM_I32_UCONVERT_SAT_F64(x) x, WASM_NUMERIC_OP(kExprI32UConvertSatF64)
#define WASM_I64_SCONVERT_SAT_F32(x) x, WASM_NUMERIC_OP(kExprI64SConvertSatF32)
#define WASM_I64_UCONVERT_SAT_F32(x) x, WASM_NUMERIC_OP(kExprI64UConvertSatF32)
#define WASM_I64_SCONVERT_SAT_F64(x) x, WASM_NUMERIC_OP(kExprI64SConvertSatF64)
#define WASM_I64_UCONVERT_SAT_F64(x) x, WASM_NUMERIC_OP(kExprI64UConvertSatF64)

#define MEMORY_ZERO 0

#define WASM_MEMORY_INIT(seg, dst, src, size) \
  dst, src, size, WASM_NUMERIC_OP(kExprMemoryInit), U32V_1(seg), MEMORY_ZERO
#define WASM_DATA_DROP(seg) WASM_NUMERIC_OP(kExprDataDrop), U32V_1(seg)
#define WASM_MEMORY0_COPY(dst, src, size) \
  dst, src, size, WASM_NUMERIC_OP(kExprMemoryCopy), MEMORY_ZERO, MEMORY_ZERO
#define WASM_MEMORY_COPY(dst_index, src_index, dst, src, size) \
  dst, src, size, WASM_NUMERIC_OP(kExprMemoryCopy), dst_index, src_index
#define WASM_MEMORY_FILL(dst, val, size) \
  dst, val, size, WASM_NUMERIC_OP(kExprMemoryFill), MEMORY_ZERO
#define WASM_TABLE_INIT(table, seg, dst, src, size)             \
  dst, src, size, WASM_NUMERIC_OP(kExprTableInit), U32V_1(seg), \
      static_cast<uint8_t>(table)
#define WASM_ELEM_DROP(seg) WASM_NUMERIC_OP(kExprElemDrop), U32V_1(seg)
#define WASM_TABLE_COPY(table_dst, table_src, dst, src, size) \
  dst, src, size, WASM_NUMERIC_OP(kExprTableCopy),            \
      static_cast<uint8_t>(table_dst), static_cast<uint8_t>(table_src)
#define WASM_TABLE_GROW(table, initial_value, delta)     \
  initial_value, delta, WASM_NUMERIC_OP(kExprTableGrow), \
      static_cast<uint8_t>(table)
#define WASM_TABLE_SIZE(table) \
  WASM_NUMERIC_OP(kExprTableSize), static_cast<uint8_t>(table)
#define WASM_TABLE_FILL(table, times, value, start)     \
  times, value, start, WASM_NUMERIC_OP(kExprTableFill), \
      static_cast<uint8_t>(table)

//------------------------------------------------------------------------------
// Memory Operations.
//------------------------------------------------------------------------------
#define WASM_MEMORY_GROW(x) x, kExprMemoryGrow, 0
#define WASM_MEMORY_SIZE kExprMemorySize, 0

#define SIG_ENTRY_v_v kWasmFunctionTypeCode, 0, 0
#define SIZEOF_SIG_ENTRY_v_v 3

#define SIG_ENTRY_v_x(a) kWasmFunctionTypeCode, 1, a, 0
#define SIG_ENTRY_v_xx(a, b) kWasmFunctionTypeCode, 2, a, b, 0
#define SIG_ENTRY_v_xxx(a, b, c) kWasmFunctionTypeCode, 3, a, b, c, 0
#define SIZEOF_SIG_ENTRY_v_x 4
#define SIZEOF_SIG_ENTRY_v_xx 5
#define SIZEOF_SIG_ENTRY_v_xxx 6

#define SIG_ENTRY_x(r) kWasmFunctionTypeCode, 0, 1, r
#define SIG_ENTRY_x_x(r, a) kWasmFunctionTypeCode, 1, a, 1, r
#define SIG_ENTRY_x_xx(r, a, b) kWasmFunctionTypeCode, 2, a, b, 1, r
#define SIG_ENTRY_xx_xx(r, s, a, b) kWasmFunctionTypeCode, 2, a, b, 2, r, s
#define SIG_ENTRY_x_xxx(r, a, b, c) kWasmFunctionTypeCode, 3, a, b, c, 1, r
#define SIZEOF_SIG_ENTRY_x 4
#define SIZEOF_SIG_ENTRY_x_x 5
#define SIZEOF_SIG_ENTRY_x_xx 6
#define SIZEOF_SIG_ENTRY_xx_xx 7
#define SIZEOF_SIG_ENTRY_x_xxx 7

#define WASM_BRV(depth, ...) __VA_ARGS__, kExprBr, static_cast<uint8_t>(depth)
#define WASM_BRV_IF(depth, val, cond) \
  val, cond, kExprBrIf, static_cast<uint8_t>(depth)
#define WASM_BRV_IFD(depth, val, cond) \
  val, cond, kExprBrIf, static_cast<uint8_t>(depth), kExprDrop

//------------------------------------------------------------------------------
// Atomic Operations.
//------------------------------------------------------------------------------
#define WASM_ATOMICS_OP(op) kAtomicPrefix, static_cast<uint8_t>(op)
#define WASM_ATOMICS_BINOP(op, x, y, representation) \
  x, y, WASM_ATOMICS_OP(op),                         \
      static_cast<uint8_t>(ElementSizeLog2Of(representation)), ZERO_OFFSET
#define WASM_ATOMICS_TERNARY_OP(op, x, y, z, representation) \
  x, y, z, WASM_ATOMICS_OP(op),                              \
      static_cast<uint8_t>(ElementSizeLog2Of(representation)), ZERO_OFFSET
#define WASM_ATOMICS_LOAD_OP(op, x, representation) \
  x, WASM_ATOMICS_OP(op),                           \
      static_cast<uint8_t>(ElementSizeLog2Of(representation)), ZERO_OFFSET
#define WASM_ATOMICS_STORE_OP(op, x, y, representation) \
  x, y, WASM_ATOMICS_OP(op),                            \
      static_cast<uint8_t>(ElementSizeLog2Of(representation)), ZERO_OFFSET
#define WASM_ATOMICS_WAIT(op, index, value, timeout, alignment, offset) \
  index, value, timeout, WASM_ATOMICS_OP(op), alignment, offset
#define WASM_ATOMICS_FENCE WASM_ATOMICS_OP(kExprAtomicFence), ZERO_OFFSET

//------------------------------------------------------------------------------
// Sign Extension Operations.
//------------------------------------------------------------------------------
#define WASM_I32_SIGN_EXT_I8(x) x, kExprI32SExtendI8
#define WASM_I32_SIGN_EXT_I16(x) x, kExprI32SExtendI16
#define WASM_I64_SIGN_EXT_I8(x) x, kExprI64SExtendI8
#define WASM_I64_SIGN_EXT_I16(x) x, kExprI64SExtendI16
#define WASM_I64_SIGN_EXT_I32(x) x, kExprI64SExtendI32

//------------------------------------------------------------------------------
// SIMD Operations.
//------------------------------------------------------------------------------
#define TO_BYTE(val) static_cast<uint8_t>(val)
// Encode all simd ops as a 2-byte LEB.
#define WASM_SIMD_OP(op) kSimdPrefix, U32V_2(ExtractPrefixedOpcodeBytes(op))
#define WASM_SIMD_OPN(op, ...) __VA_ARGS__, WASM_SIMD_OP(op)
#define WASM_SIMD_SPLAT(Type, ...) __VA_ARGS__, WASM_SIMD_OP(kExpr##Type##Splat)
#define WASM_SIMD_UNOP(op, x) x, WASM_SIMD_OP(op)
#define WASM_SIMD_BINOP(op, x, y) x, y, WASM_SIMD_OP(op)
#define WASM_SIMD_TERNOP(op, x, y, z) x, y, z, WASM_SIMD_OP(op)
#define WASM_SIMD_SHIFT_OP(op, x, y) x, y, WASM_SIMD_OP(op)
#define WASM_SIMD_CONCAT_OP(op, bytes, x, y) \
  x, y, WASM_SIMD_OP(op), TO_BYTE(bytes)
#define WASM_SIMD_SELECT(format, x, y, z) x, y, z, WASM_SIMD_OP(kExprS128Select)
#define WASM_SIMD_CONSTANT(v)                                                \
  WASM_SIMD_OP(kExprS128Const), TO_BYTE(v[0]), TO_BYTE(v[1]), TO_BYTE(v[2]), \
      TO_BYTE(v[3]), TO_BYTE(v[4]), TO_BYTE(v[5]), TO_BYTE(v[6]),            \
      TO_BYTE(v[7]), TO_BYTE(v[8]), TO_BYTE(v[9]), TO_BYTE(v[10]),           \
      TO_BYTE(v[11]), TO_BYTE(v[12]), TO_BYTE(v[13]), TO_BYTE(v[14]),        \
      TO_BYTE(v[15])

#define WASM_SIMD_F64x2_SPLAT(x) WASM_SIMD_SPLAT(F64x2, x)
#define WASM_SIMD_F64x2_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprF64x2ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_F64x2_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprF64x2ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_F32x4_SPLAT(x) WASM_SIMD_SPLAT(F32x4, x)
#define WASM_SIMD_F32x4_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprF32x4ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_F32x4_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprF32x4ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_F16x8_SPLAT(x) WASM_SIMD_SPLAT(F16x8, x)
#define WASM_SIMD_F16x8_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprF16x8ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_F16x8_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprF16x8ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I64x2_SPLAT(x) WASM_SIMD_SPLAT(I64x2, x)
#define WASM_SIMD_I64x2_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI64x2ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_I64x2_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI64x2ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I32x4_SPLAT(x) WASM_SIMD_SPLAT(I32x4, x)
#define WASM_SIMD_I32x4_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI32x4ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_I32x4_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI32x4ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I16x8_SPLAT(x) WASM_SIMD_SPLAT(I16x8, x)
#define WASM_SIMD_I16x8_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI16x8ExtractLaneS), TO_BYTE(lane)
#define WASM_SIMD_I16x8_EXTRACT_LANE_U(lane, x) \
  x, WASM_SIMD_OP(kExprI16x8ExtractLaneU), TO_BYTE(lane)
#define WASM_SIMD_I16x8_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI16x8ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I8x16_SPLAT(x) WASM_SIMD_SPLAT(I8x16, x)
#define WASM_SIMD_I8x16_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI8x16ExtractLaneS), TO_BYTE(lane)
#define WASM_SIMD_I8x16_EXTRACT_LANE_U(lane, x) \
  x, WASM_SIMD_OP(kExprI8x16ExtractLaneU), TO_BYTE(lane)
#define WASM_SIMD_I8x16_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI8x16ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I8x16_SHUFFLE_OP(opcode, m, x, y)                        \
  x, y, WASM_SIMD_OP(opcode), TO_BYTE(m[0]), TO_BYTE(m[1]), TO_BYTE(m[2]), \
      TO_BYTE(m[3]), TO_BYTE(m[4]), TO_BYTE(m[5]), TO_BYTE(m[6]),          \
      TO_BYTE(m[7]), TO_BYTE(m[8]), TO_BYTE(m[9]), TO_BYTE(m[10]),         \
      TO_BYTE(m[11]), TO_BYTE(m[12]), TO_BYTE(m[13]), TO_BYTE(m[14]),      \
      TO_BYTE(m[15])

#define WASM_SIMD_LOAD_MEM(index) \
  index, WASM_SIMD_OP(kExprS128LoadMem), ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_SIMD_LOAD_MEM_OFFSET(offset, index) \
  index, WASM_SIMD_OP(kExprS128LoadMem), ZERO_ALIGNMENT, offset
#define WASM_SIMD_STORE_MEM(index, val) \
  index, val, WASM_SIMD_OP(kExprS128StoreMem), ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_SIMD_STORE_MEM_OFFSET(offset, index, val) \
  index, val, WASM_SIMD_OP(kExprS128StoreMem), ZERO_ALIGNMENT, offset

#define WASM_SIMD_F64x2_QFMA(a, b, c) a, b, c, WASM_SIMD_OP(kExprF64x2Qfma)
#define WASM_SIMD_F64x2_QFMS(a, b, c) a, b, c, WASM_SIMD_OP(kExprF64x2Qfms)
#define WASM_SIMD_F32x4_QFMA(a, b, c) a, b, c, WASM_SIMD_OP(kExprF32x4Qfma)
#define WASM_SIMD_F32x4_QFMS(a, b, c) a, b, c, WASM_SIMD_OP(kExprF32x4Qfms)
#define WASM_SIMD_F16x8_QFMA(a, b, c) a, b, c, WASM_SIMD_OP(kExprF16x8Qfma)
#define WASM_SIMD_F16x8_QFMS(a, b, c) a, b, c, WASM_SIMD_OP(kExprF16x8Qfms)

// Like WASM_SIMD_LOAD_MEM but needs the load opcode.
#define WASM_SIMD_LOAD_OP(opcode, index) \
  index, WASM_SIMD_OP(opcode), ZERO_ALIGNMENT, ZERO_OFFSET
#define WASM_SIMD_LOAD_OP_OFFSET(opcode, index, offset) \
  index, WASM_SIMD_OP(opcode), ZERO_ALIGNMENT, offset
#define WASM_SIMD_LOAD_OP_ALIGNMENT(opcode, index, alignment) \
  index, WASM_SIMD_OP(opcode), alignment, ZERO_OFFSET

// Load a Simd lane from a numeric pointer. We need this because lanes are
// reversed in big endian. Note: a Simd value has {kSimd128Size / sizeof(*ptr)}
// lanes.
#ifdef V8_TARGET_BIG_ENDIAN
#define LANE(ptr, index) ptr[kSimd128Size / sizeof(*ptr) - (index)-1]
#else
#define LANE(ptr, index) ptr[index]
#endif

//------------------------------------------------------------------------------
// Compilation Hints.
//------------------------------------------------------------------------------
#define COMPILE_STRATEGY_DEFAULT (0x00)
#define COMPILE_STRATEGY_LAZY (0x01)
#define COMPILE_STRATEGY_EAGER (0x02)
#define BASELINE_TIER_DEFAULT (0x00 << 2)
#define BASELINE_TIER_BASELINE (0x01 << 2)
#define BASELINE_TIER_OPTIMIZED (0x02 << 2)
#define TOP_TIER_DEFAULT (0x00 << 4)
#define TOP_TIER_BASELINE (0x01 << 4)
#define TOP_TIER_OPTIMIZED (0x02 << 4)

#endif  // V8_WASM_MACRO_GEN_H_
