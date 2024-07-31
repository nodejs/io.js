// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access-inl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(ThreadIsolation, ReuseJitPage) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  ThreadIsolation::RegisterJitPage(address1, size);

  Address address2 = address1 + size;
  ThreadIsolation::RegisterJitPage(address2, size);

  ThreadIsolation::UnregisterJitPage(address1, size);
  ThreadIsolation::RegisterJitPage(address1, size);
  ThreadIsolation::UnregisterJitPage(address1, size);
  ThreadIsolation::UnregisterJitPage(address2, size);

  ThreadIsolation::RegisterJitPage(address1, 2 * size);
  ThreadIsolation::UnregisterJitPage(address1, 2 * size);
}

TEST(ThreadIsolation, CatchJitPageOverlap) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  ThreadIsolation::RegisterJitPage(address1, size);
  EXPECT_DEATH_IF_SUPPORTED(
      { ThreadIsolation::RegisterJitPage(address1 + size - 1, 1); }, "");
  ThreadIsolation::UnregisterJitPage(address1, size);
}

TEST(ThreadIsolation, JitAllocation) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  ThreadIsolation::RegisterJitPage(address1, size);

  Address address2 = address1 + size;
  ThreadIsolation::RegisterJitPage(address2, size);

  ThreadIsolation::RegisterJitAllocationForTesting(address2 + size - 1, 1);
  ThreadIsolation::RegisterJitAllocationForTesting(address1, 1);
  // An allocation spanning two pages.
  ThreadIsolation::RegisterJitAllocationForTesting(address2 - 1, 2);

  ThreadIsolation::UnregisterJitPage(address1, size);
  // The spanning allocation should've been released, try to reuse the memory.
  ThreadIsolation::RegisterJitAllocationForTesting(address2, 1);
  ThreadIsolation::UnregisterJitPage(address2, size);
}

TEST(ThreadIsolation, CatchOOBJitAllocation) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  ThreadIsolation::RegisterJitPage(address1, size);
  EXPECT_DEATH_IF_SUPPORTED(
      { ThreadIsolation::RegisterJitAllocationForTesting(address1 + size, 1); },
      "");
  ThreadIsolation::UnregisterJitPage(address1, size);
}

TEST(ThreadIsolation, MergeJitPages) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  Address address2 = address1 + size;
  Address address3 = address2 + size;

  ThreadIsolation::RegisterJitPage(address2, size);
  ThreadIsolation::RegisterJitPage(address1, size);
  ThreadIsolation::RegisterJitPage(address3, size);

  ThreadIsolation::RegisterJitAllocationForTesting(address1, 3 * size);
  ThreadIsolation::UnregisterJitAllocationForTesting(address1, 3 * size);

  // Test merge in both directions
  ThreadIsolation::UnregisterJitPage(address2, size);
  ThreadIsolation::RegisterJitPage(address2, size);

  ThreadIsolation::RegisterJitAllocationForTesting(address1, 3 * size);
  ThreadIsolation::UnregisterJitAllocationForTesting(address1, 3 * size);

  ThreadIsolation::UnregisterJitPage(address2, size);
  ThreadIsolation::UnregisterJitPage(address1, size);
  ThreadIsolation::UnregisterJitPage(address3, size);
}

TEST(ThreadIsolation, FreeRange) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  Address address2 = address1 + size;
  Address address3 = address2 + size;
  ThreadIsolation::RegisterJitPage(address1, size);
  ThreadIsolation::RegisterJitPage(address2, size);
  ThreadIsolation::RegisterJitPage(address3, size);

  ThreadIsolation::RegisterJitAllocationForTesting(address2 - 1, 1);
  ThreadIsolation::RegisterJitAllocationForTesting(address2, 1);
  ThreadIsolation::RegisterJitAllocationForTesting(address2 + 1, size - 2);
  ThreadIsolation::RegisterJitAllocationForTesting(address3 - 1, 1);
  ThreadIsolation::RegisterJitAllocationForTesting(address3, 1);

  {
    WritableJitPage jit_page(address2, size);
    EXPECT_FALSE(jit_page.Empty());
    jit_page.FreeRange(address2, 0);
    EXPECT_FALSE(jit_page.Empty());
    jit_page.FreeRange(address2, size);
    EXPECT_TRUE(jit_page.Empty());
    // Freeing an already free range should not crash.
    jit_page.FreeRange(address2, size);
  }
  {
    WritableJitPage jit_page(address1, size);
    EXPECT_FALSE(jit_page.Empty());
    jit_page.FreeRange(address1, size);
    EXPECT_TRUE(jit_page.Empty());
  }
  {
    WritableJitPage jit_page(address3, size);
    EXPECT_FALSE(jit_page.Empty());
    jit_page.FreeRange(address3, size);
    EXPECT_TRUE(jit_page.Empty());
  }

  ThreadIsolation::UnregisterJitPage(address1, size);
  ThreadIsolation::UnregisterJitPage(address2, size);
  ThreadIsolation::UnregisterJitPage(address3, size);
}

TEST(ThreadIsolation, InvalidFreeRange) {
  ThreadIsolation::Initialize(nullptr);

  Address address1 = 0x4100000;
  size_t size = 0x1000;
  ThreadIsolation::RegisterJitPage(address1, size);

  ThreadIsolation::RegisterJitAllocationForTesting(address1, 2);

  {
    WritableJitPage jit_page(address1, size);
    EXPECT_FALSE(jit_page.Empty());
    // We should die when trying to partially free an allocation.
    EXPECT_DEATH_IF_SUPPORTED({ jit_page.FreeRange(address1, 1); }, "");
    jit_page.FreeRange(address1, 2);
  }

  ThreadIsolation::UnregisterJitPage(address1, size);
}

}  // namespace internal
}  // namespace v8
