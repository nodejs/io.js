// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/prefetch.h"

#include <memory>

#include "gtest/gtest.h"

namespace {

// Below tests exercise the functions only to guarantee they compile and execute
// correctly. We make no attempt at verifying any prefetch instructions being
// generated and executed: we assume the various implementation in terms of
// __builtin_prefetch() or x86 intrinsics to be correct and well tested.

TEST(PrefetchTest, PrefetchToLocalCache_StackA) {
  char buf[100] = {};
  absl::PrefetchToLocalCache(buf);
  absl::PrefetchToLocalCacheNta(buf);
  absl::PrefetchToLocalCacheForWrite(buf);
}

TEST(PrefetchTest, PrefetchToLocalCache_Heap) {
  auto memory = std::make_unique<char[]>(200 << 10);
  memset(memory.get(), 0, 200 << 10);
  absl::PrefetchToLocalCache(memory.get());
  absl::PrefetchToLocalCacheNta(memory.get());
  absl::PrefetchToLocalCacheForWrite(memory.get());
  absl::PrefetchToLocalCache(memory.get() + (50 << 10));
  absl::PrefetchToLocalCacheNta(memory.get() + (50 << 10));
  absl::PrefetchToLocalCacheForWrite(memory.get() + (50 << 10));
  absl::PrefetchToLocalCache(memory.get() + (100 << 10));
  absl::PrefetchToLocalCacheNta(memory.get() + (100 << 10));
  absl::PrefetchToLocalCacheForWrite(memory.get() + (100 << 10));
  absl::PrefetchToLocalCache(memory.get() + (150 << 10));
  absl::PrefetchToLocalCacheNta(memory.get() + (150 << 10));
  absl::PrefetchToLocalCacheForWrite(memory.get() + (150 << 10));
}

TEST(PrefetchTest, PrefetchToLocalCache_Nullptr) {
  absl::PrefetchToLocalCache(nullptr);
  absl::PrefetchToLocalCacheNta(nullptr);
  absl::PrefetchToLocalCacheForWrite(nullptr);
}

TEST(PrefetchTest, PrefetchToLocalCache_InvalidPtr) {
  absl::PrefetchToLocalCache(reinterpret_cast<const void*>(0x785326532L));
  absl::PrefetchToLocalCacheNta(reinterpret_cast<const void*>(0x785326532L));
  absl::PrefetchToLocalCacheForWrite(reinterpret_cast<const void*>(0x78532L));
}

}  // namespace
