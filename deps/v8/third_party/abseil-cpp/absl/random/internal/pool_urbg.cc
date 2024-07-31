// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/random/internal/pool_urbg.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/internal/unaligned_access.h"
#include "absl/base/optimization.h"
#include "absl/random/internal/randen.h"
#include "absl/random/internal/seed_material.h"
#include "absl/random/seed_gen_exception.h"

using absl::base_internal::SpinLock;
using absl::base_internal::SpinLockHolder;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// RandenPoolEntry is a thread-safe pseudorandom bit generator, implementing a
// single generator within a RandenPool<T>. It is an internal implementation
// detail, and does not aim to conform to [rand.req.urng].
//
// NOTE: There are alignment issues when used on ARM, for instance.
// See the allocation code in PoolAlignedAlloc().
class RandenPoolEntry {
 public:
  static constexpr size_t kState = RandenTraits::kStateBytes / sizeof(uint32_t);
  static constexpr size_t kCapacity =
      RandenTraits::kCapacityBytes / sizeof(uint32_t);

  void Init(absl::Span<const uint32_t> data) {
    SpinLockHolder l(&mu_);  // Always uncontested.
    std::copy(data.begin(), data.end(), std::begin(state_));
    next_ = kState;
  }

  // Copy bytes into out.
  void Fill(uint8_t* out, size_t bytes) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns random bits from the buffer in units of T.
  template <typename T>
  inline T Generate() ABSL_LOCKS_EXCLUDED(mu_);

  inline void MaybeRefill() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (next_ >= kState) {
      next_ = kCapacity;
      impl_.Generate(state_);
    }
  }

 private:
  // Randen URBG state.
  uint32_t state_[kState] ABSL_GUARDED_BY(mu_);  // First to satisfy alignment.
  SpinLock mu_;
  const Randen impl_;
  size_t next_ ABSL_GUARDED_BY(mu_);
};

template <>
inline uint8_t RandenPoolEntry::Generate<uint8_t>() {
  SpinLockHolder l(&mu_);
  MaybeRefill();
  return static_cast<uint8_t>(state_[next_++]);
}

template <>
inline uint16_t RandenPoolEntry::Generate<uint16_t>() {
  SpinLockHolder l(&mu_);
  MaybeRefill();
  return static_cast<uint16_t>(state_[next_++]);
}

template <>
inline uint32_t RandenPoolEntry::Generate<uint32_t>() {
  SpinLockHolder l(&mu_);
  MaybeRefill();
  return state_[next_++];
}

template <>
inline uint64_t RandenPoolEntry::Generate<uint64_t>() {
  SpinLockHolder l(&mu_);
  if (next_ >= kState - 1) {
    next_ = kCapacity;
    impl_.Generate(state_);
  }
  auto p = state_ + next_;
  next_ += 2;

  uint64_t result;
  std::memcpy(&result, p, sizeof(result));
  return result;
}

void RandenPoolEntry::Fill(uint8_t* out, size_t bytes) {
  SpinLockHolder l(&mu_);
  while (bytes > 0) {
    MaybeRefill();
    size_t remaining = (kState - next_) * sizeof(state_[0]);
    size_t to_copy = std::min(bytes, remaining);
    std::memcpy(out, &state_[next_], to_copy);
    out += to_copy;
    bytes -= to_copy;
    next_ += (to_copy + sizeof(state_[0]) - 1) / sizeof(state_[0]);
  }
}

// Number of pooled urbg entries.
static constexpr size_t kPoolSize = 8;

// Shared pool entries.
static absl::once_flag pool_once;
ABSL_CACHELINE_ALIGNED static RandenPoolEntry* shared_pools[kPoolSize];

// Returns an id in the range [0 ... kPoolSize), which indexes into the
// pool of random engines.
//
// Each thread to access the pool is assigned a sequential ID (without reuse)
// from the pool-id space; the id is cached in a thread_local variable.
// This id is assigned based on the arrival-order of the thread to the
// GetPoolID call; this has no binary, CL, or runtime stability because
// on subsequent runs the order within the same program may be significantly
// different. However, as other thread IDs are not assigned sequentially,
// this is not expected to matter.
size_t GetPoolID() {
  static_assert(kPoolSize >= 1,
                "At least one urbg instance is required for PoolURBG");

  ABSL_CONST_INIT static std::atomic<uint64_t> sequence{0};

#ifdef ABSL_HAVE_THREAD_LOCAL
  static thread_local size_t my_pool_id = kPoolSize;
  if (ABSL_PREDICT_FALSE(my_pool_id == kPoolSize)) {
    my_pool_id = (sequence++ % kPoolSize);
  }
  return my_pool_id;
#else
  static pthread_key_t tid_key = [] {
    pthread_key_t tmp_key;
    int err = pthread_key_create(&tmp_key, nullptr);
    if (err) {
      ABSL_RAW_LOG(FATAL, "pthread_key_create failed with %d", err);
    }
    return tmp_key;
  }();

  // Store the value in the pthread_{get/set}specific. However an uninitialized
  // value is 0, so add +1 to distinguish from the null value.
  uintptr_t my_pool_id =
      reinterpret_cast<uintptr_t>(pthread_getspecific(tid_key));
  if (ABSL_PREDICT_FALSE(my_pool_id == 0)) {
    // No allocated ID, allocate the next value, cache it, and return.
    my_pool_id = (sequence++ % kPoolSize) + 1;
    int err = pthread_setspecific(tid_key, reinterpret_cast<void*>(my_pool_id));
    if (err) {
      ABSL_RAW_LOG(FATAL, "pthread_setspecific failed with %d", err);
    }
  }
  return my_pool_id - 1;
#endif
}

// Allocate a RandenPoolEntry with at least 32-byte alignment, which is required
// by ARM platform code.
RandenPoolEntry* PoolAlignedAlloc() {
  constexpr size_t kAlignment =
      ABSL_CACHELINE_SIZE > 32 ? ABSL_CACHELINE_SIZE : 32;

  // Not all the platforms that we build for have std::aligned_alloc, however
  // since we never free these objects, we can over allocate and munge the
  // pointers to the correct alignment.
  uintptr_t x = reinterpret_cast<uintptr_t>(
      new char[sizeof(RandenPoolEntry) + kAlignment]);
  auto y = x % kAlignment;
  void* aligned = reinterpret_cast<void*>(y == 0 ? x : (x + kAlignment - y));
  return new (aligned) RandenPoolEntry();
}

// Allocate and initialize kPoolSize objects of type RandenPoolEntry.
//
// The initialization strategy is to initialize one object directly from
// OS entropy, then to use that object to seed all of the individual
// pool instances.
void InitPoolURBG() {
  static constexpr size_t kSeedSize =
      RandenTraits::kStateBytes / sizeof(uint32_t);
  // Read the seed data from OS entropy once.
  uint32_t seed_material[kPoolSize * kSeedSize];
  if (!random_internal::ReadSeedMaterialFromOSEntropy(
          absl::MakeSpan(seed_material))) {
    random_internal::ThrowSeedGenException();
  }
  for (size_t i = 0; i < kPoolSize; i++) {
    shared_pools[i] = PoolAlignedAlloc();
    shared_pools[i]->Init(
        absl::MakeSpan(&seed_material[i * kSeedSize], kSeedSize));
  }
}

// Returns the pool entry for the current thread.
RandenPoolEntry* GetPoolForCurrentThread() {
  absl::call_once(pool_once, InitPoolURBG);
  return shared_pools[GetPoolID()];
}

}  // namespace

template <typename T>
typename RandenPool<T>::result_type RandenPool<T>::Generate() {
  auto* pool = GetPoolForCurrentThread();
  return pool->Generate<T>();
}

template <typename T>
void RandenPool<T>::Fill(absl::Span<result_type> data) {
  auto* pool = GetPoolForCurrentThread();
  pool->Fill(reinterpret_cast<uint8_t*>(data.data()),
             data.size() * sizeof(result_type));
}

template class RandenPool<uint8_t>;
template class RandenPool<uint16_t>;
template class RandenPool<uint32_t>;
template class RandenPool<uint64_t>;

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
