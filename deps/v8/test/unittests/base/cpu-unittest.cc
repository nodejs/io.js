// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/cpu.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "src/heap/base/memory-tagging.h"

namespace v8 {
namespace base {


#if defined(V8_HOST_ARCH_ARM64)
TEST(CPUTest, SuppressTagCheckingScope) {
  CPU cpu;
  if (!cpu.has_mte()) GTEST_SKIP();

  // Read the current value of PSTATE.TCO (it should be zero).
  uint64_t val;
  asm volatile(".arch_extension memtag \n mrs %0, tco" : "=r" (val));
  EXPECT_EQ(val, 0u);

  // Create a scope where MTE tag checks are temporarily suspended.
  {
    heap::base::SuspendTagCheckingScope s;
    asm volatile(".arch_extension memtag \n mrs %0, tco" : "=r" (val));
    EXPECT_EQ(val, 1u << 25);
  }

  // Check that the scope restores TCO afterwards.
  asm volatile(".arch_extension memtag \n mrs %0, tco" : "=r" (val));
  EXPECT_EQ(val, 0u);
}
#endif

TEST(CPUTest, FeatureImplications) {
  CPU cpu;

  // ia32 and x64 features
  EXPECT_TRUE(!cpu.has_sse() || cpu.has_mmx());
  EXPECT_TRUE(!cpu.has_sse2() || cpu.has_sse());
  EXPECT_TRUE(!cpu.has_sse3() || cpu.has_sse2());
  EXPECT_TRUE(!cpu.has_ssse3() || cpu.has_sse3());
  EXPECT_TRUE(!cpu.has_sse41() || cpu.has_sse3());
  EXPECT_TRUE(!cpu.has_sse42() || cpu.has_sse41());
  EXPECT_TRUE(!cpu.has_avx() || cpu.has_sse2());
  EXPECT_TRUE(!cpu.has_fma3() || cpu.has_avx());
  EXPECT_TRUE(!cpu.has_avx2() || cpu.has_avx());

  // arm features
  EXPECT_TRUE(!cpu.has_vfp3_d32() || cpu.has_vfp3());
}


TEST(CPUTest, RequiredFeatures) {
  CPU cpu;

#if V8_HOST_ARCH_ARM
  EXPECT_TRUE(cpu.has_fpu());
#endif

#if V8_HOST_ARCH_IA32
  EXPECT_TRUE(cpu.has_fpu());
  EXPECT_TRUE(cpu.has_sahf());
#endif

#if V8_HOST_ARCH_X64
  EXPECT_TRUE(cpu.has_fpu());
  EXPECT_TRUE(cpu.has_cmov());
  EXPECT_TRUE(cpu.has_mmx());
  EXPECT_TRUE(cpu.has_sse());
  EXPECT_TRUE(cpu.has_sse2());
#endif
}

}  // namespace base
}  // namespace v8
