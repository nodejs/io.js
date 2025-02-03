// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright(c) 2010 - 2017,
//     The Regents of the University of California(Regents).All Rights Reserved.
//
//     Redistribution and use in source and binary forms,
//     with or without modification,
//     are permitted provided that the following
//     conditions are met : 1. Redistributions of source code must retain the
//     above copyright notice, this list of conditions and the following
//     disclaimer.2. Redistributions in binary form must reproduce the above
//     copyright notice, this list of conditions and the following disclaimer in
//     the
//             documentation and /
//         or
//         other materials provided with the distribution.3. Neither the name of
//         the Regents nor the names of its contributors may be used to endorse
//         or
//         promote products derived from
//         this software without specific prior written permission.
//
//         IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT,
//     INDIRECT, SPECIAL,
//     INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
//     ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
//     EVEN IF REGENTS HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//     REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
//     INCLUDING, BUT NOT LIMITED TO,
//     THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
//     PARTICULAR PURPOSE.THE SOFTWARE AND ACCOMPANYING DOCUMENTATION,
//     IF ANY,
//     PROVIDED HEREUNDER IS PROVIDED
//     "AS IS".REGENTS HAS NO OBLIGATION TO PROVIDE MAINTENANCE,
//     SUPPORT, UPDATES, ENHANCEMENTS,
//     OR MODIFICATIONS.

// The original source code covered by the above license above has been
// modified significantly by the v8 project authors.

#include "src/execution/riscv/simulator-riscv.h"

// Only build the simulator if not compiling for real RISCV hardware.
#if defined(USE_SIMULATOR)

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

#include "src/base/bits.h"
#include "src/base/overflowing-math.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/constants-arch.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disasm.h"
#include "src/heap/combined-heap.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler-simulator.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#if V8_TARGET_ARCH_RISCV64
#define REGIx_FORMAT PRIx64
#define REGId_FORMAT PRId64
#elif V8_TARGET_ARCH_RISCV32
#define REGIx_FORMAT PRIx32
#define REGId_FORMAT PRId32
#endif

// The following code about RVV was based from:
//   https://github.com/riscv/riscv-isa-sim
// Copyright (c) 2010-2017, The Regents of the University of California
// (Regents).  All Rights Reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the Regents nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.

// IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
// SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// REGENTS HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
// HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE
// MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#ifdef CAN_USE_RVV_INSTRUCTIONS
static inline bool is_aligned(const unsigned val, const unsigned pos) {
  return pos ? (val & (pos - 1)) == 0 : true;
}

static inline bool is_overlapped(const int astart, int asize, const int bstart,
                                 int bsize) {
  asize = asize == 0 ? 1 : asize;
  bsize = bsize == 0 ? 1 : bsize;

  const int aend = astart + asize;
  const int bend = bstart + bsize;

  return std::max(aend, bend) - std::min(astart, bstart) < asize + bsize;
}
static inline bool is_overlapped_widen(const int astart, int asize,
                                       const int bstart, int bsize) {
  asize = asize == 0 ? 1 : asize;
  bsize = bsize == 0 ? 1 : bsize;

  const int aend = astart + asize;
  const int bend = bstart + bsize;

  if (astart < bstart && is_overlapped(astart, asize, bstart, bsize) &&
      !is_overlapped(astart, asize, bstart + bsize, bsize)) {
    return false;
  } else {
    return std::max(aend, bend) - std::min(astart, bstart) < asize + bsize;
  }
}

#ifdef DEBUG
#define require_align(val, pos)                  \
  if (!is_aligned(val, pos)) {                   \
    std::cout << val << " " << pos << std::endl; \
  }                                              \
  CHECK_EQ(is_aligned(val, pos), true)
#else
#define require_align(val, pos) CHECK_EQ(is_aligned(val, pos), true)
#endif

// RVV
// The following code about RVV was based from:
//   https://github.com/riscv/riscv-isa-sim
// Copyright (c) 2010-2017, The Regents of the University of California
// (Regents).  All Rights Reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the Regents nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.

// IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
// SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// REGENTS HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
// HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE
// MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
template <uint64_t N>
struct type_usew_t;
template <>
struct type_usew_t<8> {
  using type = uint8_t;
};

template <>
struct type_usew_t<16> {
  using type = uint16_t;
};

template <>
struct type_usew_t<32> {
  using type = uint32_t;
};

template <>
struct type_usew_t<64> {
  using type = uint64_t;
};

template <>
struct type_usew_t<128> {
  using type = __uint128_t;
};
template <uint64_t N>
struct type_sew_t;

template <>
struct type_sew_t<8> {
  using type = int8_t;
};

template <>
struct type_sew_t<16> {
  using type = int16_t;
};

template <>
struct type_sew_t<32> {
  using type = int32_t;
};

template <>
struct type_sew_t<64> {
  using type = int64_t;
};

template <>
struct type_sew_t<128> {
  using type = __int128_t;
};

#define VV_PARAMS(x)                                                       \
  type_sew_t<x>::type& vd =                                                \
      Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);                  \
  type_sew_t<x>::type vs1 = Rvvelt<type_sew_t<x>::type>(rvv_vs1_reg(), i); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VV_UPARAMS(x)                                                        \
  type_usew_t<x>::type& vd =                                                 \
      Rvvelt<type_usew_t<x>::type>(rvv_vd_reg(), i, true);                   \
  type_usew_t<x>::type vs1 = Rvvelt<type_usew_t<x>::type>(rvv_vs1_reg(), i); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define VX_PARAMS(x)                                                        \
  type_sew_t<x>::type& vd =                                                 \
      Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);                   \
  type_sew_t<x>::type rs1 = (type_sew_t<x>::type)(get_register(rs1_reg())); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VX_UPARAMS(x)                                                         \
  type_usew_t<x>::type& vd =                                                  \
      Rvvelt<type_usew_t<x>::type>(rvv_vd_reg(), i, true);                    \
  type_usew_t<x>::type rs1 = (type_usew_t<x>::type)(get_register(rs1_reg())); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define VI_PARAMS(x)                                                    \
  type_sew_t<x>::type& vd =                                             \
      Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);               \
  type_sew_t<x>::type simm5 = (type_sew_t<x>::type)(instr_.RvvSimm5()); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VI_UPARAMS(x)                                                     \
  type_usew_t<x>::type& vd =                                              \
      Rvvelt<type_usew_t<x>::type>(rvv_vd_reg(), i, true);                \
  type_usew_t<x>::type uimm5 = (type_usew_t<x>::type)(instr_.RvvUimm5()); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define VN_PARAMS(x)                                                    \
  constexpr int half_x = x >> 1;                                        \
  type_sew_t<half_x>::type& vd =                                        \
      Rvvelt<type_sew_t<half_x>::type>(rvv_vd_reg(), i, true);          \
  type_sew_t<x>::type uimm5 = (type_sew_t<x>::type)(instr_.RvvUimm5()); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VN_UPARAMS(x)                                                     \
  constexpr int half_x = x >> 1;                                          \
  type_usew_t<half_x>::type& vd =                                         \
      Rvvelt<type_usew_t<half_x>::type>(rvv_vd_reg(), i, true);           \
  type_usew_t<x>::type uimm5 = (type_usew_t<x>::type)(instr_.RvvUimm5()); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VXI_PARAMS(x)                                                       \
  type_sew_t<x>::type& vd =                                                 \
      Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);                   \
  type_sew_t<x>::type vs1 = Rvvelt<type_sew_t<x>::type>(rvv_vs1_reg(), i);  \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);  \
  type_sew_t<x>::type rs1 = (type_sew_t<x>::type)(get_register(rs1_reg())); \
  type_sew_t<x>::type simm5 = (type_sew_t<x>::type)(instr_.RvvSimm5());

#define VI_XI_SLIDEDOWN_PARAMS(x, off)                           \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true); \
  auto vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i + off);

#define VI_XI_SLIDEUP_PARAMS(x, offset)                          \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true); \
  auto vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i - offset);

#define VX_SLIDE1DOWN_PARAMS(x, off)                                          \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);              \
  if ((i + off) == rvv_vlmax()) {                                             \
    type_sew_t<x>::type src = (type_sew_t<x>::type)(get_register(rs1_reg())); \
    vd = src;                                                                 \
  } else {                                                                    \
    auto src = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i + off);           \
    vd = src;                                                                 \
  }

#define VX_SLIDE1UP_PARAMS(x, offset)                                         \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);              \
  if (i == 0 && rvv_vstart() == 0) {                                          \
    type_sew_t<x>::type src = (type_sew_t<x>::type)(get_register(rs1_reg())); \
    vd = src;                                                                 \
  } else {                                                                    \
    auto src = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i - offset);        \
    vd = src;                                                                 \
  }

#define VF_SLIDE1DOWN_PARAMS(x, offset)                                \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);       \
  if ((i + offset) == rvv_vlmax()) {                                   \
    auto src = base::bit_cast<type_sew_t<x>::type>(                    \
        get_fpu_register_Float##x(rs1_reg()).get_bits());              \
    vd = src;                                                          \
  } else {                                                             \
    auto src = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i + offset); \
    vd = src;                                                          \
  }

#define VF_SLIDE1UP_PARAMS(x, offset)                                  \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);       \
  if (i == rvv_vstart() && i == 0) {                                   \
    auto src = base::bit_cast<type_sew_t<x>::type>(                    \
        get_fpu_register_Float##x(rs1_reg()).get_bits());              \
    vd = src;                                                          \
  } else {                                                             \
    auto src = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i - offset); \
    vd = src;                                                          \
  }

/* Vector Integer Extension */
#define VI_VIE_PARAMS(x, scale)                                  \
  if ((x / scale) < 8) UNREACHABLE();                            \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true); \
  auto vs2 = Rvvelt<type_sew_t<x / scale>::type>(rvv_vs2_reg(), i);

#define VI_VIE_UPARAMS(x, scale)                                 \
  if ((x / scale) < 8) UNREACHABLE();                            \
  auto& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true); \
  auto vs2 = Rvvelt<type_usew_t<x / scale>::type>(rvv_vs2_reg(), i);

#define require_noover(astart, asize, bstart, bsize) \
  CHECK_EQ(!is_overlapped(astart, asize, bstart, bsize), true)
#define require_noover_widen(astart, asize, bstart, bsize) \
  CHECK_EQ(!is_overlapped_widen(astart, asize, bstart, bsize), true)

#define RVV_VI_GENERAL_LOOP_BASE \
  for (uint64_t i = rvv_vstart(); i < rvv_vl(); i++) {
#define RVV_VI_LOOP_END \
  set_rvv_vstart(0);    \
  }

#define RVV_VI_MASK_VARS       \
  const uint8_t midx = i / 64; \
  const uint8_t mpos = i % 64;

#define RVV_VI_LOOP_MASK_SKIP(BODY)                               \
  RVV_VI_MASK_VARS                                                \
  if (instr_.RvvVM() == 0) {                                      \
    bool skip = ((Rvvelt<uint64_t>(0, midx) >> mpos) & 0x1) == 0; \
    if (skip) {                                                   \
      continue;                                                   \
    }                                                             \
  }

#define RVV_VI_VV_LOOP(BODY)      \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VV_PARAMS(8);                 \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VV_PARAMS(16);                \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VV_PARAMS(32);                \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VV_PARAMS(64);                \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

#define RVV_VI_VV_ULOOP(BODY)     \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VV_UPARAMS(8);                \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VV_UPARAMS(16);               \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VV_UPARAMS(32);               \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VV_UPARAMS(64);               \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

#define RVV_VI_VX_LOOP(BODY)      \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VX_PARAMS(8);                 \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VX_PARAMS(16);                \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VX_PARAMS(32);                \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VX_PARAMS(64);                \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

#define RVV_VI_VX_ULOOP(BODY)     \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VX_UPARAMS(8);                \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VX_UPARAMS(16);               \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VX_UPARAMS(32);               \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VX_UPARAMS(64);               \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

#define RVV_VI_VI_LOOP(BODY)      \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VI_PARAMS(8);                 \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VI_PARAMS(16);                \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VI_PARAMS(32);                \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VI_PARAMS(64);                \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

#define RVV_VI_VI_ULOOP(BODY)     \
  RVV_VI_GENERAL_LOOP_BASE        \
  RVV_VI_LOOP_MASK_SKIP()         \
  if (rvv_vsew() == E8) {         \
    VI_UPARAMS(8);                \
    BODY                          \
  } else if (rvv_vsew() == E16) { \
    VI_UPARAMS(16);               \
    BODY                          \
  } else if (rvv_vsew() == E32) { \
    VI_UPARAMS(32);               \
    BODY                          \
  } else if (rvv_vsew() == E64) { \
    VI_UPARAMS(64);               \
    BODY                          \
  } else {                        \
    UNREACHABLE();                \
  }                               \
  RVV_VI_LOOP_END                 \
  rvv_trace_vd();

// widen operation loop

#define VI_WIDE_CHECK_COMMON                     \
  CHECK_LE(rvv_vflmul(), 4);                     \
  CHECK_LE(rvv_vsew() * 2, kRvvELEN);            \
  require_align(rvv_vd_reg(), rvv_vflmul() * 2); \
  require_vm;

#define VI_NARROW_CHECK_COMMON                    \
  CHECK_LE(rvv_vflmul(), 4);                      \
  CHECK_LE(rvv_vsew() * 2, kRvvELEN);             \
  require_align(rvv_vs2_reg(), rvv_vflmul() * 2); \
  require_align(rvv_vd_reg(), rvv_vflmul());      \
  require_vm;

#define RVV_VI_CHECK_SLIDE(is_over)           \
  require_align(rvv_vs2_reg(), rvv_vflmul()); \
  require_align(rvv_vd_reg(), rvv_vflmul());  \
  require_vm;                                 \
  if (is_over) require(rvv_vd_reg() != rvv_vs2_reg());

#define RVV_VI_CHECK_DDS(is_rs)                                           \
  VI_WIDE_CHECK_COMMON;                                                   \
  require_align(rvv_vs2_reg(), rvv_vflmul() * 2);                         \
  if (is_rs) {                                                            \
    require_align(rvv_vs1_reg(), rvv_vflmul());                           \
    if (rvv_vflmul() < 1) {                                               \
      require_noover(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs1_reg(),       \
                     rvv_vflmul());                                       \
    } else {                                                              \
      require_noover_widen(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs1_reg(), \
                           rvv_vflmul());                                 \
    }                                                                     \
  }

#define RVV_VI_CHECK_DSS(is_vs1)                                          \
  VI_WIDE_CHECK_COMMON;                                                   \
  require_align(rvv_vs2_reg(), rvv_vflmul());                             \
  if (rvv_vflmul() < 1) {                                                 \
    require_noover(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs2_reg(),         \
                   rvv_vflmul());                                         \
  } else {                                                                \
    require_noover_widen(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs2_reg(),   \
                         rvv_vflmul());                                   \
  }                                                                       \
  if (is_vs1) {                                                           \
    require_align(rvv_vs1_reg(), rvv_vflmul());                           \
    if (rvv_vflmul() < 1) {                                               \
      require_noover(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs1_reg(),       \
                     rvv_vflmul());                                       \
    } else {                                                              \
      require_noover_widen(rvv_vd_reg(), rvv_vflmul() * 2, rvv_vs1_reg(), \
                           rvv_vflmul());                                 \
    }                                                                     \
  }

#define RVV_VI_CHECK_SDS(is_vs1)                              \
  VI_NARROW_CHECK_COMMON;                                     \
  if (rvv_vd_reg() != rvv_vs2_reg())                          \
    require_noover(rvv_vd_reg(), rvv_vflmul(), rvv_vs2_reg(), \
                   rvv_vflmul() * 2);                         \
  if (is_vs1) require_align(rvv_vs1_reg(), rvv_vflmul());

#define RVV_VI_VV_LOOP_WIDEN(BODY) \
  RVV_VI_GENERAL_LOOP_BASE         \
  RVV_VI_LOOP_MASK_SKIP()          \
  if (rvv_vsew() == E8) {          \
    VV_PARAMS(8);                  \
    BODY;                          \
  } else if (rvv_vsew() == E16) {  \
    VV_PARAMS(16);                 \
    BODY;                          \
  } else if (rvv_vsew() == E32) {  \
    VV_PARAMS(32);                 \
    BODY;                          \
  }                                \
  RVV_VI_LOOP_END                  \
  rvv_trace_vd();

#define RVV_VI_VX_LOOP_WIDEN(BODY) \
  RVV_VI_GENERAL_LOOP_BASE         \
  if (rvv_vsew() == E8) {          \
    VX_PARAMS(8);                  \
    BODY;                          \
  } else if (rvv_vsew() == E16) {  \
    VX_PARAMS(16);                 \
    BODY;                          \
  } else if (rvv_vsew() == E32) {  \
    VX_PARAMS(32);                 \
    BODY;                          \
  }                                \
  RVV_VI_LOOP_END                  \
  rvv_trace_vd();

#define VI_WIDE_OP_AND_ASSIGN(var0, var1, var2, op0, op1, sign)                \
  switch (rvv_vsew()) {                                                        \
    case E8: {                                                                 \
      Rvvelt<uint16_t>(rvv_vd_reg(), i, true) =                                \
          op1((sign##16_t)(sign##8_t)var0 op0(sign##16_t)(sign##8_t) var1) +   \
          var2;                                                                \
    } break;                                                                   \
    case E16: {                                                                \
      Rvvelt<uint32_t>(rvv_vd_reg(), i, true) =                                \
          op1((sign##32_t)(sign##16_t)var0 op0(sign##32_t)(sign##16_t) var1) + \
          var2;                                                                \
    } break;                                                                   \
    default: {                                                                 \
      Rvvelt<uint64_t>(rvv_vd_reg(), i, true) =                                \
          op1((sign##64_t)(sign##32_t)var0 op0(sign##64_t)(sign##32_t) var1) + \
          var2;                                                                \
    } break;                                                                   \
  }

#define VI_WIDE_WVX_OP(var0, op0, sign)                              \
  switch (rvv_vsew()) {                                              \
    case E8: {                                                       \
      sign##16_t & vd_w = Rvvelt<sign##16_t>(rvv_vd_reg(), i, true); \
      sign##16_t vs2_w = Rvvelt<sign##16_t>(rvv_vs2_reg(), i);       \
      vd_w = vs2_w op0(sign##16_t)(sign##8_t) var0;                  \
    } break;                                                         \
    case E16: {                                                      \
      sign##32_t & vd_w = Rvvelt<sign##32_t>(rvv_vd_reg(), i, true); \
      sign##32_t vs2_w = Rvvelt<sign##32_t>(rvv_vs2_reg(), i);       \
      vd_w = vs2_w op0(sign##32_t)(sign##16_t) var0;                 \
    } break;                                                         \
    default: {                                                       \
      sign##64_t & vd_w = Rvvelt<sign##64_t>(rvv_vd_reg(), i, true); \
      sign##64_t vs2_w = Rvvelt<sign##64_t>(rvv_vs2_reg(), i);       \
      vd_w = vs2_w op0(sign##64_t)(sign##32_t) var0;                 \
    } break;                                                         \
  }

#define RVV_VI_VVXI_MERGE_LOOP(BODY) \
  RVV_VI_GENERAL_LOOP_BASE           \
  if (rvv_vsew() == E8) {            \
    VXI_PARAMS(8);                   \
    BODY;                            \
  } else if (rvv_vsew() == E16) {    \
    VXI_PARAMS(16);                  \
    BODY;                            \
  } else if (rvv_vsew() == E32) {    \
    VXI_PARAMS(32);                  \
    BODY;                            \
  } else if (rvv_vsew() == E64) {    \
    VXI_PARAMS(64);                  \
    BODY;                            \
  }                                  \
  RVV_VI_LOOP_END                    \
  rvv_trace_vd();

#define VV_WITH_CARRY_PARAMS(x)                                            \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i); \
  type_sew_t<x>::type vs1 = Rvvelt<type_sew_t<x>::type>(rvv_vs1_reg(), i); \
  type_sew_t<x>::type& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);

#define XI_WITH_CARRY_PARAMS(x)                                             \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);  \
  type_sew_t<x>::type rs1 = (type_sew_t<x>::type)(get_register(rs1_reg())); \
  type_sew_t<x>::type simm5 = (type_sew_t<x>::type)instr_.RvvSimm5();       \
  type_sew_t<x>::type& vd = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), i, true);

// carry/borrow bit loop
#define RVV_VI_VV_LOOP_WITH_CARRY(BODY) \
  CHECK_NE(rvv_vd_reg(), 0);            \
  RVV_VI_GENERAL_LOOP_BASE              \
  RVV_VI_MASK_VARS                      \
  if (rvv_vsew() == E8) {               \
    VV_WITH_CARRY_PARAMS(8)             \
    BODY;                               \
  } else if (rvv_vsew() == E16) {       \
    VV_WITH_CARRY_PARAMS(16)            \
    BODY;                               \
  } else if (rvv_vsew() == E32) {       \
    VV_WITH_CARRY_PARAMS(32)            \
    BODY;                               \
  } else if (rvv_vsew() == E64) {       \
    VV_WITH_CARRY_PARAMS(64)            \
    BODY;                               \
  }                                     \
  RVV_VI_LOOP_END

#define RVV_VI_XI_LOOP_WITH_CARRY(BODY) \
  CHECK_NE(rvv_vd_reg(), 0);            \
  RVV_VI_GENERAL_LOOP_BASE              \
  RVV_VI_MASK_VARS                      \
  if (rvv_vsew() == E8) {               \
    XI_WITH_CARRY_PARAMS(8)             \
    BODY;                               \
  } else if (rvv_vsew() == E16) {       \
    XI_WITH_CARRY_PARAMS(16)            \
    BODY;                               \
  } else if (rvv_vsew() == E32) {       \
    XI_WITH_CARRY_PARAMS(32)            \
    BODY;                               \
  } else if (rvv_vsew() == E64) {       \
    XI_WITH_CARRY_PARAMS(64)            \
    BODY;                               \
  }                                     \
  RVV_VI_LOOP_END

#define VV_CMP_PARAMS(x)                                                   \
  type_sew_t<x>::type vs1 = Rvvelt<type_sew_t<x>::type>(rvv_vs1_reg(), i); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VX_CMP_PARAMS(x)                                                    \
  type_sew_t<x>::type rs1 = (type_sew_t<x>::type)(get_register(rs1_reg())); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VI_CMP_PARAMS(x)                                              \
  type_sew_t<x>::type simm5 = (type_sew_t<x>::type)instr_.RvvSimm5(); \
  type_sew_t<x>::type vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define VV_UCMP_PARAMS(x)                                                    \
  type_usew_t<x>::type vs1 = Rvvelt<type_usew_t<x>::type>(rvv_vs1_reg(), i); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define VX_UCMP_PARAMS(x)                                 \
  type_usew_t<x>::type rs1 =                              \
      (type_sew_t<x>::type)(get_register(rvv_vs1_reg())); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define VI_UCMP_PARAMS(x)                                               \
  type_usew_t<x>::type uimm5 = (type_usew_t<x>::type)instr_.RvvUimm5(); \
  type_usew_t<x>::type vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define float32_t float
#define float64_t double

#define RVV_VI_LOOP_CMP_BASE                                    \
  CHECK(rvv_vsew() >= E8 && rvv_vsew() <= E64);                 \
  for (reg_t i = rvv_vstart(); i < rvv_vl(); ++i) {             \
    RVV_VI_LOOP_MASK_SKIP();                                    \
    uint64_t mmask = uint64_t(1) << mpos;                       \
    uint64_t& vdi = Rvvelt<uint64_t>(rvv_vd_reg(), midx, true); \
    uint64_t res = 0;

#define RVV_VI_LOOP_CMP_END                         \
  vdi = (vdi & ~mmask) | (((res) << mpos) & mmask); \
  }                                                 \
  rvv_trace_vd();                                   \
  set_rvv_vstart(0);

// comparision result to masking register
#define RVV_VI_VV_LOOP_CMP(BODY)  \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VV_CMP_PARAMS(8);             \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VV_CMP_PARAMS(16);            \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VV_CMP_PARAMS(32);            \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VV_CMP_PARAMS(64);            \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VX_LOOP_CMP(BODY)  \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VX_CMP_PARAMS(8);             \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VX_CMP_PARAMS(16);            \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VX_CMP_PARAMS(32);            \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VX_CMP_PARAMS(64);            \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VI_LOOP_CMP(BODY)  \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VI_CMP_PARAMS(8);             \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VI_CMP_PARAMS(16);            \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VI_CMP_PARAMS(32);            \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VI_CMP_PARAMS(64);            \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VV_ULOOP_CMP(BODY) \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VV_UCMP_PARAMS(8);            \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VV_UCMP_PARAMS(16);           \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VV_UCMP_PARAMS(32);           \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VV_UCMP_PARAMS(64);           \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VX_ULOOP_CMP(BODY) \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VX_UCMP_PARAMS(8);            \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VX_UCMP_PARAMS(16);           \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VX_UCMP_PARAMS(32);           \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VX_UCMP_PARAMS(64);           \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VI_ULOOP_CMP(BODY) \
  RVV_VI_LOOP_CMP_BASE            \
  if (rvv_vsew() == E8) {         \
    VI_UCMP_PARAMS(8);            \
    BODY;                         \
  } else if (rvv_vsew() == E16) { \
    VI_UCMP_PARAMS(16);           \
    BODY;                         \
  } else if (rvv_vsew() == E32) { \
    VI_UCMP_PARAMS(32);           \
    BODY;                         \
  } else if (rvv_vsew() == E64) { \
    VI_UCMP_PARAMS(64);           \
    BODY;                         \
  }                               \
  RVV_VI_LOOP_CMP_END

#define RVV_VI_VF_MERGE_LOOP_BASE \
  for (uint64_t i = rvv_vstart(); i < rvv_vl(); i++) {
#define RVV_VI_VF_MERGE_LOOP_END \
  set_rvv_vstart(0);             \
  }

#define RVV_VI_VF_MERGE_LOOP(BODY16, BODY32, BODY64)        \
  RVV_VI_VF_MERGE_LOOP_BASE                                 \
  switch (rvv_vsew()) {                                     \
    case E16: {                                             \
      UNIMPLEMENTED();                                      \
    }                                                       \
    case E32: {                                             \
      int32_t& vd = Rvvelt<int32_t>(rvv_vd_reg(), i, true); \
      int32_t fs1 = base::bit_cast<int32_t>(                \
          get_fpu_register_Float32(rs1_reg()).get_bits());  \
      int32_t vs2 = Rvvelt<int32_t>(rvv_vs2_reg(), i);      \
      BODY32;                                               \
      break;                                                \
    }                                                       \
    case E64: {                                             \
      int64_t& vd = Rvvelt<int64_t>(rvv_vd_reg(), i, true); \
      int64_t fs1 = base::bit_cast<int64_t>(                \
          get_fpu_register_Float64(rs1_reg()).get_bits());  \
      int64_t vs2 = Rvvelt<int64_t>(rvv_vs2_reg(), i);      \
      BODY64;                                               \
      break;                                                \
    }                                                       \
    default:                                                \
      UNREACHABLE();                                        \
      break;                                                \
  }                                                         \
  RVV_VI_VF_MERGE_LOOP_END                                  \
  rvv_trace_vd();

#define RVV_VI_VFP_LOOP_BASE                           \
  for (uint64_t i = rvv_vstart(); i < rvv_vl(); ++i) { \
    RVV_VI_LOOP_MASK_SKIP();

#define RVV_VI_VFP_LOOP_END \
  }                         \
  set_rvv_vstart(0);

#define RVV_VI_VFP_VF_LOOP(BODY16, BODY32, BODY64)        \
  RVV_VI_VFP_LOOP_BASE                                    \
  switch (rvv_vsew()) {                                   \
    case E16: {                                           \
      UNIMPLEMENTED();                                    \
    }                                                     \
    case E32: {                                           \
      float& vd = Rvvelt<float>(rvv_vd_reg(), i, true);   \
      float fs1 = get_fpu_register_float(rs1_reg());      \
      float vs2 = Rvvelt<float>(rvv_vs2_reg(), i);        \
      BODY32;                                             \
      break;                                              \
    }                                                     \
    case E64: {                                           \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true); \
      double fs1 = get_fpu_register_double(rs1_reg());    \
      double vs2 = Rvvelt<double>(rvv_vs2_reg(), i);      \
      BODY64;                                             \
      break;                                              \
    }                                                     \
    default:                                              \
      UNREACHABLE();                                      \
      break;                                              \
  }                                                       \
  RVV_VI_VFP_LOOP_END                                     \
  rvv_trace_vd();

#define RVV_VI_VFP_VV_LOOP(BODY16, BODY32, BODY64)        \
  RVV_VI_VFP_LOOP_BASE                                    \
  switch (rvv_vsew()) {                                   \
    case E16: {                                           \
      UNIMPLEMENTED();                                    \
      break;                                              \
    }                                                     \
    case E32: {                                           \
      float& vd = Rvvelt<float>(rvv_vd_reg(), i, true);   \
      float vs1 = Rvvelt<float>(rvv_vs1_reg(), i);        \
      float vs2 = Rvvelt<float>(rvv_vs2_reg(), i);        \
      BODY32;                                             \
      break;                                              \
    }                                                     \
    case E64: {                                           \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true); \
      double vs1 = Rvvelt<double>(rvv_vs1_reg(), i);      \
      double vs2 = Rvvelt<double>(rvv_vs2_reg(), i);      \
      BODY64;                                             \
      break;                                              \
    }                                                     \
    default:                                              \
      require(0);                                         \
      break;                                              \
  }                                                       \
  RVV_VI_VFP_LOOP_END                                     \
  rvv_trace_vd();

#define RVV_VFSGNJ_VV_VF_LOOP(BODY16, BODY32, BODY64)         \
  RVV_VI_VFP_LOOP_BASE                                        \
  switch (rvv_vsew()) {                                       \
    case E16: {                                               \
      UNIMPLEMENTED();                                        \
      break;                                                  \
    }                                                         \
    case E32: {                                               \
      uint32_t& vd = Rvvelt<uint32_t>(rvv_vd_reg(), i, true); \
      uint32_t vs1 = Rvvelt<uint32_t>(rvv_vs1_reg(), i);      \
      uint32_t vs2 = Rvvelt<uint32_t>(rvv_vs2_reg(), i);      \
      Float32 fs1 = get_fpu_register_Float32(rs1_reg());      \
      BODY32;                                                 \
      break;                                                  \
    }                                                         \
    case E64: {                                               \
      uint64_t& vd = Rvvelt<uint64_t>(rvv_vd_reg(), i, true); \
      uint64_t vs1 = Rvvelt<uint64_t>(rvv_vs1_reg(), i);      \
      uint64_t vs2 = Rvvelt<uint64_t>(rvv_vs2_reg(), i);      \
      Float64 fs1 = get_fpu_register_Float64(rs1_reg());      \
      BODY64;                                                 \
      break;                                                  \
    }                                                         \
    default:                                                  \
      require(0);                                             \
      break;                                                  \
  }                                                           \
  RVV_VI_VFP_LOOP_END                                         \
  rvv_trace_vd();

#define RVV_VI_VFP_VF_LOOP_WIDEN(BODY32, vs2_is_widen)                         \
  RVV_VI_VFP_LOOP_BASE                                                         \
  switch (rvv_vsew()) {                                                        \
    case E16:                                                                  \
    case E64: {                                                                \
      UNIMPLEMENTED();                                                         \
      break;                                                                   \
    }                                                                          \
    case E32: {                                                                \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true);                      \
      double fs1 = static_cast<double>(get_fpu_register_float(rs1_reg()));     \
      double vs2 = vs2_is_widen                                                \
                       ? Rvvelt<double>(rvv_vs2_reg(), i)                      \
                       : static_cast<double>(Rvvelt<float>(rvv_vs2_reg(), i)); \
      double vs3 = Rvvelt<double>(rvv_vd_reg(), i);                            \
      BODY32;                                                                  \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      UNREACHABLE();                                                           \
      break;                                                                   \
  }                                                                            \
  RVV_VI_VFP_LOOP_END                                                          \
  rvv_trace_vd();

#define RVV_VI_VFP_VV_LOOP_WIDEN(BODY32, vs2_is_widen)                         \
  RVV_VI_VFP_LOOP_BASE                                                         \
  switch (rvv_vsew()) {                                                        \
    case E16:                                                                  \
    case E64: {                                                                \
      UNIMPLEMENTED();                                                         \
      break;                                                                   \
    }                                                                          \
    case E32: {                                                                \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true);                      \
      double vs2 = vs2_is_widen                                                \
                       ? static_cast<double>(Rvvelt<double>(rvv_vs2_reg(), i)) \
                       : static_cast<double>(Rvvelt<float>(rvv_vs2_reg(), i)); \
      double vs1 = static_cast<double>(Rvvelt<float>(rvv_vs1_reg(), i));       \
      double vs3 = Rvvelt<double>(rvv_vd_reg(), i);                            \
      BODY32;                                                                  \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      require(0);                                                              \
      break;                                                                   \
  }                                                                            \
  RVV_VI_VFP_LOOP_END                                                          \
  rvv_trace_vd();

#define RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(type, check_fn, op)      \
  auto fn = [this](type frs1, type frs2) {                         \
    if (check_fn(frs1, frs2)) {                                    \
      this->set_fflags(kInvalidOperation);                         \
      return std::numeric_limits<type>::quiet_NaN();               \
    } else {                                                       \
      return frs2 op frs1;                                         \
    }                                                              \
  };                                                               \
  auto alu_out = fn(vs1, vs2);                                     \
  /** if any input or result is NaN, the result is quiet_NaN*/     \
  if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) { \
    /** signaling_nan sets kInvalidOperation bit*/                 \
    if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))             \
      set_fflags(kInvalidOperation);                               \
    alu_out = std::numeric_limits<type>::quiet_NaN();              \
  }                                                                \
  vd = alu_out;

#define RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(type, check_fn, op)      \
  auto fn = [this](type frs1, type frs2) {                         \
    if (check_fn(frs1, frs2)) {                                    \
      this->set_fflags(kInvalidOperation);                         \
      return std::numeric_limits<type>::quiet_NaN();               \
    } else {                                                       \
      return frs2 op frs1;                                         \
    }                                                              \
  };                                                               \
  auto alu_out = fn(fs1, vs2);                                     \
  /** if any input or result is NaN, the result is quiet_NaN*/     \
  if (std::isnan(alu_out) || std::isnan(fs1) || std::isnan(vs2)) { \
    /** signaling_nan sets kInvalidOperation bit*/                 \
    if (isSnan(alu_out) || isSnan(fs1) || isSnan(vs2))             \
      set_fflags(kInvalidOperation);                               \
    alu_out = std::numeric_limits<type>::quiet_NaN();              \
  }                                                                \
  vd = alu_out;

#define RVV_VI_VFP_FMA(type, _f1, _f2, _a)                                \
  auto fn = [](type f1, type f2, type a) { return std::fma(f1, f2, a); }; \
  vd = CanonicalizeFPUOpFMA<type>(fn, _f1, _f2, _a);

#define RVV_VI_VFP_FMA_VV_LOOP(BODY32, BODY64)            \
  RVV_VI_VFP_LOOP_BASE                                    \
  switch (rvv_vsew()) {                                   \
    case E16: {                                           \
      UNIMPLEMENTED();                                    \
    }                                                     \
    case E32: {                                           \
      float& vd = Rvvelt<float>(rvv_vd_reg(), i, true);   \
      float vs1 = Rvvelt<float>(rvv_vs1_reg(), i);        \
      float vs2 = Rvvelt<float>(rvv_vs2_reg(), i);        \
      BODY32;                                             \
      break;                                              \
    }                                                     \
    case E64: {                                           \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true); \
      double vs1 = Rvvelt<double>(rvv_vs1_reg(), i);      \
      double vs2 = Rvvelt<double>(rvv_vs2_reg(), i);      \
      BODY64;                                             \
      break;                                              \
    }                                                     \
    default:                                              \
      require(0);                                         \
      break;                                              \
  }                                                       \
  RVV_VI_VFP_LOOP_END                                     \
  rvv_trace_vd();

#define RVV_VI_VFP_FMA_VF_LOOP(BODY32, BODY64)            \
  RVV_VI_VFP_LOOP_BASE                                    \
  switch (rvv_vsew()) {                                   \
    case E16: {                                           \
      UNIMPLEMENTED();                                    \
    }                                                     \
    case E32: {                                           \
      float& vd = Rvvelt<float>(rvv_vd_reg(), i, true);   \
      float fs1 = get_fpu_register_float(rs1_reg());      \
      float vs2 = Rvvelt<float>(rvv_vs2_reg(), i);        \
      BODY32;                                             \
      break;                                              \
    }                                                     \
    case E64: {                                           \
      double& vd = Rvvelt<double>(rvv_vd_reg(), i, true); \
      float fs1 = get_fpu_register_float(rs1_reg());      \
      double vs2 = Rvvelt<double>(rvv_vs2_reg(), i);      \
      BODY64;                                             \
      break;                                              \
    }                                                     \
    default:                                              \
      require(0);                                         \
      break;                                              \
  }                                                       \
  RVV_VI_VFP_LOOP_END                                     \
  rvv_trace_vd();

#define RVV_VI_VFP_LOOP_CMP_BASE                                \
  for (reg_t i = rvv_vstart(); i < rvv_vl(); ++i) {             \
    RVV_VI_LOOP_MASK_SKIP();                                    \
    uint64_t mmask = uint64_t(1) << mpos;                       \
    uint64_t& vdi = Rvvelt<uint64_t>(rvv_vd_reg(), midx, true); \
    uint64_t res = 0;

#define RVV_VI_VFP_LOOP_CMP_END                         \
  switch (rvv_vsew()) {                                 \
    case E16:                                           \
    case E32:                                           \
    case E64: {                                         \
      vdi = (vdi & ~mmask) | (((res) << mpos) & mmask); \
      break;                                            \
    }                                                   \
    default:                                            \
      UNREACHABLE();                                    \
      break;                                            \
  }                                                     \
  }                                                     \
  set_rvv_vstart(0);                                    \
  rvv_trace_vd();

#define RVV_VI_VFP_LOOP_CMP(BODY16, BODY32, BODY64, is_vs1) \
  RVV_VI_VFP_LOOP_CMP_BASE                                  \
  switch (rvv_vsew()) {                                     \
    case E16: {                                             \
      UNIMPLEMENTED();                                      \
    }                                                       \
    case E32: {                                             \
      float vs2 = Rvvelt<float>(rvv_vs2_reg(), i);          \
      float vs1 = Rvvelt<float>(rvv_vs1_reg(), i);          \
      BODY32;                                               \
      break;                                                \
    }                                                       \
    case E64: {                                             \
      double vs2 = Rvvelt<double>(rvv_vs2_reg(), i);        \
      double vs1 = Rvvelt<double>(rvv_vs1_reg(), i);        \
      BODY64;                                               \
      break;                                                \
    }                                                       \
    default:                                                \
      UNREACHABLE();                                        \
      break;                                                \
  }                                                         \
  RVV_VI_VFP_LOOP_CMP_END

// reduction loop - signed
#define RVV_VI_LOOP_REDUCTION_BASE(x)                                  \
  auto& vd_0_des = Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), 0, true); \
  auto vd_0_res = Rvvelt<type_sew_t<x>::type>(rvv_vs1_reg(), 0);       \
  for (uint64_t i = rvv_vstart(); i < rvv_vl(); ++i) {                 \
    RVV_VI_LOOP_MASK_SKIP();                                           \
    auto vs2 = Rvvelt<type_sew_t<x>::type>(rvv_vs2_reg(), i);

#define RVV_VI_LOOP_REDUCTION_END(x) \
  }                                  \
  if (rvv_vl() > 0) {                \
    vd_0_des = vd_0_res;             \
  }                                  \
  set_rvv_vstart(0);

#define REDUCTION_LOOP(x, BODY) \
  RVV_VI_LOOP_REDUCTION_BASE(x) \
  BODY;                         \
  RVV_VI_LOOP_REDUCTION_END(x)

#define RVV_VI_VV_LOOP_REDUCTION(BODY) \
  if (rvv_vsew() == E8) {              \
    REDUCTION_LOOP(8, BODY)            \
  } else if (rvv_vsew() == E16) {      \
    REDUCTION_LOOP(16, BODY)           \
  } else if (rvv_vsew() == E32) {      \
    REDUCTION_LOOP(32, BODY)           \
  } else if (rvv_vsew() == E64) {      \
    REDUCTION_LOOP(64, BODY)           \
  }                                    \
  rvv_trace_vd();

#define VI_VFP_LOOP_REDUCTION_BASE(width)                              \
  float##width##_t vd_0 = Rvvelt<float##width##_t>(rvv_vd_reg(), 0);   \
  float##width##_t vs1_0 = Rvvelt<float##width##_t>(rvv_vs1_reg(), 0); \
  vd_0 = vs1_0;                                                        \
  /*bool is_active = false;*/                                          \
  for (reg_t i = rvv_vstart(); i < rvv_vl(); ++i) {                    \
    RVV_VI_LOOP_MASK_SKIP();                                           \
    float##width##_t vs2 = Rvvelt<float##width##_t>(rvv_vs2_reg(), i); \
  /*is_active = true;*/

#define VI_VFP_LOOP_REDUCTION_END(x)                           \
  }                                                            \
  set_rvv_vstart(0);                                           \
  if (rvv_vl() > 0) {                                          \
    Rvvelt<type_sew_t<x>::type>(rvv_vd_reg(), 0, true) = vd_0; \
  }

#define RVV_VI_VFP_VV_LOOP_REDUCTION(BODY16, BODY32, BODY64) \
  if (rvv_vsew() == E16) {                                   \
    UNIMPLEMENTED();                                         \
  } else if (rvv_vsew() == E32) {                            \
    VI_VFP_LOOP_REDUCTION_BASE(32)                           \
    BODY32;                                                  \
    VI_VFP_LOOP_REDUCTION_END(32)                            \
  } else if (rvv_vsew() == E64) {                            \
    VI_VFP_LOOP_REDUCTION_BASE(64)                           \
    BODY64;                                                  \
    VI_VFP_LOOP_REDUCTION_END(64)                            \
  }                                                          \
  rvv_trace_vd();

// reduction loop - unsgied
#define RVV_VI_ULOOP_REDUCTION_BASE(x)                                  \
  auto& vd_0_des = Rvvelt<type_usew_t<x>::type>(rvv_vd_reg(), 0, true); \
  auto vd_0_res = Rvvelt<type_usew_t<x>::type>(rvv_vs1_reg(), 0);       \
  for (reg_t i = rvv_vstart(); i < rvv_vl(); ++i) {                     \
    RVV_VI_LOOP_MASK_SKIP();                                            \
    auto vs2 = Rvvelt<type_usew_t<x>::type>(rvv_vs2_reg(), i);

#define REDUCTION_ULOOP(x, BODY) \
  RVV_VI_ULOOP_REDUCTION_BASE(x) \
  BODY;                          \
  RVV_VI_LOOP_REDUCTION_END(x)

#define RVV_VI_VV_ULOOP_REDUCTION(BODY) \
  if (rvv_vsew() == E8) {               \
    REDUCTION_ULOOP(8, BODY)            \
  } else if (rvv_vsew() == E16) {       \
    REDUCTION_ULOOP(16, BODY)           \
  } else if (rvv_vsew() == E32) {       \
    REDUCTION_ULOOP(32, BODY)           \
  } else if (rvv_vsew() == E64) {       \
    REDUCTION_ULOOP(64, BODY)           \
  }                                     \
  rvv_trace_vd();

#define VI_STRIP(inx) reg_t vreg_inx = inx;

#define VI_ELEMENT_SKIP(inx)       \
  if (inx >= vl) {                 \
    continue;                      \
  } else if (inx < rvv_vstart()) { \
    continue;                      \
  } else {                         \
    RVV_VI_LOOP_MASK_SKIP();       \
  }

#define require_vm                                      \
  do {                                                  \
    if (instr_.RvvVM() == 0) CHECK_NE(rvv_vd_reg(), 0); \
  } while (0);

#define VI_CHECK_STORE(elt_width, is_mask_ldst) \
  reg_t veew = is_mask_ldst ? 1 : sizeof(elt_width##_t) * 8;
// float vemul = is_mask_ldst ? 1 : ((float)veew / rvv_vsew() * Rvvvflmul);
// reg_t emul = vemul < 1 ? 1 : vemul;
// require(vemul >= 0.125 && vemul <= 8);
// require_align(rvv_rd(), vemul);
// require((nf * emul) <= (NVPR / 4) && (rvv_rd() + nf * emul) <= NVPR);

#define VI_CHECK_LOAD(elt_width, is_mask_ldst) \
  VI_CHECK_STORE(elt_width, is_mask_ldst);     \
  require_vm;

/*vd + fn * emul*/
#define RVV_VI_LD(stride, offset, elt_width, is_mask_ldst)                     \
  const reg_t nf = rvv_nf() + 1;                                               \
  const reg_t vl = is_mask_ldst ? ((rvv_vl() + 7) / 8) : rvv_vl();             \
  const int64_t baseAddr = rs1();                                              \
  for (reg_t i = 0; i < vl; ++i) {                                             \
    VI_ELEMENT_SKIP(i);                                                        \
    VI_STRIP(i);                                                               \
    set_rvv_vstart(i);                                                         \
    for (reg_t fn = 0; fn < nf; ++fn) {                                        \
      auto addr = baseAddr + (stride) + (offset) * sizeof(elt_width##_t);      \
      if (!ProbeMemory(addr, sizeof(elt_width##_t))) {                         \
        set_rvv_vstart(0);                                                     \
        return true;                                                           \
      }                                                                        \
      auto val = ReadMem<elt_width##_t>(addr, instr_.instr());                 \
      type_sew_t<sizeof(elt_width##_t) * 8>::type& vd =                        \
          Rvvelt<type_sew_t<sizeof(elt_width##_t) * 8>::type>(rvv_vd_reg(),    \
                                                              vreg_inx, true); \
      vd = val;                                                                \
    }                                                                          \
  }                                                                            \
  set_rvv_vstart(0);                                                           \
  if (v8_flags.trace_sim) {                                                    \
    __int128_t value = Vregister_[rvv_vd_reg()];                               \
    SNPrintF(trace_buf_,                                                       \
             "%016" PRIx64 "%016" PRIx64 "    (%" PRId64 ")    vlen:%" PRId64  \
             " <-- [addr: %" REGIx_FORMAT "]",                                 \
             *(reinterpret_cast<int64_t*>(&value) + 1),                        \
             *reinterpret_cast<int64_t*>(&value), icount_, rvv_vlen(),         \
             (sreg_t)(get_register(rs1_reg())));                               \
  }

#define RVV_VI_ST(stride, offset, elt_width, is_mask_ldst)                     \
  const reg_t nf = rvv_nf() + 1;                                               \
  const reg_t vl = is_mask_ldst ? ((rvv_vl() + 7) / 8) : rvv_vl();             \
  const int64_t baseAddr = rs1();                                              \
  for (reg_t i = 0; i < vl; ++i) {                                             \
    VI_STRIP(i)                                                                \
    VI_ELEMENT_SKIP(i);                                                        \
    set_rvv_vstart(i);                                                         \
    for (reg_t fn = 0; fn < nf; ++fn) {                                        \
      auto addr = baseAddr + (stride) + (offset) * sizeof(elt_width##_t);      \
      if (!ProbeMemory(addr, sizeof(elt_width##_t))) {                         \
        set_rvv_vstart(0);                                                     \
        return true;                                                           \
      }                                                                        \
      elt_width##_t vs1 = Rvvelt<type_sew_t<sizeof(elt_width##_t) * 8>::type>( \
          rvv_vs3_reg(), vreg_inx);                                            \
      WriteMem(addr, vs1, instr_.instr());                                     \
    }                                                                          \
  }                                                                            \
  set_rvv_vstart(0);                                                           \
  if (v8_flags.trace_sim) {                                                    \
    __int128_t value = Vregister_[rvv_vd_reg()];                               \
    SNPrintF(trace_buf_,                                                       \
             "%016" PRIx64 "%016" PRIx64 "    (%" PRId64 ")    vlen:%" PRId64  \
             " --> [addr: %" REGIx_FORMAT "]",                                 \
             *(reinterpret_cast<int64_t*>(&value) + 1),                        \
             *reinterpret_cast<int64_t*>(&value), icount_, rvv_vlen(),         \
             (sreg_t)(get_register(rs1_reg())));                               \
  }

#define VI_VFP_LOOP_SCALE_BASE                      \
  /*require(STATE.frm < 0x5);*/                     \
  for (reg_t i = rvv_vstart(); i < rvv_vl(); ++i) { \
    RVV_VI_LOOP_MASK_SKIP();

#define RVV_VI_VFP_CVT_SCALE(BODY8, BODY16, BODY32, CHECK8, CHECK16, CHECK32, \
                             is_widen, eew_check)                             \
  if (is_widen) {                                                             \
    RVV_VI_CHECK_DSS(false);                                                  \
  } else {                                                                    \
    RVV_VI_CHECK_SDS(false);                                                  \
  }                                                                           \
  CHECK(eew_check);                                                           \
  switch (rvv_vsew()) {                                                       \
    case E8: {                                                                \
      CHECK8                                                                  \
      VI_VFP_LOOP_SCALE_BASE                                                  \
      BODY8 /*set_fp_exceptions*/;                                            \
      RVV_VI_VFP_LOOP_END                                                     \
    } break;                                                                  \
    case E16: {                                                               \
      CHECK16                                                                 \
      VI_VFP_LOOP_SCALE_BASE                                                  \
      BODY16 /*set_fp_exceptions*/;                                           \
      RVV_VI_VFP_LOOP_END                                                     \
    } break;                                                                  \
    case E32: {                                                               \
      CHECK32                                                                 \
      VI_VFP_LOOP_SCALE_BASE                                                  \
      BODY32 /*set_fp_exceptions*/;                                           \
      RVV_VI_VFP_LOOP_END                                                     \
    } break;                                                                  \
    default:                                                                  \
      require(0);                                                             \
      break;                                                                  \
  }                                                                           \
  rvv_trace_vd();

// calculate the value of r used in rounding
static inline uint8_t get_round(int vxrm, uint64_t v, uint8_t shift) {
  uint8_t d = v8::internal::unsigned_bitextract_64(shift, shift, v);
  uint8_t d1;
  uint64_t D1, D2;

  if (shift == 0 || shift > 64) {
    return 0;
  }

  d1 = v8::internal::unsigned_bitextract_64(shift - 1, shift - 1, v);
  D1 = v8::internal::unsigned_bitextract_64(shift - 1, 0, v);
  if (vxrm == 0) { /* round-to-nearest-up (add +0.5 LSB) */
    return d1;
  } else if (vxrm == 1) { /* round-to-nearest-even */
    if (shift > 1) {
      D2 = v8::internal::unsigned_bitextract_64(shift - 2, 0, v);
      return d1 & ((D2 != 0) | d);
    } else {
      return d1 & d;
    }
  } else if (vxrm == 3) { /* round-to-odd (OR bits into LSB, aka "jam") */
    return !d & (D1 != 0);
  }
  return 0; /* round-down (truncate) */
}

template <typename Src, typename Dst>
inline Dst signed_saturation(Src v, uint n) {
  Dst smax = (Dst)(INTPTR_MAX >> (sizeof(intptr_t) * 8 - n));
  Dst smin = (Dst)(INTPTR_MIN >> (sizeof(intptr_t) * 8 - n));
  return (v > smax) ? smax : ((v < smin) ? smin : (Dst)v);
}

template <typename Src, typename Dst>
inline Dst unsigned_saturation(Src v, uint n) {
  Dst umax = (Dst)(UINTPTR_MAX >> (sizeof(uintptr_t) * 8 - n));
  return (v > umax) ? umax : ((v < 0) ? 0 : (Dst)v);
}

#define RVV_VN_CLIPU_VI_LOOP()                                   \
  RVV_VI_GENERAL_LOOP_BASE                                       \
  RVV_VI_LOOP_MASK_SKIP()                                        \
  if (rvv_vsew() == E8) {                                        \
    VN_UPARAMS(16);                                              \
    vd = unsigned_saturation<uint16_t, uint8_t>(                 \
        (static_cast<uint16_t>(vs2) >> uimm5) +                  \
            get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        8);                                                      \
  } else if (rvv_vsew() == E16) {                                \
    VN_UPARAMS(32);                                              \
    vd = unsigned_saturation<uint32_t, uint16_t>(                \
        (static_cast<uint32_t>(vs2) >> uimm5) +                  \
            get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        16);                                                     \
  } else if (rvv_vsew() == E32) {                                \
    VN_UPARAMS(64);                                              \
    vd = unsigned_saturation<uint64_t, uint32_t>(                \
        (static_cast<uint64_t>(vs2) >> uimm5) +                  \
            get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        32);                                                     \
  } else if (rvv_vsew() == E64) {                                \
    UNREACHABLE();                                               \
  } else {                                                       \
    UNREACHABLE();                                               \
  }                                                              \
  RVV_VI_LOOP_END                                                \
  rvv_trace_vd();

#define RVV_VN_CLIP_VI_LOOP()                                                 \
  RVV_VI_GENERAL_LOOP_BASE                                                    \
  RVV_VI_LOOP_MASK_SKIP()                                                     \
  if (rvv_vsew() == E8) {                                                     \
    VN_PARAMS(16);                                                            \
    vd = signed_saturation<int16_t, int8_t>(                                  \
        (vs2 >> uimm5) + get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        8);                                                                   \
  } else if (rvv_vsew() == E16) {                                             \
    VN_PARAMS(32);                                                            \
    vd = signed_saturation<int32_t, int16_t>(                                 \
        (vs2 >> uimm5) + get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        16);                                                                  \
  } else if (rvv_vsew() == E32) {                                             \
    VN_PARAMS(64);                                                            \
    vd = signed_saturation<int64_t, int32_t>(                                 \
        (vs2 >> uimm5) + get_round(static_cast<int>(rvv_vxrm()), vs2, uimm5), \
        32);                                                                  \
  } else if (rvv_vsew() == E64) {                                             \
    UNREACHABLE();                                                            \
  } else {                                                                    \
    UNREACHABLE();                                                            \
  }                                                                           \
  RVV_VI_LOOP_END                                                             \
  rvv_trace_vd();

#define CHECK_EXT(div)                                              \
  CHECK_NE(rvv_vd_reg(), rvv_vs2_reg());                            \
  reg_t from = rvv_vsew() / div;                                    \
  CHECK(from >= E8 && from <= E64);                                 \
  CHECK_GE((float)rvv_vflmul() / div, 0.125);                       \
  CHECK_LE((float)rvv_vflmul() / div, 8);                           \
  require_align(rvv_vd_reg(), rvv_vflmul());                        \
  require_align(rvv_vs2_reg(), rvv_vflmul() / div);                 \
  if ((rvv_vflmul() / div) < 1) {                                   \
    require_noover(rvv_vd_reg(), rvv_vflmul(), rvv_vs2_reg(),       \
                   rvv_vflmul() / div);                             \
  } else {                                                          \
    require_noover_widen(rvv_vd_reg(), rvv_vflmul(), rvv_vs2_reg(), \
                         rvv_vflmul() / div);                       \
  }

#define RVV_VI_VIE_8_LOOP(signed)      \
  CHECK_EXT(8)                         \
  RVV_VI_GENERAL_LOOP_BASE             \
  RVV_VI_LOOP_MASK_SKIP()              \
  if (rvv_vsew() == E64) {             \
    if (signed) {                      \
      VI_VIE_PARAMS(64, 8);            \
      vd = static_cast<int64_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(64, 8);           \
      vd = static_cast<uint64_t>(vs2); \
    }                                  \
  } else {                             \
    UNREACHABLE();                     \
  }                                    \
  RVV_VI_LOOP_END                      \
  rvv_trace_vd();

#define RVV_VI_VIE_4_LOOP(signed)      \
  CHECK_EXT(4)                         \
  RVV_VI_GENERAL_LOOP_BASE             \
  RVV_VI_LOOP_MASK_SKIP()              \
  if (rvv_vsew() == E32) {             \
    if (signed) {                      \
      VI_VIE_PARAMS(32, 4);            \
      vd = static_cast<int32_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(32, 4);           \
      vd = static_cast<uint32_t>(vs2); \
    }                                  \
  } else if (rvv_vsew() == E64) {      \
    if (signed) {                      \
      VI_VIE_PARAMS(64, 4);            \
      vd = static_cast<int64_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(64, 4);           \
      vd = static_cast<uint64_t>(vs2); \
    }                                  \
  } else {                             \
    UNREACHABLE();                     \
  }                                    \
  RVV_VI_LOOP_END                      \
  rvv_trace_vd();

#define RVV_VI_VIE_2_LOOP(signed)      \
  CHECK_EXT(2)                         \
  RVV_VI_GENERAL_LOOP_BASE             \
  RVV_VI_LOOP_MASK_SKIP()              \
  if (rvv_vsew() == E16) {             \
    if (signed) {                      \
      VI_VIE_PARAMS(16, 2);            \
      vd = static_cast<int16_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(16, 2);           \
      vd = static_cast<uint16_t>(vs2); \
    }                                  \
  } else if (rvv_vsew() == E32) {      \
    if (signed) {                      \
      VI_VIE_PARAMS(32, 2);            \
      vd = static_cast<int32_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(32, 2);           \
      vd = static_cast<uint32_t>(vs2); \
    }                                  \
  } else if (rvv_vsew() == E64) {      \
    if (signed) {                      \
      VI_VIE_PARAMS(64, 2);            \
      vd = static_cast<int64_t>(vs2);  \
    } else {                           \
      VI_VIE_UPARAMS(64, 2);           \
      vd = static_cast<uint64_t>(vs2); \
    }                                  \
  } else {                             \
    UNREACHABLE();                     \
  }                                    \
  RVV_VI_LOOP_END                      \
  rvv_trace_vd();
#endif

namespace v8 {
namespace internal {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

// Util functions.
inline bool HaveSameSign(int64_t a, int64_t b) { return ((a ^ b) >= 0); }

uint32_t get_fcsr_condition_bit(uint32_t cc) {
  if (cc == 0) {
    return 23;
  } else {
    return 24 + cc;
  }
}

// Generated by Assembler::break_()/stop(), ebreak code is passed as immediate
// field of a subsequent LUI instruction; otherwise returns -1
static inline int32_t get_ebreak_code(Instruction* instr) {
  DCHECK(instr->InstructionBits() == kBreakInstr);
  uint8_t* cur = reinterpret_cast<uint8_t*>(instr);
  Instruction* next_instr = reinterpret_cast<Instruction*>(cur + kInstrSize);
  if (next_instr->BaseOpcodeFieldRaw() == LUI)
    return (next_instr->Imm20UValue());
  else
    return -1;
}

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent was through
// ::v8::internal::OS in the same way as SNPrintF is that the Windows C Run-Time
// Library does not provide vsscanf.
#define SScanF sscanf

// The RiscvDebugger class is used by the simulator while debugging simulated
// code.
class RiscvDebugger {
 public:
  explicit RiscvDebugger(Simulator* sim) : sim_(sim) {}

  void Debug();
  // Print all registers with a nice formatting.
  void PrintRegs(char name_prefix, int start_index, int end_index);
  void PrintAllRegs();
  void PrintAllRegsIncludingFPU();

  static const Instr kNopInstr = 0x0;

 private:
  Simulator* sim_;

  sreg_t GetRegisterValue(int regnum);
  int64_t GetFPURegisterValue(int regnum);
  float GetFPURegisterValueFloat(int regnum);
  double GetFPURegisterValueDouble(int regnum);
#ifdef CAN_USE_RVV_INSTRUCTIONS
  __int128_t GetVRegisterValue(int regnum);
#endif
  bool GetValue(const char* desc, sreg_t* value);
};

#define UNSUPPORTED()                                                  \
  v8::base::EmbeddedVector<char, 256> buffer;                          \
  disasm::NameConverter converter;                                     \
  disasm::Disassembler dasm(converter);                                \
  dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(&instr_)); \
  printf("Sim: Unsupported inst. Func:%s Line:%d PC:0x%" REGIx_FORMAT, \
         __FUNCTION__, __LINE__, get_pc());                            \
  PrintF(" %-44s\n", buffer.begin());                                  \
  base::OS::Abort();

sreg_t RiscvDebugger::GetRegisterValue(int regnum) {
  if (regnum == kNumSimuRegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_register(regnum);
  }
}

int64_t RiscvDebugger::GetFPURegisterValue(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register(regnum);
  }
}

float RiscvDebugger::GetFPURegisterValueFloat(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_float(regnum);
  }
}

double RiscvDebugger::GetFPURegisterValueDouble(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_double(regnum);
  }
}

#ifdef CAN_USE_RVV_INSTRUCTIONS
__int128_t RiscvDebugger::GetVRegisterValue(int regnum) {
  if (regnum == kNumVRegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_vregister(regnum);
  }
}
#endif

bool RiscvDebugger::GetValue(const char* desc, sreg_t* value) {
  int regnum = Registers::Number(desc);
  int fpuregnum = FPURegisters::Number(desc);

  if (regnum != kInvalidRegister) {
    *value = GetRegisterValue(regnum);
    return true;
  } else if (fpuregnum != kInvalidFPURegister) {
    *value = GetFPURegisterValue(fpuregnum);
    return true;
  } else if (strncmp(desc, "0x", 2) == 0) {
#if V8_TARGET_ARCH_RISCV64
    return SScanF(desc + 2, "%" SCNx64, reinterpret_cast<reg_t*>(value)) == 1;
#elif V8_TARGET_ARCH_RISCV32
    return SScanF(desc + 2, "%" SCNx32, reinterpret_cast<reg_t*>(value)) == 1;
#endif
  } else {
#if V8_TARGET_ARCH_RISCV64
    return SScanF(desc, "%" SCNu64, reinterpret_cast<reg_t*>(value)) == 1;
#elif V8_TARGET_ARCH_RISCV32
    return SScanF(desc, "%" SCNu32, reinterpret_cast<reg_t*>(value)) == 1;
#endif
  }
}

#define REG_INFO(name)                             \
  name, GetRegisterValue(Registers::Number(name)), \
      GetRegisterValue(Registers::Number(name))

void RiscvDebugger::PrintRegs(char name_prefix, int start_index,
                              int end_index) {
  base::EmbeddedVector<char, 10> name1, name2;
  DCHECK(name_prefix == 'a' || name_prefix == 't' || name_prefix == 's');
  DCHECK(start_index >= 0 && end_index <= 99);
  int num_registers = (end_index - start_index) + 1;
  for (int i = 0; i < num_registers / 2; i++) {
    SNPrintF(name1, "%c%d", name_prefix, start_index + 2 * i);
    SNPrintF(name2, "%c%d", name_prefix, start_index + 2 * i + 1);
    PrintF("%3s: 0x%016" REGIx_FORMAT "  %14" REGId_FORMAT
           " \t%3s: 0x%016" REGIx_FORMAT "  %14" REGId_FORMAT " \n",
           REG_INFO(name1.begin()), REG_INFO(name2.begin()));
  }
  if (num_registers % 2 == 1) {
    SNPrintF(name1, "%c%d", name_prefix, end_index);
    PrintF("%3s: 0x%016" REGIx_FORMAT "  %14" REGId_FORMAT " \n",
           REG_INFO(name1.begin()));
  }
}

void RiscvDebugger::PrintAllRegs() {
  PrintF("\n");
  // ra, sp, gp
  PrintF("%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT
         "\t%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT
         "\t%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT "\n",
         REG_INFO("ra"), REG_INFO("sp"), REG_INFO("gp"));

  // tp, fp, pc
  PrintF("%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT
         "\t%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT
         "\t%3s: 0x%016" REGIx_FORMAT " %14" REGId_FORMAT "\n",
         REG_INFO("tp"), REG_INFO("fp"), REG_INFO("pc"));

  // print register a0, .., a7
  PrintRegs('a', 0, 7);
  // print registers s1, ..., s11
  PrintRegs('s', 1, 11);
  // print registers t0, ..., t6
  PrintRegs('t', 0, 6);
}

#undef REG_INFO

void RiscvDebugger::PrintAllRegsIncludingFPU() {
#define FPU_REG_INFO(n) \
  FPURegisters::Name(n), GetFPURegisterValue(n), GetFPURegisterValueDouble(n)

  PrintAllRegs();

  PrintF("\n\n");
  // f0, f1, f2, ... f31.
  DCHECK_EQ(kNumFPURegisters % 2, 0);
  for (int i = 0; i < kNumFPURegisters; i += 2)
    PrintF("%3s: 0x%016" PRIx64 "  %16.4e \t%3s: 0x%016" PRIx64 "  %16.4e\n",
           FPU_REG_INFO(i), FPU_REG_INFO(i + 1));
#undef FPU_REG_INFO
}

void RiscvDebugger::Debug() {
  intptr_t last_pc = -1;
  bool done = false;

#define COMMAND_SIZE 63
#define ARG_SIZE 255

#define STR(a) #a
#define XSTR(a) STR(a)

  char cmd[COMMAND_SIZE + 1];
  char arg1[ARG_SIZE + 1];
  char arg2[ARG_SIZE + 1];
  char* argv[3] = {cmd, arg1, arg2};

  // Make sure to have a proper terminating character if reaching the limit.
  cmd[COMMAND_SIZE] = 0;
  arg1[ARG_SIZE] = 0;
  arg2[ARG_SIZE] = 0;

  while (!done && (sim_->get_pc() != Simulator::end_sim_pc)) {
    if (last_pc != sim_->get_pc()) {
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      // Use a reasonably large buffer.
      v8::base::EmbeddedVector<char, 256> buffer;
      const char* name = sim_->builtins_.Lookup((Address)sim_->get_pc());
      if (name != nullptr) {
        PrintF("Call builtin:  %s\n", name);
      }
      dasm.InstructionDecode(buffer,
                             reinterpret_cast<uint8_t*>(sim_->get_pc()));
      PrintF("  0x%016" REGIx_FORMAT "   %s\n", sim_->get_pc(), buffer.begin());
      last_pc = sim_->get_pc();
    }
    char* line = ReadLine("sim> ");
    if (line == nullptr) {
      break;
    } else {
      char* last_input = sim_->last_debugger_input();
      if (strcmp(line, "\n") == 0 && last_input != nullptr) {
        line = last_input;
      } else {
        // Ownership is transferred to sim_;
        sim_->set_last_debugger_input(line);
      }
      // Use sscanf to parse the individual parts of the command line. At the
      // moment no command expects more than two parameters.
      int argc = SScanF(
            line,
            "%" XSTR(COMMAND_SIZE) "s "
            "%" XSTR(ARG_SIZE) "s "
            "%" XSTR(ARG_SIZE) "s",
            cmd, arg1, arg2);
      if ((strcmp(cmd, "si") == 0) || (strcmp(cmd, "stepi") == 0)) {
        Instruction* instr = reinterpret_cast<Instruction*>(sim_->get_pc());
        if (!(instr->IsTrap()) ||
            instr->InstructionBits() == rtCallRedirInstr) {
          sim_->icount_++;
          sim_->InstructionDecode(
              reinterpret_cast<Instruction*>(sim_->get_pc()));
        } else {
          // Allow si to jump over generated breakpoints.
          PrintF("/!\\ Jumping over generated breakpoint.\n");
          sim_->set_pc(sim_->get_pc() + kInstrSize);
        }
      } else if ((strcmp(cmd, "c") == 0) || (strcmp(cmd, "cont") == 0)) {
        // Execute the one instruction we broke at with breakpoints disabled.
        sim_->InstructionDecode(reinterpret_cast<Instruction*>(sim_->get_pc()));
        // Leave the debugger shell.
        done = true;
      } else if ((strcmp(cmd, "p") == 0) || (strcmp(cmd, "print") == 0)) {
        if (argc == 2) {
          sreg_t value;
          int64_t fvalue;
          double dvalue;
          if (strcmp(arg1, "all") == 0) {
            PrintAllRegs();
          } else if (strcmp(arg1, "allf") == 0) {
            PrintAllRegsIncludingFPU();
          } else {
            int regnum = Registers::Number(arg1);
            int fpuregnum = FPURegisters::Number(arg1);
#ifdef CAN_USE_RVV_INSTRUCTIONS
            int vregnum = VRegisters::Number(arg1);
#endif
            if (regnum != kInvalidRegister) {
              value = GetRegisterValue(regnum);
              PrintF("%s: 0x%08" REGIx_FORMAT "  %" REGId_FORMAT "  \n", arg1,
                     value, value);
            } else if (fpuregnum != kInvalidFPURegister) {
              fvalue = GetFPURegisterValue(fpuregnum);
              dvalue = GetFPURegisterValueDouble(fpuregnum);
              PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n",
                     FPURegisters::Name(fpuregnum), fvalue, dvalue);
#ifdef CAN_USE_RVV_INSTRUCTIONS
            } else if (vregnum != kInvalidVRegister) {
              __int128_t v = GetVRegisterValue(vregnum);
              PrintF("\t%s:0x%016" PRIx64 "%016" PRIx64 "\n",
                     VRegisters::Name(vregnum), (uint64_t)(v >> 64),
                     (uint64_t)v);
#endif
            } else {
              PrintF("%s unrecognized\n", arg1);
            }
          }
        } else {
          if (argc == 3) {
            if (strcmp(arg2, "single") == 0) {
              int64_t value;
              float fvalue;
              int fpuregnum = FPURegisters::Number(arg1);

              if (fpuregnum != kInvalidFPURegister) {
                value = GetFPURegisterValue(fpuregnum);
                value &= 0xFFFFFFFFUL;
                fvalue = GetFPURegisterValueFloat(fpuregnum);
                PrintF("%s: 0x%08" PRIx64 "  %11.4e\n", arg1, value, fvalue);
              } else {
                PrintF("%s unrecognized\n", arg1);
              }
            } else {
              PrintF("print <fpu register> single\n");
            }
          } else {
            PrintF("print <register> or print <fpu register> single\n");
          }
        }
      } else if ((strcmp(cmd, "po") == 0) ||
                 (strcmp(cmd, "printobject") == 0)) {
        if (argc == 2) {
          sreg_t value;
          StdoutStream os;
          if (GetValue(arg1, &value)) {
            Tagged<Object> obj(value);
            os << arg1 << ": \n";
#ifdef DEBUG
            Print(obj, os);
            os << "\n";
#else
            os << Brief(obj) << "\n";
#endif
          } else {
            os << arg1 << " unrecognized\n";
          }
        } else {
          PrintF("printobject <value>\n");
        }
      } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "mem") == 0) {
        sreg_t* cur = nullptr;
        sreg_t* end = nullptr;
        int next_arg = 1;

        if (strcmp(cmd, "stack") == 0) {
          cur = reinterpret_cast<sreg_t*>(sim_->get_register(Simulator::sp));
        } else {  // Command "mem".
          if (argc < 2) {
            PrintF("Need to specify <address> to mem command\n");
            continue;
          }
          sreg_t value;
          if (!GetValue(arg1, &value)) {
            PrintF("%s unrecognized\n", arg1);
            continue;
          }
          cur = reinterpret_cast<sreg_t*>(value);
          next_arg++;
        }

        sreg_t words;
        if (argc == next_arg) {
          words = 10;
        } else {
          if (!GetValue(argv[next_arg], &words)) {
            words = 10;
          }
        }
        end = cur + words;

        while (cur < end) {
          PrintF("  0x%012" PRIxPTR " :  0x%016" REGIx_FORMAT
                 "  %14" REGId_FORMAT " ",
                 reinterpret_cast<intptr_t>(cur), *cur, *cur);
          // Tagged<Object> obj(*cur);
          // Heap* current_heap = sim_->isolate_->heap();
          // if (IsSmi(obj) ||
          //     IsValidHeapObject(current_heap, Cast<HeapObject>(obj))) {
          //   PrintF(" (");
          //   if (IsSmi(obj)) {
          //     PrintF("smi %d", Smi::ToInt(obj));
          //   }
          //   PrintF(")");
          // }
          PrintF("\n");
          cur++;
        }
      } else if (strcmp(cmd, "memhex") == 0) {
        sreg_t* cur = nullptr;
        sreg_t* end = nullptr;
        int next_arg = 1;
        if (argc < 2) {
          PrintF("Need to specify <address> to memhex command\n");
          continue;
        }
        sreg_t value;
        if (!GetValue(arg1, &value)) {
          PrintF("%s unrecognized\n", arg1);
          continue;
        }
        cur = reinterpret_cast<sreg_t*>(value);
        next_arg++;

        sreg_t words;
        if (argc == next_arg) {
          words = 10;
        } else {
          if (!GetValue(argv[next_arg], &words)) {
            words = 10;
          }
        }
        end = cur + words;

        while (cur < end) {
          PrintF("  0x%012" PRIxPTR " :  0x%016" REGIx_FORMAT
                 "  %14" REGId_FORMAT " ",
                 reinterpret_cast<intptr_t>(cur), *cur, *cur);
          PrintF("\n");
          cur++;
        }
      } else if ((strcmp(cmd, "watch") == 0)) {
        if (argc < 2) {
          PrintF("Need to specify <address> to mem command\n");
          continue;
        }
        sreg_t value;
        if (!GetValue(arg1, &value)) {
          PrintF("%s unrecognized\n", arg1);
          continue;
        }
        sim_->watch_address_ = reinterpret_cast<sreg_t*>(value);
        sim_->watch_value_ = *(sim_->watch_address_);
      } else if ((strcmp(cmd, "disasm") == 0) || (strcmp(cmd, "dpc") == 0) ||
                 (strcmp(cmd, "di") == 0)) {
        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        // Use a reasonably large buffer.
        v8::base::EmbeddedVector<char, 256> buffer;

        uint8_t* cur = nullptr;
        uint8_t* end = nullptr;

        if (argc == 1) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          end = cur + (10 * kInstrSize);
        } else if (argc == 2) {
          int regnum = Registers::Number(arg1);
          if (regnum != kInvalidRegister || strncmp(arg1, "0x", 2) == 0) {
            // The argument is an address or a register name.
            sreg_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(value);
              // Disassemble 10 instructions at <arg1>.
              end = cur + (10 * kInstrSize);
            }
          } else {
            // The argument is the number of instructions.
            sreg_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
              // Disassemble <arg1> instructions.
              end = cur + (value * kInstrSize);
            }
          }
        } else {
          sreg_t value1;
          sreg_t value2;
          if (GetValue(arg1, &value1) && GetValue(arg2, &value2)) {
            cur = reinterpret_cast<uint8_t*>(value1);
            end = cur + (value2 * kInstrSize);
          }
        }

        while (cur < end) {
          dasm.InstructionDecode(buffer, cur);
          PrintF("  0x%08" PRIxPTR "   %s\n", reinterpret_cast<intptr_t>(cur),
                 buffer.begin());
          cur += kInstrSize;
        }
      } else if (strcmp(cmd, "gdb") == 0) {
        PrintF("relinquishing control to gdb\n");
        v8::base::OS::DebugBreak();
        PrintF("regaining control from gdb\n");
      } else if (strcmp(cmd, "trace") == 0) {
        PrintF("enable trace sim\n");
        v8_flags.trace_sim = true;
      } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0 ||
                 strcmp(cmd, "tbreak") == 0) {
        bool is_tbreak = strcmp(cmd, "tbreak") == 0;
        if (argc == 2) {
          sreg_t value;
          if (GetValue(arg1, &value)) {
            sim_->SetBreakpoint(reinterpret_cast<Instruction*>(value),
                                is_tbreak);
          } else {
            PrintF("%s unrecognized\n", arg1);
          }
        } else {
          sim_->ListBreakpoints();
          PrintF("Use `break <address>` to set or disable a breakpoint\n");
          PrintF(
              "Use `tbreak <address>` to set or disable a temporary "
              "breakpoint\n");
        }
      } else if (strcmp(cmd, "flags") == 0) {
        PrintF("No flags on RISC-V !\n");
      } else if (strcmp(cmd, "stop") == 0) {
        sreg_t value;
        if (argc == 3) {
          // Print information about all/the specified breakpoint(s).
          if (strcmp(arg1, "info") == 0) {
            if (strcmp(arg2, "all") == 0) {
              PrintF("Stop information:\n");
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->PrintStopInfo(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->PrintStopInfo(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          } else if (strcmp(arg1, "enable") == 0) {
            // Enable all/the specified breakpoint(s).
            if (strcmp(arg2, "all") == 0) {
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->EnableStop(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->EnableStop(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          } else if (strcmp(arg1, "disable") == 0) {
            // Disable all/the specified breakpoint(s).
            if (strcmp(arg2, "all") == 0) {
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->DisableStop(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->DisableStop(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          }
        } else {
          PrintF("Wrong usage. Use help command for more information.\n");
        }
      } else if ((strcmp(cmd, "stat") == 0) || (strcmp(cmd, "st") == 0)) {
        // Print registers and disassemble.
        PrintAllRegs();
        PrintF("\n");

        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        // Use a reasonably large buffer.
        v8::base::EmbeddedVector<char, 256> buffer;

        uint8_t* cur = nullptr;
        uint8_t* end = nullptr;

        if (argc == 1) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          end = cur + (10 * kInstrSize);
        } else if (argc == 2) {
          sreg_t value;
          if (GetValue(arg1, &value)) {
            cur = reinterpret_cast<uint8_t*>(value);
            // no length parameter passed, assume 10 instructions
            end = cur + (10 * kInstrSize);
          }
        } else {
          sreg_t value1;
          sreg_t value2;
          if (GetValue(arg1, &value1) && GetValue(arg2, &value2)) {
            cur = reinterpret_cast<uint8_t*>(value1);
            end = cur + (value2 * kInstrSize);
          }
        }

        while (cur < end) {
          dasm.InstructionDecode(buffer, cur);
          PrintF("  0x%08" PRIxPTR "   %s\n", reinterpret_cast<intptr_t>(cur),
                 buffer.begin());
          cur += kInstrSize;
        }
      } else if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "help") == 0)) {
        PrintF("cont (alias 'c')\n");
        PrintF("  Continue execution\n");
        PrintF("stepi (alias 'si')\n");
        PrintF("  Step one instruction\n");
        PrintF("print (alias 'p')\n");
        PrintF("  print <register>\n");
        PrintF("  Print register content\n");
        PrintF("  Use register name 'all' to print all GPRs\n");
        PrintF("  Use register name 'allf' to print all GPRs and FPRs\n");
        PrintF("printobject (alias 'po')\n");
        PrintF("  printobject <register>\n");
        PrintF("  Print an object from a register\n");
        PrintF("stack\n");
        PrintF("  stack [<words>]\n");
        PrintF("  Dump stack content, default dump 10 words)\n");
        PrintF("mem\n");
        PrintF("  mem <address> [<words>]\n");
        PrintF("  Dump memory content, default dump 10 words)\n");
        PrintF("watch\n");
        PrintF("  watch <address> \n");
        PrintF("  watch memory content.)\n");
        PrintF("flags\n");
        PrintF("  print flags\n");
        PrintF("disasm (alias 'di')\n");
        PrintF("  disasm [<instructions>]\n");
        PrintF("  disasm [<address/register>] (e.g., disasm pc) \n");
        PrintF("  disasm [[<address/register>] <instructions>]\n");
        PrintF("  Disassemble code, default is 10 instructions\n");
        PrintF("  from pc\n");
        PrintF("gdb \n");
        PrintF("  Return to gdb if the simulator was started with gdb\n");
        PrintF("break (alias 'b')\n");
        PrintF("  break : list all breakpoints\n");
        PrintF("  break <address> : set / enable / disable a breakpoint.\n");
        PrintF("tbreak\n");
        PrintF("  tbreak : list all breakpoints\n");
        PrintF(
            "  tbreak <address> : set / enable / disable a temporary "
            "breakpoint.\n");
        PrintF("  Set a breakpoint enabled only for one stop. \n");
        PrintF("stop feature:\n");
        PrintF("  Description:\n");
        PrintF("    Stops are debug instructions inserted by\n");
        PrintF("    the Assembler::stop() function.\n");
        PrintF("    When hitting a stop, the Simulator will\n");
        PrintF("    stop and give control to the Debugger.\n");
        PrintF("    All stop codes are watched:\n");
        PrintF("    - They can be enabled / disabled: the Simulator\n");
        PrintF("       will / won't stop when hitting them.\n");
        PrintF("    - The Simulator keeps track of how many times they \n");
        PrintF("      are met. (See the info command.) Going over a\n");
        PrintF("      disabled stop still increases its counter. \n");
        PrintF("  Commands:\n");
        PrintF("    stop info all/<code> : print infos about number <code>\n");
        PrintF("      or all stop(s).\n");
        PrintF("    stop enable/disable all/<code> : enables / disables\n");
        PrintF("      all or number <code> stop(s)\n");
      } else {
        PrintF("Unknown command: %s\n", cmd);
      }
    }
  }

#undef COMMAND_SIZE
#undef ARG_SIZE

#undef STR
#undef XSTR
}

void Simulator::SetBreakpoint(Instruction* location, bool is_tbreak) {
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    if (breakpoints_.at(i).location == location) {
      if (breakpoints_.at(i).is_tbreak != is_tbreak) {
        PrintF("Change breakpoint at %p to %s breakpoint\n",
               reinterpret_cast<void*>(location),
               is_tbreak ? "temporary" : "regular");
        breakpoints_.at(i).is_tbreak = is_tbreak;
        return;
      }
      PrintF("Existing breakpoint at %p was %s\n",
             reinterpret_cast<void*>(location),
             breakpoints_.at(i).enabled ? "disabled" : "enabled");
      breakpoints_.at(i).enabled = !breakpoints_.at(i).enabled;
      return;
    }
  }
  Breakpoint new_breakpoint = {location, true, is_tbreak};
  breakpoints_.push_back(new_breakpoint);
  PrintF("Set a %sbreakpoint at %p\n", is_tbreak ? "temporary " : "",
         reinterpret_cast<void*>(location));
}

void Simulator::ListBreakpoints() {
  PrintF("Breakpoints:\n");
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    PrintF("%p  : %s %s\n",
           reinterpret_cast<void*>(breakpoints_.at(i).location),
           breakpoints_.at(i).enabled ? "enabled" : "disabled",
           breakpoints_.at(i).is_tbreak ? ": temporary" : "");
  }
}

void Simulator::CheckBreakpoints() {
  bool hit_a_breakpoint = false;
  bool is_tbreak = false;
  Instruction* pc_ = reinterpret_cast<Instruction*>(get_pc());
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    if ((breakpoints_.at(i).location == pc_) && breakpoints_.at(i).enabled) {
      hit_a_breakpoint = true;
      if (breakpoints_.at(i).is_tbreak) {
        // Disable a temporary breakpoint.
        is_tbreak = true;
        breakpoints_.at(i).enabled = false;
      }
      break;
    }
  }
  if (hit_a_breakpoint) {
    PrintF("Hit %sa breakpoint at %p.\n", is_tbreak ? "and disabled " : "",
           reinterpret_cast<void*>(pc_));
    RiscvDebugger dbg(this);
    dbg.Debug();
  }
}

bool Simulator::ICacheMatch(void* one, void* two) {
  DCHECK_EQ(reinterpret_cast<intptr_t>(one) & CachePage::kPageMask, 0);
  DCHECK_EQ(reinterpret_cast<intptr_t>(two) & CachePage::kPageMask, 0);
  return one == two;
}

static uint32_t ICacheHash(void* key) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)) >> 2;
}

static bool AllOnOnePage(uintptr_t start, size_t size) {
  intptr_t start_page = (start & ~CachePage::kPageMask);
  intptr_t end_page = ((start + size) & ~CachePage::kPageMask);
  return start_page == end_page;
}

void Simulator::set_last_debugger_input(char* input) {
  DeleteArray(last_debugger_input_);
  last_debugger_input_ = input;
}

void Simulator::SetRedirectInstruction(Instruction* instruction) {
  instruction->SetInstructionBits(rtCallRedirInstr);
}

void Simulator::FlushICache(base::CustomMatcherHashMap* i_cache,
                            void* start_addr, size_t size) {
  int64_t start = reinterpret_cast<int64_t>(start_addr);
  int64_t intra_line = (start & CachePage::kLineMask);
  start -= intra_line;
  size += intra_line;
  size = ((size - 1) | CachePage::kLineMask) + 1;
  int offset = (start & CachePage::kPageMask);
  while (!AllOnOnePage(start, size - 1)) {
    int bytes_to_flush = CachePage::kPageSize - offset;
    FlushOnePage(i_cache, start, bytes_to_flush);
    start += bytes_to_flush;
    size -= bytes_to_flush;
    DCHECK_EQ((int64_t)0, start & CachePage::kPageMask);
    offset = 0;
  }
  if (size != 0) {
    FlushOnePage(i_cache, start, size);
  }
}

CachePage* Simulator::GetCachePage(base::CustomMatcherHashMap* i_cache,
                                   void* page) {
  base::HashMap::Entry* entry = i_cache->LookupOrInsert(page, ICacheHash(page));
  if (entry->value == nullptr) {
    CachePage* new_page = new CachePage();
    entry->value = new_page;
  }
  return reinterpret_cast<CachePage*>(entry->value);
}

// Flush from start up to and not including start + size.
void Simulator::FlushOnePage(base::CustomMatcherHashMap* i_cache,
                             intptr_t start, size_t size) {
  DCHECK_LE(size, CachePage::kPageSize);
  DCHECK(AllOnOnePage(start, size - 1));
  DCHECK_EQ(start & CachePage::kLineMask, 0);
  DCHECK_EQ(size & CachePage::kLineMask, 0);
  void* page = reinterpret_cast<void*>(start & (~CachePage::kPageMask));
  int offset = (start & CachePage::kPageMask);
  CachePage* cache_page = GetCachePage(i_cache, page);
  char* valid_bytemap = cache_page->ValidityByte(offset);
  memset(valid_bytemap, CachePage::LINE_INVALID, size >> CachePage::kLineShift);
}

void Simulator::CheckICache(base::CustomMatcherHashMap* i_cache,
                            Instruction* instr) {
  sreg_t address = reinterpret_cast<sreg_t>(instr);
  void* page = reinterpret_cast<void*>(address & (~CachePage::kPageMask));
  void* line = reinterpret_cast<void*>(address & (~CachePage::kLineMask));
  int offset = (address & CachePage::kPageMask);
  CachePage* cache_page = GetCachePage(i_cache, page);
  char* cache_valid_byte = cache_page->ValidityByte(offset);
  bool cache_hit = (*cache_valid_byte == CachePage::LINE_VALID);
  char* cached_line = cache_page->CachedData(offset & ~CachePage::kLineMask);
  if (cache_hit) {
    // Check that the data in memory matches the contents of the I-cache.
    CHECK_EQ(0, memcmp(reinterpret_cast<void*>(instr),
                       cache_page->CachedData(offset), kInstrSize));
  } else {
    // Cache miss.  Load memory into the cache.
    memcpy(cached_line, line, CachePage::kLineLength);
    *cache_valid_byte = CachePage::LINE_VALID;
  }
}

Simulator::Simulator(Isolate* isolate) : isolate_(isolate), builtins_(isolate) {
  // Set up simulator support first. Some of this information is needed to
  // setup the architecture state.
  stack_ = reinterpret_cast<uint8_t*>(base::Malloc(AllocatedStackSize()));
  pc_modified_ = false;
  icount_ = 0;
  break_count_ = 0;
  // Reset debug helpers.
  breakpoints_.clear();
  // TODO(riscv): 'next' command
  // break_on_next_ = false;

  // Set up architecture state.
  // All registers are initialized to zero to start with.
  for (int i = 0; i < kNumSimuRegisters; i++) {
    registers_[i] = 0;
  }

  for (int i = 0; i < kNumFPURegisters; i++) {
    FPUregisters_[i] = 0;
  }

  FCSR_ = 0;

  // The sp is initialized to point to the bottom (high address) of the
  // allocated stack area. To be safe in potential stack underflows we leave
  // some buffer below.
  registers_[sp] = reinterpret_cast<intptr_t>(stack_) + UsableStackSize();
  // The ra and pc are initialized to a known bad value that will cause an
  // access violation if the simulator ever tries to execute it.
  registers_[pc] = bad_ra;
  registers_[ra] = bad_ra;

  last_debugger_input_ = nullptr;
#ifdef CAN_USE_RVV_INSTRUCTIONS
  for (int i = 0; i < kNumVRegisters; ++i) {
    Vregister_[i] = 0;
  }
  vxrm_ = 0;
  vstart_ = 0;
  vxsat_ = 0;
  vxrm_ = 0;
  vcsr_ = 0;
  vtype_ = 0;
  vl_ = 0;
  vlenb_ = 0;
#endif
}

Simulator::~Simulator() {
  GlobalMonitor::Get()->RemoveLinkedAddress(&global_monitor_thread_);
  free(stack_);
}

// Get the active Simulator for the current thread.
Simulator* Simulator::current(Isolate* isolate) {
  v8::internal::Isolate::PerIsolateThreadData* isolate_data =
      isolate->FindOrAllocatePerThreadDataForThisThread();
  DCHECK_NOT_NULL(isolate_data);

  Simulator* sim = isolate_data->simulator();
  if (sim == nullptr) {
    // TODO(146): delete the simulator object when a thread/isolate goes away.
    sim = new Simulator(isolate);
    isolate_data->set_simulator(sim);
  }
  return sim;
}

// Sets the register in the architecture state. It will also deal with
// updating Simulator internal state for special registers such as PC.
void Simulator::set_register(int reg, sreg_t value) {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  if (reg == pc) {
    pc_modified_ = true;
  }

  // Zero register always holds 0.
  registers_[reg] = (reg == 0) ? 0 : value;
}

void Simulator::set_fpu_register(int fpureg, int64_t value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  FPUregisters_[fpureg] = value;
}

void Simulator::set_fpu_register_word(int fpureg, int32_t value) {
  // Set ONLY lower 32-bits, leaving upper bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* pword;
  if (kArchEndian == kLittle) {
    pword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg]);
  } else {
    pword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg]) + 1;
  }
  *pword = value;
}

void Simulator::set_fpu_register_hi_word(int fpureg, int32_t value) {
  // Set ONLY upper 32-bits, leaving lower bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* phiword;
  if (kArchEndian == kLittle) {
    phiword = (reinterpret_cast<int32_t*>(&FPUregisters_[fpureg])) + 1;
  } else {
    phiword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg]);
  }
  *phiword = value;
}

void Simulator::set_fpu_register_float(int fpureg, float value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  FPUregisters_[fpureg] = box_float(value);
}

void Simulator::set_fpu_register_float(int fpureg, Float32 value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  Float64 t = Float64::FromBits(box_float(value.get_bits()));
  memcpy(&FPUregisters_[fpureg], &t, 8);
}

void Simulator::set_fpu_register_double(int fpureg, double value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  FPUregisters_[fpureg] = base::bit_cast<int64_t>(value);
}

void Simulator::set_fpu_register_double(int fpureg, Float64 value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  memcpy(&FPUregisters_[fpureg], &value, 8);
}
// Get the register from the architecture state. This function does handle
// the special case of accessing the PC register.
sreg_t Simulator::get_register(int reg) const {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  if (reg == 0)
    return 0;
  else
    return registers_[reg] + ((reg == pc) ? Instruction::kPCReadOffset : 0);
}

double Simulator::get_double_from_register_pair(int reg) {
  // TODO(plind): bad ABI stuff, refactor or remove.
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters) && ((reg % 2) == 0));

  double dm_val = 0.0;
  // Read the bits from the unsigned integer register_[] array
  // into the double precision floating point value and return it.
  char buffer[sizeof(registers_[0])];
  memcpy(buffer, &registers_[reg], sizeof(registers_[0]));
  memcpy(&dm_val, buffer, sizeof(registers_[0]));
  return (dm_val);
}

int64_t Simulator::get_fpu_register(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return FPUregisters_[fpureg];
}

int32_t Simulator::get_fpu_register_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_signed_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_hi_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>((FPUregisters_[fpureg] >> 32) & 0xFFFFFFFF);
}

float Simulator::get_fpu_register_float(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  if (!is_boxed_float(FPUregisters_[fpureg])) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return Float32::FromBits(FPUregisters_[fpureg] & 0xFFFF'FFFF).get_scalar();
}

// Fix NaN boxing error according to
// https://github.com/riscv/riscv-isa-manual/blob/main/src/d-st-ext.adoc#nan-boxing-of-narrower-values"
Float32 Simulator::get_fpu_register_Float32(int fpureg,
                                            bool check_nanbox) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  if (check_nanbox && !is_boxed_float(FPUregisters_[fpureg])) {
    std::cout << std::hex << FPUregisters_[fpureg] << std::endl;
    return Float32::FromBits(0x7fc00000);
  }
  return Float32::FromBits(FPUregisters_[fpureg] & 0xFFFF'FFFF);
}

double Simulator::get_fpu_register_double(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return base::bit_cast<double>(FPUregisters_[fpureg]);
}

Float64 Simulator::get_fpu_register_Float64(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return Float64::FromBits(FPUregisters_[fpureg]);
}

#ifdef CAN_USE_RVV_INSTRUCTIONS
__int128_t Simulator::get_vregister(int vreg) const {
  DCHECK((vreg >= 0) && (vreg < kNumVRegisters));
  return Vregister_[vreg];
}
#endif

// Runtime FP routines take up to two double arguments and zero
// or one integer arguments. All are constructed here,
// from fa0, fa1, and a0.
void Simulator::GetFpArgs(double* x, double* y, int32_t* z) {
  *x = get_fpu_register_double(fa0);
  *y = get_fpu_register_double(fa1);
  *z = static_cast<int32_t>(get_register(a0));
}

// The return value is in fa0.
void Simulator::SetFpResult(const double& result) {
  set_fpu_register_double(fa0, result);
}

// helper functions to read/write/set/clear CRC values/bits
uint32_t Simulator::read_csr_value(uint32_t csr) {
  switch (csr) {
    case csr_fflags:  // Floating-Point Accrued Exceptions (RW)
      return (FCSR_ & kFcsrFlagsMask);
    case csr_frm:  // Floating-Point Dynamic Rounding Mode (RW)
      return (FCSR_ & kFcsrFrmMask) >> kFcsrFrmShift;
    case csr_fcsr:  // Floating-Point Control and Status Register (RW)
      return (FCSR_ & kFcsrMask);
    default:
      UNIMPLEMENTED();
  }
}

uint32_t Simulator::get_dynamic_rounding_mode() {
  return read_csr_value(csr_frm);
}

void Simulator::write_csr_value(uint32_t csr, reg_t val) {
  uint32_t value = (uint32_t)val;
  switch (csr) {
    case csr_fflags:  // Floating-Point Accrued Exceptions (RW)
      DCHECK(value <= ((1 << kFcsrFlagsBits) - 1));
      FCSR_ = (FCSR_ & (~kFcsrFlagsMask)) | value;
      break;
    case csr_frm:  // Floating-Point Dynamic Rounding Mode (RW)
      DCHECK(value <= ((1 << kFcsrFrmBits) - 1));
      FCSR_ = (FCSR_ & (~kFcsrFrmMask)) | (value << kFcsrFrmShift);
      break;
    case csr_fcsr:  // Floating-Point Control and Status Register (RW)
      DCHECK(value <= ((1 << kFcsrBits) - 1));
      FCSR_ = (FCSR_ & (~kFcsrMask)) | value;
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::set_csr_bits(uint32_t csr, reg_t val) {
  uint32_t value = (uint32_t)val;
  switch (csr) {
    case csr_fflags:  // Floating-Point Accrued Exceptions (RW)
      DCHECK(value <= ((1 << kFcsrFlagsBits) - 1));
      FCSR_ = FCSR_ | value;
      break;
    case csr_frm:  // Floating-Point Dynamic Rounding Mode (RW)
      DCHECK(value <= ((1 << kFcsrFrmBits) - 1));
      FCSR_ = FCSR_ | (value << kFcsrFrmShift);
      break;
    case csr_fcsr:  // Floating-Point Control and Status Register (RW)
      DCHECK(value <= ((1 << kFcsrBits) - 1));
      FCSR_ = FCSR_ | value;
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::clear_csr_bits(uint32_t csr, reg_t val) {
  uint32_t value = (uint32_t)val;
  switch (csr) {
    case csr_fflags:  // Floating-Point Accrued Exceptions (RW)
      DCHECK(value <= ((1 << kFcsrFlagsBits) - 1));
      FCSR_ = FCSR_ & (~value);
      break;
    case csr_frm:  // Floating-Point Dynamic Rounding Mode (RW)
      DCHECK(value <= ((1 << kFcsrFrmBits) - 1));
      FCSR_ = FCSR_ & (~(value << kFcsrFrmShift));
      break;
    case csr_fcsr:  // Floating-Point Control and Status Register (RW)
      DCHECK(value <= ((1 << kFcsrBits) - 1));
      FCSR_ = FCSR_ & (~value);
      break;
    default:
      UNIMPLEMENTED();
  }
}

bool Simulator::test_fflags_bits(uint32_t mask) {
  return (FCSR_ & kFcsrFlagsMask & mask) != 0;
}

template <typename T>
T Simulator::FMaxMinHelper(T a, T b, MaxMinKind kind) {
  // set invalid bit for signaling nan
  if ((a == std::numeric_limits<T>::signaling_NaN()) ||
      (b == std::numeric_limits<T>::signaling_NaN())) {
    set_csr_bits(csr_fflags, kInvalidOperation);
  }

  T result = 0;
  if (std::isnan(a) && std::isnan(b)) {
    result = std::numeric_limits<float>::quiet_NaN();
  } else if (std::isnan(a)) {
    result = b;
  } else if (std::isnan(b)) {
    result = a;
  } else if (b == a) {  // Handle -0.0 == 0.0 case.
    if (kind == MaxMinKind::kMax) {
      result = std::signbit(b) ? a : b;
    } else {
      result = std::signbit(b) ? b : a;
    }
  } else {
    result = (kind == MaxMinKind::kMax) ? fmax(a, b) : fmin(a, b);
  }

  return result;
}

// Raw access to the PC register.
void Simulator::set_pc(sreg_t value) {
  pc_modified_ = true;
  registers_[pc] = value;
  DCHECK(has_bad_pc() || ((value % kInstrSize) == 0) ||
         ((value % kShortInstrSize) == 0));
}

bool Simulator::has_bad_pc() const {
  return ((registers_[pc] == bad_ra) || (registers_[pc] == end_sim_pc));
}

// Raw access to the PC register without the special adjustment when reading.
sreg_t Simulator::get_pc() const { return registers_[pc]; }

// The RISC-V spec leaves it open to the implementation on how to handle
// unaligned reads and writes. For now, we simply disallow unaligned reads but
// at some point, we may want to implement some other behavior.

// TODO(plind): refactor this messy debug code when we do unaligned access.
void Simulator::DieOrDebug() {
  if (v8_flags.riscv_trap_to_simulator_debugger) {
    RiscvDebugger dbg(this);
    dbg.Debug();
  } else {
    base::OS::Abort();
  }
}

#if V8_TARGET_ARCH_RISCV64
void Simulator::TraceRegWr(int64_t value, TraceType t) {
  if (v8_flags.trace_sim) {
    union {
      int64_t fmt_int64;
      int32_t fmt_int32[2];
      float fmt_float[2];
      double fmt_double;
    } v;
    v.fmt_int64 = value;

    switch (t) {
      case WORD:
        SNPrintF(trace_buf_,
                 "%016" REGIx_FORMAT "    (%" PRId64 ")    int32:%" PRId32
                 " uint32:%" PRIu32,
                 v.fmt_int64, icount_, v.fmt_int32[0], v.fmt_int32[0]);
        break;
      case DWORD:
        SNPrintF(trace_buf_,
                 "%016" REGIx_FORMAT "    (%" PRId64 ")    int64:%" REGId_FORMAT
                 " uint64:%" PRIu64,
                 value, icount_, value, value);
        break;
      case FLOAT:
        SNPrintF(trace_buf_, "%016" REGIx_FORMAT "    (%" PRId64 ")    flt:%e",
                 v.fmt_int64, icount_, v.fmt_float[0]);
        break;
      case DOUBLE:
        SNPrintF(trace_buf_, "%016" REGIx_FORMAT "    (%" PRId64 ")    dbl:%e",
                 v.fmt_int64, icount_, v.fmt_double);
        break;
      default:
        UNREACHABLE();
    }
  }
}

#elif V8_TARGET_ARCH_RISCV32
template <typename T>
void Simulator::TraceRegWr(T value, TraceType t) {
  if (v8_flags.trace_sim) {
    union {
      int32_t fmt_int32;
      float fmt_float;
      double fmt_double;
    } v;
    if (t != DOUBLE) {
      v.fmt_int32 = value;
    } else {
      DCHECK_EQ(sizeof(T), 8);
      v.fmt_double = value;
    }
    switch (t) {
      case WORD:
        SNPrintF(trace_buf_,
                 "%016" REGIx_FORMAT "    (%" PRId64 ")    int32:%" REGId_FORMAT
                 " uint32:%" PRIu32,
                 v.fmt_int32, icount_, v.fmt_int32, v.fmt_int32);
        break;
      case FLOAT:
        SNPrintF(trace_buf_, "%016" REGIx_FORMAT "    (%" PRId64 ")    flt:%e",
                 v.fmt_int32, icount_, v.fmt_float);
        break;
      case DOUBLE:
        SNPrintF(trace_buf_, "%016" PRIx64 "    (%" PRId64 ")    dbl:%e",
                 static_cast<int64_t>(v.fmt_double), icount_, v.fmt_double);
        break;
      default:
        UNREACHABLE();
    }
  }
}
#endif

// TODO(plind): consider making icount_ printing a flag option.
template <typename T>
void Simulator::TraceMemRd(sreg_t addr, T value, sreg_t reg_value) {
  if (v8_flags.trace_sim) {
    if (std::is_integral<T>::value) {
      switch (sizeof(T)) {
        case 1:
          SNPrintF(trace_buf_,
                   "%016" REGIx_FORMAT "    (%" PRId64 ")    int8:%" PRId8
                   " uint8:%" PRIu8 " <-- [addr: %" REGIx_FORMAT "]",
                   reg_value, icount_, static_cast<int8_t>(value),
                   static_cast<uint8_t>(value), addr);
          break;
        case 2:
          SNPrintF(trace_buf_,
                   "%016" REGIx_FORMAT "    (%" PRId64 ")    int16:%" PRId16
                   " uint16:%" PRIu16 " <-- [addr: %" REGIx_FORMAT "]",
                   reg_value, icount_, static_cast<int16_t>(value),
                   static_cast<uint16_t>(value), addr);
          break;
        case 4:
          SNPrintF(trace_buf_,
                   "%016" REGIx_FORMAT "    (%" PRId64 ")    int32:%" PRId32
                   " uint32:%" PRIu32 " <-- [addr: %" REGIx_FORMAT "]",
                   reg_value, icount_, static_cast<int32_t>(value),
                   static_cast<uint32_t>(value), addr);
          break;
        case 8:
          SNPrintF(trace_buf_,
                   "%016" REGIx_FORMAT "    (%" PRId64 ")    int64:%" PRId64
                   " uint64:%" PRIu64 " <-- [addr: %" REGIx_FORMAT "]",
                   reg_value, icount_, static_cast<int64_t>(value),
                   static_cast<uint64_t>(value), addr);
          break;
        default:
          UNREACHABLE();
      }
    } else if (std::is_same<float, T>::value) {
      SNPrintF(trace_buf_,
               "%016" REGIx_FORMAT "    (%" PRId64
               ")    flt:%e <-- [addr: %" REGIx_FORMAT "]",
               reg_value, icount_, static_cast<float>(value), addr);
    } else if (std::is_same<double, T>::value) {
      SNPrintF(trace_buf_,
               "%016" REGIx_FORMAT "    (%" PRId64
               ")    dbl:%e <-- [addr: %" REGIx_FORMAT "]",
               reg_value, icount_, static_cast<double>(value), addr);
    } else {
      UNREACHABLE();
    }
  }
}

void Simulator::TraceMemRdFloat(sreg_t addr, Float32 value, int64_t reg_value) {
  if (v8_flags.trace_sim) {
    SNPrintF(trace_buf_,
             "%016" PRIx64 "    (%" PRId64
             ")    flt:%e <-- [addr: %" REGIx_FORMAT "]",
             reg_value, icount_, static_cast<float>(value.get_scalar()), addr);
  }
}

void Simulator::TraceMemRdDouble(sreg_t addr, double value, int64_t reg_value) {
  if (v8_flags.trace_sim) {
    SNPrintF(trace_buf_,
             "%016" PRIx64 "    (%" PRId64
             ")    dbl:%e <-- [addr: %" REGIx_FORMAT "]",
             reg_value, icount_, static_cast<double>(value), addr);
  }
}

void Simulator::TraceMemRdDouble(sreg_t addr, Float64 value,
                                 int64_t reg_value) {
  if (v8_flags.trace_sim) {
    SNPrintF(trace_buf_,
             "%016" PRIx64 "    (%" PRId64
             ")    dbl:%e <-- [addr: %" REGIx_FORMAT "]",
             reg_value, icount_, static_cast<double>(value.get_scalar()), addr);
  }
}

template <typename T>
void Simulator::TraceMemWr(sreg_t addr, T value) {
  if (v8_flags.trace_sim) {
    switch (sizeof(T)) {
      case 1:
        SNPrintF(trace_buf_,
                 "                    (%" PRIu64 ")    int8:%" PRId8
                 " uint8:%" PRIu8 " --> [addr: %" REGIx_FORMAT "]",
                 icount_, static_cast<int8_t>(value),
                 static_cast<uint8_t>(value), addr);
        break;
      case 2:
        SNPrintF(trace_buf_,
                 "                    (%" PRIu64 ")    int16:%" PRId16
                 " uint16:%" PRIu16 " --> [addr: %" REGIx_FORMAT "]",
                 icount_, static_cast<int16_t>(value),
                 static_cast<uint16_t>(value), addr);
        break;
      case 4:
        if (std::is_integral<T>::value) {
          SNPrintF(trace_buf_,
                   "                    (%" PRIu64 ")    int32:%" PRId32
                   " uint32:%" PRIu32 " --> [addr: %" REGIx_FORMAT "]",
                   icount_, static_cast<int32_t>(value),
                   static_cast<uint32_t>(value), addr);
        } else {
          SNPrintF(trace_buf_,
                   "                    (%" PRIu64
                   ")    flt:%e bit:%x --> [addr: %" REGIx_FORMAT "]",
                   icount_, static_cast<float>(value),
                   base::bit_cast<int32_t, float>(value), addr);
        }
        break;
      case 8:
        if (std::is_integral<T>::value) {
          SNPrintF(trace_buf_,
                   "                    (%" PRIu64 ")    int64:%" PRId64
                   " uint64:%" PRIu64 " --> [addr: %" REGIx_FORMAT "]",
                   icount_, static_cast<int64_t>(value),
                   static_cast<uint64_t>(value), addr);
        } else {
          SNPrintF(trace_buf_,
                   "                    (%" PRIu64 ")    dbl:%e bit:%" PRIx64
                   " --> [addr: %" REGIx_FORMAT "]",
                   icount_, static_cast<double>(value),
                   base::bit_cast<int64_t, double>(value), addr);
        }
        break;
      default:
        UNREACHABLE();
    }
  }
}

void Simulator::TraceMemWrDouble(sreg_t addr, double value) {
  if (v8_flags.trace_sim) {
    SNPrintF(trace_buf_,
             "                    (%" PRIu64 ")    dbl:%e bit:%" PRIx64
             "--> [addr: %" REGIx_FORMAT "]",
             icount_, value, base::bit_cast<int64_t, double>(value), addr);
  }
}
// RISCV Memory Read/Write functions

bool Simulator::ProbeMemory(uintptr_t address, uintptr_t access_size) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  uintptr_t last_accessed_byte = address + access_size - 1;
  uintptr_t current_pc = registers_[pc];
  uintptr_t landing_pad =
      trap_handler::ProbeMemory(last_accessed_byte, current_pc);
  if (!landing_pad) return true;
  set_pc(landing_pad);
  set_register(kWasmTrapHandlerFaultAddressRegister.code(), current_pc);
  return false;
#else
  return true;
#endif
}

// TODO(RISCV): check whether the specific board supports unaligned load/store
// (determined by EEI). For now, we assume the board does not support unaligned
// load/store (e.g., trapping)
template <typename T>
T Simulator::ReadMem(sreg_t addr, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" REGIx_FORMAT
           " , pc=0x%08" PRIxPTR " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#if !defined(V8_COMPRESS_POINTERS) && defined(RISCV_HAS_NO_UNALIGNED)
  // check for natural alignment
  if (!v8_flags.riscv_c_extension && ((addr & (sizeof(T) - 1)) != 0)) {
    PrintF("Unaligned read at 0x%08" REGIx_FORMAT " , pc=0x%08" V8PRIxPTR "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#endif
  T* ptr = reinterpret_cast<T*>(addr);
  T value = *ptr;
  return value;
}

template <typename T>
void Simulator::WriteMem(sreg_t addr, T value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" REGIx_FORMAT
           " , pc=0x%08" PRIxPTR " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#if !defined(V8_COMPRESS_POINTERS) && defined(RISCV_HAS_NO_UNALIGNED)
  // check for natural alignment
  if (!v8_flags.riscv_c_extension && ((addr & (sizeof(T) - 1)) != 0)) {
    PrintF("Unaligned write at 0x%08" REGIx_FORMAT " , pc=0x%08" V8PRIxPTR "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#endif
  T* ptr = reinterpret_cast<T*>(addr);
  if (!std::is_same<double, T>::value) {
    TraceMemWr(addr, value);
  } else {
    TraceMemWrDouble(addr, value);
  }
  *ptr = value;
}

template <>
void Simulator::WriteMem(sreg_t addr, Float32 value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" REGIx_FORMAT
           " , pc=0x%08" PRIxPTR " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#if !defined(V8_COMPRESS_POINTERS) && defined(RISCV_HAS_NO_UNALIGNED)
  // check for natural alignment
  if (!v8_flags.riscv_c_extension && ((addr & (sizeof(T) - 1)) != 0)) {
    PrintF("Unaligned write at 0x%08" REGIx_FORMAT " , pc=0x%08" V8PRIxPTR "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#endif
  float* ptr = reinterpret_cast<float*>(addr);
  TraceMemWr(addr, value.get_scalar());
  memcpy(ptr, &value, 4);
}

template <>
void Simulator::WriteMem(sreg_t addr, Float64 value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" REGIx_FORMAT
           " , pc=0x%08" PRIxPTR " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#if !defined(V8_COMPRESS_POINTERS) && defined(RISCV_HAS_NO_UNALIGNED)
  // check for natural alignment
  if (!v8_flags.riscv_c_extension && ((addr & (sizeof(T) - 1)) != 0)) {
    PrintF("Unaligned write at 0x%08" REGIx_FORMAT " , pc=0x%08" V8PRIxPTR "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
#endif
  double* ptr = reinterpret_cast<double*>(addr);
  TraceMemWrDouble(addr, value.get_scalar());
  memcpy(ptr, &value, 8);
}

// Returns the limit of the stack area to enable checking for stack overflows.
uintptr_t Simulator::StackLimit(uintptr_t c_limit) const {
  // The simulator uses a separate JS stack. If we have exhausted the C stack,
  // we also drop down the JS limit to reflect the exhaustion on the JS stack.
  if (GetCurrentStackPosition() < c_limit) {
    return reinterpret_cast<uintptr_t>(get_sp());
  }

  // Otherwise the limit is the JS stack. Leave a safety margin to prevent
  // overrunning the stack when pushing values.
  return reinterpret_cast<uintptr_t>(stack_) + kStackProtectionSize;
}

base::Vector<uint8_t> Simulator::GetCurrentStackView() const {
  // We do not add an additional safety margin as above in
  // Simulator::StackLimit, as this is currently only used in wasm::StackMemory,
  // which adds its own margin.
  return base::VectorOf(stack_, UsableStackSize());
}

// Unsupported instructions use Format to print an error and stop execution.
void Simulator::Format(Instruction* instr, const char* format) {
  PrintF("Simulator found unsupported instruction:\n 0x%08" PRIxPTR " : %s\n",
         reinterpret_cast<intptr_t>(instr), format);
  UNIMPLEMENTED_RISCV();
}

// Calls into the V8 runtime are based on this very simple interface.
// Note: To be able to return two values from some calls the code in
// runtime.cc uses the ObjectPair which is essentially two 32-bit values
// stuffed into a 64-bit value. With the code below we assume that all runtime
// calls return 64 bits of result. If they don't, the a1 result register
// contains a bogus value, which is fine because it is caller-saved.
#if V8_TARGET_ARCH_RISCV64
using SimulatorRuntimeCall = ObjectPair (*)(
#elif V8_TARGET_ARCH_RISCV32
using SimulatorRuntimeCall = int64_t (*)(
#endif
    sreg_t arg0, sreg_t arg1, sreg_t arg2, sreg_t arg3, sreg_t arg4,
    sreg_t arg5, sreg_t arg6, sreg_t arg7, sreg_t arg8, sreg_t arg9,
    sreg_t arg10, sreg_t arg11, sreg_t arg12, sreg_t arg13, sreg_t arg14,
    sreg_t arg15, sreg_t arg16, sreg_t arg17, sreg_t arg18, sreg_t arg19);

// These prototypes handle the four types of FP calls.
using SimulatorRuntimeCompareCall = int64_t (*)(double darg0, double darg1);
using SimulatorRuntimeFPFPCall = double (*)(double darg0, double darg1);
using SimulatorRuntimeFPCall = double (*)(double darg0);
using SimulatorRuntimeFPIntCall = double (*)(double darg0, int32_t arg0);

// This signature supports direct call in to API function native callback
// (refer to InvocationCallback in v8.h).
using SimulatorRuntimeDirectApiCall = void (*)(sreg_t arg0);

// This signature supports direct call to accessor getter callback.
using SimulatorRuntimeDirectGetterCall = void (*)(sreg_t arg0, sreg_t arg1);

// Define four args for future flexibility; at the time of this writing only
// one is ever used.
using SimulatorRuntimeFPTaggedCall = double (*)(int64_t arg0, int64_t arg1,
                                                int64_t arg2, int64_t arg3);

#ifdef V8_TARGET_ARCH_RISCV64
using MixedRuntimeCall_0 = AnyCType (*)();
#define BRACKETS(ident, N) ident[N]
#define REP_0(expr, FMT)
#define REP_1(expr, FMT) FMT(expr, 0)
#define REP_2(expr, FMT) REP_1(expr, FMT), FMT(expr, 1)
#define REP_3(expr, FMT) REP_2(expr, FMT), FMT(expr, 2)
#define REP_4(expr, FMT) REP_3(expr, FMT), FMT(expr, 3)
#define REP_5(expr, FMT) REP_4(expr, FMT), FMT(expr, 4)
#define REP_6(expr, FMT) REP_5(expr, FMT), FMT(expr, 5)
#define REP_7(expr, FMT) REP_6(expr, FMT), FMT(expr, 6)
#define REP_8(expr, FMT) REP_7(expr, FMT), FMT(expr, 7)
#define REP_9(expr, FMT) REP_8(expr, FMT), FMT(expr, 8)
#define REP_10(expr, FMT) REP_9(expr, FMT), FMT(expr, 9)
#define REP_11(expr, FMT) REP_10(expr, FMT), FMT(expr, 10)
#define REP_12(expr, FMT) REP_11(expr, FMT), FMT(expr, 11)
#define REP_13(expr, FMT) REP_12(expr, FMT), FMT(expr, 12)
#define REP_14(expr, FMT) REP_13(expr, FMT), FMT(expr, 13)
#define REP_15(expr, FMT) REP_14(expr, FMT), FMT(expr, 14)
#define REP_16(expr, FMT) REP_15(expr, FMT), FMT(expr, 15)
#define REP_17(expr, FMT) REP_16(expr, FMT), FMT(expr, 16)
#define REP_18(expr, FMT) REP_17(expr, FMT), FMT(expr, 17)
#define REP_19(expr, FMT) REP_18(expr, FMT), FMT(expr, 18)
#define REP_20(expr, FMT) REP_19(expr, FMT), FMT(expr, 19)
#define GEN_MAX_PARAM_COUNT(V) \
  V(0)                         \
  V(1)                         \
  V(2)                         \
  V(3)                         \
  V(4)                         \
  V(5)                         \
  V(6)                         \
  V(7)                         \
  V(8)                         \
  V(9)                         \
  V(10)                        \
  V(11)                        \
  V(12)                        \
  V(13)                        \
  V(14)                        \
  V(15)                        \
  V(16)                        \
  V(17)                        \
  V(18)                        \
  V(19)                        \
  V(20)
#define MIXED_RUNTIME_CALL(N) \
  using MixedRuntimeCall_##N = AnyCType (*)(REP_##N(AnyCType arg, CONCAT));
GEN_MAX_PARAM_COUNT(MIXED_RUNTIME_CALL)
#undef MIXED_RUNTIME_CALL
#define CALL_ARGS(N) REP_##N(args, BRACKETS)
#define CALL_TARGET_VARARG(N)                                   \
  if (signature.ParameterCount() == N) { /* NOLINT */           \
    MixedRuntimeCall_##N target =                               \
        reinterpret_cast<MixedRuntimeCall_##N>(target_address); \
    result = target(CALL_ARGS(N));                              \
  } else /* NOLINT */
#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define RETURN_REGISTER a0
#define FP_PARAM_REGISTERS fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7
#define FP_RETURN_REGISTER fa0
void Simulator::CallAnyCTypeFunction(Address target_address,
                                     const EncodedCSignature& signature) {
  const int64_t* stack_pointer = reinterpret_cast<int64_t*>(get_register(sp));
  const double* double_stack_pointer =
      reinterpret_cast<double*>(get_register(sp));
  const Register kParamRegisters[] = {PARAM_REGISTERS};
  const FPURegister kFPParamRegisters[] = {FP_PARAM_REGISTERS};
  CHECK_LE(signature.ParameterCount(), kMaxCParameters);
  static_assert(sizeof(AnyCType) == 8, "AnyCType is assumed to be 64-bit.");
  AnyCType args[kMaxCParameters];
  int num_gp_params = 0, num_fp_params = 0, num_stack_params = 0;
  for (int i = 0; i < signature.ParameterCount(); ++i) {
    if (signature.IsFloat(i)) {
      if (num_fp_params < 8) {
        args[i].double_value =
            get_fpu_register_double(kFPParamRegisters[num_fp_params++]);
      } else {
        args[i].double_value = double_stack_pointer[num_stack_params++];
      }
    } else {
      if (num_gp_params < 8) {
        args[i].int64_value = get_register(kParamRegisters[num_gp_params++]);
      } else {
        args[i].int64_value = stack_pointer[num_stack_params++];
      }
    }
  }
  AnyCType result;
  GEN_MAX_PARAM_COUNT(CALL_TARGET_VARARG)
  /* else */ {
    UNREACHABLE();
  }
  static_assert(20 == kMaxCParameters,
                "If you've changed kMaxCParameters, please change the "
                "GEN_MAX_PARAM_COUNT macro.");
  if (v8_flags.trace_sim) {
    printf("CallAnyCTypeFunction end result \n");
  }
#undef CALL_TARGET_VARARG
#undef CALL_ARGS
#undef GEN_MAX_PARAM_COUNT
  if (signature.IsReturnFloat()) {
    if (signature.IsReturnFloat64()) {
      set_fpu_register_double(FP_RETURN_REGISTER, result.double_value);
    } else {
      set_fpu_register_float(FP_RETURN_REGISTER, result.float_value);
    }
  } else {
    set_register(RETURN_REGISTER, result.int64_value);
  }
}
#undef PARAM_REGISTERS
#undef RETURN_REGISTER
#undef FP_PARAM_REGISTERS
#undef FP_RETURN_REGISTER
#endif  // V8_TARGET_ARCH_RISCV64

// Software interrupt instructions are used by the simulator to call into the
// C-based V8 runtime. They are also used for debugging with simulator.
void Simulator::SoftwareInterrupt() {
  // There are two instructions that could get us here, the ebreak or ecall
  // instructions are "SYSTEM" class opcode distinuished by Imm12Value field w/
  // the rest of instruction fields being zero
  int32_t func = instr_.Imm12Value();
  // We first check if we met a call_rt_redirected.
  if (instr_.InstructionBits() == rtCallRedirInstr) {  // ECALL
    Redirection* redirection = Redirection::FromInstruction(instr_.instr());

    // This is dodgy but it works because the C entry stubs are never moved.
    int64_t saved_ra = get_register(ra);
    intptr_t external =
        reinterpret_cast<intptr_t>(redirection->external_function());
#ifdef V8_TARGET_ARCH_RISCV64
    Address func_addr =
        reinterpret_cast<Address>(redirection->external_function());
    SimulatorData* simulator_data = isolate_->simulator_data();
    DCHECK_NOT_NULL(simulator_data);
    const EncodedCSignature& signature =
        simulator_data->GetSignatureForTarget(func_addr);
    if (signature.IsValid()) {
      CHECK_EQ(redirection->type(), ExternalReference::FAST_C_CALL);
      CallAnyCTypeFunction(external, signature);
      set_register(ra, saved_ra);
      set_pc(get_register(ra));
      return;
    }
#endif

    sreg_t* stack_pointer = reinterpret_cast<sreg_t*>(get_register(sp));

    const sreg_t arg0 = get_register(a0);
    const sreg_t arg1 = get_register(a1);
    const sreg_t arg2 = get_register(a2);
    const sreg_t arg3 = get_register(a3);
    const sreg_t arg4 = get_register(a4);
    const sreg_t arg5 = get_register(a5);
    const sreg_t arg6 = get_register(a6);
    const sreg_t arg7 = get_register(a7);
    const sreg_t arg8 = stack_pointer[0];
    const sreg_t arg9 = stack_pointer[1];
    const sreg_t arg10 = stack_pointer[2];
    const sreg_t arg11 = stack_pointer[3];
    const sreg_t arg12 = stack_pointer[4];
    const sreg_t arg13 = stack_pointer[5];
    const sreg_t arg14 = stack_pointer[6];
    const sreg_t arg15 = stack_pointer[7];
    const sreg_t arg16 = stack_pointer[8];
    const sreg_t arg17 = stack_pointer[9];
    const sreg_t arg18 = stack_pointer[10];
    const sreg_t arg19 = stack_pointer[11];
    static_assert(kMaxCParameters == 20);

    bool fp_call =
        (redirection->type() == ExternalReference::BUILTIN_FP_FP_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_COMPARE_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_FP_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_FP_INT_CALL);

    sreg_t pc = get_pc();

    if (fp_call) {
      double dval0, dval1;  // one or two double parameters
      int32_t ival;         // zero or one integer parameters
      int64_t iresult = 0;  // integer return value
      double dresult = 0;   // double return value
      GetFpArgs(&dval0, &dval1, &ival);
      SimulatorRuntimeCall generic_target =
          reinterpret_cast<SimulatorRuntimeCall>(external);
      if (v8_flags.trace_sim) {
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_FP_FP_CALL:
          case ExternalReference::BUILTIN_COMPARE_CALL:
            PrintF("Call to host function %s at %p with args %f, %f",
                   ExternalReferenceTable::NameOfIsolateIndependentAddress(
                       pc, IsolateGroup::current()->external_ref_table()),
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0, dval1);
            break;
          case ExternalReference::BUILTIN_FP_CALL:
            PrintF("Call to host function %s at %p with arg %f",
                   ExternalReferenceTable::NameOfIsolateIndependentAddress(
                       pc, IsolateGroup::current()->external_ref_table()),
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0);
            break;
          case ExternalReference::BUILTIN_FP_INT_CALL:
            PrintF("Call to host function %s at %p with args %f, %d",
                   ExternalReferenceTable::NameOfIsolateIndependentAddress(
                       pc, IsolateGroup::current()->external_ref_table()),
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0, ival);
            break;
          default:
            UNREACHABLE();
        }
      }
      switch (redirection->type()) {
        case ExternalReference::BUILTIN_COMPARE_CALL: {
          SimulatorRuntimeCompareCall target =
              reinterpret_cast<SimulatorRuntimeCompareCall>(external);
          iresult = target(dval0, dval1);
          set_register(a0, static_cast<sreg_t>(iresult));
          //  set_register(a1, static_cast<int64_t>(iresult >> 32));
          break;
        }
        case ExternalReference::BUILTIN_FP_FP_CALL: {
          SimulatorRuntimeFPFPCall target =
              reinterpret_cast<SimulatorRuntimeFPFPCall>(external);
          dresult = target(dval0, dval1);
          SetFpResult(dresult);
          break;
        }
        case ExternalReference::BUILTIN_FP_CALL: {
          SimulatorRuntimeFPCall target =
              reinterpret_cast<SimulatorRuntimeFPCall>(external);
          dresult = target(dval0);
          SetFpResult(dresult);
          break;
        }
        case ExternalReference::BUILTIN_FP_INT_CALL: {
          SimulatorRuntimeFPIntCall target =
              reinterpret_cast<SimulatorRuntimeFPIntCall>(external);
          dresult = target(dval0, ival);
          SetFpResult(dresult);
          break;
        }
        default:
          UNREACHABLE();
      }
      if (v8_flags.trace_sim) {
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_COMPARE_CALL:
            PrintF("Returned %08x\n", static_cast<int32_t>(iresult));
            break;
          case ExternalReference::BUILTIN_FP_FP_CALL:
          case ExternalReference::BUILTIN_FP_CALL:
          case ExternalReference::BUILTIN_FP_INT_CALL:
            PrintF("Returned %f\n", dresult);
            break;
          default:
            UNREACHABLE();
        }
      }
    } else if (redirection->type() ==
               ExternalReference::BUILTIN_FP_POINTER_CALL) {
      if (v8_flags.trace_sim) {
        PrintF("Call to host function at %p args %08" REGIx_FORMAT " \n",
               reinterpret_cast<void*>(external), arg0);
      }
      SimulatorRuntimeFPTaggedCall target =
          reinterpret_cast<SimulatorRuntimeFPTaggedCall>(external);
      double dresult = target(arg0, arg1, arg2, arg3);
      SetFpResult(dresult);
      if (v8_flags.trace_sim) {
        PrintF("Returned %f\n", dresult);
      }
    } else if (redirection->type() == ExternalReference::DIRECT_API_CALL) {
      // See callers of MacroAssembler::CallApiFunctionAndReturn for
      // explanation of register usage.
      // void f(v8::FunctionCallbackInfo&)
      if (v8_flags.trace_sim) {
        PrintF("Call to host function %s at %p args %08" REGIx_FORMAT " \n",
               ExternalReferenceTable::NameOfIsolateIndependentAddress(
                   pc, IsolateGroup::current()->external_ref_table()),
               reinterpret_cast<void*>(external), arg0);
      }
      SimulatorRuntimeDirectApiCall target =
          reinterpret_cast<SimulatorRuntimeDirectApiCall>(external);
      target(arg0);
    } else if (redirection->type() == ExternalReference::DIRECT_GETTER_CALL) {
      // See callers of MacroAssembler::CallApiFunctionAndReturn for
      // explanation of register usage.
      // void f(v8::Local<String> property, v8::PropertyCallbackInfo& info)
      if (v8_flags.trace_sim) {
        PrintF("Call to host function at %p args %08" REGIx_FORMAT
               "  %08" REGIx_FORMAT " \n",
               reinterpret_cast<void*>(external), arg0, arg1);
      }
      SimulatorRuntimeDirectGetterCall target =
          reinterpret_cast<SimulatorRuntimeDirectGetterCall>(external);
      target(arg0, arg1);
    } else {
#ifdef V8_TARGET_ARCH_RISCV64
      DCHECK(redirection->type() == ExternalReference::BUILTIN_CALL ||
             redirection->type() == ExternalReference::BUILTIN_CALL_PAIR);
#else   // V8_TARGET_ARCH_RISCV32
        //  FAST_C_CALL is temporarily handled here as well, because we lack
        //  proper support for direct C calls with FP params in the simulator.
        //  The generic BUILTIN_CALL path assumes all parameters are passed in
        //  the GP registers, thus supporting calling the slow callback without
        //  crashing. The reason for that is that in the mjsunit tests we check
        //  the `fast_c_api.supports_fp_params` (which is false on non-simulator
        //  builds for arm/arm64), thus we expect that the slow path will be
        //  called. And since the slow path passes the arguments as a `const
        //  FunctionCallbackInfo<Value>&` (which is a GP argument), the call is
        //  made correctly.
      DCHECK(redirection->type() == ExternalReference::BUILTIN_CALL ||
             redirection->type() == ExternalReference::BUILTIN_CALL_PAIR ||
             redirection->type() == ExternalReference::FAST_C_CALL);
#endif  // V8_TARGET_ARCH_RISCV64
      SimulatorRuntimeCall target =
          reinterpret_cast<SimulatorRuntimeCall>(external);
      if (v8_flags.trace_sim) {
        PrintF(
            "Call to host function %s at %p "
            "args %08" REGIx_FORMAT " , %08" REGIx_FORMAT " , %08" REGIx_FORMAT
            " , %08" REGIx_FORMAT " , %08" REGIx_FORMAT " , %08" REGIx_FORMAT
            " , %08" REGIx_FORMAT " , %08" REGIx_FORMAT " , %08" REGIx_FORMAT
            " , %08" REGIx_FORMAT " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT
            " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT
            " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT
            " , %016" REGIx_FORMAT " , %016" REGIx_FORMAT " \n",
            ExternalReferenceTable::NameOfIsolateIndependentAddress(
                pc, IsolateGroup::current()->external_ref_table()),
            reinterpret_cast<void*>(FUNCTION_ADDR(target)), arg0, arg1, arg2,
            arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12,
            arg13, arg14, arg15, arg16, arg17, arg18, arg19);
      }
#if V8_TARGET_ARCH_RISCV64
      ObjectPair result = target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                                 arg8, arg9, arg10, arg11, arg12, arg13, arg14,
                                 arg15, arg16, arg17, arg18, arg19);
      set_register(a0, (sreg_t)(result.x));
      set_register(a1, (sreg_t)(result.y));

#elif V8_TARGET_ARCH_RISCV32
      int64_t result = target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                              arg8, arg9, arg10, arg11, arg12, arg13, arg14,
                              arg15, arg16, arg17, arg18, arg19);
      set_register(a0, (sreg_t)result);
      set_register(a1, (sreg_t)(result >> 32));
#endif
    }
    if (v8_flags.trace_sim) {
      PrintF("Returned %08" REGIx_FORMAT "  : %08" REGIx_FORMAT " \n",
             get_register(a1), get_register(a0));
    }
    set_register(ra, saved_ra);
    set_pc(get_register(ra));

  } else if (func == 1) {  // EBREAK
    int32_t code = get_ebreak_code(instr_.instr());
    set_pc(get_pc() + kInstrSize * 2);
    if (code != -1 && static_cast<uint32_t>(code) <= kMaxStopCode) {
      if (IsWatchpoint(code)) {
        PrintWatchpoint(code);
      } else if (IsTracepoint(code)) {
        if (!v8_flags.debug_sim) {
          PrintF("Add --debug-sim when tracepoint instruction is used.\n");
          abort();
        }
        printf("%d %d %d %d\n", code, code & LOG_TRACE, code & LOG_REGS,
               code & kDebuggerTracingDirectivesMask);
        switch (code & kDebuggerTracingDirectivesMask) {
          case TRACE_ENABLE:
            if (code & LOG_TRACE) {
              v8_flags.trace_sim = true;
            }
            if (code & LOG_REGS) {
              RiscvDebugger dbg(this);
              dbg.PrintAllRegs();
            }
            break;
          case TRACE_DISABLE:
            if (code & LOG_TRACE) {
              v8_flags.trace_sim = false;
            }
            break;
          default:
            UNREACHABLE();
        }
      } else {
        IncreaseStopCounter(code);
        HandleStop(code);
      }
    } else {
      // All remaining break_ codes, and all traps are handled here.
      RiscvDebugger dbg(this);
      dbg.Debug();
    }
  } else {
    UNREACHABLE();
  }
}

// Stop helper functions.
bool Simulator::IsWatchpoint(reg_t code) {
  return (code <= kMaxWatchpointCode);
}

bool Simulator::IsTracepoint(reg_t code) {
  return (code <= kMaxTracepointCode && code > kMaxWatchpointCode);
}

void Simulator::PrintWatchpoint(reg_t code) {
  RiscvDebugger dbg(this);
  ++break_count_;
  PrintF("\n---- watchpoint %" REGId_FORMAT
         "  marker: %3d  (instr count: %8" PRId64
         " ) ----------"
         "----------------------------------",
         code, break_count_, icount_);
  dbg.PrintAllRegs();  // Print registers and continue running.
}

void Simulator::HandleStop(reg_t code) {
  // Stop if it is enabled, otherwise go on jumping over the stop
  // and the message address.
  if (IsEnabledStop(code)) {
    PrintF("Simulator hit stop (%" REGId_FORMAT ")\n", code);
    DieOrDebug();
  }
}

bool Simulator::IsStopInstruction(Instruction* instr) {
  if (instr->InstructionBits() != kBreakInstr) return false;
  int32_t code = get_ebreak_code(instr);
  return code != -1 && static_cast<uint32_t>(code) > kMaxWatchpointCode &&
         static_cast<uint32_t>(code) <= kMaxStopCode;
}

bool Simulator::IsEnabledStop(reg_t code) {
  DCHECK_LE(code, kMaxStopCode);
  DCHECK_GT(code, kMaxWatchpointCode);
  return !(watched_stops_[code].count & kStopDisabledBit);
}

void Simulator::EnableStop(reg_t code) {
  if (!IsEnabledStop(code)) {
    watched_stops_[code].count &= ~kStopDisabledBit;
  }
}

void Simulator::DisableStop(reg_t code) {
  if (IsEnabledStop(code)) {
    watched_stops_[code].count |= kStopDisabledBit;
  }
}

void Simulator::IncreaseStopCounter(reg_t code) {
  DCHECK_LE(code, kMaxStopCode);
  if ((watched_stops_[code].count & ~(1 << 31)) == 0x7FFFFFFF) {
    PrintF("Stop counter for code %" REGId_FORMAT
           "  has overflowed.\n"
           "Enabling this code and reseting the counter to 0.\n",
           code);
    watched_stops_[code].count = 0;
    EnableStop(code);
  } else {
    watched_stops_[code].count++;
  }
}

// Print a stop status.
void Simulator::PrintStopInfo(reg_t code) {
  if (code <= kMaxWatchpointCode) {
    PrintF("That is a watchpoint, not a stop.\n");
    return;
  } else if (code > kMaxStopCode) {
    PrintF("Code too large, only %u stops can be used\n", kMaxStopCode + 1);
    return;
  }
  const char* state = IsEnabledStop(code) ? "Enabled" : "Disabled";
  int32_t count = watched_stops_[code].count & ~kStopDisabledBit;
  // Don't print the state of unused breakpoints.
  if (count != 0) {
    if (watched_stops_[code].desc) {
      PrintF("stop %" REGId_FORMAT "  - 0x%" REGIx_FORMAT
             " : \t%s, \tcounter = %i, \t%s\n",
             code, code, state, count, watched_stops_[code].desc);
    } else {
      PrintF("stop %" REGId_FORMAT "  - 0x%" REGIx_FORMAT
             " : \t%s, \tcounter = %i\n",
             code, code, state, count);
    }
  }
}

void Simulator::SignalException(Exception e) {
  FATAL("Error: Exception %i raised.", static_cast<int>(e));
}

// RISCV Instruction Decode Routine
void Simulator::DecodeRVRType() {
  switch (instr_.InstructionBits() & kRTypeMask) {
    case RO_ADD: {
      set_rd(sext_xlen(rs1() + rs2()));
      break;
    }
    case RO_SUB: {
      set_rd(sext_xlen(rs1() - rs2()));
      break;
    }
    case RO_SLL: {
      set_rd(sext_xlen(rs1() << (rs2() & (xlen - 1))));
      break;
    }
    case RO_SLT: {
      set_rd(sreg_t(rs1()) < sreg_t(rs2()));
      break;
    }
    case RO_SLTU: {
      set_rd(reg_t(rs1()) < reg_t(rs2()));
      break;
    }
    case RO_XOR: {
      set_rd(rs1() ^ rs2());
      break;
    }
    case RO_SRL: {
      set_rd(sext_xlen(zext_xlen(rs1()) >> (rs2() & (xlen - 1))));
      break;
    }
    case RO_SRA: {
      set_rd(sext_xlen(sext_xlen(rs1()) >> (rs2() & (xlen - 1))));
      break;
    }
    case RO_OR: {
      set_rd(rs1() | rs2());
      break;
    }
    case RO_AND: {
      set_rd(rs1() & rs2());
      break;
    }
    case RO_ANDN:
      set_rd(rs1() & ~rs2());
      break;
    case RO_ORN:
      set_rd(rs1() | (~rs2()));
      break;
    case RO_XNOR:
      set_rd((~rs1()) ^ (~rs2()));
      break;
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_ADDW: {
      set_rd(sext32(rs1() + rs2()));
      break;
    }
    case RO_ADDUW:
      set_rd(zext32(rs1()) + rs2());
      break;
    case RO_SUBW: {
      set_rd(sext32(rs1() - rs2()));
      break;
    }
    case RO_SLLW: {
      set_rd(sext32(rs1() << (rs2() & 0x1F)));
      break;
    }
    case RO_SRLW: {
      set_rd(sext32(uint32_t(rs1()) >> (rs2() & 0x1F)));
      break;
    }
    case RO_SRAW: {
      set_rd(sext32(int32_t(rs1()) >> (rs2() & 0x1F)));
      break;
    }
    case RO_SH1ADDUW: {
      set_rd(rs2() + (zext32(rs1()) << 1));
      break;
    }
    case RO_SH2ADDUW: {
      set_rd(rs2() + (zext32(rs1()) << 2));
      break;
    }
    case RO_SH3ADDUW: {
      set_rd(rs2() + (zext32(rs1()) << 3));
      break;
    }
    case RO_ROLW: {
      reg_t extz_rs1 = zext32(rs1());
      sreg_t shamt = rs2() & 31;
      set_rd(sext32((extz_rs1 << shamt) | (extz_rs1 >> (32 - shamt))));
      break;
    }
    case RO_RORW: {
      reg_t extz_rs1 = zext32(rs1());
      sreg_t shamt = rs2() & 31;
      set_rd(sext32((extz_rs1 >> shamt) | (extz_rs1 << (32 - shamt))));
      break;
    }
#endif /* V8_TARGET_ARCH_RISCV64 */
      // TODO(riscv): Add RISCV M extension macro
    case RO_MUL: {
      set_rd(rs1() * rs2());
      break;
    }
    case RO_MULH: {
      set_rd(mulh(rs1(), rs2()));
      break;
    }
    case RO_MULHSU: {
      set_rd(mulhsu(rs1(), rs2()));
      break;
    }
    case RO_MULHU: {
      set_rd(mulhu(rs1(), rs2()));
      break;
    }
    case RO_DIV: {
      sreg_t lhs = sext_xlen(rs1());
      sreg_t rhs = sext_xlen(rs2());
      if (rhs == 0) {
        set_rd(-1);
      } else if (lhs == INTPTR_MIN && rhs == -1) {
        set_rd(lhs);
      } else {
        set_rd(sext_xlen(lhs / rhs));
      }
      break;
    }
    case RO_DIVU: {
      reg_t lhs = zext_xlen(rs1());
      reg_t rhs = zext_xlen(rs2());
      if (rhs == 0) {
        set_rd(UINTPTR_MAX);
      } else {
        set_rd(zext_xlen(lhs / rhs));
      }
      break;
    }
    case RO_REM: {
      sreg_t lhs = sext_xlen(rs1());
      sreg_t rhs = sext_xlen(rs2());
      if (rhs == 0) {
        set_rd(lhs);
      } else if (lhs == INTPTR_MIN && rhs == -1) {
        set_rd(0);
      } else {
        set_rd(sext_xlen(lhs % rhs));
      }
      break;
    }
    case RO_REMU: {
      reg_t lhs = zext_xlen(rs1());
      reg_t rhs = zext_xlen(rs2());
      if (rhs == 0) {
        set_rd(lhs);
      } else {
        set_rd(zext_xlen(lhs % rhs));
      }
      break;
    }
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_MULW: {
      set_rd(sext32(sext32(rs1()) * sext32(rs2())));
      break;
    }
    case RO_DIVW: {
      sreg_t lhs = sext32(rs1());
      sreg_t rhs = sext32(rs2());
      if (rhs == 0) {
        set_rd(-1);
      } else if (lhs == INT32_MIN && rhs == -1) {
        set_rd(lhs);
      } else {
        set_rd(sext32(lhs / rhs));
      }
      break;
    }
    case RO_DIVUW: {
      reg_t lhs = zext32(rs1());
      reg_t rhs = zext32(rs2());
      if (rhs == 0) {
        set_rd(UINT32_MAX);
      } else {
        set_rd(zext32(lhs / rhs));
      }
      break;
    }
    case RO_REMW: {
      sreg_t lhs = sext32(rs1());
      sreg_t rhs = sext32(rs2());
      if (rhs == 0) {
        set_rd(lhs);
      } else if (lhs == INT32_MIN && rhs == -1) {
        set_rd(0);
      } else {
        set_rd(sext32(lhs % rhs));
      }
      break;
    }
    case RO_REMUW: {
      reg_t lhs = zext32(rs1());
      reg_t rhs = zext32(rs2());
      if (rhs == 0) {
        set_rd(zext32(lhs));
      } else {
        set_rd(zext32(lhs % rhs));
      }
      break;
    }
#endif /*V8_TARGET_ARCH_RISCV64*/
    case RO_SH1ADD:
      set_rd(rs2() + (rs1() << 1));
      break;
    case RO_SH2ADD:
      set_rd(rs2() + (rs1() << 2));
      break;
    case RO_SH3ADD:
      set_rd(rs2() + (rs1() << 3));
      break;
    case RO_MAX:
      set_rd(rs1() < rs2() ? rs2() : rs1());
      break;
    case RO_MAXU:
      set_rd(reg_t(rs1()) < reg_t(rs2()) ? rs2() : rs1());
      break;
    case RO_MIN:
      set_rd(rs1() < rs2() ? rs1() : rs2());
      break;
    case RO_MINU:
      set_rd(reg_t(rs1()) < reg_t(rs2()) ? rs1() : rs2());
      break;
    case RO_ZEXTH:
      set_rd(zext_xlen(uint16_t(rs1())));
      break;
    case RO_ROL: {
      sreg_t shamt = rs2() & (xlen - 1);
      set_rd((reg_t(rs1()) << shamt) | (reg_t(rs1()) >> (xlen - shamt)));
      break;
    }
    case RO_ROR: {
      sreg_t shamt = rs2() & (xlen - 1);
      set_rd((reg_t(rs1()) >> shamt) | (reg_t(rs1()) << (xlen - shamt)));
      break;
    }
    case RO_BCLR: {
      sreg_t index = rs2() & (xlen - 1);
      set_rd(rs1() & ~(1l << index));
      break;
    }
    case RO_BEXT: {
      sreg_t index = rs2() & (xlen - 1);
      set_rd((rs1() >> index) & 1);
      break;
    }
    case RO_BINV: {
      sreg_t index = rs2() & (xlen - 1);
      set_rd(rs1() ^ (1 << index));
      break;
    }
    case RO_BSET: {
      sreg_t index = rs2() & (xlen - 1);
      set_rd(rs1() | (1 << index));
      break;
    }
    case RO_CZERO_EQZ: {
      sreg_t condition = rs2();
      set_rd(condition == 0 ? 0 : rs1());
      break;
    }
    case RO_CZERO_NEZ: {
      sreg_t condition = rs2();
      set_rd(condition != 0 ? 0 : rs1());
      break;
    }
    default: {
      switch (instr_.BaseOpcode()) {
        case AMO:
          DecodeRVRAType();
          break;
        case OP_FP:
          DecodeRVRFPType();
          break;
        default:
          UNSUPPORTED();
      }
    }
  }
}

float Simulator::RoundF2FHelper(float input_val, int rmode) {
  if (rmode == DYN) rmode = get_dynamic_rounding_mode();

  float rounded = 0;
  switch (rmode) {
    case RNE: {  // Round to Nearest, tiest to Even
      rounded = floorf(input_val);
      float error = input_val - rounded;

      // Take care of correctly handling the range [-0.5, -0.0], which must
      // yield -0.0.
      if ((-0.5 <= input_val) && (input_val < 0.0)) {
        rounded = -0.0;

        // If the error is greater than 0.5, or is equal to 0.5 and the integer
        // result is odd, round up.
      } else if ((error > 0.5) ||
                 ((error == 0.5) && (std::fmod(rounded, 2) != 0))) {
        rounded++;
      }
      break;
    }
    case RTZ:  // Round towards Zero
      rounded = std::truncf(input_val);
      break;
    case RDN:  // Round Down (towards -infinity)
      rounded = floorf(input_val);
      break;
    case RUP:  // Round Up (towards +infinity)
      rounded = ceilf(input_val);
      break;
    case RMM:  // Round to Nearest, tiest to Max Magnitude
      rounded = std::roundf(input_val);
      break;
    default:
      UNREACHABLE();
  }

  return rounded;
}

double Simulator::RoundF2FHelper(double input_val, int rmode) {
  if (rmode == DYN) rmode = get_dynamic_rounding_mode();

  double rounded = 0;
  switch (rmode) {
    case RNE: {  // Round to Nearest, tiest to Even
      rounded = std::floor(input_val);
      double error = input_val - rounded;

      // Take care of correctly handling the range [-0.5, -0.0], which must
      // yield -0.0.
      if ((-0.5 <= input_val) && (input_val < 0.0)) {
        rounded = -0.0;

        // If the error is greater than 0.5, or is equal to 0.5 and the integer
        // result is odd, round up.
      } else if ((error > 0.5) ||
                 ((error == 0.5) && (std::fmod(rounded, 2) != 0))) {
        rounded++;
      }
      break;
    }
    case RTZ:  // Round towards Zero
      rounded = std::trunc(input_val);
      break;
    case RDN:  // Round Down (towards -infinity)
      rounded = std::floor(input_val);
      break;
    case RUP:  // Round Up (towards +infinity)
      rounded = std::ceil(input_val);
      break;
    case RMM:  // Round to Nearest, tiest to Max Magnitude
      rounded = std::round(input_val);
      break;
    default:
      UNREACHABLE();
  }
  return rounded;
}

// convert rounded floating-point to integer types, handle input values that
// are out-of-range, underflow, or NaN, and set appropriate fflags
template <typename I_TYPE, typename F_TYPE>
I_TYPE Simulator::RoundF2IHelper(F_TYPE original, int rmode) {
  DCHECK(std::is_integral<I_TYPE>::value);

  DCHECK((std::is_same<F_TYPE, float>::value ||
          std::is_same<F_TYPE, double>::value));

  I_TYPE max_i = std::numeric_limits<I_TYPE>::max();
  I_TYPE min_i = std::numeric_limits<I_TYPE>::min();

  if (!std::isfinite(original)) {
    set_fflags(kInvalidOperation);
    if (std::isnan(original) ||
        original == std::numeric_limits<F_TYPE>::infinity()) {
      return max_i;
    } else {
      DCHECK(original == -std::numeric_limits<F_TYPE>::infinity());
      return min_i;
    }
  }

  F_TYPE rounded = RoundF2FHelper(original, rmode);
  if (original != rounded) set_fflags(kInexact);

  if (!std::isfinite(rounded)) {
    set_fflags(kInvalidOperation);
    if (std::isnan(rounded) ||
        rounded == std::numeric_limits<F_TYPE>::infinity()) {
      return max_i;
    } else {
      DCHECK(rounded == -std::numeric_limits<F_TYPE>::infinity());
      return min_i;
    }
  }

  // Since integer max values are either all 1s (for unsigned) or all 1s
  // except for sign-bit (for signed), they cannot be represented precisely in
  // floating point, in order to precisely tell whether the rounded floating
  // point is within the max range, we compare against (max_i+1) which would
  // have a single 1 w/ many trailing zeros
  float max_i_plus_1 =
      std::is_same<uint64_t, I_TYPE>::value
          ? 0x1p64f  // uint64_t::max + 1 cannot be represented in integers,
                     // so use its float representation directly
          : static_cast<float>(static_cast<uint64_t>(max_i) + 1);
  if (rounded >= max_i_plus_1) {
    set_fflags(kFPUOverflow | kInvalidOperation);
    return max_i;
  }

  // Since min_i (either 0 for unsigned, or for signed) is represented
  // precisely in floating-point,  comparing rounded directly against min_i
  if (rounded <= min_i) {
    if (rounded < min_i) set_fflags(kFPUOverflow | kInvalidOperation);
    return min_i;
  }

  F_TYPE underflow_fval =
      std::is_same<F_TYPE, float>::value ? FLT_MIN : DBL_MIN;
  if (rounded < underflow_fval && rounded > -underflow_fval && rounded != 0) {
    set_fflags(kUnderflow);
  }

  return static_cast<I_TYPE>(rounded);
}

template <typename T>
static int64_t FclassHelper(T value) {
  switch (std::fpclassify(value)) {
    case FP_INFINITE:
      return (std::signbit(value) ? kNegativeInfinity : kPositiveInfinity);
    case FP_NAN:
      return (isSnan(value) ? kSignalingNaN : kQuietNaN);
    case FP_NORMAL:
      return (std::signbit(value) ? kNegativeNormalNumber
                                  : kPositiveNormalNumber);
    case FP_SUBNORMAL:
      return (std::signbit(value) ? kNegativeSubnormalNumber
                                  : kPositiveSubnormalNumber);
    case FP_ZERO:
      return (std::signbit(value) ? kNegativeZero : kPositiveZero);
    default:
      UNREACHABLE();
  }
}

template <typename T>
bool Simulator::CompareFHelper(T input1, T input2, FPUCondition cc) {
  DCHECK(std::is_floating_point<T>::value);
  bool result = false;
  switch (cc) {
    case LT:
    case LE:
      // FLT, FLE are signaling compares
      if (std::isnan(input1) || std::isnan(input2)) {
        set_fflags(kInvalidOperation);
        result = false;
      } else {
        result = (cc == LT) ? (input1 < input2) : (input1 <= input2);
      }
      break;

    case EQ:
      if (std::numeric_limits<T>::signaling_NaN() == input1 ||
          std::numeric_limits<T>::signaling_NaN() == input2) {
        set_fflags(kInvalidOperation);
      }
      if (std::isnan(input1) || std::isnan(input2)) {
        result = false;
      } else {
        result = (input1 == input2);
      }
      break;
    case NE:
      if (std::numeric_limits<T>::signaling_NaN() == input1 ||
          std::numeric_limits<T>::signaling_NaN() == input2) {
        set_fflags(kInvalidOperation);
      }
      if (std::isnan(input1) || std::isnan(input2)) {
        result = true;
      } else {
        result = (input1 != input2);
      }
      break;
    default:
      UNREACHABLE();
  }
  return result;
}

template <typename T>
static inline bool is_invalid_fmul(T src1, T src2) {
  return (isinf(src1) && src2 == static_cast<T>(0.0)) ||
         (src1 == static_cast<T>(0.0) && isinf(src2));
}

template <typename T>
static inline bool is_invalid_fadd(T src1, T src2) {
  return (isinf(src1) && isinf(src2) &&
          std::signbit(src1) != std::signbit(src2));
}

template <typename T>
static inline bool is_invalid_fsub(T src1, T src2) {
  return (isinf(src1) && isinf(src2) &&
          std::signbit(src1) == std::signbit(src2));
}

template <typename T>
static inline bool is_invalid_fdiv(T src1, T src2) {
  return ((src1 == 0 && src2 == 0) || (isinf(src1) && isinf(src2)));
}

template <typename T>
static inline bool is_invalid_fsqrt(T src1) {
  return (src1 < 0);
}

void Simulator::DecodeRVRAType() {
  // TODO(riscv): Add macro for RISCV A extension
  // Special handling for A extension instructions because it uses func5
  // For all A extension instruction, V8 simulator is pure sequential. No
  // Memory address lock or other synchronizaiton behaviors.
  switch (instr_.InstructionBits() & kRATypeMask) {
    case RO_LR_W: {
      sreg_t addr = rs1();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      {
        base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
        if ((addr & 0x3) != 0) {
          DieOrDebug();
        }
        auto val = ReadMem<int32_t>(addr, instr_.instr());
        set_rd(sext32(val), false);
        TraceMemRd(addr, val, get_register(rd_reg()));
        local_monitor_.NotifyLoadLinked(addr, TransactionSize::Word);
        GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                      &global_monitor_thread_);
      }
      break;
    }
    case RO_SC_W: {
      sreg_t addr = rs1();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      if ((addr & 0x3) != 0) {
        DieOrDebug();
      }
      base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
      if (local_monitor_.NotifyStoreConditional(addr, TransactionSize::Word) &&
          GlobalMonitor::Get()->NotifyStoreConditional_Locked(
              addr, &global_monitor_thread_)) {
        local_monitor_.NotifyStore();
        GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
        WriteMem<int32_t>(rs1(), (int32_t)rs2(), instr_.instr());
        set_rd(0, false);
      } else {
        set_rd(1, false);
      }
      break;
    }
    case RO_AMOSWAP_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return (uint32_t)rs2(); }, instr_.instr(),
          WORD)));
      break;
    }
    case RO_AMOADD_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return lhs + (uint32_t)rs2(); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOXOR_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return lhs ^ (uint32_t)rs2(); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOAND_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return lhs & (uint32_t)rs2(); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOOR_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return lhs | (uint32_t)rs2(); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOMIN_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<int32_t>(
          rs1(), [&](int32_t lhs) { return std::min(lhs, (int32_t)rs2()); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOMAX_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<int32_t>(
          rs1(), [&](int32_t lhs) { return std::max(lhs, (int32_t)rs2()); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOMINU_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return std::min(lhs, (uint32_t)rs2()); },
          instr_.instr(), WORD)));
      break;
    }
    case RO_AMOMAXU_W: {
      if ((rs1() & 0x3) != 0) {
        DieOrDebug();
      }
      set_rd(sext32(amo<uint32_t>(
          rs1(), [&](uint32_t lhs) { return std::max(lhs, (uint32_t)rs2()); },
          instr_.instr(), WORD)));
      break;
    }
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_LR_D: {
      int64_t addr = rs1();
      if (!ProbeMemory(addr, sizeof(int64_t))) return;
      {
        base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
        auto val = ReadMem<int64_t>(addr, instr_.instr());
        set_rd(val, false);
        TraceMemRd(addr, val, get_register(rd_reg()));
        local_monitor_.NotifyLoadLinked(addr, TransactionSize::DoubleWord);
        GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                      &global_monitor_thread_);
        break;
      }
    }
    case RO_SC_D: {
      int64_t addr = rs1();
      if (!ProbeMemory(addr, sizeof(int64_t))) return;
      base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
      if (local_monitor_.NotifyStoreConditional(addr,
                                                TransactionSize::DoubleWord) &&
          (GlobalMonitor::Get()->NotifyStoreConditional_Locked(
              addr, &global_monitor_thread_))) {
        GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
        WriteMem<int64_t>(rs1(), rs2(), instr_.instr());
        set_rd(0, false);
      } else {
        set_rd(1, false);
      }
      break;
    }
    case RO_AMOSWAP_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return rs2(); }, instr_.instr(), DWORD));
      break;
    }
    case RO_AMOADD_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return lhs + rs2(); }, instr_.instr(),
          DWORD));
      break;
    }
    case RO_AMOXOR_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return lhs ^ rs2(); }, instr_.instr(),
          DWORD));
      break;
    }
    case RO_AMOAND_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return lhs & rs2(); }, instr_.instr(),
          DWORD));
      break;
    }
    case RO_AMOOR_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return lhs | rs2(); }, instr_.instr(),
          DWORD));
      break;
    }
    case RO_AMOMIN_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return std::min(lhs, rs2()); },
          instr_.instr(), DWORD));
      break;
    }
    case RO_AMOMAX_D: {
      set_rd(amo<int64_t>(
          rs1(), [&](int64_t lhs) { return std::max(lhs, rs2()); },
          instr_.instr(), DWORD));
      break;
    }
    case RO_AMOMINU_D: {
      set_rd(amo<uint64_t>(
          rs1(), [&](uint64_t lhs) { return std::min(lhs, (uint64_t)rs2()); },
          instr_.instr(), DWORD));
      break;
    }
    case RO_AMOMAXU_D: {
      set_rd(amo<uint64_t>(
          rs1(), [&](uint64_t lhs) { return std::max(lhs, (uint64_t)rs2()); },
          instr_.instr(), DWORD));
      break;
    }
#endif /*V8_TARGET_ARCH_RISCV64*/
    // TODO(riscv): End Add macro for RISCV A extension
    default: {
      UNSUPPORTED();
    }
  }
}

void Simulator::DecodeRVRFPType() {
  // OP_FP instructions (F/D) uses func7 first. Some further uses func3 and
  // rs2()

  // kRATypeMask is only for func7
  switch (instr_.InstructionBits() & kRFPTypeMask) {
    // TODO(riscv): Add macro for RISCV F extension
    case RO_FADD_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2) {
        if (is_invalid_fadd(frs1, frs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return frs1 + frs2;
        }
      };
      set_frd(CanonicalizeFPUOp2<float>(fn));
      break;
    }
    case RO_FSUB_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2) {
        if (is_invalid_fsub(frs1, frs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return frs1 - frs2;
        }
      };
      set_frd(CanonicalizeFPUOp2<float>(fn));
      break;
    }
    case RO_FMUL_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2) {
        if (is_invalid_fmul(frs1, frs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return frs1 * frs2;
        }
      };
      set_frd(CanonicalizeFPUOp2<float>(fn));
      break;
    }
    case RO_FDIV_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2) {
        if (is_invalid_fdiv(frs1, frs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else if (frs2 == 0.0f) {
          this->set_fflags(kDivideByZero);
          return (std::signbit(frs1) == std::signbit(frs2)
                      ? std::numeric_limits<float>::infinity()
                      : -std::numeric_limits<float>::infinity());
        } else {
          return frs1 / frs2;
        }
      };
      set_frd(CanonicalizeFPUOp2<float>(fn));
      break;
    }
    case RO_FSQRT_S: {
      if (instr_.Rs2Value() == 0b00000) {
        // TODO(riscv): use rm value (round mode)
        auto fn = [this](float frs) {
          if (is_invalid_fsqrt(frs)) {
            this->set_fflags(kInvalidOperation);
            return std::numeric_limits<float>::quiet_NaN();
          } else {
            return std::sqrt(frs);
          }
        };
        set_frd(CanonicalizeFPUOp1<float>(fn));
      } else {
        UNSUPPORTED();
      }
      break;
    }
    case RO_FSGNJ_S: {  // RO_FSGNJN_S  RO_FSQNJX_S
      switch (instr_.Funct3Value()) {
        case 0b000: {  // RO_FSGNJ_S
          set_frd(fsgnj32(frs1_boxed(), frs2_boxed(), false, false));
          break;
        }
        case 0b001: {  // RO_FSGNJN_S
          set_frd(fsgnj32(frs1_boxed(), frs2_boxed(), true, false));
          break;
        }
        case 0b010: {  // RO_FSQNJX_S
          set_frd(fsgnj32(frs1_boxed(), frs2_boxed(), false, true));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FMIN_S: {  // RO_FMAX_S
      switch (instr_.Funct3Value()) {
        case 0b000: {  // RO_FMIN_S
          set_frd(FMaxMinHelper(frs1(), frs2(), MaxMinKind::kMin));
          break;
        }
        case 0b001: {  // RO_FMAX_S
          set_frd(FMaxMinHelper(frs1(), frs2(), MaxMinKind::kMax));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FCVT_W_S: {  // RO_FCVT_WU_S , 64F RO_FCVT_L_S RO_FCVT_LU_S
      float original_val = frs1();
      switch (instr_.Rs2Value()) {
        case 0b00000: {  // RO_FCVT_W_S
          set_rd(RoundF2IHelper<int32_t>(original_val, instr_.RoundMode()));
          break;
        }
        case 0b00001: {  // RO_FCVT_WU_S
          set_rd(sext32(
              RoundF2IHelper<uint32_t>(original_val, instr_.RoundMode())));
          break;
        }
#ifdef V8_TARGET_ARCH_RISCV64
        case 0b00010: {  // RO_FCVT_L_S
          set_rd(RoundF2IHelper<int64_t>(original_val, instr_.RoundMode()));
          break;
        }
        case 0b00011: {  // RO_FCVT_LU_S
          set_rd(RoundF2IHelper<uint64_t>(original_val, instr_.RoundMode()));
          break;
        }
#endif /* V8_TARGET_ARCH_RISCV64 */
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FMV: {  // RO_FCLASS_S
      switch (instr_.Funct3Value()) {
        case 0b000: {
          if (instr_.Rs2Value() == 0b00000) {
            // RO_FMV_X_W
            set_rd(sext32(get_fpu_register_word(rs1_reg())));
          } else {
            UNSUPPORTED();
          }
          break;
        }
        case 0b001: {  // RO_FCLASS_S
          set_rd(FclassHelper(frs1()));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FLE_S: {  // RO_FEQ_S RO_FLT_S RO_FLE_S
      switch (instr_.Funct3Value()) {
        case 0b010: {  // RO_FEQ_S
          set_rd(CompareFHelper(frs1(), frs2(), EQ));
          break;
        }
        case 0b001: {  // RO_FLT_S
          set_rd(CompareFHelper(frs1(), frs2(), LT));
          break;
        }
        case 0b000: {  // RO_FLE_S
          set_rd(CompareFHelper(frs1(), frs2(), LE));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FCVT_S_W: {  // RO_FCVT_S_WU , 64F RO_FCVT_S_L RO_FCVT_S_LU
      switch (instr_.Rs2Value()) {
        case 0b00000: {  // RO_FCVT_S_W
          set_frd(static_cast<float>((int32_t)rs1()));
          break;
        }
        case 0b00001: {  // RO_FCVT_S_WU
          set_frd(static_cast<float>((uint32_t)rs1()));
          break;
        }
#ifdef V8_TARGET_ARCH_RISCV64
        case 0b00010: {  // RO_FCVT_S_L
          set_frd(static_cast<float>((int64_t)rs1()));
          break;
        }
        case 0b00011: {  // RO_FCVT_S_LU
          set_frd(static_cast<float>((uint64_t)rs1()));
          break;
        }
#endif /* V8_TARGET_ARCH_RISCV64 */
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FMV_W_X: {
      if (instr_.Funct3Value() == 0b000) {
        // since FMV preserves source bit-pattern, no need to canonize
        Float32 result = Float32::FromBits((uint32_t)rs1());
        set_frd(result);
      } else {
        UNSUPPORTED();
      }
      break;
    }
      // TODO(riscv): Add macro for RISCV D extension
    case RO_FADD_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2) {
        if (is_invalid_fadd(drs1, drs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return drs1 + drs2;
        }
      };
      set_drd(CanonicalizeFPUOp2<double>(fn));
      break;
    }
    case RO_FSUB_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2) {
        if (is_invalid_fsub(drs1, drs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return drs1 - drs2;
        }
      };
      set_drd(CanonicalizeFPUOp2<double>(fn));
      break;
    }
    case RO_FMUL_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2) {
        if (is_invalid_fmul(drs1, drs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return drs1 * drs2;
        }
      };
      set_drd(CanonicalizeFPUOp2<double>(fn));
      break;
    }
    case RO_FDIV_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2) {
        if (is_invalid_fdiv(drs1, drs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else if (drs2 == 0.0) {
          this->set_fflags(kDivideByZero);
          return (std::signbit(drs1) == std::signbit(drs2)
                      ? std::numeric_limits<double>::infinity()
                      : -std::numeric_limits<double>::infinity());
        } else {
          return drs1 / drs2;
        }
      };
      set_drd(CanonicalizeFPUOp2<double>(fn));
      break;
    }
    case RO_FSQRT_D: {
      if (instr_.Rs2Value() == 0b00000) {
        // TODO(riscv): use rm value (round mode)
        auto fn = [this](double drs) {
          if (is_invalid_fsqrt(drs)) {
            this->set_fflags(kInvalidOperation);
            return std::numeric_limits<double>::quiet_NaN();
          } else {
            return std::sqrt(drs);
          }
        };
        set_drd(CanonicalizeFPUOp1<double>(fn));
      } else {
        UNSUPPORTED();
      }
      break;
    }
    case RO_FSGNJ_D: {  // RO_FSGNJN_D RO_FSQNJX_D
      switch (instr_.Funct3Value()) {
        case 0b000: {  // RO_FSGNJ_D
          set_drd(fsgnj64(drs1_boxed(), drs2_boxed(), false, false));
          break;
        }
        case 0b001: {  // RO_FSGNJN_D
          set_drd(fsgnj64(drs1_boxed(), drs2_boxed(), true, false));
          break;
        }
        case 0b010: {  // RO_FSQNJX_D
          set_drd(fsgnj64(drs1_boxed(), drs2_boxed(), false, true));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FMIN_D: {  // RO_FMAX_D
      switch (instr_.Funct3Value()) {
        case 0b000: {  // RO_FMIN_D
          set_drd(FMaxMinHelper(drs1(), drs2(), MaxMinKind::kMin));
          break;
        }
        case 0b001: {  // RO_FMAX_D
          set_drd(FMaxMinHelper(drs1(), drs2(), MaxMinKind::kMax));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case (RO_FCVT_S_D & kRFPTypeMask): {
      if (instr_.Rs2Value() == 0b00001) {
        auto fn = [](double drs) { return static_cast<float>(drs); };
        set_frd(CanonicalizeDoubleToFloatOperation(fn));
      } else {
        UNSUPPORTED();
      }
      break;
    }
    case RO_FCVT_D_S: {
      if (instr_.Rs2Value() == 0b00000) {
        auto fn = [](float frs) { return static_cast<double>(frs); };
        set_drd(CanonicalizeFloatToDoubleOperation(fn));
      } else {
        UNSUPPORTED();
      }
      break;
    }
    case RO_FLE_D: {  // RO_FEQ_D RO_FLT_D RO_FLE_D
      switch (instr_.Funct3Value()) {
        case 0b010: {  // RO_FEQ_S
          set_rd(CompareFHelper(drs1(), drs2(), EQ));
          break;
        }
        case 0b001: {  // RO_FLT_D
          set_rd(CompareFHelper(drs1(), drs2(), LT));
          break;
        }
        case 0b000: {  // RO_FLE_D
          set_rd(CompareFHelper(drs1(), drs2(), LE));
          break;
        }
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case (RO_FCLASS_D & kRFPTypeMask): {  // RO_FCLASS_D , 64D RO_FMV_X_D
      if (instr_.Rs2Value() != 0b00000) {
        UNSUPPORTED();
      }
      switch (instr_.Funct3Value()) {
        case 0b001: {  // RO_FCLASS_D
          set_rd(FclassHelper(drs1()));
          break;
        }
#ifdef V8_TARGET_ARCH_RISCV64
        case 0b000: {  // RO_FMV_X_D
          set_rd(base::bit_cast<int64_t>(drs1()));
          break;
        }
#endif /* V8_TARGET_ARCH_RISCV64 */
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FCVT_W_D: {  // RO_FCVT_WU_D , 64F RO_FCVT_L_D RO_FCVT_LU_D
      double original_val = drs1();
      switch (instr_.Rs2Value()) {
        case 0b00000: {  // RO_FCVT_W_D
          set_rd(RoundF2IHelper<int32_t>(original_val, instr_.RoundMode()));
          break;
        }
        case 0b00001: {  // RO_FCVT_WU_D
          set_rd(sext32(
              RoundF2IHelper<uint32_t>(original_val, instr_.RoundMode())));
          break;
        }
#ifdef V8_TARGET_ARCH_RISCV64
        case 0b00010: {  // RO_FCVT_L_D
          set_rd(RoundF2IHelper<int64_t>(original_val, instr_.RoundMode()));
          break;
        }
        case 0b00011: {  // RO_FCVT_LU_D
          set_rd(RoundF2IHelper<uint64_t>(original_val, instr_.RoundMode()));
          break;
        }
#endif /* V8_TARGET_ARCH_RISCV64 */
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
    case RO_FCVT_D_W: {  // RO_FCVT_D_WU , 64F RO_FCVT_D_L RO_FCVT_D_LU
      switch (instr_.Rs2Value()) {
        case 0b00000: {  // RO_FCVT_D_W
          set_drd((int32_t)rs1());
          break;
        }
        case 0b00001: {  // RO_FCVT_D_WU
          set_drd((uint32_t)rs1());
          break;
        }
#ifdef V8_TARGET_ARCH_RISCV64
        case 0b00010: {  // RO_FCVT_D_L
          set_drd((int64_t)rs1());
          break;
        }
        case 0b00011: {  // RO_FCVT_D_LU
          set_drd((uint64_t)rs1());
          break;
        }
#endif /* V8_TARGET_ARCH_RISCV64 */
        default: {
          UNSUPPORTED();
        }
      }
      break;
    }
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_FMV_D_X: {
      if (instr_.Funct3Value() == 0b000 && instr_.Rs2Value() == 0b00000) {
        // Since FMV preserves source bit-pattern, no need to canonize
        set_drd(base::bit_cast<double>(rs1()));
      } else {
        UNSUPPORTED();
      }
      break;
    }
#endif /* V8_TARGET_ARCH_RISCV64 */
    default: {
      UNSUPPORTED();
    }
  }
}

void Simulator::DecodeRVR4Type() {
  switch (instr_.InstructionBits() & kR4TypeMask) {
    // TODO(riscv): use F Extension macro block
    case RO_FMADD_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2, float frs3) {
        if (is_invalid_fmul(frs1, frs2) || is_invalid_fadd(frs1 * frs2, frs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return std::fma(frs1, frs2, frs3);
        }
      };
      set_frd(CanonicalizeFPUOp3<float>(fn));
      break;
    }
    case RO_FMSUB_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2, float frs3) {
        if (is_invalid_fmul(frs1, frs2) || is_invalid_fsub(frs1 * frs2, frs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return std::fma(frs1, frs2, -frs3);
        }
      };
      set_frd(CanonicalizeFPUOp3<float>(fn));
      break;
    }
    case RO_FNMSUB_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2, float frs3) {
        if (is_invalid_fmul(frs1, frs2) || is_invalid_fsub(frs3, frs1 * frs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return -std::fma(frs1, frs2, -frs3);
        }
      };
      set_frd(CanonicalizeFPUOp3<float>(fn));
      break;
    }
    case RO_FNMADD_S: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](float frs1, float frs2, float frs3) {
        if (is_invalid_fmul(frs1, frs2) || is_invalid_fadd(frs1 * frs2, frs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<float>::quiet_NaN();
        } else {
          return -std::fma(frs1, frs2, frs3);
        }
      };
      set_frd(CanonicalizeFPUOp3<float>(fn));
      break;
    }
    // TODO(riscv): use F Extension macro block
    case RO_FMADD_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2, double drs3) {
        if (is_invalid_fmul(drs1, drs2) || is_invalid_fadd(drs1 * drs2, drs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return std::fma(drs1, drs2, drs3);
        }
      };
      set_drd(CanonicalizeFPUOp3<double>(fn));
      break;
    }
    case RO_FMSUB_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2, double drs3) {
        if (is_invalid_fmul(drs1, drs2) || is_invalid_fsub(drs1 * drs2, drs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return std::fma(drs1, drs2, -drs3);
        }
      };
      set_drd(CanonicalizeFPUOp3<double>(fn));
      break;
    }
    case RO_FNMSUB_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2, double drs3) {
        if (is_invalid_fmul(drs1, drs2) || is_invalid_fsub(drs3, drs1 * drs2)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return -std::fma(drs1, drs2, -drs3);
        }
      };
      set_drd(CanonicalizeFPUOp3<double>(fn));
      break;
    }
    case RO_FNMADD_D: {
      // TODO(riscv): use rm value (round mode)
      auto fn = [this](double drs1, double drs2, double drs3) {
        if (is_invalid_fmul(drs1, drs2) || is_invalid_fadd(drs1 * drs2, drs3)) {
          this->set_fflags(kInvalidOperation);
          return std::numeric_limits<double>::quiet_NaN();
        } else {
          return -std::fma(drs1, drs2, drs3);
        }
      };
      set_drd(CanonicalizeFPUOp3<double>(fn));
      break;
    }
    default:
      UNSUPPORTED();
  }
}

#ifdef CAN_USE_RVV_INSTRUCTIONS
bool Simulator::DecodeRvvVL() {
  uint32_t instr_temp =
      instr_.InstructionBits() & (kRvvMopMask | kRvvNfMask | kBaseOpcodeMask);
  if (RO_V_VL == instr_temp) {
    if (!(instr_.InstructionBits() & (kRvvRs2Mask))) {
      switch (instr_.vl_vs_width()) {
        case 8: {
          RVV_VI_LD(0, (i * nf + fn), int8, false);
          break;
        }
        case 16: {
          RVV_VI_LD(0, (i * nf + fn), int16, false);
          break;
        }
        case 32: {
          RVV_VI_LD(0, (i * nf + fn), int32, false);
          break;
        }
        case 64: {
          RVV_VI_LD(0, (i * nf + fn), int64, false);
          break;
        }
        default:
          UNIMPLEMENTED_RISCV();
          break;
      }
      return true;
    } else {
      UNIMPLEMENTED_RISCV();
      return true;
    }
  } else if (RO_V_VLS == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VLX == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VLSEG2 == instr_temp || RO_V_VLSEG3 == instr_temp ||
             RO_V_VLSEG4 == instr_temp || RO_V_VLSEG5 == instr_temp ||
             RO_V_VLSEG6 == instr_temp || RO_V_VLSEG7 == instr_temp ||
             RO_V_VLSEG8 == instr_temp) {
    if (!(instr_.InstructionBits() & (kRvvRs2Mask))) {
      UNIMPLEMENTED_RISCV();
      return true;
    } else {
      UNIMPLEMENTED_RISCV();
      return true;
    }
  } else if (RO_V_VLSSEG2 == instr_temp || RO_V_VLSSEG3 == instr_temp ||
             RO_V_VLSSEG4 == instr_temp || RO_V_VLSSEG5 == instr_temp ||
             RO_V_VLSSEG6 == instr_temp || RO_V_VLSSEG7 == instr_temp ||
             RO_V_VLSSEG8 == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VLXSEG2 == instr_temp || RO_V_VLXSEG3 == instr_temp ||
             RO_V_VLXSEG4 == instr_temp || RO_V_VLXSEG5 == instr_temp ||
             RO_V_VLXSEG6 == instr_temp || RO_V_VLXSEG7 == instr_temp ||
             RO_V_VLXSEG8 == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else {
    return false;
  }
}

bool Simulator::DecodeRvvVS() {
  uint32_t instr_temp =
      instr_.InstructionBits() & (kRvvMopMask | kRvvNfMask | kBaseOpcodeMask);
  if (RO_V_VS == instr_temp) {
    if (!(instr_.InstructionBits() & (kRvvRs2Mask))) {
      switch (instr_.vl_vs_width()) {
        case 8: {
          RVV_VI_ST(0, (i * nf + fn), uint8, false);
          break;
        }
        case 16: {
          RVV_VI_ST(0, (i * nf + fn), uint16, false);
          break;
        }
        case 32: {
          RVV_VI_ST(0, (i * nf + fn), uint32, false);
          break;
        }
        case 64: {
          RVV_VI_ST(0, (i * nf + fn), uint64, false);
          break;
        }
        default:
          UNIMPLEMENTED_RISCV();
          break;
      }
    } else {
      UNIMPLEMENTED_RISCV();
    }
    return true;
  } else if (RO_V_VSS == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VSX == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VSU == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VSSEG2 == instr_temp || RO_V_VSSEG3 == instr_temp ||
             RO_V_VSSEG4 == instr_temp || RO_V_VSSEG5 == instr_temp ||
             RO_V_VSSEG6 == instr_temp || RO_V_VSSEG7 == instr_temp ||
             RO_V_VSSEG8 == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VSSSEG2 == instr_temp || RO_V_VSSSEG3 == instr_temp ||
             RO_V_VSSSEG4 == instr_temp || RO_V_VSSSEG5 == instr_temp ||
             RO_V_VSSSEG6 == instr_temp || RO_V_VSSSEG7 == instr_temp ||
             RO_V_VSSSEG8 == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else if (RO_V_VSXSEG2 == instr_temp || RO_V_VSXSEG3 == instr_temp ||
             RO_V_VSXSEG4 == instr_temp || RO_V_VSXSEG5 == instr_temp ||
             RO_V_VSXSEG6 == instr_temp || RO_V_VSXSEG7 == instr_temp ||
             RO_V_VSXSEG8 == instr_temp) {
    UNIMPLEMENTED_RISCV();
    return true;
  } else {
    return false;
  }
}
#endif

Builtin Simulator::LookUp(Address pc) {
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    if (builtins_.code(builtin)->contains(isolate_, pc)) return builtin;
  }
  return Builtin::kNoBuiltinId;
}

void Simulator::DecodeRVIType() {
  switch (instr_.InstructionBits() & kITypeMask) {
    case RO_JALR: {
      set_rd(get_pc() + kInstrSize);
      // Note: No need to shift 2 for JALR's imm12, but set lowest bit to 0.
      sreg_t next_pc = (rs1() + imm12()) & ~sreg_t(1);
      set_pc(next_pc);
      if (v8_flags.trace_sim) {
        Builtin builtin = LookUp((Address)get_pc());
        if (builtin != Builtin::kNoBuiltinId) {
          auto code = builtins_.code(builtin);
          if ((rs1_reg() != ra || imm12() != 0)) {
            if ((Address)get_pc() == code->instruction_start()) {
              sreg_t arg0 = get_register(a0);
              sreg_t arg1 = get_register(a1);
              sreg_t arg2 = get_register(a2);
              sreg_t arg3 = get_register(a3);
              sreg_t arg4 = get_register(a4);
              sreg_t arg5 = get_register(a5);
              sreg_t arg6 = get_register(a6);
              sreg_t arg7 = get_register(a7);
              sreg_t* stack_pointer =
                  reinterpret_cast<sreg_t*>(get_register(sp));
              sreg_t arg8 = stack_pointer[0];
              sreg_t arg9 = stack_pointer[1];
              PrintF(
                  "Call to Builtin at %s "
                  "a0 %08" REGIx_FORMAT " ,a1 %08" REGIx_FORMAT
                  " ,a2 %08" REGIx_FORMAT " ,a3 %08" REGIx_FORMAT
                  " ,a4 %08" REGIx_FORMAT " ,a5 %08" REGIx_FORMAT
                  " ,a6 %08" REGIx_FORMAT " ,a7 %08" REGIx_FORMAT
                  " ,0(sp) %08" REGIx_FORMAT " ,8(sp) %08" REGIx_FORMAT
                  " ,sp %08" REGIx_FORMAT ",fp %08" REGIx_FORMAT " \n",
                  builtins_.name(builtin), arg0, arg1, arg2, arg3, arg4, arg5,
                  arg6, arg7, arg8, arg9, get_register(sp), get_register(fp));
            }
          } else if (rd_reg() == zero_reg) {
            PrintF("Return to Builtin at %s \n", builtins_.name(builtin));
          }
        }
      }
      break;
    }
    case RO_LB: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int8_t))) return;
      int8_t val = ReadMem<int8_t>(addr, instr_.instr());
      set_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
    case RO_LH: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int16_t))) return;
      int16_t val = ReadMem<int16_t>(addr, instr_.instr());
      set_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
    case RO_LW: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      int32_t val = ReadMem<int32_t>(addr, instr_.instr());
      set_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
    case RO_LBU: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int8_t))) return;
      uint8_t val = ReadMem<uint8_t>(addr, instr_.instr());
      set_rd(zext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
    case RO_LHU: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int16_t))) return;
      uint16_t val = ReadMem<uint16_t>(addr, instr_.instr());
      set_rd(zext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_LWU: {
      int64_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      uint32_t val = ReadMem<uint32_t>(addr, instr_.instr());
      set_rd(zext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
    case RO_LD: {
      int64_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(int64_t))) return;
      int64_t val = ReadMem<int64_t>(addr, instr_.instr());
      set_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rd_reg()));
      break;
    }
#endif /*V8_TARGET_ARCH_RISCV64*/
    case RO_ADDI: {
      set_rd(sext_xlen(rs1() + imm12()));
      break;
    }
    case RO_SLTI: {
      set_rd(sreg_t(rs1()) < sreg_t(imm12()));
      break;
    }
    case RO_SLTIU: {
      set_rd(reg_t(rs1()) < reg_t(imm12()));
      break;
    }
    case RO_XORI: {
      set_rd(imm12() ^ rs1());
      break;
    }
    case RO_ORI: {
      set_rd(imm12() | rs1());
      break;
    }
    case RO_ANDI: {
      set_rd(imm12() & rs1());
      break;
    }
    case OP_SHL: {
      switch (instr_.Funct6FieldRaw() | OP_SHL) {
        case RO_SLLI:
          require(shamt6() < xlen);
          set_rd(sext_xlen(rs1() << shamt6()));
          break;
        case RO_BCLRI: {
          require(shamt6() < xlen);
          sreg_t index = shamt6() & (xlen - 1);
          set_rd(rs1() & ~(1l << index));
          break;
        }
        case RO_BINVI: {
          require(shamt6() < xlen);
          sreg_t index = shamt6() & (xlen - 1);
          set_rd(rs1() ^ (1l << index));
          break;
        }
        case RO_BSETI: {
          require(shamt6() < xlen);
          sreg_t index = shamt6() & (xlen - 1);
          set_rd(rs1() | (1l << index));
          break;
        }
        case OP_COUNT:
          switch (instr_.Shamt()) {
            case 0: {  // clz
              sreg_t x = rs1();
              int highest_setbit = -1;
              for (auto i = xlen - 1; i >= 0; i--) {
                if ((x & (1l << i))) {
                  highest_setbit = i;
                  break;
                }
              }
              set_rd(xlen - 1 - highest_setbit);
              break;
            }
            case 1: {  // ctz
              sreg_t x = rs1();
              int lowest_setbit = xlen;
              for (auto i = 0; i < xlen; i++) {
                if ((x & (1l << i))) {
                  lowest_setbit = i;
                  break;
                }
              }
              set_rd(lowest_setbit);
              break;
            }
            case 2: {  // cpop
              int i = 0;
              sreg_t n = rs1();
              while (n) {
                n &= (n - 1);
                i++;
              }
              set_rd(i);
              break;
            }
            case 4:
              set_rd(int8_t(rs1()));
              break;
            case 5:
              set_rd(int16_t(rs1()));
              break;
            default:
              UNSUPPORTED_RISCV();
          }
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
    case OP_SHR: {  //  RO_SRAI
      switch (instr_.Funct6FieldRaw() | OP_SHR) {
        case RO_SRLI:
          require(shamt6() < xlen);
          set_rd(sext_xlen(zext_xlen(rs1()) >> shamt6()));
          break;
        case RO_SRAI:
          require(shamt6() < xlen);
          set_rd(sext_xlen(sext_xlen(rs1()) >> shamt6()));
          break;
        case RO_BEXTI: {
          require(shamt6() < xlen);
          sreg_t index = shamt6() & (xlen - 1);
          set_rd((rs1() >> index) & 1);
          break;
        }
        case RO_ORCB&(kFunct6Mask | OP_SHR): {
          reg_t rs1_val = rs1();
          reg_t result = 0;
          reg_t mask = 0xFF;
          reg_t step = 8;
          for (reg_t i = 0; i < xlen; i += step) {
            if ((rs1_val & mask) != 0) {
              result |= mask;
            }
            mask <<= step;
          }
          set_rd(result);
          break;
        }
        case RO_RORI: {
#ifdef V8_TARGET_ARCH_RISCV64
          int16_t shamt = shamt6();
#else
          int16_t shamt = shamt5();
#endif
          set_rd((reg_t(rs1()) >> shamt) | (reg_t(rs1()) << (xlen - shamt)));
          break;
        }
        case RO_REV8: {
          if (imm12() == RO_REV8_IMM12) {
            reg_t input = rs1();
            reg_t output = 0;
            reg_t j = xlen - 1;
            for (int i = 0; i < xlen; i += 8) {
              output |= ((input >> (j - 7)) & 0xff) << i;
              j -= 8;
            }
            set_rd(output);
            break;
          }
          UNSUPPORTED_RISCV();
        }
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_ADDIW: {
      set_rd(sext32(rs1() + imm12()));
      break;
    }
    case OP_SHLW:
      switch (instr_.Funct7FieldRaw() | OP_SHLW) {
        case RO_SLLIW:
          set_rd(sext32(rs1() << shamt5()));
          break;
        case RO_SLLIUW:
          set_rd(zext32(rs1()) << shamt6());
          break;
        case OP_COUNTW: {
          switch (instr_.Shamt()) {
            case 0: {  // clzw
              sreg_t x = rs1();
              int highest_setbit = -1;
              for (auto i = 31; i >= 0; i--) {
                if ((x & (1l << i))) {
                  highest_setbit = i;
                  break;
                }
              }
              set_rd(31 - highest_setbit);
              break;
            }
            case 1: {  // ctzw
              sreg_t x = rs1();
              int lowest_setbit = 32;
              for (auto i = 0; i < 32; i++) {
                if ((x & (1l << i))) {
                  lowest_setbit = i;
                  break;
                }
              }
              set_rd(lowest_setbit);
              break;
            }
            case 2: {  // cpopw
              int i = 0;
              int32_t n = static_cast<int32_t>(rs1());
              while (n) {
                n &= (n - 1);
                i++;
              }
              set_rd(i);
              break;
            }
            default:
              UNSUPPORTED_RISCV();
          }
          break;
        }
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    case OP_SHRW: {  //  RO_SRAI
      switch (instr_.Funct7FieldRaw() | OP_SHRW) {
        case RO_SRLIW:
          set_rd(sext32(uint32_t(rs1()) >> shamt5()));
          break;
        case RO_SRAIW:
          set_rd(sext32(int32_t(rs1()) >> shamt5()));
          break;
        case RO_RORIW: {
          reg_t extz_rs1 = zext32(rs1());
          int16_t shamt = shamt5();
          set_rd(sext32((extz_rs1 >> shamt) | (extz_rs1 << (32 - shamt))));
          break;
        }
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    }
#endif /*V8_TARGET_ARCH_RISCV64*/
    case RO_FENCE: {
      // DO nothing in sumulator
      break;
    }
    case RO_ECALL: {                   // RO_EBREAK
      if (instr_.Imm12Value() == 0) {  // ECALL
        SoftwareInterrupt();
      } else if (instr_.Imm12Value() == 1) {  // EBREAK
        SoftwareInterrupt();
      } else {
        UNSUPPORTED();
      }
      break;
    }
      // TODO(riscv): use Zifencei Standard Extension macro block
    case RO_FENCE_I: {
      // spike: flush icache.
      break;
    }
      // TODO(riscv): use Zicsr Standard Extension macro block
    case RO_CSRRW: {
      if (rd_reg() != zero_reg) {
        set_rd(zext_xlen(read_csr_value(csr_reg())));
      }
      write_csr_value(csr_reg(), rs1());
      break;
    }
    case RO_CSRRS: {
      set_rd(zext_xlen(read_csr_value(csr_reg())));
      if (rs1_reg() != zero_reg) {
        set_csr_bits(csr_reg(), rs1());
      }
      break;
    }
    case RO_CSRRC: {
      set_rd(zext_xlen(read_csr_value(csr_reg())));
      if (rs1_reg() != zero_reg) {
        clear_csr_bits(csr_reg(), rs1());
      }
      break;
    }
    case RO_CSRRWI: {
      if (rd_reg() != zero_reg) {
        set_rd(zext_xlen(read_csr_value(csr_reg())));
      }
      write_csr_value(csr_reg(), imm5CSR());
      break;
    }
    case RO_CSRRSI: {
      set_rd(zext_xlen(read_csr_value(csr_reg())));
      if (imm5CSR() != 0) {
        set_csr_bits(csr_reg(), imm5CSR());
      }
      break;
    }
    case RO_CSRRCI: {
      set_rd(zext_xlen(read_csr_value(csr_reg())));
      if (imm5CSR() != 0) {
        clear_csr_bits(csr_reg(), imm5CSR());
      }
      break;
    }
    // TODO(riscv): use F Extension macro block
    case RO_FLW: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(float))) return;
      uint32_t val = ReadMem<uint32_t>(addr, instr_.instr());
      set_frd(Float32::FromBits(val), false);
      TraceMemRdFloat(addr, Float32::FromBits(val),
                      get_fpu_register(frd_reg()));
      break;
    }
    // TODO(riscv): use D Extension macro block
    case RO_FLD: {
      sreg_t addr = rs1() + imm12();
      if (!ProbeMemory(addr, sizeof(double))) return;
      uint64_t val = ReadMem<uint64_t>(addr, instr_.instr());
      set_drd(Float64::FromBits(val), false);
      TraceMemRdDouble(addr, Float64::FromBits(val),
                       get_fpu_register(frd_reg()));
      break;
    }
    default: {
#ifdef CAN_USE_RVV_INSTRUCTIONS
      if (!DecodeRvvVL()) {
        UNSUPPORTED();
      }
      break;
#else
      UNSUPPORTED();
#endif
    }
  }
}

void Simulator::DecodeRVSType() {
  switch (instr_.InstructionBits() & kSTypeMask) {
    case RO_SB:
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(int8_t))) return;
      WriteMem<uint8_t>(rs1() + s_imm12(), (uint8_t)rs2(), instr_.instr());
      break;
    case RO_SH:
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(int16_t))) return;
      WriteMem<uint16_t>(rs1() + s_imm12(), (uint16_t)rs2(), instr_.instr());
      break;
    case RO_SW:
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(int32_t))) return;
      WriteMem<uint32_t>(rs1() + s_imm12(), (uint32_t)rs2(), instr_.instr());
      break;
#ifdef V8_TARGET_ARCH_RISCV64
    case RO_SD:
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(int64_t))) return;
      WriteMem<uint64_t>(rs1() + s_imm12(), (uint64_t)rs2(), instr_.instr());
      break;
#endif /*V8_TARGET_ARCH_RISCV64*/
    // TODO(riscv): use F Extension macro block
    case RO_FSW: {
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(float))) return;
      WriteMem<Float32>(rs1() + s_imm12(),
                        get_fpu_register_Float32(rs2_reg(), false),
                        instr_.instr());
      break;
    }
    // TODO(riscv): use D Extension macro block
    case RO_FSD: {
      if (!ProbeMemory(rs1() + s_imm12(), sizeof(double))) return;
      WriteMem<Float64>(rs1() + s_imm12(), get_fpu_register_Float64(rs2_reg()),
                        instr_.instr());
      break;
    }
    default:
#ifdef CAN_USE_RVV_INSTRUCTIONS
      if (!DecodeRvvVS()) {
        UNSUPPORTED();
      }
      break;
#else
      UNSUPPORTED();
#endif
  }
}

void Simulator::DecodeRVBType() {
  switch (instr_.InstructionBits() & kBTypeMask) {
    case RO_BEQ:
      if (rs1() == rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    case RO_BNE:
      if (rs1() != rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    case RO_BLT:
      if (rs1() < rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    case RO_BGE:
      if (rs1() >= rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    case RO_BLTU:
      if ((reg_t)rs1() < (reg_t)rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    case RO_BGEU:
      if ((reg_t)rs1() >= (reg_t)rs2()) {
        int64_t next_pc = get_pc() + boffset();
        set_pc(next_pc);
      }
      break;
    default:
      UNSUPPORTED();
  }
}
void Simulator::DecodeRVUType() {
  // U Type doesn't have additoinal mask
  switch (instr_.BaseOpcodeFieldRaw()) {
    case LUI:
      set_rd(u_imm20());
      break;
    case AUIPC:
      set_rd(sext_xlen(u_imm20() + get_pc()));
      break;
    default:
      UNSUPPORTED();
  }
}
void Simulator::DecodeRVJType() {
  // J Type doesn't have additional mask
  switch (instr_.BaseOpcodeValue()) {
    case JAL: {
      set_rd(get_pc() + kInstrSize);
      int64_t next_pc = get_pc() + imm20J();
      set_pc(next_pc);
      break;
    }
    default:
      UNSUPPORTED();
  }
}
void Simulator::DecodeCRType() {
  switch (instr_.RvcFunct4Value()) {
    case 0b1000:
      if (instr_.RvcRs1Value() != 0 && instr_.RvcRs2Value() == 0) {  // c.jr
        set_pc(rvc_rs1());
      } else if (instr_.RvcRdValue() != 0 &&
                 instr_.RvcRs2Value() != 0) {  // c.mv
        set_rvc_rd(sext_xlen(rvc_rs2()));
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    case 0b1001:
      if (instr_.RvcRs1Value() == 0 && instr_.RvcRs2Value() == 0) {  // c.ebreak
        DieOrDebug();
      } else if (instr_.RvcRdValue() != 0 &&
                 instr_.RvcRs2Value() == 0) {  // c.jalr
        set_register(ra, get_pc() + kShortInstrSize);
        set_pc(rvc_rs1());
      } else if (instr_.RvcRdValue() != 0 &&
                 instr_.RvcRs2Value() != 0) {  // c.add
        set_rvc_rd(sext_xlen(rvc_rs1() + rvc_rs2()));
      } else {
        UNSUPPORTED();
      }
      break;
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCAType() {
  switch (instr_.InstructionBits() & kCATypeMask) {
    case RO_C_SUB:
      set_rvc_rs1s(sext_xlen(rvc_rs1s() - rvc_rs2s()));
      break;
    case RO_C_XOR:
      set_rvc_rs1s(rvc_rs1s() ^ rvc_rs2s());
      break;
    case RO_C_OR:
      set_rvc_rs1s(rvc_rs1s() | rvc_rs2s());
      break;
    case RO_C_AND:
      set_rvc_rs1s(rvc_rs1s() & rvc_rs2s());
      break;
#if V8_TARGET_ARCH_RISCV64
    case RO_C_SUBW:
      set_rvc_rs1s(sext32(rvc_rs1s() - rvc_rs2s()));
      break;
    case RO_C_ADDW:
      set_rvc_rs1s(sext32(rvc_rs1s() + rvc_rs2s()));
      break;
#endif
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCIType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_NOP_ADDI:
      if (instr_.RvcRdValue() == 0)  // c.nop
        break;
      else  // c.addi
        set_rvc_rd(sext_xlen(rvc_rs1() + rvc_imm6()));
      break;
#if V8_TARGET_ARCH_RISCV64
    case RO_C_ADDIW:
      set_rvc_rd(sext32(rvc_rs1() + rvc_imm6()));
      break;
#endif
    case RO_C_LI:
      set_rvc_rd(sext_xlen(rvc_imm6()));
      break;
    case RO_C_LUI_ADD:
      if (instr_.RvcRdValue() == 2) {
        // c.addi16sp
        int64_t value = get_register(sp) + rvc_imm6_addi16sp();
        set_register(sp, value);
      } else if (instr_.RvcRdValue() != 0 && instr_.RvcRdValue() != 2) {
        // c.lui
        set_rvc_rd(rvc_u_imm6());
      } else {
        UNSUPPORTED();
      }
      break;
    case RO_C_SLLI:
      set_rvc_rd(sext_xlen(rvc_rs1() << rvc_shamt6()));
      break;
    case RO_C_FLDSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_ldsp();
      uint64_t val = ReadMem<uint64_t>(addr, instr_.instr());
      set_rvc_drd(Float64::FromBits(val), false);
      TraceMemRdDouble(addr, Float64::FromBits(val),
                       get_fpu_register(rvc_frd_reg()));
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case RO_C_LWSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_lwsp();
      int64_t val = ReadMem<int32_t>(addr, instr_.instr());
      set_rvc_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rvc_rd_reg()));
      break;
    }
    case RO_C_LDSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_ldsp();
      int64_t val = ReadMem<int64_t>(addr, instr_.instr());
      set_rvc_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rvc_rd_reg()));
      break;
    }
#elif V8_TARGET_ARCH_RISCV32
    case RO_C_FLWSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_ldsp();
      uint32_t val = ReadMem<uint32_t>(addr, instr_.instr());
      set_rvc_frd(Float32::FromBits(val), false);
      TraceMemRdFloat(addr, Float32::FromBits(val),
                      get_fpu_register(rvc_frd_reg()));
      break;
    }
    case RO_C_LWSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_lwsp();
      int32_t val = ReadMem<int32_t>(addr, instr_.instr());
      set_rvc_rd(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rvc_rd_reg()));
      break;
    }
#endif
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCIWType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_ADDI4SPN: {
      set_rvc_rs2s(get_register(sp) + rvc_imm8_addi4spn());
      break;
      default:
        UNSUPPORTED();
    }
  }
}

void Simulator::DecodeCSSType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_FSDSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_sdsp();
      WriteMem<Float64>(addr, get_fpu_register_Float64(rvc_rs2_reg()),
                        instr_.instr());
      break;
    }
#if V8_TARGET_ARCH_RISCV32
    case RO_C_FSWSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_sdsp();
      WriteMem<Float32>(addr, get_fpu_register_Float32(rvc_rs2_reg(), false),
                        instr_.instr());
      break;
    }
#endif
    case RO_C_SWSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_swsp();
      WriteMem<int32_t>(addr, (int32_t)rvc_rs2(), instr_.instr());
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case RO_C_SDSP: {
      sreg_t addr = get_register(sp) + rvc_imm6_sdsp();
      WriteMem<int64_t>(addr, (int64_t)rvc_rs2(), instr_.instr());
      break;
    }
#endif
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCLType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_LW: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_w();
      int64_t val = ReadMem<int32_t>(addr, instr_.instr());
      set_rvc_rs2s(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rvc_rs2s_reg()));
      break;
    }
    case RO_C_FLD: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_d();
      uint64_t val = ReadMem<uint64_t>(addr, instr_.instr());
      set_rvc_drs2s(Float64::FromBits(val), false);
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case RO_C_LD: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_d();
      int64_t val = ReadMem<int64_t>(addr, instr_.instr());
      set_rvc_rs2s(sext_xlen(val), false);
      TraceMemRd(addr, val, get_register(rvc_rs2s_reg()));
      break;
    }
#elif V8_TARGET_ARCH_RISCV32
    case RO_C_FLW: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_d();
      uint32_t val = ReadMem<uint32_t>(addr, instr_.instr());
      set_rvc_frs2s(Float32::FromBits(val), false);
      break;
    }
#endif
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCSType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_SW: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_w();
      WriteMem<int32_t>(addr, (int32_t)rvc_rs2s(), instr_.instr());
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case RO_C_SD: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_d();
      WriteMem<int64_t>(addr, (int64_t)rvc_rs2s(), instr_.instr());
      break;
    }
#endif
    case RO_C_FSD: {
      sreg_t addr = rvc_rs1s() + rvc_imm5_d();
      WriteMem<double>(addr, static_cast<double>(rvc_drs2s()), instr_.instr());
      break;
    }
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCJType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_J: {
      set_pc(get_pc() + instr_.RvcImm11CJValue());
      break;
    }
    default:
      UNSUPPORTED();
  }
}

void Simulator::DecodeCBType() {
  switch (instr_.RvcOpcode()) {
    case RO_C_BNEZ:
      if (rvc_rs1() != 0) {
        sreg_t next_pc = get_pc() + rvc_imm8_b();
        set_pc(next_pc);
      }
      break;
    case RO_C_BEQZ:
      if (rvc_rs1() == 0) {
        sreg_t next_pc = get_pc() + rvc_imm8_b();
        set_pc(next_pc);
      }
      break;
    case RO_C_MISC_ALU:
      if (instr_.RvcFunct2BValue() == 0b00) {  // c.srli
        set_rvc_rs1s(sext_xlen(sext_xlen(rvc_rs1s()) >> rvc_shamt6()));
      } else if (instr_.RvcFunct2BValue() == 0b01) {  // c.srai
        require(rvc_shamt6() < xlen);
        set_rvc_rs1s(sext_xlen(sext_xlen(rvc_rs1s()) >> rvc_shamt6()));
      } else if (instr_.RvcFunct2BValue() == 0b10) {  // c.andi
        set_rvc_rs1s(rvc_imm6() & rvc_rs1s());
      } else {
        UNSUPPORTED();
      }
      break;
    default:
      UNSUPPORTED();
  }
}

/**
 * RISCV-ISA-SIM
 *
 * @link      https://github.com/riscv/riscv-isa-sim/
 * @copyright Copyright (c)  The Regents of the University of California
 * @license   hhttps://github.com/riscv/riscv-isa-sim/blob/master/LICENSE
 */
// ref:  https://locklessinc.com/articles/sat_arithmetic/
template <typename T, typename UT>
static inline T sat_add(T x, T y, bool& sat) {
  UT ux = x;
  UT uy = y;
  UT res = ux + uy;
  sat = false;
  int sh = sizeof(T) * 8 - 1;

  /* Calculate overflowed result. (Don't change the sign bit of ux) */
  ux = (ux >> sh) + (((UT)0x1 << sh) - 1);

  /* Force compiler to use cmovns instruction */
  if ((T)((ux ^ uy) | ~(uy ^ res)) >= 0) {
    res = ux;
    sat = true;
  }

  return res;
}

template <typename T, typename UT>
static inline T sat_sub(T x, T y, bool& sat) {
  UT ux = x;
  UT uy = y;
  UT res = ux - uy;
  sat = false;
  int sh = sizeof(T) * 8 - 1;

  /* Calculate overflowed result. (Don't change the sign bit of ux) */
  ux = (ux >> sh) + (((UT)0x1 << sh) - 1);

  /* Force compiler to use cmovns instruction */
  if ((T)((ux ^ uy) & (ux ^ res)) < 0) {
    res = ux;
    sat = true;
  }

  return res;
}

template <typename T>
T sat_addu(T x, T y, bool& sat) {
  T res = x + y;
  sat = false;

  sat = res < x;
  res |= -(res < x);

  return res;
}

template <typename T>
T sat_subu(T x, T y, bool& sat) {
  T res = x - y;
  sat = false;

  sat = !(res <= x);
  res &= -(res <= x);

  return res;
}

#ifdef CAN_USE_RVV_INSTRUCTIONS
void Simulator::DecodeRvvIVV() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_IVV);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VADD_VV: {
      RVV_VI_VV_LOOP({ vd = vs1 + vs2; });
      break;
    }
    case RO_V_VSADD_VV: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VV_PARAMS(8);
          vd = sat_add<int8_t, uint8_t>(vs2, vs1, sat);
          break;
        }
        case E16: {
          VV_PARAMS(16);
          vd = sat_add<int16_t, uint16_t>(vs2, vs1, sat);
          break;
        }
        case E32: {
          VV_PARAMS(32);
          vd = sat_add<int32_t, uint32_t>(vs2, vs1, sat);
          break;
        }
        default: {
          VV_PARAMS(64);
          vd = sat_add<int64_t, uint64_t>(vs2, vs1, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VSADDU_VV:
      RVV_VI_VV_ULOOP({
        vd = vs2 + vs1;
        vd |= -(vd < vs2);
      })
      break;
    case RO_V_VSUB_VV: {
      RVV_VI_VV_LOOP({ vd = vs2 - vs1; })
      break;
    }
    case RO_V_VSSUB_VV: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VV_PARAMS(8);
          vd = sat_sub<int8_t, uint8_t>(vs2, vs1, sat);
          break;
        }
        case E16: {
          VV_PARAMS(16);
          vd = sat_sub<int16_t, uint16_t>(vs2, vs1, sat);
          break;
        }
        case E32: {
          VV_PARAMS(32);
          vd = sat_sub<int32_t, uint32_t>(vs2, vs1, sat);
          break;
        }
        default: {
          VV_PARAMS(64);
          vd = sat_sub<int64_t, uint64_t>(vs2, vs1, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VSSUBU_VV: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VV_UPARAMS(8);
          vd = sat_subu<uint8_t>(vs2, vs1, sat);
          break;
        }
        case E16: {
          VV_UPARAMS(16);
          vd = sat_subu<uint16_t>(vs2, vs1, sat);
          break;
        }
        case E32: {
          VV_UPARAMS(32);
          vd = sat_subu<uint32_t>(vs2, vs1, sat);
          break;
        }
        default: {
          VV_UPARAMS(64);
          vd = sat_subu<uint64_t>(vs2, vs1, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VAND_VV: {
      RVV_VI_VV_LOOP({ vd = vs1 & vs2; })
      break;
    }
    case RO_V_VOR_VV: {
      RVV_VI_VV_LOOP({ vd = vs1 | vs2; })
      break;
    }
    case RO_V_VXOR_VV: {
      RVV_VI_VV_LOOP({ vd = vs1 ^ vs2; })
      break;
    }
    case RO_V_VMAXU_VV: {
      RVV_VI_VV_ULOOP({
        if (vs1 <= vs2) {
          vd = vs2;
        } else {
          vd = vs1;
        }
      })
      break;
    }
    case RO_V_VMAX_VV: {
      RVV_VI_VV_LOOP({
        if (vs1 <= vs2) {
          vd = vs2;
        } else {
          vd = vs1;
        }
      })
      break;
    }
    case RO_V_VMINU_VV: {
      RVV_VI_VV_ULOOP({
        if (vs1 <= vs2) {
          vd = vs1;
        } else {
          vd = vs2;
        }
      })
      break;
    }
    case RO_V_VMIN_VV: {
      RVV_VI_VV_LOOP({
        if (vs1 <= vs2) {
          vd = vs1;
        } else {
          vd = vs2;
        }
      })
      break;
    }
    case RO_V_VMV_VV: {
      if (instr_.RvvVM()) {
        RVV_VI_VVXI_MERGE_LOOP({
          vd = vs1;
          USE(simm5);
          USE(vs2);
          USE(rs1);
        });
      } else {
        RVV_VI_VVXI_MERGE_LOOP({
          bool use_first = (Rvvelt<uint64_t>(0, (i / 64)) >> (i % 64)) & 0x1;
          vd = use_first ? vs1 : vs2;
          USE(simm5);
          USE(rs1);
        });
      }
      break;
    }
    case RO_V_VMSEQ_VV: {
      RVV_VI_VV_LOOP_CMP({ res = vs1 == vs2; })
      break;
    }
    case RO_V_VMSNE_VV: {
      RVV_VI_VV_LOOP_CMP({ res = vs1 != vs2; })
      break;
    }
    case RO_V_VMSLTU_VV: {
      RVV_VI_VV_ULOOP_CMP({ res = vs2 < vs1; })
      break;
    }
    case RO_V_VMSLT_VV: {
      RVV_VI_VV_LOOP_CMP({ res = vs2 < vs1; })
      break;
    }
    case RO_V_VMSLE_VV: {
      RVV_VI_VV_LOOP_CMP({ res = vs2 <= vs1; })
      break;
    }
    case RO_V_VMSLEU_VV: {
      RVV_VI_VV_ULOOP_CMP({ res = vs2 <= vs1; })
      break;
    }
    case RO_V_VADC_VV:
      if (instr_.RvvVM()) {
        RVV_VI_VV_LOOP_WITH_CARRY({
          auto& v0 = Rvvelt<uint64_t>(0, midx);
          vd = vs1 + vs2 + (v0 >> mpos) & 0x1;
        })
      } else {
        UNREACHABLE();
      }
      break;
    case RO_V_VSLL_VV: {
      RVV_VI_VV_LOOP({ vd = vs2 << (vs1 & (rvv_sew() - 1)); })
      break;
    }
    case RO_V_VSRL_VV:
      RVV_VI_VV_ULOOP({ vd = vs2 >> (vs1 & (rvv_sew() - 1)); })
      break;
    case RO_V_VSRA_VV:
      RVV_VI_VV_LOOP({ vd = vs2 >> (vs1 & (rvv_sew() - 1)); })
      break;
    case RO_V_VSMUL_VV: {
      RVV_VI_GENERAL_LOOP_BASE
      RVV_VI_LOOP_MASK_SKIP()
      if (rvv_vsew() == E8) {
        VV_PARAMS(8);
        int16_t result = (int16_t)vs1 * (int16_t)vs2;
        uint8_t round = get_round(static_cast<int>(rvv_vxrm()), result, 7);
        result = (result >> 7) + round;
        vd = signed_saturation<int16_t, int8_t>(result, 8);
      } else if (rvv_vsew() == E16) {
        VV_PARAMS(16);
        int32_t result = (int32_t)vs1 * (int32_t)vs2;
        uint8_t round = get_round(static_cast<int>(rvv_vxrm()), result, 15);
        result = (result >> 15) + round;
        vd = signed_saturation<int32_t, int16_t>(result, 16);
      } else if (rvv_vsew() == E32) {
        VV_PARAMS(32);
        int64_t result = (int64_t)vs1 * (int64_t)vs2;
        uint8_t round = get_round(static_cast<int>(rvv_vxrm()), result, 31);
        result = (result >> 31) + round;
        vd = signed_saturation<int64_t, int32_t>(result, 32);
      } else if (rvv_vsew() == E64) {
        VV_PARAMS(64);
        __int128_t result = (__int128_t)vs1 * (__int128_t)vs2;
        uint8_t round = get_round(static_cast<int>(rvv_vxrm()), result, 63);
        result = (result >> 63) + round;
        vd = signed_saturation<__int128_t, int64_t>(result, 64);
      } else {
        UNREACHABLE();
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
      break;
    }
    case RO_V_VRGATHER_VV: {
      RVV_VI_GENERAL_LOOP_BASE
      CHECK_NE(rvv_vs1_reg(), rvv_vd_reg());
      CHECK_NE(rvv_vs2_reg(), rvv_vd_reg());
      switch (rvv_vsew()) {
        case E8: {
          auto vs1 = Rvvelt<uint8_t>(rvv_vs1_reg(), i);
          // if (i > 255) continue;
          Rvvelt<uint8_t>(rvv_vd_reg(), i, true) =
              vs1 >= rvv_vlmax() ? 0 : Rvvelt<uint8_t>(rvv_vs2_reg(), vs1);
          break;
        }
        case E16: {
          auto vs1 = Rvvelt<uint16_t>(rvv_vs1_reg(), i);
          Rvvelt<uint16_t>(rvv_vd_reg(), i, true) =
              vs1 >= rvv_vlmax() ? 0 : Rvvelt<uint16_t>(rvv_vs2_reg(), vs1);
          break;
        }
        case E32: {
          auto vs1 = Rvvelt<uint32_t>(rvv_vs1_reg(), i);
          Rvvelt<uint32_t>(rvv_vd_reg(), i, true) =
              vs1 >= rvv_vlmax() ? 0 : Rvvelt<uint32_t>(rvv_vs2_reg(), vs1);
          break;
        }
        default: {
          auto vs1 = Rvvelt<uint64_t>(rvv_vs1_reg(), i);
          Rvvelt<uint64_t>(rvv_vd_reg(), i, true) =
              vs1 >= rvv_vlmax() ? 0 : Rvvelt<uint64_t>(rvv_vs2_reg(), vs1);
          break;
        }
      }
      RVV_VI_LOOP_END;
      rvv_trace_vd();
      break;
    }
    default:
      // v8::base::EmbeddedVector<char, 256> buffer;
      // SNPrintF(trace_buf_, " ");
      // disasm::NameConverter converter;
      // disasm::Disassembler dasm(converter);
      // // Use a reasonably large buffer.
      // dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(&instr_));

      // PrintF("EXECUTING  0x%08" PRIxPTR "   %-44s\n",
      //        reinterpret_cast<intptr_t>(&instr_), buffer.begin());
      UNIMPLEMENTED_RISCV();
      break;
  }
  set_rvv_vstart(0);
}

void Simulator::DecodeRvvIVI() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_IVI);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VADD_VI: {
      RVV_VI_VI_LOOP({ vd = simm5 + vs2; })
      break;
    }
    case RO_V_VSADD_VI: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VI_PARAMS(8);
          vd = sat_add<int8_t, uint8_t>(vs2, simm5, sat);
          break;
        }
        case E16: {
          VI_PARAMS(16);
          vd = sat_add<int16_t, uint16_t>(vs2, simm5, sat);
          break;
        }
        case E32: {
          VI_PARAMS(32);
          vd = sat_add<int32_t, uint32_t>(vs2, simm5, sat);
          break;
        }
        default: {
          VI_PARAMS(64);
          vd = sat_add<int64_t, uint64_t>(vs2, simm5, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VSADDU_VI: {
      RVV_VI_VI_ULOOP({
        vd = vs2 + uimm5;
        vd |= -(vd < vs2);
      })
      break;
    }
    case RO_V_VRSUB_VI: {
      RVV_VI_VI_LOOP({ vd = simm5 - vs2; })
      break;
    }
    case RO_V_VAND_VI: {
      RVV_VI_VI_LOOP({ vd = simm5 & vs2; })
      break;
    }
    case RO_V_VOR_VI: {
      RVV_VI_VI_LOOP({ vd = simm5 | vs2; })
      break;
    }
    case RO_V_VXOR_VI: {
      RVV_VI_VI_LOOP({ vd = simm5 ^ vs2; })
      break;
    }
    case RO_V_VMV_VI:
      if (instr_.RvvVM()) {
        RVV_VI_VVXI_MERGE_LOOP({
          vd = simm5;
          USE(vs1);
          USE(vs2);
          USE(rs1);
        });
      } else {
        RVV_VI_VVXI_MERGE_LOOP({
          bool use_first = (Rvvelt<uint64_t>(0, (i / 64)) >> (i % 64)) & 0x1;
          vd = use_first ? simm5 : vs2;
          USE(vs1);
          USE(rs1);
        });
      }
      break;
    case RO_V_VMSEQ_VI:
      RVV_VI_VI_LOOP_CMP({ res = simm5 == vs2; })
      break;
    case RO_V_VMSNE_VI:
      RVV_VI_VI_LOOP_CMP({ res = simm5 != vs2; })
      break;
    case RO_V_VMSLEU_VI:
      RVV_VI_VI_ULOOP_CMP({ res = vs2 <= uimm5; })
      break;
    case RO_V_VMSLE_VI:
      RVV_VI_VI_LOOP_CMP({ res = vs2 <= simm5; })
      break;
    case RO_V_VMSGT_VI:
      RVV_VI_VI_LOOP_CMP({ res = vs2 > simm5; })
      break;
    case RO_V_VSLIDEDOWN_VI: {
      RVV_VI_CHECK_SLIDE(false);
      const uint8_t sh = instr_.RvvUimm5();
      RVV_VI_GENERAL_LOOP_BASE

      reg_t offset = 0;
      bool is_valid = (i + sh) < rvv_vlmax();

      if (is_valid) {
        offset = sh;
      }

      switch (rvv_vsew()) {
        case E8: {
          VI_XI_SLIDEDOWN_PARAMS(8, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        case E16: {
          VI_XI_SLIDEDOWN_PARAMS(16, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        case E32: {
          VI_XI_SLIDEDOWN_PARAMS(32, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        default: {
          VI_XI_SLIDEDOWN_PARAMS(64, offset);
          vd = is_valid ? vs2 : 0;
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VSLIDEUP_VI: {
      RVV_VI_CHECK_SLIDE(true);

      const uint8_t offset = instr_.RvvUimm5();
      RVV_VI_GENERAL_LOOP_BASE
      if (rvv_vstart() < offset && i < offset) continue;

      switch (rvv_vsew()) {
        case E8: {
          VI_XI_SLIDEUP_PARAMS(8, offset);
          vd = vs2;
        } break;
        case E16: {
          VI_XI_SLIDEUP_PARAMS(16, offset);
          vd = vs2;
        } break;
        case E32: {
          VI_XI_SLIDEUP_PARAMS(32, offset);
          vd = vs2;
        } break;
        default: {
          VI_XI_SLIDEUP_PARAMS(64, offset);
          vd = vs2;
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VSRL_VI:
      RVV_VI_VI_ULOOP({ vd = vs2 >> (uimm5 & (rvv_sew() - 1)); })
      break;
    case RO_V_VSRA_VI:
      RVV_VI_VI_LOOP({ vd = vs2 >> (simm5 & (rvv_sew() - 1) & 0x1f); })
      break;
    case RO_V_VSLL_VI:
      RVV_VI_VI_ULOOP({ vd = vs2 << (uimm5 & (rvv_sew() - 1)); })
      break;
    case RO_V_VADC_VI:
      if (instr_.RvvVM()) {
        RVV_VI_XI_LOOP_WITH_CARRY({
          auto& v0 = Rvvelt<uint64_t>(0, midx);
          vd = simm5 + vs2 + (v0 >> mpos) & 0x1;
          USE(rs1);
        })
      } else {
        UNREACHABLE();
      }
      break;
    case RO_V_VNCLIP_WI:
      RVV_VN_CLIP_VI_LOOP()
      break;
    case RO_V_VNCLIPU_WI:
      RVV_VN_CLIPU_VI_LOOP()
      break;
    default:
      UNIMPLEMENTED_RISCV();
      break;
  }
}

void Simulator::DecodeRvvIVX() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_IVX);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VADD_VX: {
      RVV_VI_VX_LOOP({ vd = rs1 + vs2; })
      break;
    }
    case RO_V_VSADD_VX: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VX_PARAMS(8);
          vd = sat_add<int8_t, uint8_t>(vs2, rs1, sat);
          break;
        }
        case E16: {
          VX_PARAMS(16);
          vd = sat_add<int16_t, uint16_t>(vs2, rs1, sat);
          break;
        }
        case E32: {
          VX_PARAMS(32);
          vd = sat_add<int32_t, uint32_t>(vs2, rs1, sat);
          break;
        }
        default: {
          VX_PARAMS(64);
          vd = sat_add<int64_t, uint64_t>(vs2, rs1, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VSADDU_VX: {
      RVV_VI_VX_ULOOP({
        vd = vs2 + rs1;
        vd |= -(vd < vs2);
      })
      break;
    }
    case RO_V_VSUB_VX: {
      RVV_VI_VX_LOOP({ vd = vs2 - rs1; })
      break;
    }
    case RO_V_VSSUB_VX: {
      RVV_VI_GENERAL_LOOP_BASE
      bool sat = false;
      switch (rvv_vsew()) {
        case E8: {
          VX_PARAMS(8);
          vd = sat_sub<int8_t, uint8_t>(vs2, rs1, sat);
          break;
        }
        case E16: {
          VX_PARAMS(16);
          vd = sat_sub<int16_t, uint16_t>(vs2, rs1, sat);
          break;
        }
        case E32: {
          VX_PARAMS(32);
          vd = sat_sub<int32_t, uint32_t>(vs2, rs1, sat);
          break;
        }
        default: {
          VX_PARAMS(64);
          vd = sat_sub<int64_t, uint64_t>(vs2, rs1, sat);
          break;
        }
      }
      set_rvv_vxsat(sat);
      RVV_VI_LOOP_END
      break;
    }
    case RO_V_VRSUB_VX: {
      RVV_VI_VX_LOOP({ vd = rs1 - vs2; })
      break;
    }
    case RO_V_VAND_VX: {
      RVV_VI_VX_LOOP({ vd = rs1 & vs2; })
      break;
    }
    case RO_V_VOR_VX: {
      RVV_VI_VX_LOOP({ vd = rs1 | vs2; })
      break;
    }
    case RO_V_VXOR_VX: {
      RVV_VI_VX_LOOP({ vd = rs1 ^ vs2; })
      break;
    }
    case RO_V_VMAX_VX: {
      RVV_VI_VX_LOOP({
        if (rs1 <= vs2) {
          vd = vs2;
        } else {
          vd = rs1;
        }
      })
      break;
    }
    case RO_V_VMAXU_VX: {
      RVV_VI_VX_ULOOP({
        if (rs1 <= vs2) {
          vd = vs2;
        } else {
          vd = rs1;
        }
      })
      break;
    }
    case RO_V_VMINU_VX: {
      RVV_VI_VX_ULOOP({
        if (rs1 <= vs2) {
          vd = rs1;
        } else {
          vd = vs2;
        }
      })
      break;
    }
    case RO_V_VMIN_VX: {
      RVV_VI_VX_LOOP({
        if (rs1 <= vs2) {
          vd = rs1;
        } else {
          vd = vs2;
        }
      })
      break;
    }
    case RO_V_VMV_VX:
      if (instr_.RvvVM()) {
        RVV_VI_VVXI_MERGE_LOOP({
          vd = rs1;
          USE(vs1);
          USE(vs2);
          USE(simm5);
        });
      } else {
        RVV_VI_VVXI_MERGE_LOOP({
          bool use_first = (Rvvelt<uint64_t>(0, (i / 64)) >> (i % 64)) & 0x1;
          vd = use_first ? rs1 : vs2;
          USE(vs1);
          USE(simm5);
        });
      }
      break;
    case RO_V_VMSEQ_VX:
      RVV_VI_VX_LOOP_CMP({ res = vs2 == rs1; })
      break;
    case RO_V_VMSNE_VX:
      RVV_VI_VX_LOOP_CMP({ res = vs2 != rs1; })
      break;
    case RO_V_VMSLT_VX:
      RVV_VI_VX_LOOP_CMP({ res = vs2 < rs1; })
      break;
    case RO_V_VMSLTU_VX:
      RVV_VI_VX_ULOOP_CMP({ res = vs2 < rs1; })
      break;
    case RO_V_VMSLE_VX:
      RVV_VI_VX_LOOP_CMP({ res = vs2 <= rs1; })
      break;
    case RO_V_VMSLEU_VX:
      RVV_VI_VX_ULOOP_CMP({ res = vs2 <= rs1; })
      break;
    case RO_V_VMSGT_VX:
      RVV_VI_VX_LOOP_CMP({ res = vs2 > rs1; })
      break;
    case RO_V_VMSGTU_VX:
      RVV_VI_VX_ULOOP_CMP({ res = vs2 > rs1; })
      break;
    case RO_V_VSLIDEDOWN_VX: {
      RVV_VI_CHECK_SLIDE(false);

      const sreg_t sh = get_register(rs1_reg());
      RVV_VI_GENERAL_LOOP_BASE

      reg_t offset = 0;
      bool is_valid = (i + sh) < rvv_vlmax();

      if (is_valid) {
        offset = sh;
      }

      switch (rvv_vsew()) {
        case E8: {
          VI_XI_SLIDEDOWN_PARAMS(8, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        case E16: {
          VI_XI_SLIDEDOWN_PARAMS(16, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        case E32: {
          VI_XI_SLIDEDOWN_PARAMS(32, offset);
          vd = is_valid ? vs2 : 0;
        } break;
        default: {
          VI_XI_SLIDEDOWN_PARAMS(64, offset);
          vd = is_valid ? vs2 : 0;
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VSLIDEUP_VX: {
      RVV_VI_CHECK_SLIDE(true);

      const reg_t offset = get_register(rs1_reg());
      RVV_VI_GENERAL_LOOP_BASE
      if (rvv_vstart() < offset && i < offset) continue;

      switch (rvv_vsew()) {
        case E8: {
          VI_XI_SLIDEUP_PARAMS(8, offset);
          vd = vs2;
        } break;
        case E16: {
          VI_XI_SLIDEUP_PARAMS(16, offset);
          vd = vs2;
        } break;
        case E32: {
          VI_XI_SLIDEUP_PARAMS(32, offset);
          vd = vs2;
        } break;
        default: {
          VI_XI_SLIDEUP_PARAMS(64, offset);
          vd = vs2;
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VADC_VX:
      if (instr_.RvvVM()) {
        RVV_VI_XI_LOOP_WITH_CARRY({
          auto& v0 = Rvvelt<uint64_t>(0, midx);
          vd = rs1 + vs2 + (v0 >> mpos) & 0x1;
          USE(simm5);
        })
      } else {
        UNREACHABLE();
      }
      break;
    case RO_V_VSLL_VX: {
      RVV_VI_VX_LOOP({ vd = vs2 << (rs1 & (rvv_sew() - 1)); })
      break;
    }
    case RO_V_VSRL_VX: {
      RVV_VI_VX_ULOOP({ vd = (vs2 >> (rs1 & (rvv_sew() - 1))); })
      break;
    }
    case RO_V_VSRA_VX: {
      RVV_VI_VX_LOOP({ vd = ((vs2) >> (rs1 & (rvv_sew() - 1))); })
      break;
    }
    default:
      UNIMPLEMENTED_RISCV();
      break;
  }
}

void Simulator::DecodeRvvMVV() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_MVV);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VMUNARY0: {
      if (instr_.Vs1Value() == VID_V) {
        CHECK(rvv_vsew() >= E8 && rvv_vsew() <= E64);
        uint8_t rd_num = rvv_vd_reg();
        require_align(rd_num, rvv_vflmul());
        require_vm;
        for (uint8_t i = rvv_vstart(); i < rvv_vl(); ++i) {
          RVV_VI_LOOP_MASK_SKIP();
          switch (rvv_vsew()) {
            case E8:
              Rvvelt<uint8_t>(rd_num, i, true) = i;
              break;
            case E16:
              Rvvelt<uint16_t>(rd_num, i, true) = i;
              break;
            case E32:
              Rvvelt<uint32_t>(rd_num, i, true) = i;
              break;
            default:
              Rvvelt<uint64_t>(rd_num, i, true) = i;
              break;
          }
        }
        set_rvv_vstart(0);
      } else {
        UNIMPLEMENTED_RISCV();
      }
      break;
    }
    case RO_V_VMUL_VV: {
      RVV_VI_VV_LOOP({ vd = vs2 * vs1; })
      break;
    }
    case RO_V_VWMUL_VV: {
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VV_LOOP_WIDEN({
        VI_WIDE_OP_AND_ASSIGN(vs2, vs1, 0, *, +, int);
        USE(vd);
      })
      break;
    }
    case RO_V_VWMULU_VV: {
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VV_LOOP_WIDEN({
        VI_WIDE_OP_AND_ASSIGN(vs2, vs1, 0, *, +, uint);
        USE(vd);
      })
      break;
    }
    case RO_V_VMULHU_VV: {
      RVV_VI_VV_LOOP({ vd = ((__uint128_t)vs2 * vs1) >> rvv_sew(); })
      break;
    }
    case RO_V_VMULH_VV: {
      RVV_VI_VV_LOOP({ vd = ((__int128_t)vs2 * vs1) >> rvv_sew(); })
      break;
    }
    case RO_V_VDIV_VV: {
      RVV_VI_VV_LOOP({ vd = vs2 / vs1; })
      break;
    }
    case RO_V_VDIVU_VV: {
      RVV_VI_VV_LOOP({ vd = vs2 / vs1; })
      break;
    }
    case RO_V_VWXUNARY0: {
      if (rvv_vs1_reg() == 0) {
        // vmv.x.s
        switch (rvv_vsew()) {
          case E8:
            set_rd(Rvvelt<type_sew_t<8>::type>(rvv_vs2_reg(), 0));
            break;
          case E16:
            set_rd(Rvvelt<type_sew_t<16>::type>(rvv_vs2_reg(), 0));
            break;
          case E32:
            set_rd(Rvvelt<type_sew_t<32>::type>(rvv_vs2_reg(), 0));
            break;
          case E64:
            set_rd(Rvvelt<type_sew_t<64>::type>(rvv_vs2_reg(), 0));
            break;
          default:
            UNREACHABLE();
        }
        set_rvv_vstart(0);
        rvv_trace_vd();
      } else if (rvv_vs1_reg() == 0b10000) {
        // vpopc
        reg_t cnt = 0;
        RVV_VI_GENERAL_LOOP_BASE
        RVV_VI_LOOP_MASK_SKIP()
        const uint8_t idx = i / 64;
        const uint8_t pos = i % 64;
        bool mask = (Rvvelt<uint64_t>(rvv_vs2_reg(), idx) >> pos) & 0x1;
        if (mask) cnt++;
        RVV_VI_LOOP_END
        set_register(rd_reg(), cnt);
        rvv_trace_vd();
      } else if (rvv_vs1_reg() == 0b10001) {
        // vfirst
        sreg_t index = -1;
        RVV_VI_GENERAL_LOOP_BASE
        RVV_VI_LOOP_MASK_SKIP()
        const uint8_t idx = i / 64;
        const uint8_t pos = i % 64;
        bool mask = (Rvvelt<uint64_t>(rvv_vs2_reg(), idx) >> pos) & 0x1;
        if (mask) {
          index = i;
          break;
        }
        RVV_VI_LOOP_END
        set_register(rd_reg(), index);
        rvv_trace_vd();
      } else {
        v8::base::EmbeddedVector<char, 256> buffer;
        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(&instr_));
        PrintF("EXECUTING  0x%08" PRIxPTR "   %-44s\n",
               reinterpret_cast<intptr_t>(&instr_), buffer.begin());
        UNIMPLEMENTED_RISCV();
      }
    } break;
    case RO_V_VREDMAXU:
      RVV_VI_VV_ULOOP_REDUCTION(
          { vd_0_res = (vd_0_res >= vs2) ? vd_0_res : vs2; })
      break;
    case RO_V_VREDMAX:
      RVV_VI_VV_LOOP_REDUCTION(
          { vd_0_res = (vd_0_res >= vs2) ? vd_0_res : vs2; })
      break;
    case RO_V_VREDMINU:
      RVV_VI_VV_ULOOP_REDUCTION(
          { vd_0_res = (vd_0_res <= vs2) ? vd_0_res : vs2; })
      break;
    case RO_V_VREDMIN:
      RVV_VI_VV_LOOP_REDUCTION(
          { vd_0_res = (vd_0_res <= vs2) ? vd_0_res : vs2; })
      break;
    case RO_V_VXUNARY0:
      if (rvv_vs1_reg() == 0b00010) {
        RVV_VI_VIE_8_LOOP(false);
      } else if (rvv_vs1_reg() == 0b00011) {
        RVV_VI_VIE_8_LOOP(true);
      } else if (rvv_vs1_reg() == 0b00100) {
        RVV_VI_VIE_4_LOOP(false);
      } else if (rvv_vs1_reg() == 0b00101) {
        RVV_VI_VIE_4_LOOP(true);
      } else if (rvv_vs1_reg() == 0b00110) {
        RVV_VI_VIE_2_LOOP(false);
      } else if (rvv_vs1_reg() == 0b00111) {
        RVV_VI_VIE_2_LOOP(true);
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    case RO_V_VWADDU_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VV_LOOP_WIDEN({
        VI_WIDE_OP_AND_ASSIGN(vs2, vs1, 0, +, +, uint);
        USE(vd);
      })
      break;
    case RO_V_VWADD_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VV_LOOP_WIDEN({
        VI_WIDE_OP_AND_ASSIGN(vs2, vs1, 0, +, +, int);
        USE(vd);
      })
      break;
    case RO_V_VCOMPRESS_VV: {
      CHECK_EQ(rvv_vstart(), 0);
      require_align(rvv_vd_reg(), rvv_vflmul());
      require_align(rvv_vs2_reg(), rvv_vflmul());
      require(rvv_vd_reg() != rvv_vs2_reg());
      require_noover(rvv_vd_reg(), rvv_vflmul(), rvv_vs1_reg(), 1);

      reg_t pos = 0;

      RVV_VI_GENERAL_LOOP_BASE
      const uint64_t midx = i / 64;
      const uint64_t mpos = i % 64;

      bool do_mask = (Rvvelt<uint64_t>(rvv_vs1_reg(), midx) >> mpos) & 0x1;
      if (do_mask) {
        switch (rvv_vsew()) {
          case E8:
            Rvvelt<uint8_t>(rvv_vd_reg(), pos, true) =
                Rvvelt<uint8_t>(rvv_vs2_reg(), i);
            break;
          case E16:
            Rvvelt<uint16_t>(rvv_vd_reg(), pos, true) =
                Rvvelt<uint16_t>(rvv_vs2_reg(), i);
            break;
          case E32:
            Rvvelt<uint32_t>(rvv_vd_reg(), pos, true) =
                Rvvelt<uint32_t>(rvv_vs2_reg(), i);
            break;
          default:
            Rvvelt<uint64_t>(rvv_vd_reg(), pos, true) =
                Rvvelt<uint64_t>(rvv_vs2_reg(), i);
            break;
        }

        ++pos;
      }
      RVV_VI_LOOP_END;
      rvv_trace_vd();
    } break;
    default:
      v8::base::EmbeddedVector<char, 256> buffer;
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(&instr_));
      PrintF("EXECUTING  0x%08" PRIxPTR "   %-44s\n",
             reinterpret_cast<intptr_t>(&instr_), buffer.begin());
      UNIMPLEMENTED_RISCV();
      break;
  }
}

void Simulator::DecodeRvvMVX() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_MVX);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VRXUNARY0:
      // vmv.s.x
      if (instr_.Vs2Value() == 0x0) {
        if (rvv_vl() > 0 && rvv_vstart() < rvv_vl()) {
          switch (rvv_vsew()) {
            case E8:
              Rvvelt<uint8_t>(rvv_vd_reg(), 0, true) =
                  (uint8_t)get_register(rs1_reg());
              break;
            case E16:
              Rvvelt<uint16_t>(rvv_vd_reg(), 0, true) =
                  (uint16_t)get_register(rs1_reg());
              break;
            case E32:
              Rvvelt<uint32_t>(rvv_vd_reg(), 0, true) =
                  (uint32_t)get_register(rs1_reg());
              break;
            case E64:
              Rvvelt<uint64_t>(rvv_vd_reg(), 0, true) =
                  (uint64_t)get_register(rs1_reg());
              break;
            default:
              UNREACHABLE();
          }
        }
        set_rvv_vstart(0);
        rvv_trace_vd();
      } else {
        UNSUPPORTED_RISCV();
      }
      break;
    case RO_V_VDIV_VX: {
      RVV_VI_VX_LOOP({ vd = vs2 / rs1; })
      break;
    }
    case RO_V_VDIVU_VX: {
      RVV_VI_VX_ULOOP({ vd = vs2 / rs1; })
      break;
    }
    case RO_V_VMUL_VX: {
      RVV_VI_VX_LOOP({ vd = vs2 * rs1; })
      break;
    }
    case RO_V_VWADDUW_VX: {
      RVV_VI_CHECK_DDS(false);
      RVV_VI_VX_LOOP_WIDEN({
        VI_WIDE_WVX_OP(rs1, +, uint);
        USE(vd);
        USE(vs2);
      })
      break;
    }
    case RO_V_VSLIDE1DOWN_VX: {
      RVV_VI_CHECK_SLIDE(false);
      RVV_VI_GENERAL_LOOP_BASE
      switch (rvv_vsew()) {
        case E8: {
          VX_SLIDE1DOWN_PARAMS(8, 1);
        } break;
        case E16: {
          VX_SLIDE1DOWN_PARAMS(16, 1);
        } break;
        case E32: {
          VX_SLIDE1DOWN_PARAMS(32, 1);
        } break;
        default: {
          VX_SLIDE1DOWN_PARAMS(64, 1);
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VSLIDE1UP_VX: {
      RVV_VI_CHECK_SLIDE(true);
      RVV_VI_GENERAL_LOOP_BASE
      if (i < rvv_vstart()) continue;
      switch (rvv_vsew()) {
        case E8: {
          VX_SLIDE1UP_PARAMS(8, 1);
        } break;
        case E16: {
          VX_SLIDE1UP_PARAMS(16, 1);
        } break;
        case E32: {
          VX_SLIDE1UP_PARAMS(32, 1);
        } break;
        default: {
          VX_SLIDE1UP_PARAMS(64, 1);
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    default:
      v8::base::EmbeddedVector<char, 256> buffer;
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(&instr_));
      PrintF("EXECUTING  0x%08" PRIxPTR "   %-44s\n",
             reinterpret_cast<intptr_t>(&instr_), buffer.begin());
      UNIMPLEMENTED_RISCV();
      break;
  }
}

void Simulator::DecodeRvvFVV() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_FVV);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VFDIV_VV: {
      RVV_VI_VFP_VV_LOOP(
          { UNIMPLEMENTED(); },
          {
            // TODO(riscv): use rm value (round mode)
            auto fn = [this](float vs1, float vs2) {
              if (is_invalid_fdiv(vs1, vs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<float>::quiet_NaN();
              } else if (vs1 == 0.0f) {
                this->set_fflags(kDivideByZero);
                return (std::signbit(vs1) == std::signbit(vs2)
                            ? std::numeric_limits<float>::infinity()
                            : -std::numeric_limits<float>::infinity());
              } else {
                return vs2 / vs1;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
            }
            vd = alu_out;
          },
          {
            // TODO(riscv): use rm value (round mode)
            auto fn = [this](double vs1, double vs2) {
              if (is_invalid_fdiv(vs1, vs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else if (vs1 == 0.0f) {
                this->set_fflags(kDivideByZero);
                return (std::signbit(vs1) == std::signbit(vs2)
                            ? std::numeric_limits<double>::infinity()
                            : -std::numeric_limits<double>::infinity());
              } else {
                return vs2 / vs1;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<double>::quiet_NaN();
            }
            vd = alu_out;
          })
      break;
    }
    case RO_V_VFMUL_VV: {
      RVV_VI_VFP_VV_LOOP(
          { UNIMPLEMENTED(); },
          {
            // TODO(riscv): use rm value (round mode)
            auto fn = [this](double drs1, double drs2) {
              if (is_invalid_fmul(drs1, drs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else {
                return drs1 * drs2;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
            }
            vd = alu_out;
          },
          {
            // TODO(riscv): use rm value (round mode)
            auto fn = [this](double drs1, double drs2) {
              if (is_invalid_fmul(drs1, drs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else {
                return drs1 * drs2;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<double>::quiet_NaN();
            }
            vd = alu_out;
          })
      break;
    }
    case RO_V_VFUNARY0:
      switch (instr_.Vs1Value()) {
        case VFCVT_X_F_V:
          RVV_VI_VFP_VF_LOOP(
              { UNIMPLEMENTED(); },
              {
                Rvvelt<int32_t>(rvv_vd_reg(), i) =
                    RoundF2IHelper<int32_t>(vs2, read_csr_value(csr_frm));
                USE(vd);
                USE(fs1);
              },
              {
                Rvvelt<int64_t>(rvv_vd_reg(), i) =
                    RoundF2IHelper<int64_t>(vs2, read_csr_value(csr_frm));
                USE(vd);
                USE(fs1);
              })
          break;
        case VFCVT_XU_F_V:
          RVV_VI_VFP_VF_LOOP(
              { UNIMPLEMENTED(); },
              {
                Rvvelt<uint32_t>(rvv_vd_reg(), i) =
                    RoundF2IHelper<uint32_t>(vs2, read_csr_value(csr_frm));
                USE(vd);
                USE(fs1);
              },
              {
                Rvvelt<uint64_t>(rvv_vd_reg(), i) =
                    RoundF2IHelper<uint64_t>(vs2, read_csr_value(csr_frm));
                USE(vd);
                USE(fs1);
              })
          break;
        case VFCVT_F_XU_V:
          RVV_VI_VFP_VF_LOOP({ UNIMPLEMENTED(); },
                             {
                               auto vs2_i = Rvvelt<uint32_t>(rvv_vs2_reg(), i);
                               vd = static_cast<float>(vs2_i);
                               USE(vs2);
                               USE(fs1);
                             },
                             {
                               auto vs2_i = Rvvelt<uint64_t>(rvv_vs2_reg(), i);
                               vd = static_cast<double>(vs2_i);
                               USE(vs2);
                               USE(fs1);
                             })
          break;
        case VFCVT_F_X_V:
          RVV_VI_VFP_VF_LOOP({ UNIMPLEMENTED(); },
                             {
                               auto vs2_i = Rvvelt<int32_t>(rvv_vs2_reg(), i);
                               vd = static_cast<float>(vs2_i);
                               USE(vs2);
                               USE(fs1);
                             },
                             {
                               auto vs2_i = Rvvelt<int64_t>(rvv_vs2_reg(), i);
                               vd = static_cast<double>(vs2_i);
                               USE(vs2);
                               USE(fs1);
                             })
          break;
        case VFNCVT_F_F_W:
          RVV_VI_VFP_CVT_SCALE(
              { UNREACHABLE(); }, { UNREACHABLE(); },
              {
                auto vs2 = Rvvelt<double>(rvv_vs2_reg(), i);
                Rvvelt<float>(rvv_vd_reg(), i, true) =
                    CanonicalizeDoubleToFloatOperation(
                        [](double drs) { return static_cast<float>(drs); },
                        vs2);
              },
              { ; }, { ; }, { ; }, false, (rvv_vsew() >= E16))
          break;
        case VFNCVT_X_F_W:
          RVV_VI_VFP_CVT_SCALE(
              { UNREACHABLE(); }, { UNREACHABLE(); },
              {
                auto vs2 = Rvvelt<double>(rvv_vs2_reg(), i);
                int32_t& vd = Rvvelt<int32_t>(rvv_vd_reg(), i, true);
                vd = RoundF2IHelper<int32_t>(vs2, read_csr_value(csr_frm));
              },
              { ; }, { ; }, { ; }, false, (rvv_vsew() <= E32))
          break;
        case VFNCVT_XU_F_W:
          RVV_VI_VFP_CVT_SCALE(
              { UNREACHABLE(); }, { UNREACHABLE(); },
              {
                auto vs2 = Rvvelt<double>(rvv_vs2_reg(), i);
                uint32_t& vd = Rvvelt<uint32_t>(rvv_vd_reg(), i, true);
                vd = RoundF2IHelper<uint32_t>(vs2, read_csr_value(csr_frm));
              },
              { ; }, { ; }, { ; }, false, (rvv_vsew() <= E32))
          break;
        case VFWCVT_F_X_V:
          RVV_VI_VFP_CVT_SCALE({ UNREACHABLE(); },
                               {
                                 auto vs2 = Rvvelt<int16_t>(rvv_vs2_reg(), i);
                                 Rvvelt<float32_t>(rvv_vd_reg(), i, true) =
                                     static_cast<float>(vs2);
                               },
                               {
                                 auto vs2 = Rvvelt<int32_t>(rvv_vs2_reg(), i);
                                 Rvvelt<double>(rvv_vd_reg(), i, true) =
                                     static_cast<double>(vs2);
                               },
                               { ; }, { ; }, { ; }, true, (rvv_vsew() >= E8))
          break;
        case VFWCVT_F_XU_V:
          RVV_VI_VFP_CVT_SCALE({ UNREACHABLE(); },
                               {
                                 auto vs2 = Rvvelt<uint16_t>(rvv_vs2_reg(), i);
                                 Rvvelt<float32_t>(rvv_vd_reg(), i, true) =
                                     static_cast<float>(vs2);
                               },
                               {
                                 auto vs2 = Rvvelt<uint32_t>(rvv_vs2_reg(), i);
                                 Rvvelt<double>(rvv_vd_reg(), i, true) =
                                     static_cast<double>(vs2);
                               },
                               { ; }, { ; }, { ; }, true, (rvv_vsew() >= E8))
          break;
        case VFWCVT_XU_F_V:
          RVV_VI_VFP_CVT_SCALE({ UNREACHABLE(); }, { UNREACHABLE(); },
                               {
                                 auto vs2 = Rvvelt<float32_t>(rvv_vs2_reg(), i);
                                 Rvvelt<uint64_t>(rvv_vd_reg(), i, true) =
                                     static_cast<uint64_t>(vs2);
                               },
                               { ; }, { ; }, { ; }, true, (rvv_vsew() >= E16))
          break;
        case VFWCVT_X_F_V:
          RVV_VI_VFP_CVT_SCALE({ UNREACHABLE(); }, { UNREACHABLE(); },
                               {
                                 auto vs2 = Rvvelt<float32_t>(rvv_vs2_reg(), i);
                                 Rvvelt<int64_t>(rvv_vd_reg(), i, true) =
                                     static_cast<int64_t>(vs2);
                               },
                               { ; }, { ; }, { ; }, true, (rvv_vsew() >= E16))
          break;
        case VFWCVT_F_F_V:
          RVV_VI_VFP_CVT_SCALE({ UNREACHABLE(); }, { UNREACHABLE(); },
                               {
                                 auto vs2 = Rvvelt<float32_t>(rvv_vs2_reg(), i);
                                 Rvvelt<double>(rvv_vd_reg(), i, true) =
                                     static_cast<double>(vs2);
                               },
                               { ; }, { ; }, { ; }, true, (rvv_vsew() >= E16))
          break;
        default:
          UNSUPPORTED_RISCV();
      }
      break;
    case RO_V_VFUNARY1:
      switch (instr_.Vs1Value()) {
        case VFCLASS_V:
          RVV_VI_VFP_VF_LOOP(
              { UNIMPLEMENTED(); },
              {
                int32_t& vd_i = Rvvelt<int32_t>(rvv_vd_reg(), i, true);
                vd_i = int32_t(FclassHelper(vs2));
                USE(fs1);
                USE(vd);
              },
              {
                int64_t& vd_i = Rvvelt<int64_t>(rvv_vd_reg(), i, true);
                vd_i = FclassHelper(vs2);
                USE(fs1);
                USE(vd);
              })
          break;
        case VFSQRT_V:
          RVV_VI_VFP_VF_LOOP({ UNIMPLEMENTED(); },
                             {
                               vd = std::sqrt(vs2);
                               USE(fs1);
                             },
                             {
                               vd = std::sqrt(vs2);
                               USE(fs1);
                             })
          break;
        case VFRSQRT7_V:
          RVV_VI_VFP_VF_LOOP(
              {},
              {
                vd = base::RecipSqrt(vs2);
                USE(fs1);
              },
              {
                vd = base::RecipSqrt(vs2);
                USE(fs1);
              })
          break;
        case VFREC7_V:
          RVV_VI_VFP_VF_LOOP(
              {},
              {
                vd = base::Recip(vs2);
                USE(fs1);
              },
              {
                vd = base::Recip(vs2);
                USE(fs1);
              })
          break;
        default:
          break;
      }
      break;
    case RO_V_VMFEQ_VV: {
      RVV_VI_VFP_LOOP_CMP({ UNIMPLEMENTED(); },
                          { res = CompareFHelper(vs2, vs1, EQ); },
                          { res = CompareFHelper(vs2, vs1, EQ); }, true)
    } break;
    case RO_V_VMFNE_VV: {
      RVV_VI_VFP_LOOP_CMP({ UNIMPLEMENTED(); },
                          { res = CompareFHelper(vs2, vs1, NE); },
                          { res = CompareFHelper(vs2, vs1, NE); }, true)
    } break;
    case RO_V_VMFLT_VV: {
      RVV_VI_VFP_LOOP_CMP({ UNIMPLEMENTED(); },
                          { res = CompareFHelper(vs2, vs1, LT); },
                          { res = CompareFHelper(vs2, vs1, LT); }, true)
    } break;
    case RO_V_VMFLE_VV: {
      RVV_VI_VFP_LOOP_CMP({ UNIMPLEMENTED(); },
                          { res = CompareFHelper(vs2, vs1, LE); },
                          { res = CompareFHelper(vs2, vs1, LE); }, true)
    } break;
    case RO_V_VFMAX_VV: {
      RVV_VI_VFP_VV_LOOP({ UNIMPLEMENTED(); },
                         { vd = FMaxMinHelper(vs2, vs1, MaxMinKind::kMax); },
                         { vd = FMaxMinHelper(vs2, vs1, MaxMinKind::kMax); })
      break;
    }
    case RO_V_VFREDMAX_VV: {
      RVV_VI_VFP_VV_LOOP_REDUCTION(
          { UNIMPLEMENTED(); },
          { vd_0 = FMaxMinHelper(vd_0, vs2, MaxMinKind::kMax); },
          { vd_0 = FMaxMinHelper(vd_0, vs2, MaxMinKind::kMax); })
      break;
    }
    case RO_V_VFMIN_VV: {
      RVV_VI_VFP_VV_LOOP({ UNIMPLEMENTED(); },
                         { vd = FMaxMinHelper(vs2, vs1, MaxMinKind::kMin); },
                         { vd = FMaxMinHelper(vs2, vs1, MaxMinKind::kMin); })
      break;
    }
    case RO_V_VFSGNJ_VV:
      RVV_VFSGNJ_VV_VF_LOOP({ UNIMPLEMENTED(); },
                            {
                              vd = fsgnj32(Float32::FromBits(vs2),
                                           Float32::FromBits(vs1), false, false)
                                       .get_bits();
                              USE(fs1);
                            },
                            {
                              vd = fsgnj64(Float64::FromBits(vs2),
                                           Float64::FromBits(vs1), false, false)
                                       .get_bits();
                              USE(fs1);
                            })
      break;
    case RO_V_VFSGNJN_VV:
      RVV_VFSGNJ_VV_VF_LOOP({ UNIMPLEMENTED(); },
                            {
                              vd = fsgnj32(Float32::FromBits(vs2),
                                           Float32::FromBits(vs1), true, false)
                                       .get_bits();
                              USE(fs1);
                            },
                            {
                              vd = fsgnj64(Float64::FromBits(vs2),
                                           Float64::FromBits(vs1), true, false)
                                       .get_bits();
                              USE(fs1);
                            })
      break;
    case RO_V_VFSGNJX_VV:
      RVV_VFSGNJ_VV_VF_LOOP({ UNIMPLEMENTED(); },
                            {
                              vd = fsgnj32(Float32::FromBits(vs2),
                                           Float32::FromBits(vs1), false, true)
                                       .get_bits();
                              USE(fs1);
                            },
                            {
                              vd = fsgnj64(Float64::FromBits(vs2),
                                           Float64::FromBits(vs1), false, true)
                                       .get_bits();
                              USE(fs1);
                            })
      break;
    case RO_V_VFADD_VV:
      RVV_VI_VFP_VV_LOOP(
          { UNIMPLEMENTED(); },
          {
            auto fn = [this](float frs1, float frs2) {
              if (is_invalid_fadd(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<float>::quiet_NaN();
              } else {
                return frs1 + frs2;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
            }
            vd = alu_out;
          },
          {
            auto fn = [this](double frs1, double frs2) {
              if (is_invalid_fadd(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else {
                return frs1 + frs2;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<double>::quiet_NaN();
            }
            vd = alu_out;
          })
      break;
    case RO_V_VFSUB_VV:
      RVV_VI_VFP_VV_LOOP(
          { UNIMPLEMENTED(); },
          {
            auto fn = [this](float frs1, float frs2) {
              if (is_invalid_fsub(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<float>::quiet_NaN();
              } else {
                return frs2 - frs1;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
            }

            vd = alu_out;
          },
          {
            auto fn = [this](double frs1, double frs2) {
              if (is_invalid_fsub(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else {
                return frs2 - frs1;
              }
            };
            auto alu_out = fn(vs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(vs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<double>::quiet_NaN();
            }
            vd = alu_out;
          })
      break;
    case RO_V_VFWADD_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN(
          {
            RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(double, is_invalid_fadd, +);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFWSUB_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN(
          {
            RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(double, is_invalid_fsub, -);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFWADD_W_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN(
          {
            RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(double, is_invalid_fadd, +);
            USE(vs3);
          },
          true)
      break;
    case RO_V_VFWSUB_W_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN(
          {
            RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(double, is_invalid_fsub, -);
            USE(vs3);
          },
          true)
      break;
    case RO_V_VFWMUL_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN(
          {
            RVV_VI_VFP_VV_ARITH_CHECK_COMPUTE(double, is_invalid_fmul, *);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFWREDUSUM_VS:
    case RO_V_VFWREDOSUM_VS:
      RVV_VI_CHECK_DSS(true);
      switch (rvv_vsew()) {
        case E16:
        case E64: {
          UNIMPLEMENTED();
        }
        case E32: {
          double& vd = Rvvelt<double>(rvv_vd_reg(), 0, true);
          double vs1 = Rvvelt<double>(rvv_vs1_reg(), 0);
          double alu_out = vs1;
          for (uint64_t i = rvv_vstart(); i < rvv_vl(); ++i) {
            double vs2 = static_cast<double>(Rvvelt<float>(rvv_vs2_reg(), i));
            if (is_invalid_fadd(alu_out, vs2)) {
              set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
              break;
            }
            alu_out = alu_out + vs2;
            if (std::isnan(alu_out) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(vs2)) set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
              break;
            }
          }
          vd = alu_out;
          break;
        }
        default:
          require(false);
          break;
      }
      rvv_trace_vd();
      break;
    case RO_V_VFMADD_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, vd, vs1, vs2)},
                             {RVV_VI_VFP_FMA(double, vd, vs1, vs2)})
      break;
    case RO_V_VFNMADD_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, -vd, vs1, -vs2)},
                             {RVV_VI_VFP_FMA(double, -vd, vs1, -vs2)})
      break;
    case RO_V_VFMSUB_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, vd, vs1, -vs2)},
                             {RVV_VI_VFP_FMA(double, vd, vs1, -vs2)})
      break;
    case RO_V_VFNMSUB_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, -vd, vs1, +vs2)},
                             {RVV_VI_VFP_FMA(double, -vd, vs1, +vs2)})
      break;
    case RO_V_VFMACC_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, vs2, vs1, vd)},
                             {RVV_VI_VFP_FMA(double, vs2, vs1, vd)})
      break;
    case RO_V_VFNMACC_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, -vs2, vs1, -vd)},
                             {RVV_VI_VFP_FMA(double, -vs2, vs1, -vd)})
      break;
    case RO_V_VFMSAC_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, vs2, vs1, -vd)},
                             {RVV_VI_VFP_FMA(double, vs2, vs1, -vd)})
      break;
    case RO_V_VFNMSAC_VV:
      RVV_VI_VFP_FMA_VV_LOOP({RVV_VI_VFP_FMA(float, -vs2, vs1, +vd)},
                             {RVV_VI_VFP_FMA(double, -vs2, vs1, +vd)})
      break;
    case RO_V_VFWMACC_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN({RVV_VI_VFP_FMA(double, vs2, vs1, vs3)}, false)
      break;
    case RO_V_VFWNMACC_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN({RVV_VI_VFP_FMA(double, -vs2, vs1, -vs3)}, false)
      break;
    case RO_V_VFWMSAC_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN({RVV_VI_VFP_FMA(double, vs2, vs1, -vs3)}, false)
      break;
    case RO_V_VFWNMSAC_VV:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VV_LOOP_WIDEN({RVV_VI_VFP_FMA(double, -vs2, vs1, +vs3)}, false)
      break;
    case RO_V_VFMV_FS:
      switch (rvv_vsew()) {
        case E16: {
          UNIMPLEMENTED();
        }
        case E32: {
          uint32_t fs2 = Rvvelt<uint32_t>(rvv_vs2_reg(), 0);
          set_frd(Float32::FromBits(fs2));
          break;
        }
        case E64: {
          uint64_t fs2 = Rvvelt<uint64_t>(rvv_vs2_reg(), 0);
          set_drd(Float64::FromBits(fs2));
          break;
        }
        default:
          require(0);
          break;
      }
      break;
    default:
      UNSUPPORTED_RISCV();
  }
}

void Simulator::DecodeRvvFVF() {
  DCHECK_EQ(instr_.InstructionBits() & (kBaseOpcodeMask | kFunct3Mask), OP_FVF);
  switch (instr_.InstructionBits() & kVTypeMask) {
    case RO_V_VFSGNJ_VF:
      RVV_VFSGNJ_VV_VF_LOOP(
          {},
          {
            vd = fsgnj32(Float32::FromBits(vs2), fs1, false, false).get_bits();
            USE(vs1);
          },
          {
            vd = fsgnj64(Float64::FromBits(vs2), fs1, false, false).get_bits();
            USE(vs1);
          })
      break;
    case RO_V_VFSGNJN_VF:
      RVV_VFSGNJ_VV_VF_LOOP(
          {},
          {
            vd = fsgnj32(Float32::FromBits(vs2), fs1, true, false).get_bits();
            USE(vs1);
          },
          {
            vd = fsgnj64(Float64::FromBits(vs2), fs1, true, false).get_bits();
            USE(vs1);
          })
      break;
    case RO_V_VFSGNJX_VF:
      RVV_VFSGNJ_VV_VF_LOOP(
          {},
          {
            vd = fsgnj32(Float32::FromBits(vs2), fs1, false, true).get_bits();
            USE(vs1);
          },
          {
            vd = fsgnj64(Float64::FromBits(vs2), fs1, false, true).get_bits();
            USE(vs1);
          })
      break;
    case RO_V_VFMV_VF:
      if (instr_.RvvVM()) {
        RVV_VI_VF_MERGE_LOOP(
            {},
            {
              vd = fs1;
              USE(vs2);
            },
            {
              vd = fs1;
              USE(vs2);
            });
      } else {
        RVV_VI_VF_MERGE_LOOP(
            {},
            {
              bool use_first =
                  (Rvvelt<uint64_t>(0, (i / 64)) >> (i % 64)) & 0x1;
              vd = use_first ? fs1 : vs2;
            },
            {
              bool use_first =
                  (Rvvelt<uint64_t>(0, (i / 64)) >> (i % 64)) & 0x1;
              vd = use_first ? fs1 : vs2;
            });
      }
      break;
    case RO_V_VFADD_VF:
      RVV_VI_VFP_VF_LOOP(
          { UNIMPLEMENTED(); },
          {
            auto fn = [this](float frs1, float frs2) {
              if (is_invalid_fadd(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<float>::quiet_NaN();
              } else {
                return frs1 + frs2;
              }
            };
            auto alu_out = fn(fs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(fs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(fs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<float>::quiet_NaN();
            }
            vd = alu_out;
          },
          {
            auto fn = [this](double frs1, double frs2) {
              if (is_invalid_fadd(frs1, frs2)) {
                this->set_fflags(kInvalidOperation);
                return std::numeric_limits<double>::quiet_NaN();
              } else {
                return frs1 + frs2;
              }
            };
            auto alu_out = fn(fs1, vs2);
            // if any input or result is NaN, the result is quiet_NaN
            if (std::isnan(alu_out) || std::isnan(fs1) || std::isnan(vs2)) {
              // signaling_nan sets kInvalidOperation bit
              if (isSnan(alu_out) || isSnan(fs1) || isSnan(vs2))
                set_fflags(kInvalidOperation);
              alu_out = std::numeric_limits<double>::quiet_NaN();
            }
            vd = alu_out;
          })
      break;
    case RO_V_VFWADD_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN(
          {
            RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(double, is_invalid_fadd, +);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFWSUB_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN(
          {
            RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(double, is_invalid_fsub, -);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFWADD_W_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN(
          {
            RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(double, is_invalid_fadd, +);
            USE(vs3);
          },
          true)
      break;
    case RO_V_VFWSUB_W_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN(
          {
            RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(double, is_invalid_fsub, -);
            USE(vs3);
          },
          true)
      break;
    case RO_V_VFWMUL_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN(
          {
            RVV_VI_VFP_VF_ARITH_CHECK_COMPUTE(double, is_invalid_fmul, *);
            USE(vs3);
          },
          false)
      break;
    case RO_V_VFMADD_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, vd, fs1, vs2)},
                             {RVV_VI_VFP_FMA(double, vd, fs1, vs2)})
      break;
    case RO_V_VFNMADD_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, -vd, fs1, -vs2)},
                             {RVV_VI_VFP_FMA(double, -vd, fs1, -vs2)})
      break;
    case RO_V_VFMSUB_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, vd, fs1, -vs2)},
                             {RVV_VI_VFP_FMA(double, vd, fs1, -vs2)})
      break;
    case RO_V_VFNMSUB_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, -vd, fs1, vs2)},
                             {RVV_VI_VFP_FMA(double, -vd, fs1, vs2)})
      break;
    case RO_V_VFMACC_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, vs2, fs1, vd)},
                             {RVV_VI_VFP_FMA(double, vs2, fs1, vd)})
      break;
    case RO_V_VFNMACC_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, -vs2, fs1, -vd)},
                             {RVV_VI_VFP_FMA(double, -vs2, fs1, -vd)})
      break;
    case RO_V_VFMSAC_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, vs2, fs1, -vd)},
                             {RVV_VI_VFP_FMA(double, vs2, fs1, -vd)})
      break;
    case RO_V_VFNMSAC_VF:
      RVV_VI_VFP_FMA_VF_LOOP({RVV_VI_VFP_FMA(float, -vs2, fs1, vd)},
                             {RVV_VI_VFP_FMA(double, -vs2, fs1, vd)})
      break;
    case RO_V_VFWMACC_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN({RVV_VI_VFP_FMA(double, vs2, fs1, vs3)}, false)
      break;
    case RO_V_VFWNMACC_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN({RVV_VI_VFP_FMA(double, -vs2, fs1, -vs3)}, false)
      break;
    case RO_V_VFWMSAC_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN({RVV_VI_VFP_FMA(double, vs2, fs1, -vs3)}, false)
      break;
    case RO_V_VFWNMSAC_VF:
      RVV_VI_CHECK_DSS(true);
      RVV_VI_VFP_VF_LOOP_WIDEN({RVV_VI_VFP_FMA(double, -vs2, fs1, vs3)}, false)
      break;
    case RO_V_VFMV_SF: {
      if (instr_.Vs2Value() == 0x0) {
        if (rvv_vl() > 0 && rvv_vstart() < rvv_vl()) {
          switch (rvv_vsew()) {
            case E8:
              UNREACHABLE();
            case E16:
              UNREACHABLE();
            case E32:
              Rvvelt<uint32_t>(rvv_vd_reg(), 0, true) =
                  (uint32_t)(get_fpu_register_Float32(rs1_reg()).get_bits());
              break;
            case E64:
              Rvvelt<uint64_t>(rvv_vd_reg(), 0, true) =
                  (uint64_t)(get_fpu_register_Float64(rs1_reg()).get_bits());
              break;
            default:
              UNREACHABLE();
          }
        }
        set_rvv_vstart(0);
        rvv_trace_vd();
      } else {
        UNSUPPORTED_RISCV();
      }
    } break;
    case RO_V_VFSLIDE1DOWN_VF: {
      RVV_VI_CHECK_SLIDE(false);
      RVV_VI_GENERAL_LOOP_BASE
      switch (rvv_vsew()) {
        case E8: {
          UNSUPPORTED();
        }
        case E16: {
          UNSUPPORTED();
        }
        case E32: {
          VF_SLIDE1DOWN_PARAMS(32, 1);
        } break;
        default: {
          VF_SLIDE1DOWN_PARAMS(64, 1);
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    case RO_V_VFSLIDE1UP_VF: {
      RVV_VI_CHECK_SLIDE(true);
      RVV_VI_GENERAL_LOOP_BASE
      if (i < rvv_vstart()) continue;
      switch (rvv_vsew()) {
        case E8: {
          UNSUPPORTED();
        }
        case E16: {
          UNSUPPORTED();
        }
        case E32: {
          VF_SLIDE1UP_PARAMS(32, 1);
        } break;
        default: {
          VF_SLIDE1UP_PARAMS(64, 1);
        } break;
      }
      RVV_VI_LOOP_END
      rvv_trace_vd();
    } break;
    default:
      UNSUPPORTED_RISCV();
  }
}
void Simulator::DecodeVType() {
  switch (instr_.InstructionBits() & (kFunct3Mask | kBaseOpcodeMask)) {
    case OP_IVV:
      DecodeRvvIVV();
      return;
    case OP_FVV:
      DecodeRvvFVV();
      return;
    case OP_MVV:
      DecodeRvvMVV();
      return;
    case OP_IVI:
      DecodeRvvIVI();
      return;
    case OP_IVX:
      DecodeRvvIVX();
      return;
    case OP_FVF:
      DecodeRvvFVF();
      return;
    case OP_MVX:
      DecodeRvvMVX();
      return;
  }
  switch (instr_.InstructionBits() &
          (kBaseOpcodeMask | kFunct3Mask | 0x80000000)) {
    case RO_V_VSETVLI: {
      uint64_t avl;
      set_rvv_vtype(rvv_zimm());
      CHECK_GE(rvv_vsew(), E8);
      CHECK_LE(rvv_vsew(), E64);
      if (rs1_reg() != zero_reg) {
        avl = rs1();
      } else if (rd_reg() != zero_reg) {
        avl = ~0;
      } else {
        avl = rvv_vl();
      }
      avl = avl <= rvv_vlmax() ? avl : rvv_vlmax();
      set_rvv_vl(avl);
      set_rd(rvv_vl());
      set_rvv_vstart(0);
      rvv_trace_status();
      break;
    }
    case RO_V_VSETVL: {
      if (!(instr_.InstructionBits() & 0x40000000)) {
        uint64_t avl;
        set_rvv_vtype(rs2());
        CHECK_GE(rvv_sew(), E8);
        CHECK_LE(rvv_sew(), E64);
        if (rs1_reg() != zero_reg) {
          avl = rs1();
        } else if (rd_reg() != zero_reg) {
          avl = ~0;
        } else {
          avl = rvv_vl();
        }
        avl = avl <= rvv_vlmax()        ? avl
              : avl < (rvv_vlmax() * 2) ? avl / 2
                                        : rvv_vlmax();
        set_rvv_vl(avl);
        set_rd(rvv_vl());
        rvv_trace_status();
      } else {
        DCHECK_EQ(instr_.InstructionBits() &
                      (kBaseOpcodeMask | kFunct3Mask | 0xC0000000),
                  RO_V_VSETIVLI);
        uint64_t avl;
        set_rvv_vtype(rvv_zimm());
        avl = instr_.Rvvuimm();
        avl = avl <= rvv_vlmax()        ? avl
              : avl < (rvv_vlmax() * 2) ? avl / 2
                                        : rvv_vlmax();
        set_rvv_vl(avl);
        set_rd(rvv_vl());
        rvv_trace_status();
        break;
      }
      break;
    }
    default:
      FATAL("Error: Unsupport on FILE:%s:%d.", __FILE__, __LINE__);
  }
}
#endif

// Executes the current instruction.
void Simulator::InstructionDecode(Instruction* instr) {
  if (v8_flags.check_icache) {
    CheckICache(i_cache(), instr);
  }
  pc_modified_ = false;

  v8::base::EmbeddedVector<char, 256> buffer;

  if (v8_flags.trace_sim || v8_flags.debug_sim) {
    SNPrintF(trace_buf_, " ");
    disasm::NameConverter converter;
    disasm::Disassembler dasm(converter);
    // Use a reasonably large buffer.
    dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(instr));

    // PrintF("EXECUTING  0x%08" PRIxPTR "   %-44s\n",
    //        reinterpret_cast<intptr_t>(instr), buffer.begin());
  }
  instr_ = instr;
  switch (instr_.InstructionType()) {
    case Instruction::kRType:
      DecodeRVRType();
      break;
    case Instruction::kR4Type:
      DecodeRVR4Type();
      break;
    case Instruction::kIType:
      DecodeRVIType();
      break;
    case Instruction::kSType:
      DecodeRVSType();
      break;
    case Instruction::kBType:
      DecodeRVBType();
      break;
    case Instruction::kUType:
      DecodeRVUType();
      break;
    case Instruction::kJType:
      DecodeRVJType();
      break;
    case Instruction::kCRType:
      DecodeCRType();
      break;
    case Instruction::kCAType:
      DecodeCAType();
      break;
    case Instruction::kCJType:
      DecodeCJType();
      break;
    case Instruction::kCBType:
      DecodeCBType();
      break;
    case Instruction::kCIType:
      DecodeCIType();
      break;
    case Instruction::kCIWType:
      DecodeCIWType();
      break;
    case Instruction::kCSSType:
      DecodeCSSType();
      break;
    case Instruction::kCLType:
      DecodeCLType();
      break;
    case Instruction::kCSType:
      DecodeCSType();
      break;
#ifdef CAN_USE_RVV_INSTRUCTIONS
    case Instruction::kVType:
      DecodeVType();
      break;
#endif
    default:
      if (1) {
        std::cout << "Unrecognized instruction [@pc=0x" << std::hex
                  << registers_[pc] << "]: 0x" << instr->InstructionBits()
                  << std::endl;
      }
      UNSUPPORTED();
  }

  if (v8_flags.trace_sim) {
    PrintF("  0x%012" PRIxPTR "      %-44s\t%s\n",
           reinterpret_cast<intptr_t>(instr), buffer.begin(),
           trace_buf_.begin());
  }

  if (!pc_modified_) {
    set_register(pc,
                 reinterpret_cast<sreg_t>(instr) + instr->InstructionSize());
  }

  if (watch_address_ != nullptr) {
    PrintF("  0x%012" PRIxPTR " :  0x%016" REGIx_FORMAT "  %14" REGId_FORMAT
           " ",
           reinterpret_cast<intptr_t>(watch_address_), *watch_address_,
           *watch_address_);
    // Object obj(*watch_address_);
    // Heap* current_heap = isolate_->heap();
    // if (obj.IsSmi() || IsValidHeapObject(current_heap,
    // Cast<HeapObject>(obj))) {
    //   PrintF(" (");
    //   if (obj.IsSmi()) {
    //     PrintF("smi %d", Smi::ToInt(obj));
    //   } else {
    //     ShortPrint(obj);
    //   }
    //   PrintF(")");
    // }
    PrintF("\n");
    if (watch_value_ != *watch_address_) {
      RiscvDebugger dbg(this);
      dbg.Debug();
      watch_value_ = *watch_address_;
    }
  }
}

void Simulator::Execute() {
  // Get the PC to simulate. Cannot use the accessor here as we need the
  // raw PC value and not the one used as input to arithmetic instructions.
  sreg_t program_counter = get_pc();
  while (program_counter != end_sim_pc) {
    Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
    icount_++;
    if (icount_ == static_cast<sreg_t>(v8_flags.stop_sim_at)) {
      RiscvDebugger dbg(this);
      dbg.Debug();
    } else {
      InstructionDecode(instr);
    }
    CheckBreakpoints();
    program_counter = get_pc();
  }
}

void Simulator::CallInternal(Address entry) {
  // Adjust JS-based stack limit to C-based stack limit.
  isolate_->stack_guard()->AdjustStackLimitForSimulator();

  // Prepare to execute the code at entry.
  set_register(pc, static_cast<sreg_t>(entry));
  // Put down marker for end of simulation. The simulator will stop simulation
  // when the PC reaches this value. By saving the "end simulation" value into
  // the LR the simulation stops when returning to this call point.
  set_register(ra, end_sim_pc);

  // Remember the values of callee-saved registers.
  sreg_t s0_val = get_register(s0);
  sreg_t s1_val = get_register(s1);
  sreg_t s2_val = get_register(s2);
  sreg_t s3_val = get_register(s3);
  sreg_t s4_val = get_register(s4);
  sreg_t s5_val = get_register(s5);
  sreg_t s6_val = get_register(s6);
  sreg_t s7_val = get_register(s7);
  sreg_t s8_val = get_register(s8);
  sreg_t s9_val = get_register(s9);
  sreg_t s10_val = get_register(s10);
  sreg_t s11_val = get_register(s11);
  sreg_t gp_val = get_register(gp);
  sreg_t sp_val = get_register(sp);

  // Set up the callee-saved registers with a known value. To be able to check
  // that they are preserved properly across JS execution. If this value is
  // small int, it should be SMI.
  sreg_t callee_saved_value = icount_ != 0 ? icount_ & ~kSmiTagMask : -1;
  set_register(s0, callee_saved_value);
  set_register(s1, callee_saved_value);
  set_register(s2, callee_saved_value);
  set_register(s3, callee_saved_value);
  set_register(s4, callee_saved_value);
  set_register(s5, callee_saved_value);
  set_register(s6, callee_saved_value);
  set_register(s7, callee_saved_value);
  set_register(s8, callee_saved_value);
  set_register(s9, callee_saved_value);
  set_register(s10, callee_saved_value);
  set_register(s11, callee_saved_value);
  set_register(gp, callee_saved_value);

  // Start the simulation.
  Execute();

  // Check that the callee-saved registers have been preserved.
  CHECK_EQ(callee_saved_value, get_register(s0));
  CHECK_EQ(callee_saved_value, get_register(s1));
  CHECK_EQ(callee_saved_value, get_register(s2));
  CHECK_EQ(callee_saved_value, get_register(s3));
  CHECK_EQ(callee_saved_value, get_register(s4));
  CHECK_EQ(callee_saved_value, get_register(s5));
  CHECK_EQ(callee_saved_value, get_register(s6));
  CHECK_EQ(callee_saved_value, get_register(s7));
  CHECK_EQ(callee_saved_value, get_register(s8));
  CHECK_EQ(callee_saved_value, get_register(s9));
  CHECK_EQ(callee_saved_value, get_register(s10));
  CHECK_EQ(callee_saved_value, get_register(s11));
  CHECK_EQ(callee_saved_value, get_register(gp));

  // Restore callee-saved registers with the original value.
  set_register(s0, s0_val);
  set_register(s1, s1_val);
  set_register(s2, s2_val);
  set_register(s3, s3_val);
  set_register(s4, s4_val);
  set_register(s5, s5_val);
  set_register(s6, s6_val);
  set_register(s7, s7_val);
  set_register(s8, s8_val);
  set_register(s9, s9_val);
  set_register(s10, s10_val);
  set_register(s11, s11_val);
  set_register(gp, gp_val);
  set_register(sp, sp_val);
}

#ifdef V8_TARGET_ARCH_RISCV64
void Simulator::CallImpl(Address entry, CallArgument* args) {
  int index_gp = 0;
  int index_fp = 0;
  std::vector<int64_t> stack_args(0);
  for (int i = 0; !args[i].IsEnd(); i++) {
    CallArgument arg = args[i];
    if (arg.IsGP() && (index_gp < 8)) {
      set_register(index_gp + kRegCode_a0, arg.bits());
      index_gp++;
    } else if (arg.IsFP() && (index_fp < 8)) {
      set_fpu_register(index_fp + kDoubleCode_fa0, arg.bits());
      index_fp++;
    } else {
      DCHECK(arg.IsFP() || arg.IsGP());
      stack_args.push_back(arg.bits());
    }
  }
  if (v8_flags.trace_sim) {
    std::cout << "CallImpl: reg_arg_count = " << index_fp + index_gp << std::hex
              << " entry-pc (JSEntry) = 0x" << entry
              << " a0 (Isolate-root) = 0x" << get_register(a0)
              << " a1 (orig_func/new_target) = 0x" << get_register(a1)
              << " a2 (func/target) = 0x" << get_register(a2)
              << " a3 (receiver) = 0x" << get_register(a3) << " a4 (argc) = 0x"
              << get_register(a4) << " a5 (argv) = 0x" << get_register(a5)
              << std::endl;
  }
  // Remaining arguments passed on stack.
  int64_t original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  int64_t stack_args_size =
      stack_args.size() * sizeof(stack_args[0]) + kCArgsSlotsSize;
  int64_t entry_stack = original_stack - stack_args_size;
  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  char* stack_argument = reinterpret_cast<char*>(entry_stack);
  memcpy(stack_argument + kCArgSlotCount, stack_args.data(),
         stack_args.size() * sizeof(int64_t));
  set_register(sp, entry_stack);
  CallInternal(entry);
  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);
}
#else
intptr_t Simulator::CallImpl(Address entry, int argument_count,
                             const intptr_t* arguments) {
  constexpr int kRegisterPassedArguments = 8;
  // Set up arguments.
  // RISC-V 64G ISA has a0-a7 for passing arguments
  int reg_arg_count = std::min(kRegisterPassedArguments, argument_count);
  if (reg_arg_count > 0) set_register(a0, arguments[0]);
  if (reg_arg_count > 1) set_register(a1, arguments[1]);
  if (reg_arg_count > 2) set_register(a2, arguments[2]);
  if (reg_arg_count > 3) set_register(a3, arguments[3]);
  if (reg_arg_count > 4) set_register(a4, arguments[4]);
  if (reg_arg_count > 5) set_register(a5, arguments[5]);
  if (reg_arg_count > 6) set_register(a6, arguments[6]);
  if (reg_arg_count > 7) set_register(a7, arguments[7]);
  if (v8_flags.trace_sim) {
    std::cout << "CallImpl: reg_arg_count = " << reg_arg_count << std::hex
              << " entry-pc (JSEntry) = 0x" << entry
              << " a0 (Isolate-root) = 0x" << get_register(a0)
              << " a1 (orig_func/new_target) = 0x" << get_register(a1)
              << " a2 (func/target) = 0x" << get_register(a2)
              << " a3 (receiver) = 0x" << get_register(a3) << " a4 (argc) = 0x"
              << get_register(a4) << " a5 (argv) = 0x" << get_register(a5)
              << std::endl;
  }
  // Remaining arguments passed on stack.
  sreg_t original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  int stack_args_count = argument_count - reg_arg_count;
  int stack_args_size = stack_args_count * sizeof(*arguments) + kCArgsSlotsSize;
  sreg_t entry_stack = original_stack - stack_args_size;
  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  intptr_t* stack_argument = reinterpret_cast<intptr_t*>(entry_stack);
  memcpy(stack_argument + kCArgSlotCount, arguments + reg_arg_count,
         stack_args_count * sizeof(*arguments));
  set_register(sp, entry_stack);
  CallInternal(entry);
  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);
  // return get_register(a0);
  // RISCV uses a0 to return result
  return get_register(a0);
}
#endif  // V8_TARGET_ARCH_RISCV64

double Simulator::CallFP(Address entry, double d0, double d1) {
  set_fpu_register_double(fa0, d0);
  set_fpu_register_double(fa1, d1);
  CallInternal(entry);
  return get_fpu_register_double(fa0);
}

uintptr_t Simulator::PushAddress(uintptr_t address) {
  int64_t new_sp = get_register(sp) - sizeof(uintptr_t);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(new_sp);
  *stack_slot = address;
  set_register(sp, new_sp);
  return new_sp;
}

uintptr_t Simulator::PopAddress() {
  int64_t current_sp = get_register(sp);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(current_sp);
  uintptr_t address = *stack_slot;
  set_register(sp, current_sp + sizeof(uintptr_t));
  return address;
}

Simulator::LocalMonitor::LocalMonitor()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      size_(TransactionSize::None) {}

void Simulator::LocalMonitor::Clear() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
  size_ = TransactionSize::None;
}

void Simulator::LocalMonitor::NotifyLoad() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non linked load could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on load.
    Clear();
  }
}

void Simulator::LocalMonitor::NotifyLoadLinked(uintptr_t addr,
                                               TransactionSize size) {
  access_state_ = MonitorAccess::RMW;
  tagged_addr_ = addr;
  size_ = size;
}

void Simulator::LocalMonitor::NotifyStore() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non exclusive store could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on store.
    Clear();
  }
}

bool Simulator::LocalMonitor::NotifyStoreConditional(uintptr_t addr,
                                                     TransactionSize size) {
  if (access_state_ == MonitorAccess::RMW) {
    if (addr == tagged_addr_ && size_ == size) {
      Clear();
      return true;
    } else {
      return false;
    }
  } else {
    DCHECK(access_state_ == MonitorAccess::Open);
    return false;
  }
}

Simulator::GlobalMonitor::LinkedAddress::LinkedAddress()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      next_(nullptr),
      prev_(nullptr),
      failure_counter_(0) {}

void Simulator::GlobalMonitor::LinkedAddress::Clear_Locked() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
}

void Simulator::GlobalMonitor::LinkedAddress::NotifyLoadLinked_Locked(
    uintptr_t addr) {
  access_state_ = MonitorAccess::RMW;
  tagged_addr_ = addr;
}

void Simulator::GlobalMonitor::LinkedAddress::NotifyStore_Locked() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non exclusive store could clear the global monitor. As a result, it's
    // most strict to unconditionally clear global monitors on store.
    Clear_Locked();
  }
}

bool Simulator::GlobalMonitor::LinkedAddress::NotifyStoreConditional_Locked(
    uintptr_t addr, bool is_requesting_thread) {
  if (access_state_ == MonitorAccess::RMW) {
    if (is_requesting_thread) {
      if (addr == tagged_addr_) {
        Clear_Locked();
        // Introduce occasional sc/scd failures. This is to simulate the
        // behavior of hardware, which can randomly fail due to background
        // cache evictions.
        if (failure_counter_++ >= kMaxFailureCounter) {
          failure_counter_ = 0;
          return false;
        } else {
          return true;
        }
      }
    } else if ((addr & kExclusiveTaggedAddrMask) ==
               (tagged_addr_ & kExclusiveTaggedAddrMask)) {
      // Check the masked addresses when responding to a successful lock by
      // another thread so the implementation is more conservative (i.e. the
      // granularity of locking is as large as possible.)
      Clear_Locked();
      return false;
    }
  }
  return false;
}

void Simulator::GlobalMonitor::NotifyLoadLinked_Locked(
    uintptr_t addr, LinkedAddress* linked_address) {
  linked_address->NotifyLoadLinked_Locked(addr);
  PrependProcessor_Locked(linked_address);
}

void Simulator::GlobalMonitor::NotifyStore_Locked(
    LinkedAddress* linked_address) {
  // Notify each thread of the store operation.
  for (LinkedAddress* iter = head_; iter; iter = iter->next_) {
    iter->NotifyStore_Locked();
  }
}

bool Simulator::GlobalMonitor::NotifyStoreConditional_Locked(
    uintptr_t addr, LinkedAddress* linked_address) {
  DCHECK(IsProcessorInLinkedList_Locked(linked_address));
  if (linked_address->NotifyStoreConditional_Locked(addr, true)) {
    // Notify the other processors that this StoreConditional succeeded.
    for (LinkedAddress* iter = head_; iter; iter = iter->next_) {
      if (iter != linked_address) {
        iter->NotifyStoreConditional_Locked(addr, false);
      }
    }
    return true;
  } else {
    return false;
  }
}

bool Simulator::GlobalMonitor::IsProcessorInLinkedList_Locked(
    LinkedAddress* linked_address) const {
  return head_ == linked_address || linked_address->next_ ||
         linked_address->prev_;
}

void Simulator::GlobalMonitor::PrependProcessor_Locked(
    LinkedAddress* linked_address) {
  if (IsProcessorInLinkedList_Locked(linked_address)) {
    return;
  }

  if (head_) {
    head_->prev_ = linked_address;
  }
  linked_address->prev_ = nullptr;
  linked_address->next_ = head_;
  head_ = linked_address;
}

void Simulator::GlobalMonitor::RemoveLinkedAddress(
    LinkedAddress* linked_address) {
  base::MutexGuard lock_guard(&mutex);
  if (!IsProcessorInLinkedList_Locked(linked_address)) {
    return;
  }

  if (linked_address->prev_) {
    linked_address->prev_->next_ = linked_address->next_;
  } else {
    head_ = linked_address->next_;
  }
  if (linked_address->next_) {
    linked_address->next_->prev_ = linked_address->prev_;
  }
  linked_address->prev_ = nullptr;
  linked_address->next_ = nullptr;
}

#undef SScanF
#undef BRACKETS

}  // namespace internal
}  // namespace v8

#endif  // USE_SIMULATOR
