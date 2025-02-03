/* cpu_features.h -- Processor features detection.
 *
 * Copyright 2018 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "zlib.h"

/* TODO(cavalcantii): remove checks for x86_flags on deflate.
 */
extern int arm_cpu_enable_crc32;
extern int arm_cpu_enable_pmull;
extern int x86_cpu_enable_sse2;
extern int x86_cpu_enable_ssse3;
extern int x86_cpu_enable_simd;
extern int x86_cpu_enable_avx512;

extern int riscv_cpu_enable_rvv;
extern int riscv_cpu_enable_vclmul;

void cpu_check_features(void);
