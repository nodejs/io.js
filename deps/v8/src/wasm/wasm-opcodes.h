// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_OPCODES_H_
#define V8_WASM_WASM_OPCODES_H_

#include <memory>

#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-constants.h"

namespace v8 {
namespace internal {

namespace wasm {

struct WasmModule;

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const FunctionSig& function);
V8_EXPORT_PRIVATE bool IsJSCompatibleSignature(const FunctionSig* sig);

// Format of all opcode macros: kExprName, binary, signature, wat name

// Control expressions and blocks.
#define FOREACH_CONTROL_OPCODE(V)           \
  V(Unreachable, 0x00, _, "unreachable")    \
  V(Nop, 0x01, _, "nop")                    \
  V(Block, 0x02, _, "block")                \
  V(Loop, 0x03, _, "loop")                  \
  V(If, 0x04, _, "if")                      \
  V(Else, 0x05, _, "else")                  \
  V(Try, 0x06, _, "try")                    \
  V(Catch, 0x07, _, "catch")                \
  V(Throw, 0x08, _, "throw")                \
  V(Rethrow, 0x09, _, "rethrow")            \
  V(TryTable, 0x1f, _, "try_table")         \
  V(ThrowRef, 0x0a, _, "throw_ref")         \
  V(End, 0x0b, _, "end")                    \
  V(Br, 0x0c, _, "br")                      \
  V(BrIf, 0x0d, _, "br_if")                 \
  V(BrTable, 0x0e, _, "br_table")           \
  V(Return, 0x0f, _, "return")              \
  V(Delegate, 0x18, _, "delegate")          \
  V(CatchAll, 0x19, _, "catch_all")         \
  V(BrOnNull, 0xd5, _, "br_on_null")        \
  V(BrOnNonNull, 0xd6, _, "br_on_non_null") \
  V(NopForTestingUnsupportedInLiftoff, 0x16, _, "nop_for_testing")

// Constants, locals, globals, calls, etc.
#define FOREACH_MISC_OPCODE(V)                           \
  V(CallFunction, 0x10, _, "call")                       \
  V(CallIndirect, 0x11, _, "call_indirect")              \
  V(ReturnCall, 0x12, _, "return_call")                  \
  V(ReturnCallIndirect, 0x13, _, "return_call_indirect") \
  V(CallRef, 0x14, _, "call_ref")                        \
  V(ReturnCallRef, 0x15, _, "return_call_ref")           \
  V(Drop, 0x1a, _, "drop")                               \
  V(Select, 0x1b, _, "select")                           \
  V(SelectWithType, 0x1c, _, "select")                   \
  V(LocalGet, 0x20, _, "local.get")                      \
  V(LocalSet, 0x21, _, "local.set")                      \
  V(LocalTee, 0x22, _, "local.tee")                      \
  V(GlobalGet, 0x23, _, "global.get")                    \
  V(GlobalSet, 0x24, _, "global.set")                    \
  V(TableGet, 0x25, _, "table.get")                      \
  V(TableSet, 0x26, _, "table.set")                      \
  V(I32Const, 0x41, _, "i32.const")                      \
  V(I64Const, 0x42, _, "i64.const")                      \
  V(F32Const, 0x43, _, "f32.const")                      \
  V(F64Const, 0x44, _, "f64.const")                      \
  V(RefNull, 0xd0, _, "ref.null")                        \
  V(RefIsNull, 0xd1, _, "ref.is_null")                   \
  V(RefFunc, 0xd2, _, "ref.func")                        \
  V(RefAsNonNull, 0xd4, _, "ref.as_non_null")            \
  V(RefEq, 0xd3, _, "ref.eq")

// Load memory expressions.
#define FOREACH_LOAD_MEM_OPCODE(V)            \
  V(I32LoadMem, 0x28, i_i, "i32.load")        \
  V(I64LoadMem, 0x29, l_i, "i64.load")        \
  V(F32LoadMem, 0x2a, f_i, "f32.load")        \
  V(F64LoadMem, 0x2b, d_i, "f64.load")        \
  V(I32LoadMem8S, 0x2c, i_i, "i32.load8_s")   \
  V(I32LoadMem8U, 0x2d, i_i, "i32.load8_u")   \
  V(I32LoadMem16S, 0x2e, i_i, "i32.load16_s") \
  V(I32LoadMem16U, 0x2f, i_i, "i32.load16_u") \
  V(I64LoadMem8S, 0x30, l_i, "i64.load8_s")   \
  V(I64LoadMem8U, 0x31, l_i, "i64.load8_u")   \
  V(I64LoadMem16S, 0x32, l_i, "i64.load16_s") \
  V(I64LoadMem16U, 0x33, l_i, "i64.load16_u") \
  V(I64LoadMem32S, 0x34, l_i, "i64.load32_s") \
  V(I64LoadMem32U, 0x35, l_i, "i64.load32_u") \
  V(F32LoadMemF16, 0xfc30, f_i, "f32.load_f16")

// Store memory expressions.
#define FOREACH_STORE_MEM_OPCODE(V)           \
  V(I32StoreMem, 0x36, v_ii, "i32.store")     \
  V(I64StoreMem, 0x37, v_il, "i64.store")     \
  V(F32StoreMem, 0x38, v_if, "f32.store")     \
  V(F64StoreMem, 0x39, v_id, "f64.store")     \
  V(I32StoreMem8, 0x3a, v_ii, "i32.store8")   \
  V(I32StoreMem16, 0x3b, v_ii, "i32.store16") \
  V(I64StoreMem8, 0x3c, v_il, "i64.store8")   \
  V(I64StoreMem16, 0x3d, v_il, "i64.store16") \
  V(I64StoreMem32, 0x3e, v_il, "i64.store32") \
  V(F32StoreMemF16, 0xfc31, v_if, "f32.store_f16")

// Miscellaneous memory expressions
#define FOREACH_MISC_MEM_OPCODE(V)        \
  V(MemorySize, 0x3f, i_v, "memory.size") \
  V(MemoryGrow, 0x40, i_i, "memory.grow")

// Expressions with signatures.

// Opcodes that can also be used in constant expressions (via the 'extended
// constant expressions' proposal).
#define FOREACH_SIMPLE_EXTENDED_CONST_OPCODE(V) \
  V(I32Add, 0x6a, i_ii, "i32.add")              \
  V(I32Sub, 0x6b, i_ii, "i32.sub")              \
  V(I32Mul, 0x6c, i_ii, "i32.mul")              \
  V(I64Add, 0x7c, l_ll, "i64.add")              \
  V(I64Sub, 0x7d, l_ll, "i64.sub")              \
  V(I64Mul, 0x7e, l_ll, "i64.mul")

#define FOREACH_SIMPLE_NON_CONST_OPCODE(V)               \
  V(I32Eqz, 0x45, i_i, "i32.eqz")                        \
  V(I32Eq, 0x46, i_ii, "i32.eq")                         \
  V(I32Ne, 0x47, i_ii, "i32.ne")                         \
  V(I32LtS, 0x48, i_ii, "i32.lt_s")                      \
  V(I32LtU, 0x49, i_ii, "i32.lt_u")                      \
  V(I32GtS, 0x4a, i_ii, "i32.gt_s")                      \
  V(I32GtU, 0x4b, i_ii, "i32.gt_u")                      \
  V(I32LeS, 0x4c, i_ii, "i32.le_s")                      \
  V(I32LeU, 0x4d, i_ii, "i32.le_u")                      \
  V(I32GeS, 0x4e, i_ii, "i32.ge_s")                      \
  V(I32GeU, 0x4f, i_ii, "i32.ge_u")                      \
  V(I64Eqz, 0x50, i_l, "i64.eqz")                        \
  V(I64Eq, 0x51, i_ll, "i64.eq")                         \
  V(I64Ne, 0x52, i_ll, "i64.ne")                         \
  V(I64LtS, 0x53, i_ll, "i64.lt_s")                      \
  V(I64LtU, 0x54, i_ll, "i64.lt_u")                      \
  V(I64GtS, 0x55, i_ll, "i64.gt_s")                      \
  V(I64GtU, 0x56, i_ll, "i64.gt_u")                      \
  V(I64LeS, 0x57, i_ll, "i64.le_s")                      \
  V(I64LeU, 0x58, i_ll, "i64.le_u")                      \
  V(I64GeS, 0x59, i_ll, "i64.ge_s")                      \
  V(I64GeU, 0x5a, i_ll, "i64.ge_u")                      \
  V(F32Eq, 0x5b, i_ff, "f32.eq")                         \
  V(F32Ne, 0x5c, i_ff, "f32.ne")                         \
  V(F32Lt, 0x5d, i_ff, "f32.lt")                         \
  V(F32Gt, 0x5e, i_ff, "f32.gt")                         \
  V(F32Le, 0x5f, i_ff, "f32.le")                         \
  V(F32Ge, 0x60, i_ff, "f32.ge")                         \
  V(F64Eq, 0x61, i_dd, "f64.eq")                         \
  V(F64Ne, 0x62, i_dd, "f64.ne")                         \
  V(F64Lt, 0x63, i_dd, "f64.lt")                         \
  V(F64Gt, 0x64, i_dd, "f64.gt")                         \
  V(F64Le, 0x65, i_dd, "f64.le")                         \
  V(F64Ge, 0x66, i_dd, "f64.ge")                         \
  V(I32Clz, 0x67, i_i, "i32.clz")                        \
  V(I32Ctz, 0x68, i_i, "i32.ctz")                        \
  V(I32Popcnt, 0x69, i_i, "i32.popcnt")                  \
  V(I32DivS, 0x6d, i_ii, "i32.div_s")                    \
  V(I32DivU, 0x6e, i_ii, "i32.div_u")                    \
  V(I32RemS, 0x6f, i_ii, "i32.rem_s")                    \
  V(I32RemU, 0x70, i_ii, "i32.rem_u")                    \
  V(I32And, 0x71, i_ii, "i32.and")                       \
  V(I32Ior, 0x72, i_ii, "i32.or")                        \
  V(I32Xor, 0x73, i_ii, "i32.xor")                       \
  V(I32Shl, 0x74, i_ii, "i32.shl")                       \
  V(I32ShrS, 0x75, i_ii, "i32.shr_s")                    \
  V(I32ShrU, 0x76, i_ii, "i32.shr_u")                    \
  V(I32Rol, 0x77, i_ii, "i32.rotl")                      \
  V(I32Ror, 0x78, i_ii, "i32.rotr")                      \
  V(I64Clz, 0x79, l_l, "i64.clz")                        \
  V(I64Ctz, 0x7a, l_l, "i64.ctz")                        \
  V(I64Popcnt, 0x7b, l_l, "i64.popcnt")                  \
  V(I64DivS, 0x7f, l_ll, "i64.div_s")                    \
  V(I64DivU, 0x80, l_ll, "i64.div_u")                    \
  V(I64RemS, 0x81, l_ll, "i64.rem_s")                    \
  V(I64RemU, 0x82, l_ll, "i64.rem_u")                    \
  V(I64And, 0x83, l_ll, "i64.and")                       \
  V(I64Ior, 0x84, l_ll, "i64.or")                        \
  V(I64Xor, 0x85, l_ll, "i64.xor")                       \
  V(I64Shl, 0x86, l_ll, "i64.shl")                       \
  V(I64ShrS, 0x87, l_ll, "i64.shr_s")                    \
  V(I64ShrU, 0x88, l_ll, "i64.shr_u")                    \
  V(I64Rol, 0x89, l_ll, "i64.rotl")                      \
  V(I64Ror, 0x8a, l_ll, "i64.rotr")                      \
  V(F32Abs, 0x8b, f_f, "f32.abs")                        \
  V(F32Neg, 0x8c, f_f, "f32.neg")                        \
  V(F32Ceil, 0x8d, f_f, "f32.ceil")                      \
  V(F32Floor, 0x8e, f_f, "f32.floor")                    \
  V(F32Trunc, 0x8f, f_f, "f32.trunc")                    \
  V(F32NearestInt, 0x90, f_f, "f32.nearest")             \
  V(F32Sqrt, 0x91, f_f, "f32.sqrt")                      \
  V(F32Add, 0x92, f_ff, "f32.add")                       \
  V(F32Sub, 0x93, f_ff, "f32.sub")                       \
  V(F32Mul, 0x94, f_ff, "f32.mul")                       \
  V(F32Div, 0x95, f_ff, "f32.div")                       \
  V(F32Min, 0x96, f_ff, "f32.min")                       \
  V(F32Max, 0x97, f_ff, "f32.max")                       \
  V(F32CopySign, 0x98, f_ff, "f32.copysign")             \
  V(F64Abs, 0x99, d_d, "f64.abs")                        \
  V(F64Neg, 0x9a, d_d, "f64.neg")                        \
  V(F64Ceil, 0x9b, d_d, "f64.ceil")                      \
  V(F64Floor, 0x9c, d_d, "f64.floor")                    \
  V(F64Trunc, 0x9d, d_d, "f64.trunc")                    \
  V(F64NearestInt, 0x9e, d_d, "f64.nearest")             \
  V(F64Sqrt, 0x9f, d_d, "f64.sqrt")                      \
  V(F64Add, 0xa0, d_dd, "f64.add")                       \
  V(F64Sub, 0xa1, d_dd, "f64.sub")                       \
  V(F64Mul, 0xa2, d_dd, "f64.mul")                       \
  V(F64Div, 0xa3, d_dd, "f64.div")                       \
  V(F64Min, 0xa4, d_dd, "f64.min")                       \
  V(F64Max, 0xa5, d_dd, "f64.max")                       \
  V(F64CopySign, 0xa6, d_dd, "f64.copysign")             \
  V(I32ConvertI64, 0xa7, i_l, "i32.wrap_i64")            \
  V(I32SConvertF32, 0xa8, i_f, "i32.trunc_f32_s")        \
  V(I32UConvertF32, 0xa9, i_f, "i32.trunc_f32_u")        \
  V(I32SConvertF64, 0xaa, i_d, "i32.trunc_f64_s")        \
  V(I32UConvertF64, 0xab, i_d, "i32.trunc_f64_u")        \
  V(I64SConvertI32, 0xac, l_i, "i64.extend_i32_s")       \
  V(I64UConvertI32, 0xad, l_i, "i64.extend_i32_u")       \
  V(I64SConvertF32, 0xae, l_f, "i64.trunc_f32_s")        \
  V(I64UConvertF32, 0xaf, l_f, "i64.trunc_f32_u")        \
  V(I64SConvertF64, 0xb0, l_d, "i64.trunc_f64_s")        \
  V(I64UConvertF64, 0xb1, l_d, "i64.trunc_f64_u")        \
  V(F32SConvertI32, 0xb2, f_i, "f32.convert_i32_s")      \
  V(F32UConvertI32, 0xb3, f_i, "f32.convert_i32_u")      \
  V(F32SConvertI64, 0xb4, f_l, "f32.convert_i64_s")      \
  V(F32UConvertI64, 0xb5, f_l, "f32.convert_i64_u")      \
  V(F32ConvertF64, 0xb6, f_d, "f32.demote_f64")          \
  V(F64SConvertI32, 0xb7, d_i, "f64.convert_i32_s")      \
  V(F64UConvertI32, 0xb8, d_i, "f64.convert_i32_u")      \
  V(F64SConvertI64, 0xb9, d_l, "f64.convert_i64_s")      \
  V(F64UConvertI64, 0xba, d_l, "f64.convert_i64_u")      \
  V(F64ConvertF32, 0xbb, d_f, "f64.promote_f32")         \
  V(I32ReinterpretF32, 0xbc, i_f, "i32.reinterpret_f32") \
  V(I64ReinterpretF64, 0xbd, l_d, "i64.reinterpret_f64") \
  V(F32ReinterpretI32, 0xbe, f_i, "f32.reinterpret_i32") \
  V(F64ReinterpretI64, 0xbf, d_l, "f64.reinterpret_i64") \
  V(I32SExtendI8, 0xc0, i_i, "i32.extend8_s")            \
  V(I32SExtendI16, 0xc1, i_i, "i32.extend16_s")          \
  V(I64SExtendI8, 0xc2, l_l, "i64.extend8_s")            \
  V(I64SExtendI16, 0xc3, l_l, "i64.extend16_s")          \
  V(I64SExtendI32, 0xc4, l_l, "i64.extend32_s")

#define FOREACH_SIMPLE_OPCODE(V)          \
  FOREACH_SIMPLE_EXTENDED_CONST_OPCODE(V) \
  FOREACH_SIMPLE_NON_CONST_OPCODE(V)

#define FOREACH_SIMPLE_PROTOTYPE_OPCODE(V)

// For compatibility with Asm.js.
// These opcodes are not spec'ed (or visible) externally; the idea is
// to use unused ranges for internal purposes.
#define FOREACH_ASMJS_COMPAT_OPCODE(V)                         \
  V(F64Acos, 0xdc, d_d, "f64.acos")                            \
  V(F64Asin, 0xdd, d_d, "f64.asin")                            \
  V(F64Atan, 0xde, d_d, "f64.atan")                            \
  V(F64Cos, 0xdf, d_d, "f64.cos")                              \
  V(F64Sin, 0xe0, d_d, "f64.sin")                              \
  V(F64Tan, 0xe1, d_d, "f64.tan")                              \
  V(F64Exp, 0xe2, d_d, "f64.exp")                              \
  V(F64Log, 0xe3, d_d, "f64.log")                              \
  V(F64Atan2, 0xe4, d_dd, "f64.atan2")                         \
  V(F64Pow, 0xe5, d_dd, "f64.pow")                             \
  V(F64Mod, 0xe6, d_dd, "f64.mod")                             \
  V(I32AsmjsDivS, 0xe7, i_ii, "i32.asmjs_div_s")               \
  V(I32AsmjsDivU, 0xe8, i_ii, "i32.asmjs_div_u")               \
  V(I32AsmjsRemS, 0xe9, i_ii, "i32.asmjs_rem_s")               \
  V(I32AsmjsRemU, 0xea, i_ii, "i32.asmjs_rem_u")               \
  V(I32AsmjsLoadMem8S, 0xeb, i_i, "i32.asmjs_load8_s")         \
  V(I32AsmjsLoadMem8U, 0xec, i_i, "i32.asmjs_load8_u")         \
  V(I32AsmjsLoadMem16S, 0xed, i_i, "i32.asmjs_load16_s")       \
  V(I32AsmjsLoadMem16U, 0xee, i_i, "i32.asmjs_load16_u")       \
  V(I32AsmjsLoadMem, 0xef, i_i, "i32.asmjs_load32")            \
  V(F32AsmjsLoadMem, 0xf0, f_i, "f32.asmjs_load")              \
  V(F64AsmjsLoadMem, 0xf1, d_i, "f64.asmjs_load")              \
  V(I32AsmjsStoreMem8, 0xf2, i_ii, "i32.asmjs_store8")         \
  V(I32AsmjsStoreMem16, 0xf3, i_ii, "i32.asmjs_store16")       \
  V(I32AsmjsStoreMem, 0xf4, i_ii, "i32.asmjs_store")           \
  V(F32AsmjsStoreMem, 0xf5, f_if, "f32.asmjs_store")           \
  V(F64AsmjsStoreMem, 0xf6, d_id, "f64.asmjs_store")           \
  V(I32AsmjsSConvertF32, 0xf7, i_f, "i32.asmjs_convert_f32_s") \
  V(I32AsmjsUConvertF32, 0xf8, i_f, "i32.asmjs_convert_f32_u") \
  V(I32AsmjsSConvertF64, 0xf9, i_d, "i32.asmjs_convert_f64_s") \
  V(I32AsmjsUConvertF64, 0xfa, i_d, "i32.asmjs_convert_f64_u")

#define FOREACH_SIMD_MEM_OPCODE(V)                     \
  V(S128LoadMem, 0xfd00, s_i, "v128.load")             \
  V(S128Load8x8S, 0xfd01, s_i, "v128.load8x8_s")       \
  V(S128Load8x8U, 0xfd02, s_i, "v128.load8x8_u")       \
  V(S128Load16x4S, 0xfd03, s_i, "v128.load16x4_s")     \
  V(S128Load16x4U, 0xfd04, s_i, "v128.load16x4_u")     \
  V(S128Load32x2S, 0xfd05, s_i, "v128.load32x2_s")     \
  V(S128Load32x2U, 0xfd06, s_i, "v128.load32x2_u")     \
  V(S128Load8Splat, 0xfd07, s_i, "v128.load8_splat")   \
  V(S128Load16Splat, 0xfd08, s_i, "v128.load16_splat") \
  V(S128Load32Splat, 0xfd09, s_i, "v128.load32_splat") \
  V(S128Load64Splat, 0xfd0a, s_i, "v128.load64_splat") \
  V(S128StoreMem, 0xfd0b, v_is, "v128.store")          \
  V(S128Load32Zero, 0xfd5c, s_i, "v128.load32_zero")   \
  V(S128Load64Zero, 0xfd5d, s_i, "v128.load64_zero")

#define FOREACH_SIMD_MEM_1_OPERAND_OPCODE(V)            \
  V(S128Load8Lane, 0xfd54, s_is, "v128.load8_lane")     \
  V(S128Load16Lane, 0xfd55, s_is, "v128.load16_lane")   \
  V(S128Load32Lane, 0xfd56, s_is, "v128.load32_lane")   \
  V(S128Load64Lane, 0xfd57, s_is, "v128.load64_lane")   \
  V(S128Store8Lane, 0xfd58, v_is, "v128.store8_lane")   \
  V(S128Store16Lane, 0xfd59, v_is, "v128.store16_lane") \
  V(S128Store32Lane, 0xfd5a, v_is, "v128.store32_lane") \
  V(S128Store64Lane, 0xfd5b, v_is, "v128.store64_lane")

#define FOREACH_SIMD_CONST_OPCODE(V) V(S128Const, 0xfd0c, _, "v128.const")

#define FOREACH_SIMD_MASK_OPERAND_OPCODE(V) \
  V(I8x16Shuffle, 0xfd0d, s_ss, "i8x16.shuffle")

#define FOREACH_SIMD_MVP_0_OPERAND_OPCODE(V)                                 \
  V(I8x16Swizzle, 0xfd0e, s_ss, "i8x16.swizzle")                             \
  V(I8x16Splat, 0xfd0f, s_i, "i8x16.splat")                                  \
  V(I16x8Splat, 0xfd10, s_i, "i16x8.splat")                                  \
  V(I32x4Splat, 0xfd11, s_i, "i32x4.splat")                                  \
  V(I64x2Splat, 0xfd12, s_l, "i64x2.splat")                                  \
  V(F32x4Splat, 0xfd13, s_f, "f32x4.splat")                                  \
  V(F64x2Splat, 0xfd14, s_d, "f64x2.splat")                                  \
  V(I8x16Eq, 0xfd23, s_ss, "i8x16.eq")                                       \
  V(I8x16Ne, 0xfd24, s_ss, "i8x16.ne")                                       \
  V(I8x16LtS, 0xfd25, s_ss, "i8x16.lt_s")                                    \
  V(I8x16LtU, 0xfd26, s_ss, "i8x16.lt_u")                                    \
  V(I8x16GtS, 0xfd27, s_ss, "i8x16.gt_s")                                    \
  V(I8x16GtU, 0xfd28, s_ss, "i8x16.gt_u")                                    \
  V(I8x16LeS, 0xfd29, s_ss, "i8x16.le_s")                                    \
  V(I8x16LeU, 0xfd2a, s_ss, "i8x16.le_u")                                    \
  V(I8x16GeS, 0xfd2b, s_ss, "i8x16.ge_s")                                    \
  V(I8x16GeU, 0xfd2c, s_ss, "i8x16.ge_u")                                    \
  V(I16x8Eq, 0xfd2d, s_ss, "i16x8.eq")                                       \
  V(I16x8Ne, 0xfd2e, s_ss, "i16x8.ne")                                       \
  V(I16x8LtS, 0xfd2f, s_ss, "i16x8.lt_s")                                    \
  V(I16x8LtU, 0xfd30, s_ss, "i16x8.lt_u")                                    \
  V(I16x8GtS, 0xfd31, s_ss, "i16x8.gt_s")                                    \
  V(I16x8GtU, 0xfd32, s_ss, "i16x8.gt_u")                                    \
  V(I16x8LeS, 0xfd33, s_ss, "i16x8.le_s")                                    \
  V(I16x8LeU, 0xfd34, s_ss, "i16x8.le_u")                                    \
  V(I16x8GeS, 0xfd35, s_ss, "i16x8.ge_s")                                    \
  V(I16x8GeU, 0xfd36, s_ss, "i16x8.ge_u")                                    \
  V(I32x4Eq, 0xfd37, s_ss, "i32x4.eq")                                       \
  V(I32x4Ne, 0xfd38, s_ss, "i32x4.ne")                                       \
  V(I32x4LtS, 0xfd39, s_ss, "i32x4.lt_s")                                    \
  V(I32x4LtU, 0xfd3a, s_ss, "i32x4.lt_u")                                    \
  V(I32x4GtS, 0xfd3b, s_ss, "i32x4.gt_s")                                    \
  V(I32x4GtU, 0xfd3c, s_ss, "i32x4.gt_u")                                    \
  V(I32x4LeS, 0xfd3d, s_ss, "i32x4.le_s")                                    \
  V(I32x4LeU, 0xfd3e, s_ss, "i32x4.le_u")                                    \
  V(I32x4GeS, 0xfd3f, s_ss, "i32x4.ge_s")                                    \
  V(I32x4GeU, 0xfd40, s_ss, "i32x4.ge_u")                                    \
  V(F32x4Eq, 0xfd41, s_ss, "f32x4.eq")                                       \
  V(F32x4Ne, 0xfd42, s_ss, "f32x4.ne")                                       \
  V(F32x4Lt, 0xfd43, s_ss, "f32x4.lt")                                       \
  V(F32x4Gt, 0xfd44, s_ss, "f32x4.gt")                                       \
  V(F32x4Le, 0xfd45, s_ss, "f32x4.le")                                       \
  V(F32x4Ge, 0xfd46, s_ss, "f32x4.ge")                                       \
  V(F64x2Eq, 0xfd47, s_ss, "f64x2.eq")                                       \
  V(F64x2Ne, 0xfd48, s_ss, "f64x2.ne")                                       \
  V(F64x2Lt, 0xfd49, s_ss, "f64x2.lt")                                       \
  V(F64x2Gt, 0xfd4a, s_ss, "f64x2.gt")                                       \
  V(F64x2Le, 0xfd4b, s_ss, "f64x2.le")                                       \
  V(F64x2Ge, 0xfd4c, s_ss, "f64x2.ge")                                       \
  V(S128Not, 0xfd4d, s_s, "v128.not")                                        \
  V(S128And, 0xfd4e, s_ss, "v128.and")                                       \
  V(S128AndNot, 0xfd4f, s_ss, "v128.andnot")                                 \
  V(S128Or, 0xfd50, s_ss, "v128.or")                                         \
  V(S128Xor, 0xfd51, s_ss, "v128.xor")                                       \
  V(S128Select, 0xfd52, s_sss, "v128.bitselect")                             \
  V(V128AnyTrue, 0xfd53, i_s, "v128.any_true")                               \
  V(F32x4DemoteF64x2Zero, 0xfd5e, s_s, "f32x4.demote_f64x2_zero")            \
  V(F64x2PromoteLowF32x4, 0xfd5f, s_s, "f64x2.promote_low_f32x4")            \
  V(I8x16Abs, 0xfd60, s_s, "i8x16.abs")                                      \
  V(I8x16Neg, 0xfd61, s_s, "i8x16.neg")                                      \
  V(I8x16Popcnt, 0xfd62, s_s, "i8x16.popcnt")                                \
  V(I8x16AllTrue, 0xfd63, i_s, "i8x16.all_true")                             \
  V(I8x16BitMask, 0xfd64, i_s, "i8x16.bitmask")                              \
  V(I8x16SConvertI16x8, 0xfd65, s_ss, "i8x16.narrow_i16x8_s")                \
  V(I8x16UConvertI16x8, 0xfd66, s_ss, "i8x16.narrow_i16x8_u")                \
  V(F32x4Ceil, 0xfd67, s_s, "f32x4.ceil")                                    \
  V(F32x4Floor, 0xfd68, s_s, "f32x4.floor")                                  \
  V(F32x4Trunc, 0xfd69, s_s, "f32x4.trunc")                                  \
  V(F32x4NearestInt, 0xfd6a, s_s, "f32x4.nearest")                           \
  V(I8x16Shl, 0xfd6b, s_si, "i8x16.shl")                                     \
  V(I8x16ShrS, 0xfd6c, s_si, "i8x16.shr_s")                                  \
  V(I8x16ShrU, 0xfd6d, s_si, "i8x16.shr_u")                                  \
  V(I8x16Add, 0xfd6e, s_ss, "i8x16.add")                                     \
  V(I8x16AddSatS, 0xfd6f, s_ss, "i8x16.add_sat_s")                           \
  V(I8x16AddSatU, 0xfd70, s_ss, "i8x16.add_sat_u")                           \
  V(I8x16Sub, 0xfd71, s_ss, "i8x16.sub")                                     \
  V(I8x16SubSatS, 0xfd72, s_ss, "i8x16.sub_sat_s")                           \
  V(I8x16SubSatU, 0xfd73, s_ss, "i8x16.sub_sat_u")                           \
  V(F64x2Ceil, 0xfd74, s_s, "f64x2.ceil")                                    \
  V(F64x2Floor, 0xfd75, s_s, "f64x2.floor")                                  \
  V(I8x16MinS, 0xfd76, s_ss, "i8x16.min_s")                                  \
  V(I8x16MinU, 0xfd77, s_ss, "i8x16.min_u")                                  \
  V(I8x16MaxS, 0xfd78, s_ss, "i8x16.max_s")                                  \
  V(I8x16MaxU, 0xfd79, s_ss, "i8x16.max_u")                                  \
  V(F64x2Trunc, 0xfd7a, s_s, "f64x2.trunc")                                  \
  V(I8x16RoundingAverageU, 0xfd7b, s_ss, "i8x16.avgr_u")                     \
  V(I16x8ExtAddPairwiseI8x16S, 0xfd7c, s_s, "i16x8.extadd_pairwise_i8x16_s") \
  V(I16x8ExtAddPairwiseI8x16U, 0xfd7d, s_s, "i16x8.extadd_pairwise_i8x16_u") \
  V(I32x4ExtAddPairwiseI16x8S, 0xfd7e, s_s, "i32x4.extadd_pairwise_i16x8_s") \
  V(I32x4ExtAddPairwiseI16x8U, 0xfd7f, s_s, "i32x4.extadd_pairwise_i16x8_u") \
  V(I16x8Abs, 0xfd80, s_s, "i16x8.abs")                                      \
  V(I16x8Neg, 0xfd81, s_s, "i16x8.neg")                                      \
  V(I16x8Q15MulRSatS, 0xfd82, s_ss, "i16x8.q15mulr_sat_s")                   \
  V(I16x8AllTrue, 0xfd83, i_s, "i16x8.all_true")                             \
  V(I16x8BitMask, 0xfd84, i_s, "i16x8.bitmask")                              \
  V(I16x8SConvertI32x4, 0xfd85, s_ss, "i16x8.narrow_i32x4_s")                \
  V(I16x8UConvertI32x4, 0xfd86, s_ss, "i16x8.narrow_i32x4_u")                \
  V(I16x8SConvertI8x16Low, 0xfd87, s_s, "i16x8.extend_low_i8x16_s")          \
  V(I16x8SConvertI8x16High, 0xfd88, s_s, "i16x8.extend_high_i8x16_s")        \
  V(I16x8UConvertI8x16Low, 0xfd89, s_s, "i16x8.extend_low_i8x16_u")          \
  V(I16x8UConvertI8x16High, 0xfd8a, s_s, "i16x8.extend_high_i8x16_u")        \
  V(I16x8Shl, 0xfd8b, s_si, "i16x8.shl")                                     \
  V(I16x8ShrS, 0xfd8c, s_si, "i16x8.shr_s")                                  \
  V(I16x8ShrU, 0xfd8d, s_si, "i16x8.shr_u")                                  \
  V(I16x8Add, 0xfd8e, s_ss, "i16x8.add")                                     \
  V(I16x8AddSatS, 0xfd8f, s_ss, "i16x8.add_sat_s")                           \
  V(I16x8AddSatU, 0xfd90, s_ss, "i16x8.add_sat_u")                           \
  V(I16x8Sub, 0xfd91, s_ss, "i16x8.sub")                                     \
  V(I16x8SubSatS, 0xfd92, s_ss, "i16x8.sub_sat_s")                           \
  V(I16x8SubSatU, 0xfd93, s_ss, "i16x8.sub_sat_u")                           \
  V(F64x2NearestInt, 0xfd94, s_s, "f64x2.nearest")                           \
  V(I16x8Mul, 0xfd95, s_ss, "i16x8.mul")                                     \
  V(I16x8MinS, 0xfd96, s_ss, "i16x8.min_s")                                  \
  V(I16x8MinU, 0xfd97, s_ss, "i16x8.min_u")                                  \
  V(I16x8MaxS, 0xfd98, s_ss, "i16x8.max_s")                                  \
  V(I16x8MaxU, 0xfd99, s_ss, "i16x8.max_u")                                  \
  V(I16x8RoundingAverageU, 0xfd9b, s_ss, "i16x8.avgr_u")                     \
  V(I16x8ExtMulLowI8x16S, 0xfd9c, s_ss, "i16x8.extmul_low_i8x16_s")          \
  V(I16x8ExtMulHighI8x16S, 0xfd9d, s_ss, "i16x8.extmul_high_i8x16_s")        \
  V(I16x8ExtMulLowI8x16U, 0xfd9e, s_ss, "i16x8.extmul_low_i8x16_u")          \
  V(I16x8ExtMulHighI8x16U, 0xfd9f, s_ss, "i16x8.extmul_high_i8x16_u")        \
  V(I32x4Abs, 0xfda0, s_s, "i32x4.abs")                                      \
  V(I32x4Neg, 0xfda1, s_s, "i32x4.neg")                                      \
  V(I32x4AllTrue, 0xfda3, i_s, "i32x4.all_true")                             \
  V(I32x4BitMask, 0xfda4, i_s, "i32x4.bitmask")                              \
  V(I32x4SConvertI16x8Low, 0xfda7, s_s, "i32x4.extend_low_i16x8_s")          \
  V(I32x4SConvertI16x8High, 0xfda8, s_s, "i32x4.extend_high_i16x8_s")        \
  V(I32x4UConvertI16x8Low, 0xfda9, s_s, "i32x4.extend_low_i16x8_u")          \
  V(I32x4UConvertI16x8High, 0xfdaa, s_s, "i32x4.extend_high_i16x8_u")        \
  V(I32x4Shl, 0xfdab, s_si, "i32x4.shl")                                     \
  V(I32x4ShrS, 0xfdac, s_si, "i32x4.shr_s")                                  \
  V(I32x4ShrU, 0xfdad, s_si, "i32x4.shr_u")                                  \
  V(I32x4Add, 0xfdae, s_ss, "i32x4.add")                                     \
  V(I32x4Sub, 0xfdb1, s_ss, "i32x4.sub")                                     \
  V(I32x4Mul, 0xfdb5, s_ss, "i32x4.mul")                                     \
  V(I32x4MinS, 0xfdb6, s_ss, "i32x4.min_s")                                  \
  V(I32x4MinU, 0xfdb7, s_ss, "i32x4.min_u")                                  \
  V(I32x4MaxS, 0xfdb8, s_ss, "i32x4.max_s")                                  \
  V(I32x4MaxU, 0xfdb9, s_ss, "i32x4.max_u")                                  \
  V(I32x4DotI16x8S, 0xfdba, s_ss, "i32x4.dot_i16x8_s")                       \
  V(I32x4ExtMulLowI16x8S, 0xfdbc, s_ss, "i32x4.extmul_low_i16x8_s")          \
  V(I32x4ExtMulHighI16x8S, 0xfdbd, s_ss, "i32x4.extmul_high_i16x8_s")        \
  V(I32x4ExtMulLowI16x8U, 0xfdbe, s_ss, "i32x4.extmul_low_i16x8_u")          \
  V(I32x4ExtMulHighI16x8U, 0xfdbf, s_ss, "i32x4.extmul_high_i16x8_u")        \
  V(I64x2Abs, 0xfdc0, s_s, "i64x2.abs")                                      \
  V(I64x2Neg, 0xfdc1, s_s, "i64x2.neg")                                      \
  V(I64x2AllTrue, 0xfdc3, i_s, "i64x2.all_true")                             \
  V(I64x2BitMask, 0xfdc4, i_s, "i64x2.bitmask")                              \
  V(I64x2SConvertI32x4Low, 0xfdc7, s_s, "i64x2.extend_low_i32x4_s")          \
  V(I64x2SConvertI32x4High, 0xfdc8, s_s, "i64x2.extend_high_i32x4_s")        \
  V(I64x2UConvertI32x4Low, 0xfdc9, s_s, "i64x2.extend_low_i32x4_u")          \
  V(I64x2UConvertI32x4High, 0xfdca, s_s, "i64x2.extend_high_i32x4_u")        \
  V(I64x2Shl, 0xfdcb, s_si, "i64x2.shl")                                     \
  V(I64x2ShrS, 0xfdcc, s_si, "i64x2.shr_s")                                  \
  V(I64x2ShrU, 0xfdcd, s_si, "i64x2.shr_u")                                  \
  V(I64x2Add, 0xfdce, s_ss, "i64x2.add")                                     \
  V(I64x2Sub, 0xfdd1, s_ss, "i64x2.sub")                                     \
  V(I64x2Mul, 0xfdd5, s_ss, "i64x2.mul")                                     \
  V(I64x2Eq, 0xfdd6, s_ss, "i64x2.eq")                                       \
  V(I64x2Ne, 0xfdd7, s_ss, "i64x2.ne")                                       \
  V(I64x2LtS, 0xfdd8, s_ss, "i64x2.lt_s")                                    \
  V(I64x2GtS, 0xfdd9, s_ss, "i64x2.gt_s")                                    \
  V(I64x2LeS, 0xfdda, s_ss, "i64x2.le_s")                                    \
  V(I64x2GeS, 0xfddb, s_ss, "i64x2.ge_s")                                    \
  V(I64x2ExtMulLowI32x4S, 0xfddc, s_ss, "i64x2.extmul_low_i32x4_s")          \
  V(I64x2ExtMulHighI32x4S, 0xfddd, s_ss, "i64x2.extmul_high_i32x4_s")        \
  V(I64x2ExtMulLowI32x4U, 0xfdde, s_ss, "i64x2.extmul_low_i32x4_u")          \
  V(I64x2ExtMulHighI32x4U, 0xfddf, s_ss, "i64x2.extmul_high_i32x4_u")        \
  V(F32x4Abs, 0xfde0, s_s, "f32x4.abs")                                      \
  V(F32x4Neg, 0xfde1, s_s, "f32x4.neg")                                      \
  V(F32x4Sqrt, 0xfde3, s_s, "f32x4.sqrt")                                    \
  V(F32x4Add, 0xfde4, s_ss, "f32x4.add")                                     \
  V(F32x4Sub, 0xfde5, s_ss, "f32x4.sub")                                     \
  V(F32x4Mul, 0xfde6, s_ss, "f32x4.mul")                                     \
  V(F32x4Div, 0xfde7, s_ss, "f32x4.div")                                     \
  V(F32x4Min, 0xfde8, s_ss, "f32x4.min")                                     \
  V(F32x4Max, 0xfde9, s_ss, "f32x4.max")                                     \
  V(F32x4Pmin, 0xfdea, s_ss, "f32x4.pmin")                                   \
  V(F32x4Pmax, 0xfdeb, s_ss, "f32x4.pmax")                                   \
  V(F64x2Abs, 0xfdec, s_s, "f64x2.abs")                                      \
  V(F64x2Neg, 0xfded, s_s, "f64x2.neg")                                      \
  V(F64x2Sqrt, 0xfdef, s_s, "f64x2.sqrt")                                    \
  V(F64x2Add, 0xfdf0, s_ss, "f64x2.add")                                     \
  V(F64x2Sub, 0xfdf1, s_ss, "f64x2.sub")                                     \
  V(F64x2Mul, 0xfdf2, s_ss, "f64x2.mul")                                     \
  V(F64x2Div, 0xfdf3, s_ss, "f64x2.div")                                     \
  V(F64x2Min, 0xfdf4, s_ss, "f64x2.min")                                     \
  V(F64x2Max, 0xfdf5, s_ss, "f64x2.max")                                     \
  V(F64x2Pmin, 0xfdf6, s_ss, "f64x2.pmin")                                   \
  V(F64x2Pmax, 0xfdf7, s_ss, "f64x2.pmax")                                   \
  V(I32x4SConvertF32x4, 0xfdf8, s_s, "i32x4.trunc_sat_f32x4_s")              \
  V(I32x4UConvertF32x4, 0xfdf9, s_s, "i32x4.trunc_sat_f32x4_u")              \
  V(F32x4SConvertI32x4, 0xfdfa, s_s, "f32x4.convert_i32x4_s")                \
  V(F32x4UConvertI32x4, 0xfdfb, s_s, "f32x4.convert_i32x4_u")                \
  V(I32x4TruncSatF64x2SZero, 0xfdfc, s_s, "i32x4.trunc_sat_f64x2_s_zero")    \
  V(I32x4TruncSatF64x2UZero, 0xfdfd, s_s, "i32x4.trunc_sat_f64x2_u_zero")    \
  V(F64x2ConvertLowI32x4S, 0xfdfe, s_s, "f64x2.convert_low_i32x4_s")         \
  V(F64x2ConvertLowI32x4U, 0xfdff, s_s, "f64x2.convert_low_i32x4_u")

#define FOREACH_RELAXED_SIMD_OPCODE(V)                                     \
  V(I8x16RelaxedSwizzle, 0xfd100, s_ss, "i8x16.relaxed_swizzle")           \
  V(I32x4RelaxedTruncF32x4S, 0xfd101, s_s, "i32x4.relaxed_trunc_f32x4_s")  \
  V(I32x4RelaxedTruncF32x4U, 0xfd102, s_s, "i32x4.relaxed_trunc_f32x4_u")  \
  V(I32x4RelaxedTruncF64x2SZero, 0xfd103, s_s,                             \
    "i32x4.relaxed_trunc_f64x2_s_zero")                                    \
  V(I32x4RelaxedTruncF64x2UZero, 0xfd104, s_s,                             \
    "i32x4.relaxed_trunc_f64x2_u_zero")                                    \
  V(F32x4Qfma, 0xfd105, s_sss, "f32x4.qfma")                               \
  V(F32x4Qfms, 0xfd106, s_sss, "f32x4.qfms")                               \
  V(F64x2Qfma, 0xfd107, s_sss, "f64x2.qfma")                               \
  V(F64x2Qfms, 0xfd108, s_sss, "f64x2.qfms")                               \
  V(I8x16RelaxedLaneSelect, 0xfd109, s_sss, "i8x16.relaxed_laneselect")    \
  V(I16x8RelaxedLaneSelect, 0xfd10a, s_sss, "i16x8.relaxed_laneselect")    \
  V(I32x4RelaxedLaneSelect, 0xfd10b, s_sss, "i32x4.relaxed_laneselect")    \
  V(I64x2RelaxedLaneSelect, 0xfd10c, s_sss, "i64x2.relaxed_laneselect")    \
  V(F32x4RelaxedMin, 0xfd10d, s_ss, "f32x4.relaxed_min")                   \
  V(F32x4RelaxedMax, 0xfd10e, s_ss, "f32x4.relaxed_max")                   \
  V(F64x2RelaxedMin, 0xfd10f, s_ss, "f64x2.relaxed_min")                   \
  V(F64x2RelaxedMax, 0xfd110, s_ss, "f64x2.relaxed_max")                   \
  V(I16x8RelaxedQ15MulRS, 0xfd111, s_ss, "i16x8.relaxed_q15mulr_s")        \
  V(I16x8DotI8x16I7x16S, 0xfd112, s_ss, "i16x8.dot_i8x16_i7x16_s")         \
  V(I32x4DotI8x16I7x16AddS, 0xfd113, s_sss, "i32x4.dot_i8x16_i7x16_add_s") \
  V(F16x8Splat, 0xfd120, s_f, "f16x8.splat")                               \
  V(F16x8Abs, 0xfd130, s_s, "f16x8.abs")                                   \
  V(F16x8Neg, 0xfd131, s_s, "f16x8.neg")                                   \
  V(F16x8Sqrt, 0xfd132, s_s, "f16x8.sqrt")                                 \
  V(F16x8Ceil, 0xfd133, s_s, "f16x8.ceil")                                 \
  V(F16x8Floor, 0xfd134, s_s, "f16x8.floor")                               \
  V(F16x8Trunc, 0xfd135, s_s, "f16x8.trunc")                               \
  V(F16x8NearestInt, 0xfd136, s_s, "f16x8.nearest")                        \
  V(F16x8Eq, 0xfd137, s_ss, "f16x8.eq")                                    \
  V(F16x8Ne, 0xfd138, s_ss, "f16x8.ne")                                    \
  V(F16x8Lt, 0xfd139, s_ss, "f16x8.lt")                                    \
  V(F16x8Gt, 0xfd13a, s_ss, "f16x8.gt")                                    \
  V(F16x8Le, 0xfd13b, s_ss, "f16x8.le")                                    \
  V(F16x8Ge, 0xfd13c, s_ss, "f16x8.ge")                                    \
  V(F16x8Add, 0xfd13d, s_ss, "f16x8.add")                                  \
  V(F16x8Sub, 0xfd13e, s_ss, "f16x8.sub")                                  \
  V(F16x8Mul, 0xfd13f, s_ss, "f16x8.mul")                                  \
  V(F16x8Div, 0xfd140, s_ss, "f16x8.div")                                  \
  V(F16x8Min, 0xfd141, s_ss, "f16x8.min")                                  \
  V(F16x8Max, 0xfd142, s_ss, "f16x8.max")                                  \
  V(F16x8Pmin, 0xfd143, s_ss, "f16x8.pmin")                                \
  V(F16x8Pmax, 0xfd144, s_ss, "f16x8.pmax")                                \
  V(I16x8SConvertF16x8, 0xfd145, s_s, "i16x8.trunc_sat_f16x8_s")           \
  V(I16x8UConvertF16x8, 0xfd146, s_s, "i16x8.trunc_sat_f16x8_u")           \
  V(F16x8SConvertI16x8, 0xfd147, s_s, "f16x8.convert_i16x8_s")             \
  V(F16x8UConvertI16x8, 0xfd148, s_s, "f16x8.convert_i16x8_u")             \
  V(F16x8DemoteF32x4Zero, 0xfd149, s_s, "f16x8.demote_f32x4_zero")         \
  V(F16x8DemoteF64x2Zero, 0xfd14a, s_s, "f16x8.demote_f64x2_zero")         \
  V(F32x4PromoteLowF16x8, 0xfd14b, s_s, "f32x4.promote_low_f16x8")         \
  V(F16x8Qfma, 0xfd14e, s_sss, "f16x8.madd")                               \
  V(F16x8Qfms, 0xfd14f, s_sss, "f16x8.nmadd")

#define FOREACH_SIMD_1_OPERAND_1_PARAM_OPCODE(V)          \
  V(I8x16ExtractLaneS, 0xfd15, _, "i8x16.extract_lane_s") \
  V(I8x16ExtractLaneU, 0xfd16, _, "i8x16.extract_lane_u") \
  V(I16x8ExtractLaneS, 0xfd18, _, "i16x8.extract_lane_s") \
  V(I16x8ExtractLaneU, 0xfd19, _, "i16x8.extract_lane_u") \
  V(I32x4ExtractLane, 0xfd1b, _, "i32x4.extract_lane")    \
  V(I64x2ExtractLane, 0xfd1d, _, "i64x2.extract_lane")    \
  V(F32x4ExtractLane, 0xfd1f, _, "f32x4.extract_lane")    \
  V(F64x2ExtractLane, 0xfd21, _, "f64x2.extract_lane")    \
  V(F16x8ExtractLane, 0xfd121, _, "f16x8.extract_lane")

#define FOREACH_SIMD_1_OPERAND_2_PARAM_OPCODE(V)       \
  V(I8x16ReplaceLane, 0xfd17, _, "i8x16.replace_lane") \
  V(I16x8ReplaceLane, 0xfd1a, _, "i16x8.replace_lane") \
  V(I32x4ReplaceLane, 0xfd1c, _, "i32x4.replace_lane") \
  V(I64x2ReplaceLane, 0xfd1e, _, "i64x2.replace_lane") \
  V(F32x4ReplaceLane, 0xfd20, _, "f32x4.replace_lane") \
  V(F64x2ReplaceLane, 0xfd22, _, "f64x2.replace_lane") \
  V(F16x8ReplaceLane, 0xfd122, _, "f16x8.replace_lane")

#define FOREACH_SIMD_0_OPERAND_OPCODE(V) \
  FOREACH_SIMD_MVP_0_OPERAND_OPCODE(V)   \
  FOREACH_RELAXED_SIMD_OPCODE(V)

#define FOREACH_SIMD_1_OPERAND_OPCODE(V)   \
  FOREACH_SIMD_1_OPERAND_1_PARAM_OPCODE(V) \
  FOREACH_SIMD_1_OPERAND_2_PARAM_OPCODE(V)

#define FOREACH_SIMD_OPCODE(V)         \
  FOREACH_SIMD_0_OPERAND_OPCODE(V)     \
  FOREACH_SIMD_1_OPERAND_OPCODE(V)     \
  FOREACH_SIMD_MASK_OPERAND_OPCODE(V)  \
  FOREACH_SIMD_MEM_OPCODE(V)           \
  FOREACH_SIMD_MEM_1_OPERAND_OPCODE(V) \
  FOREACH_SIMD_CONST_OPCODE(V)

#define FOREACH_NUMERIC_OPCODE_WITH_SIG(V)                 \
  V(I32SConvertSatF32, 0xfc00, i_f, "i32.trunc_sat_f32_s") \
  V(I32UConvertSatF32, 0xfc01, i_f, "i32.trunc_sat_f32_u") \
  V(I32SConvertSatF64, 0xfc02, i_d, "i32.trunc_sat_f64_s") \
  V(I32UConvertSatF64, 0xfc03, i_d, "i32.trunc_sat_f64_u") \
  V(I64SConvertSatF32, 0xfc04, l_f, "i64.trunc_sat_f32_s") \
  V(I64UConvertSatF32, 0xfc05, l_f, "i64.trunc_sat_f32_u") \
  V(I64SConvertSatF64, 0xfc06, l_d, "i64.trunc_sat_f64_s") \
  V(I64UConvertSatF64, 0xfc07, l_d, "i64.trunc_sat_f64_u") \
  V(DataDrop, 0xfc09, v_v, "data.drop")                    \
  V(TableInit, 0xfc0c, v_iii, "table.init")                \
  V(ElemDrop, 0xfc0d, v_v, "elem.drop")                    \
  V(TableCopy, 0xfc0e, v_iii, "table.copy")                \
  V(TableSize, 0xfc10, i_v, "table.size")

#define FOREACH_NUMERIC_OPCODE_VARIADIC(V)                \
  V(MemoryInit, 0xfc08, _, "memory.init")                 \
  V(MemoryCopy, 0xfc0a, _, "memory.copy")                 \
  V(MemoryFill, 0xfc0b, _, "memory.fill")                 \
  /* TableGrow is polymorphic in the first parameter. */  \
  /* It's whatever the table type is. */                  \
  V(TableGrow, 0xfc0f, _, "table.grow")                   \
  /* TableFill is polymorphic in the second parameter. */ \
  /* It's whatever the table type is. */                  \
  V(TableFill, 0xfc11, _, "table.fill")

#define FOREACH_NUMERIC_OPCODE(V) \
  FOREACH_NUMERIC_OPCODE_WITH_SIG(V) FOREACH_NUMERIC_OPCODE_VARIADIC(V)

// kExprName, binary, signature for memory32, wat name, signature for memory64.
#define FOREACH_ATOMIC_OPCODE(V)                                              \
  V(AtomicNotify, 0xfe00, i_ii, "memory.atomic.notify", i_li)                 \
  V(I32AtomicWait, 0xfe01, i_iil, "memory.atomic.wait32", i_lil)              \
  V(I64AtomicWait, 0xfe02, i_ill, "memory.atomic.wait64", i_lll)              \
  V(I32AtomicLoad, 0xfe10, i_i, "i32.atomic.load", i_l)                       \
  V(I64AtomicLoad, 0xfe11, l_i, "i64.atomic.load", l_l)                       \
  V(I32AtomicLoad8U, 0xfe12, i_i, "i32.atomic.load8_u", i_l)                  \
  V(I32AtomicLoad16U, 0xfe13, i_i, "i32.atomic.load16_u", i_l)                \
  V(I64AtomicLoad8U, 0xfe14, l_i, "i64.atomic.load8_u", l_l)                  \
  V(I64AtomicLoad16U, 0xfe15, l_i, "i64.atomic.load16_u", l_l)                \
  V(I64AtomicLoad32U, 0xfe16, l_i, "i64.atomic.load32_u", l_l)                \
  V(I32AtomicStore, 0xfe17, v_ii, "i32.atomic.store", v_li)                   \
  V(I64AtomicStore, 0xfe18, v_il, "i64.atomic.store", v_ll)                   \
  V(I32AtomicStore8U, 0xfe19, v_ii, "i32.atomic.store8", v_li)                \
  V(I32AtomicStore16U, 0xfe1a, v_ii, "i32.atomic.store16", v_li)              \
  V(I64AtomicStore8U, 0xfe1b, v_il, "i64.atomic.store8", v_ll)                \
  V(I64AtomicStore16U, 0xfe1c, v_il, "i64.atomic.store16", v_ll)              \
  V(I64AtomicStore32U, 0xfe1d, v_il, "i64.atomic.store32", v_ll)              \
  V(I32AtomicAdd, 0xfe1e, i_ii, "i32.atomic.rmw.add", i_li)                   \
  V(I64AtomicAdd, 0xfe1f, l_il, "i64.atomic.rmw.add", l_ll)                   \
  V(I32AtomicAdd8U, 0xfe20, i_ii, "i32.atomic.rmw8.add_u", i_li)              \
  V(I32AtomicAdd16U, 0xfe21, i_ii, "i32.atomic.rmw16.add_u", i_li)            \
  V(I64AtomicAdd8U, 0xfe22, l_il, "i64.atomic.rmw8.add_u", l_ll)              \
  V(I64AtomicAdd16U, 0xfe23, l_il, "i64.atomic.rmw16.add_u", l_ll)            \
  V(I64AtomicAdd32U, 0xfe24, l_il, "i64.atomic.rmw32.add_u", l_ll)            \
  V(I32AtomicSub, 0xfe25, i_ii, "i32.atomic.rmw.sub", i_li)                   \
  V(I64AtomicSub, 0xfe26, l_il, "i64.atomic.rmw.sub", l_ll)                   \
  V(I32AtomicSub8U, 0xfe27, i_ii, "i32.atomic.rmw8.sub_u", i_li)              \
  V(I32AtomicSub16U, 0xfe28, i_ii, "i32.atomic.rmw16.sub_u", i_li)            \
  V(I64AtomicSub8U, 0xfe29, l_il, "i64.atomic.rmw8.sub_u", l_ll)              \
  V(I64AtomicSub16U, 0xfe2a, l_il, "i64.atomic.rmw16.sub_u", l_ll)            \
  V(I64AtomicSub32U, 0xfe2b, l_il, "i64.atomic.rmw32.sub_u", l_ll)            \
  V(I32AtomicAnd, 0xfe2c, i_ii, "i32.atomic.rmw.and", i_li)                   \
  V(I64AtomicAnd, 0xfe2d, l_il, "i64.atomic.rmw.and", l_ll)                   \
  V(I32AtomicAnd8U, 0xfe2e, i_ii, "i32.atomic.rmw8.and_u", i_li)              \
  V(I32AtomicAnd16U, 0xfe2f, i_ii, "i32.atomic.rmw16.and_u", i_li)            \
  V(I64AtomicAnd8U, 0xfe30, l_il, "i64.atomic.rmw8.and_u", l_ll)              \
  V(I64AtomicAnd16U, 0xfe31, l_il, "i64.atomic.rmw16.and_u", l_ll)            \
  V(I64AtomicAnd32U, 0xfe32, l_il, "i64.atomic.rmw32.and_u", l_ll)            \
  V(I32AtomicOr, 0xfe33, i_ii, "i32.atomic.rmw.or", i_li)                     \
  V(I64AtomicOr, 0xfe34, l_il, "i64.atomic.rmw.or", l_ll)                     \
  V(I32AtomicOr8U, 0xfe35, i_ii, "i32.atomic.rmw8.or_u", i_li)                \
  V(I32AtomicOr16U, 0xfe36, i_ii, "i32.atomic.rmw16.or_u", i_li)              \
  V(I64AtomicOr8U, 0xfe37, l_il, "i64.atomic.rmw8.or_u", l_ll)                \
  V(I64AtomicOr16U, 0xfe38, l_il, "i64.atomic.rmw16.or_u", l_ll)              \
  V(I64AtomicOr32U, 0xfe39, l_il, "i64.atomic.rmw32.or_u", l_ll)              \
  V(I32AtomicXor, 0xfe3a, i_ii, "i32.atomic.rmw.xor", i_li)                   \
  V(I64AtomicXor, 0xfe3b, l_il, "i64.atomic.rmw.xor", l_ll)                   \
  V(I32AtomicXor8U, 0xfe3c, i_ii, "i32.atomic.rmw8.xor_u", i_li)              \
  V(I32AtomicXor16U, 0xfe3d, i_ii, "i32.atomic.rmw16.xor_u", i_li)            \
  V(I64AtomicXor8U, 0xfe3e, l_il, "i64.atomic.rmw8.xor_u", l_ll)              \
  V(I64AtomicXor16U, 0xfe3f, l_il, "i64.atomic.rmw16.xor_u", l_ll)            \
  V(I64AtomicXor32U, 0xfe40, l_il, "i64.atomic.rmw32.xor_u", l_ll)            \
  V(I32AtomicExchange, 0xfe41, i_ii, "i32.atomic.rmw.xchg", i_li)             \
  V(I64AtomicExchange, 0xfe42, l_il, "i64.atomic.rmw.xchg", l_ll)             \
  V(I32AtomicExchange8U, 0xfe43, i_ii, "i32.atomic.rmw8.xchg_u", i_li)        \
  V(I32AtomicExchange16U, 0xfe44, i_ii, "i32.atomic.rmw16.xchg_u", i_li)      \
  V(I64AtomicExchange8U, 0xfe45, l_il, "i64.atomic.rmw8.xchg_u", l_ll)        \
  V(I64AtomicExchange16U, 0xfe46, l_il, "i64.atomic.rmw16.xchg_u", l_ll)      \
  V(I64AtomicExchange32U, 0xfe47, l_il, "i64.atomic.rmw32.xchg_u", l_ll)      \
  V(I32AtomicCompareExchange, 0xfe48, i_iii, "i32.atomic.rmw.cmpxchg", i_lii) \
  V(I64AtomicCompareExchange, 0xfe49, l_ill, "i64.atomic.rmw.cmpxchg", l_lll) \
  V(I32AtomicCompareExchange8U, 0xfe4a, i_iii, "i32.atomic.rmw8.cmpxchg_u",   \
    i_lii)                                                                    \
  V(I32AtomicCompareExchange16U, 0xfe4b, i_iii, "i32.atomic.rmw16.cmpxchg_u", \
    i_lii)                                                                    \
  V(I64AtomicCompareExchange8U, 0xfe4c, l_ill, "i64.atomic.rmw8.cmpxchg_u",   \
    l_lll)                                                                    \
  V(I64AtomicCompareExchange16U, 0xfe4d, l_ill, "i64.atomic.rmw16.cmpxchg_u", \
    l_lll)                                                                    \
  V(I64AtomicCompareExchange32U, 0xfe4e, l_ill, "i64.atomic.rmw32.cmpxchg_u", \
    l_lll)

#define FOREACH_ATOMIC_0_OPERAND_OPCODE(V)                      \
  /* AtomicFence does not target a particular linear memory. */ \
  V(AtomicFence, 0xfe03, v_v, "atomic.fence", v_v)

#define FOREACH_GC_OPCODE(V) /*              Force 80 columns               */ \
  V(StructNew, 0xfb00, _, "struct.new")                                        \
  V(StructNewDefault, 0xfb01, _, "struct.new_default")                         \
  V(StructGet, 0xfb02, _, "struct.get")                                        \
  V(StructGetS, 0xfb03, _, "struct.get_s")                                     \
  V(StructGetU, 0xfb04, _, "struct.get_u")                                     \
  V(StructSet, 0xfb05, _, "struct.set")                                        \
  V(ArrayNew, 0xfb06, _, "array.new")                                          \
  V(ArrayNewDefault, 0xfb07, _, "array.new_default")                           \
  V(ArrayNewFixed, 0xfb08, _, "array.new_fixed")                               \
  V(ArrayNewData, 0xfb09, _, "array.new_data")                                 \
  V(ArrayNewElem, 0xfb0a, _, "array.new_elem")                                 \
  V(ArrayGet, 0xfb0b, _, "array.get")                                          \
  V(ArrayGetS, 0xfb0c, _, "array.get_s")                                       \
  V(ArrayGetU, 0xfb0d, _, "array.get_u")                                       \
  V(ArraySet, 0xfb0e, _, "array.set")                                          \
  V(ArrayLen, 0xfb0f, _, "array.len")                                          \
  V(ArrayFill, 0xfb10, _, "array.fill")                                        \
  V(ArrayCopy, 0xfb11, _, "array.copy")                                        \
  V(ArrayInitData, 0xfb12, _, "array.init_data")                               \
  V(ArrayInitElem, 0xfb13, _, "array.init_elem")                               \
  V(RefTest, 0xfb14, _, "ref.test")                                            \
  V(RefTestNull, 0xfb15, _, "ref.test null")                                   \
  V(RefCast, 0xfb16, _, "ref.cast")                                            \
  V(RefCastNull, 0xfb17, _, "ref.cast null")                                   \
  V(BrOnCast, 0xfb18, _, "br_on_cast")                                         \
  V(BrOnCastFail, 0xfb19, _, "br_on_cast_fail")                                \
  V(AnyConvertExtern, 0xfb1a, _, "any.convert_extern")                         \
  V(ExternConvertAny, 0xfb1b, _, "extern.convert_any")                         \
  V(RefI31, 0xfb1c, _, "ref.i31")                                              \
  V(I31GetS, 0xfb1d, _, "i31.get_s")                                           \
  V(I31GetU, 0xfb1e, _, "i31.get_u")                                           \
  V(RefCastNop, 0xfb4c, _, "ref.cast_nop")                                     \
  /* Stringref proposal. */                                                    \
  V(StringNewUtf8, 0xfb80, _, "string.new_utf8")                               \
  V(StringNewWtf16, 0xfb81, _, "string.new_wtf16")                             \
  V(StringConst, 0xfb82, _, "string.const")                                    \
  V(StringMeasureUtf8, 0xfb83, _, "string.measure_utf8")                       \
  V(StringMeasureWtf8, 0xfb84, _, "string.measure_wtf8")                       \
  V(StringMeasureWtf16, 0xfb85, _, "string.measure_wtf16")                     \
  V(StringEncodeUtf8, 0xfb86, _, "string.encode_utf8")                         \
  V(StringEncodeWtf16, 0xfb87, _, "string.encode_wtf16")                       \
  V(StringConcat, 0xfb88, _, "string.concat")                                  \
  V(StringEq, 0xfb89, _, "string.eq")                                          \
  V(StringIsUSVSequence, 0xfb8a, _, "string.is_usv_sequence")                  \
  V(StringNewLossyUtf8, 0xfb8b, _, "string.new_lossy_utf8")                    \
  V(StringNewWtf8, 0xfb8c, _, "string.new_wtf8")                               \
  V(StringEncodeLossyUtf8, 0xfb8d, _, "string.encode_lossy_utf8")              \
  V(StringEncodeWtf8, 0xfb8e, _, "string.encode_wtf8")                         \
  V(StringNewUtf8Try, 0xfb8f, _, "string.new_utf8_try")                        \
  V(StringAsWtf8, 0xfb90, _, "string.as_wtf8")                                 \
  V(StringViewWtf8Advance, 0xfb91, _, "stringview_wtf8.advance")               \
  V(StringViewWtf8EncodeUtf8, 0xfb92, _, "stringview_wtf8.encode_utf8")        \
  V(StringViewWtf8Slice, 0xfb93, _, "stringview_wtf8.slice")                   \
  V(StringViewWtf8EncodeLossyUtf8, 0xfb94, _,                                  \
    "stringview_wtf8.encode_lossy_utf8")                                       \
  V(StringViewWtf8EncodeWtf8, 0xfb95, _, "stringview_wtf8.encode_wtf8")        \
  V(StringAsWtf16, 0xfb98, _, "string.as_wtf16")                               \
  V(StringViewWtf16Length, 0xfb99, _, "stringview_wtf16.length")               \
  V(StringViewWtf16GetCodeunit, 0xfb9a, _, "stringview_wtf16.get_codeunit")    \
  V(StringViewWtf16Encode, 0xfb9b, _, "stringview_wtf16.encode")               \
  V(StringViewWtf16Slice, 0xfb9c, _, "stringview_wtf16.slice")                 \
  V(StringAsIter, 0xfba0, _, "string.as_iter")                                 \
  V(StringViewIterNext, 0xfba1, _, "stringview_iter.next")                     \
  V(StringViewIterAdvance, 0xfba2, _, "stringview_iter.advance")               \
  V(StringViewIterRewind, 0xfba3, _, "stringview_iter.rewind")                 \
  V(StringViewIterSlice, 0xfba4, _, "stringview_iter.slice")                   \
  V(StringCompare, 0xfba8, _, "string.compare")                                \
  V(StringFromCodePoint, 0xfba9, _, "string.from_code_point")                  \
  V(StringHash, 0xfbaa, _, "string.hash")                                      \
  V(StringNewUtf8Array, 0xfbb0, _, "string.new_utf8_array")                    \
  V(StringNewWtf16Array, 0xfbb1, _, "string.new_wtf16_array")                  \
  V(StringEncodeUtf8Array, 0xfbb2, _, "string.encode_utf8_array")              \
  V(StringEncodeWtf16Array, 0xfbb3, _, "string.encode_wtf16_array")            \
  V(StringNewLossyUtf8Array, 0xfbb4, _, "string.new_lossy_utf8_array")         \
  V(StringNewWtf8Array, 0xfbb5, _, "string.new_wtf8_array")                    \
  V(StringEncodeLossyUtf8Array, 0xfbb6, _, "string.encode_lossy_utf8_array")   \
  V(StringEncodeWtf8Array, 0xfbb7, _, "string.encode_wtf8_array")              \
  V(StringNewUtf8ArrayTry, 0xfbb8, _, "string.new_utf8_array_try")

// All opcodes.
#define FOREACH_OPCODE(V)            \
  FOREACH_CONTROL_OPCODE(V)          \
  FOREACH_MISC_OPCODE(V)             \
  FOREACH_SIMPLE_OPCODE(V)           \
  FOREACH_SIMPLE_PROTOTYPE_OPCODE(V) \
  FOREACH_STORE_MEM_OPCODE(V)        \
  FOREACH_LOAD_MEM_OPCODE(V)         \
  FOREACH_MISC_MEM_OPCODE(V)         \
  FOREACH_ASMJS_COMPAT_OPCODE(V)     \
  FOREACH_SIMD_OPCODE(V)             \
  FOREACH_ATOMIC_OPCODE(V)           \
  FOREACH_ATOMIC_0_OPERAND_OPCODE(V) \
  FOREACH_NUMERIC_OPCODE(V)          \
  FOREACH_GC_OPCODE(V)

// All signatures.
#define FOREACH_SIGNATURE(V)                        \
  FOREACH_SIMD_SIGNATURE(V)                         \
  V(d_d, kWasmF64, kWasmF64)                        \
  V(d_dd, kWasmF64, kWasmF64, kWasmF64)             \
  V(d_f, kWasmF64, kWasmF32)                        \
  V(d_i, kWasmF64, kWasmI32)                        \
  V(d_id, kWasmF64, kWasmI32, kWasmF64)             \
  V(d_l, kWasmF64, kWasmI64)                        \
  V(f_d, kWasmF32, kWasmF64)                        \
  V(f_f, kWasmF32, kWasmF32)                        \
  V(f_ff, kWasmF32, kWasmF32, kWasmF32)             \
  V(f_i, kWasmF32, kWasmI32)                        \
  V(f_if, kWasmF32, kWasmI32, kWasmF32)             \
  V(f_l, kWasmF32, kWasmI64)                        \
  V(i_a, kWasmI32, kWasmAnyRef)                     \
  V(i_ci, kWasmI32, kWasmFuncRef, kWasmI32)         \
  V(i_d, kWasmI32, kWasmF64)                        \
  V(i_dd, kWasmI32, kWasmF64, kWasmF64)             \
  V(i_f, kWasmI32, kWasmF32)                        \
  V(i_ff, kWasmI32, kWasmF32, kWasmF32)             \
  V(i_i, kWasmI32, kWasmI32)                        \
  V(i_ii, kWasmI32, kWasmI32, kWasmI32)             \
  V(i_iii, kWasmI32, kWasmI32, kWasmI32, kWasmI32)  \
  V(i_iil, kWasmI32, kWasmI32, kWasmI32, kWasmI64)  \
  V(i_ill, kWasmI32, kWasmI32, kWasmI64, kWasmI64)  \
  V(i_l, kWasmI32, kWasmI64)                        \
  V(i_li, kWasmI32, kWasmI64, kWasmI32)             \
  V(i_lii, kWasmI32, kWasmI64, kWasmI32, kWasmI32)  \
  V(i_lil, kWasmI32, kWasmI64, kWasmI32, kWasmI64)  \
  V(i_lll, kWasmI32, kWasmI64, kWasmI64, kWasmI64)  \
  V(i_ll, kWasmI32, kWasmI64, kWasmI64)             \
  V(i_qq, kWasmI32, kWasmEqRef, kWasmEqRef)         \
  V(i_v, kWasmI32)                                  \
  V(l_d, kWasmI64, kWasmF64)                        \
  V(l_f, kWasmI64, kWasmF32)                        \
  V(l_i, kWasmI64, kWasmI32)                        \
  V(l_il, kWasmI64, kWasmI32, kWasmI64)             \
  V(l_ill, kWasmI64, kWasmI32, kWasmI64, kWasmI64)  \
  V(l_l, kWasmI64, kWasmI64)                        \
  V(l_ll, kWasmI64, kWasmI64, kWasmI64)             \
  V(l_lll, kWasmI64, kWasmI64, kWasmI64, kWasmI64)  \
  V(v_id, kWasmVoid, kWasmI32, kWasmF64)            \
  V(v_if, kWasmVoid, kWasmI32, kWasmF32)            \
  V(v_i, kWasmVoid, kWasmI32)                       \
  V(v_ii, kWasmVoid, kWasmI32, kWasmI32)            \
  V(v_iii, kWasmVoid, kWasmI32, kWasmI32, kWasmI32) \
  V(v_il, kWasmVoid, kWasmI32, kWasmI64)            \
  V(v_li, kWasmVoid, kWasmI64, kWasmI32)            \
  V(v_ll, kWasmVoid, kWasmI64, kWasmI64)            \
  V(v_v, kWasmVoid)

#define FOREACH_SIMD_SIGNATURE(V)                      \
  V(s_s, kWasmS128, kWasmS128)                         \
  V(s_f, kWasmS128, kWasmF32)                          \
  V(s_d, kWasmS128, kWasmF64)                          \
  V(s_ss, kWasmS128, kWasmS128, kWasmS128)             \
  V(s_i, kWasmS128, kWasmI32)                          \
  V(s_l, kWasmS128, kWasmI64)                          \
  V(s_si, kWasmS128, kWasmS128, kWasmI32)              \
  V(i_s, kWasmI32, kWasmS128)                          \
  V(v_is, kWasmVoid, kWasmI32, kWasmS128)              \
  V(s_sss, kWasmS128, kWasmS128, kWasmS128, kWasmS128) \
  V(s_is, kWasmS128, kWasmI32, kWasmS128)

#define FOREACH_PREFIX(V) \
  V(GC, 0xfb)             \
  V(Numeric, 0xfc)        \
  V(Simd, 0xfd)           \
  V(Atomic, 0xfe)

// Prefixed opcodes are encoded as 1 prefix byte, followed by LEB encoded
// opcode bytes. We internally encode them as {WasmOpcode} as follows:
// 1) non-prefixed opcodes use the opcode itself as {WasmOpcode} enum value;
// 2) prefixed opcodes in [0, 0xff] use {(prefix << 8) | opcode};
// 3) prefixed opcodes in [0x100, 0xfff] use {(prefix << 12) | opcode} (this is
//    only used for relaxed simd so far).
//
// This encoding is bijective (i.e. a one-to-one mapping in both directions).
// The used opcode ranges are:
// 1) [0, 0xff]  ->  no prefix, 8 bits opcode
// 2) [0xfb00, 0xfe00]  ->  prefix shifted by 8 bits, and 8 bits opcode
// 3) [0xfd100, 0xfdfff]  ->  prefix shifted by 12 bits, and 12 bits opcode
//                            (only [0xfd100, 0xfd1ff] used so far)
//
// This allows to compute back the prefix and the non-prefixed opcode from each
// WasmOpcode, see {WasmOpcodes::ExtractPrefix} and
// {ExtractPrefixedOpcodeBytes} (for testing).
enum WasmOpcode {
// Declare expression opcodes.
#define DECLARE_NAMED_ENUM(name, opcode, ...) kExpr##name = opcode,
  FOREACH_OPCODE(DECLARE_NAMED_ENUM)
#undef DECLARE_NAMED_ENUM
#define DECLARE_PREFIX(name, opcode) k##name##Prefix = opcode,
      FOREACH_PREFIX(DECLARE_PREFIX)
#undef DECLARE_PREFIX
};

enum TrapReason {
#define DECLARE_ENUM(name) k##name,
  FOREACH_WASM_TRAPREASON(DECLARE_ENUM)
  kTrapCount
#undef DECLARE_ENUM
};

// A collection of opcode-related static methods.
class V8_EXPORT_PRIVATE WasmOpcodes {
 public:
  static constexpr const char* OpcodeName(WasmOpcode);
  static constexpr const FunctionSig* Signature(WasmOpcode);
  static constexpr const FunctionSig* SignatureForAtomicOp(WasmOpcode opcode,
                                                           bool is_memory64);
  static constexpr const FunctionSig* AsmjsSignature(WasmOpcode);
  static constexpr bool IsPrefixOpcode(WasmOpcode);
  static constexpr bool IsControlOpcode(WasmOpcode);
  static constexpr bool IsExternRefOpcode(WasmOpcode);
  static constexpr bool IsThrowingOpcode(WasmOpcode);
  static constexpr bool IsRelaxedSimdOpcode(WasmOpcode);
  static constexpr bool IsFP16SimdOpcode(WasmOpcode);
#if DEBUG
  static constexpr bool IsMemoryAccessOpcode(WasmOpcode);
#endif  // DEBUG
  // Check whether the given opcode always jumps, i.e. all instructions after
  // this one in the current block are dead. Returns false for |end|.
  static constexpr bool IsUnconditionalJump(WasmOpcode);
  static constexpr bool IsBreakable(WasmOpcode);

  static constexpr MessageTemplate TrapReasonToMessageId(TrapReason);
  static constexpr TrapReason MessageIdToTrapReason(MessageTemplate message);

  // Extract the prefix byte (or 0x00) from a {WasmOpcode}.
  static constexpr uint8_t ExtractPrefix(WasmOpcode);
  static inline const char* TrapReasonMessage(TrapReason);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_OPCODES_H_
