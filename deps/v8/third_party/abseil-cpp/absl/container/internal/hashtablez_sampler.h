// Copyright 2018 The Abseil Authors.
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
//
// -----------------------------------------------------------------------------
// File: hashtablez_sampler.h
// -----------------------------------------------------------------------------
//
// This header file defines the API for a low level library to sample hashtables
// and collect runtime statistics about them.
//
// `HashtablezSampler` controls the lifecycle of `HashtablezInfo` objects which
// store information about a single sample.
//
// `Record*` methods store information into samples.
// `Sample()` and `Unsample()` make use of a single global sampler with
// properties controlled by the flags hashtablez_enabled,
// hashtablez_sample_rate, and hashtablez_max_samples.
//
// WARNING
//
// Using this sampling API may cause sampled Swiss tables to use the global
// allocator (operator `new`) in addition to any custom allocator.  If you
// are using a table in an unusual circumstance where allocation or calling a
// linux syscall is unacceptable, this could interfere.
//
// This utility is internal-only. Use at your own risk.

#ifndef ABSL_CONTAINER_INTERNAL_HASHTABLEZ_SAMPLER_H_
#define ABSL_CONTAINER_INTERNAL_HASHTABLEZ_SAMPLER_H_

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/internal/per_thread_tls.h"
#include "absl/base/optimization.h"
#include "absl/profiling/internal/sample_recorder.h"
#include "absl/synchronization/mutex.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Stores information about a sampled hashtable.  All mutations to this *must*
// be made through `Record*` functions below.  All reads from this *must* only
// occur in the callback to `HashtablezSampler::Iterate`.
struct HashtablezInfo : public profiling_internal::Sample<HashtablezInfo> {
  // Constructs the object but does not fill in any fields.
  HashtablezInfo();
  ~HashtablezInfo();
  HashtablezInfo(const HashtablezInfo&) = delete;
  HashtablezInfo& operator=(const HashtablezInfo&) = delete;

  // Puts the object into a clean state, fills in the logically `const` members,
  // blocking for any readers that are currently sampling the object.
  void PrepareForSampling(int64_t stride, size_t inline_element_size_value)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(init_mu);

  // These fields are mutated by the various Record* APIs and need to be
  // thread-safe.
  std::atomic<size_t> capacity;
  std::atomic<size_t> size;
  std::atomic<size_t> num_erases;
  std::atomic<size_t> num_rehashes;
  std::atomic<size_t> max_probe_length;
  std::atomic<size_t> total_probe_length;
  std::atomic<size_t> hashes_bitwise_or;
  std::atomic<size_t> hashes_bitwise_and;
  std::atomic<size_t> hashes_bitwise_xor;
  std::atomic<size_t> max_reserve;

  // All of the fields below are set by `PrepareForSampling`, they must not be
  // mutated in `Record*` functions.  They are logically `const` in that sense.
  // These are guarded by init_mu, but that is not externalized to clients,
  // which can read them only during `SampleRecorder::Iterate` which will hold
  // the lock.
  static constexpr int kMaxStackDepth = 64;
  absl::Time create_time;
  int32_t depth;
  void* stack[kMaxStackDepth];
  size_t inline_element_size;  // How big is the slot?
};

void RecordRehashSlow(HashtablezInfo* info, size_t total_probe_length);

void RecordReservationSlow(HashtablezInfo* info, size_t target_capacity);

void RecordClearedReservationSlow(HashtablezInfo* info);

void RecordStorageChangedSlow(HashtablezInfo* info, size_t size,
                              size_t capacity);

void RecordInsertSlow(HashtablezInfo* info, size_t hash,
                      size_t distance_from_desired);

void RecordEraseSlow(HashtablezInfo* info);

struct SamplingState {
  int64_t next_sample;
  // When we make a sampling decision, we record that distance so we can weight
  // each sample.
  int64_t sample_stride;
};

HashtablezInfo* SampleSlow(SamplingState& next_sample,
                           size_t inline_element_size);
void UnsampleSlow(HashtablezInfo* info);

#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
#error ABSL_INTERNAL_HASHTABLEZ_SAMPLE cannot be directly set
#endif  // defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)

#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
class HashtablezInfoHandle {
 public:
  explicit HashtablezInfoHandle() : info_(nullptr) {}
  explicit HashtablezInfoHandle(HashtablezInfo* info) : info_(info) {}

  // We do not have a destructor. Caller is responsible for calling Unregister
  // before destroying the handle.
  void Unregister() {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    UnsampleSlow(info_);
  }

  inline bool IsSampled() const { return ABSL_PREDICT_FALSE(info_ != nullptr); }

  inline void RecordStorageChanged(size_t size, size_t capacity) {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordStorageChangedSlow(info_, size, capacity);
  }

  inline void RecordRehash(size_t total_probe_length) {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordRehashSlow(info_, total_probe_length);
  }

  inline void RecordReservation(size_t target_capacity) {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordReservationSlow(info_, target_capacity);
  }

  inline void RecordClearedReservation() {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordClearedReservationSlow(info_);
  }

  inline void RecordInsert(size_t hash, size_t distance_from_desired) {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordInsertSlow(info_, hash, distance_from_desired);
  }

  inline void RecordErase() {
    if (ABSL_PREDICT_TRUE(info_ == nullptr)) return;
    RecordEraseSlow(info_);
  }

  friend inline void swap(HashtablezInfoHandle& lhs,
                          HashtablezInfoHandle& rhs) {
    std::swap(lhs.info_, rhs.info_);
  }

 private:
  friend class HashtablezInfoHandlePeer;
  HashtablezInfo* info_;
};
#else
// Ensure that when Hashtablez is turned off at compile time, HashtablezInfo can
// be removed by the linker, in order to reduce the binary size.
class HashtablezInfoHandle {
 public:
  explicit HashtablezInfoHandle() = default;
  explicit HashtablezInfoHandle(std::nullptr_t) {}

  inline void Unregister() {}
  inline bool IsSampled() const { return false; }
  inline void RecordStorageChanged(size_t /*size*/, size_t /*capacity*/) {}
  inline void RecordRehash(size_t /*total_probe_length*/) {}
  inline void RecordReservation(size_t /*target_capacity*/) {}
  inline void RecordClearedReservation() {}
  inline void RecordInsert(size_t /*hash*/, size_t /*distance_from_desired*/) {}
  inline void RecordErase() {}

  friend inline void swap(HashtablezInfoHandle& /*lhs*/,
                          HashtablezInfoHandle& /*rhs*/) {}
};
#endif  // defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)

#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
extern ABSL_PER_THREAD_TLS_KEYWORD SamplingState global_next_sample;
#endif  // defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)

// Returns an RAII sampling handle that manages registration and unregistation
// with the global sampler.
inline HashtablezInfoHandle Sample(
    size_t inline_element_size ABSL_ATTRIBUTE_UNUSED) {
#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
  if (ABSL_PREDICT_TRUE(--global_next_sample.next_sample > 0)) {
    return HashtablezInfoHandle(nullptr);
  }
  return HashtablezInfoHandle(
      SampleSlow(global_next_sample, inline_element_size));
#else
  return HashtablezInfoHandle(nullptr);
#endif  // !ABSL_PER_THREAD_TLS
}

using HashtablezSampler =
    ::absl::profiling_internal::SampleRecorder<HashtablezInfo>;

// Returns a global Sampler.
HashtablezSampler& GlobalHashtablezSampler();

using HashtablezConfigListener = void (*)();
void SetHashtablezConfigListener(HashtablezConfigListener l);

// Enables or disables sampling for Swiss tables.
bool IsHashtablezEnabled();
void SetHashtablezEnabled(bool enabled);
void SetHashtablezEnabledInternal(bool enabled);

// Sets the rate at which Swiss tables will be sampled.
int32_t GetHashtablezSampleParameter();
void SetHashtablezSampleParameter(int32_t rate);
void SetHashtablezSampleParameterInternal(int32_t rate);

// Sets a soft max for the number of samples that will be kept.
size_t GetHashtablezMaxSamples();
void SetHashtablezMaxSamples(size_t max);
void SetHashtablezMaxSamplesInternal(size_t max);

// Configuration override.
// This allows process-wide sampling without depending on order of
// initialization of static storage duration objects.
// The definition of this constant is weak, which allows us to inject a
// different value for it at link time.
extern "C" bool ABSL_INTERNAL_C_SYMBOL(AbslContainerInternalSampleEverything)();

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_HASHTABLEZ_SAMPLER_H_
