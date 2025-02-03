// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#include "src/wasm/interpreter/wasm-interpreter.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

constexpr Decoder::NoValidationTag kNoValidate;

bool WasmBytecodeGenerator::DecodeSimdOp(WasmOpcode opcode,
                                         WasmInstruction::Optional* optional,
                                         Decoder* decoder,
                                         InterpreterCode* code, pc_t pc,
                                         int* const len) {
  static const bool kIsReservedSimdOpcode[256] = {
      // simdop  Instruction                   Immediate operands
      // --------------------------------------------------------
      false,  // 0x00    v128.load                     m:memarg
      false,  // 0x01    v128.load8x8_s                m:memarg
      false,  // 0x02    v128.load8x8_u                m:memarg
      false,  // 0x03    v128.load16x4_s               m:memarg
      false,  // 0x04    v128.load16x4_u               m:memarg
      false,  // 0x05    v128.load32x2_s               m:memarg
      false,  // 0x06    v128.load32x2_u               m:memarg
      false,  // 0x07    v128.load8_splat              m:memarg
      false,  // 0x08    v128.load16_splat             m:memarg
      false,  // 0x09    v128.load32_splat             m:memarg
      false,  // 0x0a    v128.load64_splat             m:memarg
      false,  // 0x0b    v128.store                    m:memarg
      false,  // 0x0c    v128.const                    i:ImmByte[16]
      false,  // 0x0d    i8x16.shuffle                 s:ImmLaneIdx32[16]
      false,  // 0x0e    i8x16.swizzle                 -
      false,  // 0x0f    i8x16.splat                   -
      false,  // 0x10    i16x8.splat                   -
      false,  // 0x11    i32x4.splat                   -
      false,  // 0x12    i64x2.splat                   -
      false,  // 0x13    f32x4.splat                   -
      false,  // 0x14    f64x2.splat                   -
      false,  // 0x15    i8x16.extract_lane_s          i:ImmLaneIdx16
      false,  // 0x16    i8x16.extract_lane_u          i:ImmLaneIdx16
      false,  // 0x17    i8x16.replace_lane            i:ImmLaneIdx16
      false,  // 0x18    i16x8.extract_lane_s          i:ImmLaneIdx8
      false,  // 0x19    i16x8.extract_lane_u          i:ImmLaneIdx8
      false,  // 0x1a    i16x8.replace_lane            i:ImmLaneIdx8
      false,  // 0x1b    i32x4.extract_lane            i:ImmLaneIdx4
      false,  // 0x1c    i32x4.replace_lane            i:ImmLaneIdx4
      false,  // 0x1d    i64x2.extract_lane            i:ImmLaneIdx2
      false,  // 0x1e    i64x2.replace_lane            i:ImmLaneIdx2
      false,  // 0x1f    f32x4.extract_lane            i:ImmLaneIdx4
      false,  // 0x20    f32x4.replace_lane            i:ImmLaneIdx4
      false,  // 0x21    f64x2.extract_lane            i:ImmLaneIdx2
      false,  // 0x22    f64x2.replace_lane            i:ImmLaneIdx2
      false,  // 0x23    i8x16.eq                      -
      false,  // 0x24    i8x16.ne                      -
      false,  // 0x25    i8x16.lt_s                    -
      false,  // 0x26    i8x16.lt_u                    -
      false,  // 0x27    i8x16.gt_s                    -
      false,  // 0x28    i8x16.gt_u                    -
      false,  // 0x29    i8x16.le_s                    -
      false,  // 0x2a    i8x16.le_u                    -
      false,  // 0x2b    i8x16.ge_s                    -
      false,  // 0x2c    i8x16.ge_u                    -
      false,  // 0x2d    i16x8.eq                      -
      false,  // 0x2e    i16x8.ne                      -
      false,  // 0x2f    i16x8.lt_s                    -
      false,  // 0x30    i16x8.lt_u                    -
      false,  // 0x31    i16x8.gt_s                    -
      false,  // 0x32    i16x8.gt_u                    -
      false,  // 0x33    i16x8.le_s                    -
      false,  // 0x34    i16x8.le_u                    -
      false,  // 0x35    i16x8.ge_s                    -
      false,  // 0x36    i16x8.ge_u                    -
      false,  // 0x37    i32x4.eq                      -
      false,  // 0x38    i32x4.ne                      -
      false,  // 0x39    i32x4.lt_s                    -
      false,  // 0x3a    i32x4.lt_u                    -
      false,  // 0x3b    i32x4.gt_s                    -
      false,  // 0x3c    i32x4.gt_u                    -
      false,  // 0x3d    i32x4.le_s                    -
      false,  // 0x3e    i32x4.le_u                    -
      false,  // 0x3f    i32x4.ge_s                    -
      false,  // 0x40    i32x4.ge_u                    -
      false,  // 0x41    f32x4.eq                      -
      false,  // 0x42    f32x4.ne                      -
      false,  // 0x43    f32x4.lt                      -
      false,  // 0x44    f32x4.gt                      -
      false,  // 0x45    f32x4.le                      -
      false,  // 0x46    f32x4.ge                      -
      false,  // 0x47    f64x2.eq                      -
      false,  // 0x48    f64x2.ne                      -
      false,  // 0x49    f64x2.lt                      -
      false,  // 0x4a    f64x2.gt                      -
      false,  // 0x4b    f64x2.le                      -
      false,  // 0x4c    f64x2.ge                      -
      false,  // 0x4d    v128.not                      -
      false,  // 0x4e    v128.and                      -
      false,  // 0x4f    v128.andnot                   -
      false,  // 0x50    v128.or                       -
      false,  // 0x51    v128.xor                      -
      false,  // 0x52    v128.bitselect                -
      false,  // 0x53    v128.any_true                 -
      false,  // 0x54    v128.load8_lane               m:memarg, i:ImmLaneIdx16
      false,  // 0x55    v128.load16_lane              m:memarg, i:ImmLaneIdx8
      false,  // 0x56    v128.load32_lane              m:memarg, i:ImmLaneIdx4
      false,  // 0x57    v128.load64_lane              m:memarg, i:ImmLaneIdx2
      false,  // 0x58    v128.store8_lane              m:memarg, i:ImmLaneIdx16
      false,  // 0x59    v128.store16_lane             m:memarg, i:ImmLaneIdx8
      false,  // 0x5a    v128.store32_lane             m:memarg, i:ImmLaneIdx4
      false,  // 0x5b    v128.store64_lane             m:memarg, i:ImmLaneIdx2
      false,  // 0x5c    v128.load32_zero              m:memarg
      false,  // 0x5d    v128.load64_zero              m:memarg
      false,  // 0x5e    f32x4.demote_f64x2_zero       -
      false,  // 0x5f    f64x2.promote_low_f32x4       -
      false,  // 0x60    i8x16.abs                     -
      false,  // 0x61    i8x16.neg                     -
      false,  // 0x62    i8x16.popcnt                  -
      false,  // 0x63    i8x16.all_true                -
      false,  // 0x64    i8x16.bitmask                 -
      false,  // 0x65    i8x16.narrow_i16x8_s          -
      false,  // 0x66    i8x16.narrow_i16x8_u          -
      false,  // 0x67    f32x4.ceil                    -
      false,  // 0x68    f32x4.floor                   -
      false,  // 0x69    f32x4.trunc                   -
      false,  // 0x6a    f32x4.nearest                 -
      false,  // 0x6b    i8x16.shl                     -
      false,  // 0x6c    i8x16.shr_s                   -
      false,  // 0x6d    i8x16.shr_u                   -
      false,  // 0x6e    i8x16.add                     -
      false,  // 0x6f    i8x16.add_sat_s               -
      false,  // 0x70    i8x16.add_sat_u               -
      false,  // 0x71    i8x16.sub                     -
      false,  // 0x72    i8x16.sub_sat_s               -
      false,  // 0x73    i8x16.sub_sat_u               -
      false,  // 0x74    f64x2.ceil                    -
      false,  // 0x75    f64x2.floor                   -
      false,  // 0x76    i8x16.min_s                   -
      false,  // 0x77    i8x16.min_u                   -
      false,  // 0x78    i8x16.max_s                   -
      false,  // 0x79    i8x16.max_u                   -
      false,  // 0x7a    f64x2.trunc                   -
      false,  // 0x7b    i8x16.avgr_u                  -
      false,  // 0x7c    i16x8.extadd_pairwise_i8x16_s -
      false,  // 0x7d    i16x8.extadd_pairwise_i8x16_u -
      false,  // 0x7e    i32x4.extadd_pairwise_i16x8_s -
      false,  // 0x7f    i32x4.extadd_pairwise_i16x8_u -
      false,  // 0x80    i16x8.abs                     -
      false,  // 0x81    i16x8.neg                     -
      false,  // 0x82    i16x8.q15mulr_sat_s           -
      false,  // 0x83    i16x8.all_true                -
      false,  // 0x84    i16x8.bitmask                 -
      false,  // 0x85    i16x8.narrow_i32x4_s          -
      false,  // 0x86    i16x8.narrow_i32x4_u          -
      false,  // 0x87    i16x8.extend_low_i8x16_s      -
      false,  // 0x88    i16x8.extend_high_i8x16_s     -
      false,  // 0x89    i16x8.extend_low_i8x16_u      -
      false,  // 0x8a    i16x8.extend_high_i8x16_u     -
      false,  // 0x8b    i16x8.shl                     -
      false,  // 0x8c    i16x8.shr_s                   -
      false,  // 0x8d    i16x8.shr_u                   -
      false,  // 0x8e    i16x8.add                     -
      false,  // 0x8f    i16x8.add_sat_s               -
      false,  // 0x90    i16x8.add_sat_u               -
      false,  // 0x91    i16x8.sub                     -
      false,  // 0x92    i16x8.sub_sat_s               -
      false,  // 0x93    i16x8.sub_sat_u               -
      false,  // 0x94    f64x2.nearest                 -
      false,  // 0x95    i16x8.mul                     -
      false,  // 0x96    i16x8.min_s                   -
      false,  // 0x97    i16x8.min_u                   -
      false,  // 0x98    i16x8.max_s                   -
      false,  // 0x99    i16x8.max_u                   -
      true,   // 0x9a    (reserved)
      false,  // 0x9b    i16x8.avgr_u                  -
      false,  // 0x9c    i16x8.extmul_low_i8x16_s      -
      false,  // 0x9d    i16x8.extmul_high_i8x16_s     -
      false,  // 0x9e    i16x8.extmul_low_i8x16_u      -
      false,  // 0x9f    i16x8.extmul_high_i8x16_u     -
      false,  // 0xa0    i32x4.abs                     -
      false,  // 0xa1    i32x4.neg                     -
      true,   // 0xa2    (reserved)
      false,  // 0xa3    i32x4.all_true                -
      false,  // 0xa4    i32x4.bitmask                 -
      true,   // 0xa5    (reserved)
      true,   // 0xa6    (reserved)
      false,  // 0xa7    i32x4.extend_low_i16x8_s      -
      false,  // 0xa8    i32x4.extend_high_i16x8_s     -
      false,  // 0xa9    i32x4.extend_low_i16x8_u      -
      false,  // 0xaa    i32x4.extend_high_i16x8_u     -
      false,  // 0xab    i32x4.shl                     -
      false,  // 0xac    i32x4.shr_s                   -
      false,  // 0xad    i32x4.shr_u                   -
      false,  // 0xae    i32x4.add                     -
      true,   // 0xaf    (reserved)
      true,   // 0xb0    (reserved)
      false,  // 0xb1    i32x4.sub                     -
      true,   // 0xb2    (reserved)
      true,   // 0xb3    (reserved)
      true,   // 0xb4    (reserved)
      false,  // 0xb5    i32x4.mul                     -
      false,  // 0xb6    i32x4.min_s                   -
      false,  // 0xb7    i32x4.min_u                   -
      false,  // 0xb8    i32x4.max_s                   -
      false,  // 0xb9    i32x4.max_u                   -
      false,  // 0xba    i32x4.dot_i16x8_s             -
      true,   // 0xbb    (reserved)
      false,  // 0xbc    i32x4.extmul_low_i16x8_s      -
      false,  // 0xbd    i32x4.extmul_high_i16x8_s     -
      false,  // 0xbe    i32x4.extmul_low_i16x8_u      -
      false,  // 0xbf    i32x4.extmul_high_i16x8_u     -
      false,  // 0xc0    i64x2.abs                     -
      false,  // 0xc1    i64x2.neg                     -
      true,   // 0xc2    (reserved)
      false,  // 0xc3    i64x2.all_true                -
      false,  // 0xc4    i64x2.bitmask                 -
      true,   // 0xc5    (reserved)
      true,   // 0xc6    (reserved)
      false,  // 0xc7    i64x2.extend_low_i32x4_s      -
      false,  // 0xc8    i64x2.extend_high_i32x4_s     -
      false,  // 0xc9    i64x2.extend_low_i32x4_u      -
      false,  // 0xca    i64x2.extend_high_i32x4_u     -
      false,  // 0xcb    i64x2.shl                     -
      false,  // 0xcc    i64x2.shr_s                   -
      false,  // 0xcd    i64x2.shr_u                   -
      false,  // 0xce    i64x2.add                     -
      true,   // 0xcf    (reserved)
      true,   // 0xd0    (reserved)
      false,  // 0xd1    i64x2.sub                     -
      true,   // 0xd2    (reserved)
      true,   // 0xd3    (reserved)
      true,   // 0xd4    (reserved)
      false,  // 0xd5    i64x2.mul                     -
      false,  // 0xd6    i64x2.eq                      -
      false,  // 0xd7    i64x2.ne                      -
      false,  // 0xd8    i64x2.lt_s                    -
      false,  // 0xd9    i64x2.gt_s                    -
      false,  // 0xda    i64x2.le_s                    -
      false,  // 0xdb    i64x2.ge_s                    -
      false,  // 0xdc    i64x2.extmul_low_i32x4_s      -
      false,  // 0xdd    i64x2.extmul_high_i32x4_s     -
      false,  // 0xde    i64x2.extmul_low_i32x4_u      -
      false,  // 0xdf    i64x2.extmul_high_i32x4_u     -
      false,  // 0xe0    f32x4.abs                     -
      false,  // 0xe1    f32x4.neg                     -
      true,   // 0xe2    (reserved)
      false,  // 0xe3    f32x4.sqrt                    -
      false,  // 0xe4    f32x4.add                     -
      false,  // 0xe5    f32x4.sub                     -
      false,  // 0xe6    f32x4.mul                     -
      false,  // 0xe7    f32x4.div                     -
      false,  // 0xe8    f32x4.min                     -
      false,  // 0xe9    f32x4.max                     -
      false,  // 0xea    f32x4.pmin                    -
      false,  // 0xeb    f32x4.pmax                    -
      false,  // 0xec    f64x2.abs                     -
      false,  // 0xed    f64x2.neg                     -
      false,  // 0xef    f64x2.sqrt                    -
      false,  // 0xf0    f64x2.add                     -
      false,  // 0xf1    f64x2.sub                     -
      false,  // 0xf2    f64x2.mul                     -
      false,  // 0xf3    f64x2.div                     -
      false,  // 0xf4    f64x2.min                     -
      false,  // 0xf5    f64x2.max                     -
      false,  // 0xf6    f64x2.pmin                    -
      false,  // 0xf7    f64x2.pmax                    -
      false,  // 0xf8    i32x4.trunc_sat_f32x4_s       -
      false,  // 0xf9    i32x4.trunc_sat_f32x4_u       -
      false,  // 0xfa    f32x4.convert_i32x4_s         -
      false,  // 0xfb    f32x4.convert_i32x4_u         -
      false,  // 0xfc    i32x4.trunc_sat_f64x2_s_zero  -
      false,  // 0xfd    i32x4.trunc_sat_f64x2_u_zero  -
      false,  // 0xfe    f64x2.convert_low_i32x4_s     -
      false   // 0xff    f64x2.convert_low_i32x4_u     -
  };

  if ((opcode >= kExprS128LoadMem && opcode <= kExprS128StoreMem) ||
      opcode == kExprS128Load32Zero || opcode == kExprS128Load64Zero) {
    MemoryAccessImmediate imm(decoder, code->at(pc + *len), 64, IsMemory64(),
                              Decoder::kNoValidation);
    optional->offset = imm.offset;
    *len += imm.length;
  } else if (opcode == kExprS128Const) {
    Simd128Immediate imm(decoder, code->at(pc + *len), kNoValidate);
    optional->simd_immediate_index = simd_immediates_.size();
    simd_immediates_.push_back(
        Simd128(imm.value));  // TODO(paolosev@microsoft.com): avoid duplicates?
    *len += 16;
  } else if (opcode == kExprI8x16Shuffle) {
    Simd128Immediate imm(decoder, code->at(pc + *len), kNoValidate);
    optional->simd_immediate_index = simd_immediates_.size();
    simd_immediates_.push_back(
        Simd128(imm.value));  // TODO(paolosev@microsoft.com): avoid duplicates?
    *len += 16;
  } else if ((opcode >= kExprI8x16ExtractLaneS) &&
             (opcode <= kExprF64x2ReplaceLane)) {
    SimdLaneImmediate imm(decoder, code->at(pc + *len), kNoValidate);
    if (imm.lane >= kSimd128Size) {
      return false;
    }
    optional->simd_lane = imm.lane;
    *len += 1;
  } else if ((opcode >= kExprS128Load8Lane) &&
             (opcode <= kExprS128Store64Lane)) {
    MemoryAccessImmediate mem_imm(decoder, code->at(pc + *len), 64,
                                  IsMemory64(), Decoder::kNoValidation);
    if (mem_imm.offset >= ((uint64_t)1 << 48)) {
      return false;
    }
    *len += mem_imm.length;

    SimdLaneImmediate lane_imm(decoder, code->at(pc + *len), kNoValidate);
    if (lane_imm.lane >= kSimd128Size) {
      return false;
    }

    optional->simd_loadstore_lane.offset = mem_imm.offset;
    optional->simd_loadstore_lane.lane = lane_imm.lane;
    *len += lane_imm.length;
  } else if (WasmOpcodes::IsRelaxedSimdOpcode(opcode)) {
    // Relaxed SIMD opcodes:
    // 0x100   i8x16.relaxed_swizzle         -
    // 0x101   i32x4.relaxed_trunc_f32x4_s   -
    // 0x102   i32x4.relaxed_trunc_f32x4_u   -
    // 0x103   i32x4.relaxed_trunc_f64x2_s_zero -
    // 0x104   i32x4.relaxed_trunc_f64x2_u_zero -
    // 0x105   f32x4.relaxed_madd            -
    // 0x106   f32x4.relaxed_nmadd           -
    // 0x107   f64x2.relaxed_madd            -
    // 0x108   f64x2.relaxed_nmadd           -
    // 0x109   i8x16.relaxed_laneselect      -
    // 0x10a   i16x8.relaxed_laneselect      -
    // 0x10b   i32x4.relaxed_laneselect      -
    // 0x10c   i64x2.relaxed_laneselect      -
    // 0x10d   f32x4.relaxed_min             -
    // 0x10e   f32x4.relaxed_max             -
    // 0x10f   f64x2.relaxed_min             -
    // 0x110   f64x2.relaxed_max             -
    // 0x111   i16x8.relaxed_q15mulr_s       -
    // 0x112   i16x8.relaxed_dot_i8x16_i7x16_s -
    // 0x113   i32x4.relaxed_dot_i8x16_i7x16_add_s -
    return opcode <= 0xfd113;
    // Handle relaxed SIMD opcodes (in [0xfd100, 0xfd1ff]).
  } else if (opcode >= 0xfd200 || kIsReservedSimdOpcode[opcode & 0xff]) {
    FATAL("Unknown or unimplemented opcode #%d:%s", code->start[pc],
          WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(code->start[pc])));
    UNREACHABLE();
  } else {
    // No immediate operands.
  }
  return true;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
