// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/base/platform/platform.h"
#include "src/objects/backing-store.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class BackingStoreTest : public TestWithIsolate {};

TEST_F(BackingStoreTest, GrowWasmMemoryInPlace) {
  auto backing_store = BackingStore::AllocateWasmMemory(
      isolate(), 1, 2, WasmMemoryFlag::kWasmMemory32, SharedFlag::kNotShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_capacity());

  std::optional<size_t> result =
      backing_store->GrowWasmMemoryInPlace(isolate(), 1, 2);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1u);
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, GrowWasmMemoryInPlace_neg) {
  auto backing_store = BackingStore::AllocateWasmMemory(
      isolate(), 1, 2, WasmMemoryFlag::kWasmMemory32, SharedFlag::kNotShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_capacity());

  std::optional<size_t> result =
      backing_store->GrowWasmMemoryInPlace(isolate(), 2, 2);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, GrowSharedWasmMemoryInPlace) {
  auto backing_store = BackingStore::AllocateWasmMemory(
      isolate(), 2, 3, WasmMemoryFlag::kWasmMemory32, SharedFlag::kShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(3 * wasm::kWasmPageSize, backing_store->byte_capacity());

  std::optional<size_t> result =
      backing_store->GrowWasmMemoryInPlace(isolate(), 1, 3);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 2u);
  EXPECT_EQ(3 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, CopyWasmMemory) {
  auto bs1 = BackingStore::AllocateWasmMemory(
      isolate(), 1, 2, WasmMemoryFlag::kWasmMemory32, SharedFlag::kNotShared);
  CHECK(bs1);
  EXPECT_TRUE(bs1->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, bs1->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, bs1->byte_capacity());

  auto bs2 =
      bs1->CopyWasmMemory(isolate(), 3, 3, WasmMemoryFlag::kWasmMemory32);
  EXPECT_TRUE(bs2->is_wasm_memory());
  EXPECT_EQ(3 * wasm::kWasmPageSize, bs2->byte_length());
  EXPECT_EQ(3 * wasm::kWasmPageSize, bs2->byte_capacity());
}

class GrowerThread : public base::Thread {
 public:
  GrowerThread(Isolate* isolate, uint32_t increment, uint32_t max,
               std::shared_ptr<BackingStore> backing_store)
      : base::Thread(base::Thread::Options("GrowerThread")),
        isolate_(isolate),
        increment_(increment),
        max_(max),
        backing_store_(backing_store) {}

  void Run() override {
    size_t max_length = max_ * wasm::kWasmPageSize;
    while (true) {
      size_t current_length = backing_store_->byte_length();
      if (current_length >= max_length) break;
      std::optional<size_t> result =
          backing_store_->GrowWasmMemoryInPlace(isolate_, increment_, max_);
      size_t new_length = backing_store_->byte_length();
      if (result.has_value()) {
        CHECK_LE(current_length / wasm::kWasmPageSize, result.value());
        CHECK_GE(new_length, current_length + increment_);
      } else {
        CHECK_EQ(max_length, new_length);
      }
    }
  }

 private:
  Isolate* isolate_;
  uint32_t increment_;
  uint32_t max_;
  std::shared_ptr<BackingStore> backing_store_;
};

TEST_F(BackingStoreTest, RacyGrowWasmMemoryInPlace) {
  constexpr int kNumThreads = 10;
  constexpr int kMaxPages = 1024;
  GrowerThread* threads[kNumThreads];

  std::shared_ptr<BackingStore> backing_store =
      BackingStore::AllocateWasmMemory(isolate(), 0, kMaxPages,
                                       WasmMemoryFlag::kWasmMemory32,
                                       SharedFlag::kShared);

  for (int i = 0; i < kNumThreads; i++) {
    threads[i] = new GrowerThread(isolate(), 1, kMaxPages, backing_store);
    CHECK(threads[i]->Start());
  }

  for (int i = 0; i < kNumThreads; i++) {
    threads[i]->Join();
  }

  EXPECT_EQ(kMaxPages * wasm::kWasmPageSize, backing_store->byte_length());

  for (int i = 0; i < kNumThreads; i++) {
    delete threads[i];
  }
}

}  // namespace internal
}  // namespace v8
