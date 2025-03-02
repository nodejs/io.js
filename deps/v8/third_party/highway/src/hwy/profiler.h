// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_PROFILER_H_
#define HIGHWAY_HWY_PROFILER_H_

// High precision, low overhead time measurements. Returns exact call counts and
// total elapsed time for user-defined 'zones' (code regions, i.e. C++ scopes).
//
// Uses RAII to capture begin/end timestamps, with user-specified zone names:
//   { PROFILER_ZONE("name"); /*code*/ } or
// the name of the current function:
//   void FuncToMeasure() { PROFILER_FUNC; /*code*/ }.
//
// After all threads have exited any zones, invoke PROFILER_PRINT_RESULTS() to
// print call counts and average durations [CPU cycles] to stdout, sorted in
// descending order of total duration.
//
// The binary MUST be built with --dynamic_mode=off because we rely on the data
// segments being nearby; if not, an assertion will likely fail.

#include "hwy/base.h"

// Configuration settings:

// If zero, this file has no effect and no measurements will be recorded.
#ifndef PROFILER_ENABLED
#define PROFILER_ENABLED 0
#endif

// How many mebibytes to allocate (if PROFILER_ENABLED) per thread that
// enters at least one zone. Once this buffer is full, the thread will analyze
// and discard packets, thus temporarily adding some observer overhead.
// Each zone occupies 16 bytes.
#ifndef PROFILER_THREAD_STORAGE
#define PROFILER_THREAD_STORAGE 200ULL
#endif

#if PROFILER_ENABLED || HWY_IDE

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>  // strcmp

#include <algorithm>  // std::sort
#include <atomic>

#include "hwy/aligned_allocator.h"
#include "hwy/cache_control.h"  // FlushStream
// #include "hwy/contrib/sort/vqsort.h"
#include "hwy/robust_statistics.h"
#include "hwy/timer-inl.h"
#include "hwy/timer.h"

#define PROFILER_PRINT_OVERHEAD 0

namespace hwy {

// Upper bounds for fixed-size data structures (guarded via HWY_DASSERT):

// How many threads can actually enter a zone (those that don't do not count).
// Memory use is about kMaxThreads * PROFILER_THREAD_STORAGE MiB.
// WARNING: a fiber library can spawn hundreds of threads.
static constexpr size_t kMaxThreads = 256;

static constexpr size_t kMaxDepth = 64;  // Maximum nesting of zones.

static constexpr size_t kMaxZones = 256;  // Total number of zones.

#pragma pack(push, 1)

// Represents zone entry/exit events. Stores a full-resolution timestamp plus
// an offset (representing zone name or identifying exit packets). POD.
class Packet {
 public:
  // If offsets do not fit, UpdateOrAdd will overrun our heap allocation
  // (governed by kMaxZones). We have seen multi-megabyte offsets.
  static constexpr size_t kOffsetBits = 25;
  static constexpr uint64_t kOffsetBias = 1ULL << (kOffsetBits - 1);

  // We need full-resolution timestamps; at an effective rate of 4 GHz,
  // this permits 1 minute zone durations (for longer durations, split into
  // multiple zones). Wraparound is handled by masking.
  static constexpr size_t kTimestampBits = 64 - kOffsetBits;
  static constexpr uint64_t kTimestampMask = (1ULL << kTimestampBits) - 1;

  static Packet Make(const size_t biased_offset, const uint64_t timestamp) {
    HWY_DASSERT(biased_offset != 0);
    HWY_DASSERT(biased_offset < (1ULL << kOffsetBits));

    Packet packet;
    packet.bits_ =
        (biased_offset << kTimestampBits) + (timestamp & kTimestampMask);

    HWY_DASSERT(packet.BiasedOffset() == biased_offset);
    HWY_DASSERT(packet.Timestamp() == (timestamp & kTimestampMask));
    return packet;
  }

  uint64_t Timestamp() const { return bits_ & kTimestampMask; }

  size_t BiasedOffset() const {
    const size_t biased_offset = (bits_ >> kTimestampBits);
    HWY_DASSERT(biased_offset != 0);
    HWY_DASSERT(biased_offset < (1ULL << kOffsetBits));
    return biased_offset;
  }

 private:
  uint64_t bits_;
};
static_assert(sizeof(Packet) == 8, "Wrong Packet size");

// All translation units must use the same string origin. A static member
// function ensures this without requiring a separate .cc file.
struct StringOrigin {
  // Returns the address of a string literal. Assuming zone names are also
  // literals and stored nearby, we can represent them as offsets from this,
  // which is faster to compute than hashes or even a static index.
  static const char* Get() {
    // Chosen such that no zone name is a prefix nor suffix of this string
    // to ensure they aren't merged. Note zone exit packets use
    // `biased_offset == kOffsetBias`.
    static const char* string_origin = "__#__";
    return string_origin - Packet::kOffsetBias;
  }
};

// Representation of an active zone, stored in a stack. Used to deduct
// child duration from the parent's self time. POD.
struct Node {
  Packet packet;
  uint64_t child_total;
};
static_assert(sizeof(Node) == 16, "Wrong Node size");

// Holds statistics for all zones with the same name. POD.
struct Accumulator {
  static constexpr size_t kNumCallBits = 64 - Packet::kOffsetBits;

  uint64_t BiasedOffset() const {
    const size_t biased_offset = u128.lo >> kNumCallBits;
    HWY_DASSERT(biased_offset != 0);
    HWY_DASSERT(biased_offset < (1ULL << Packet::kOffsetBits));
    return biased_offset;
  }
  uint64_t NumCalls() const { return u128.lo & ((1ULL << kNumCallBits) - 1); }
  uint64_t Duration() const { return u128.hi; }

  void Set(uint64_t biased_offset, uint64_t num_calls, uint64_t duration) {
    HWY_DASSERT(biased_offset != 0);
    HWY_DASSERT(biased_offset < (1ULL << Packet::kOffsetBits));
    HWY_DASSERT(num_calls < (1ULL << kNumCallBits));

    u128.hi = duration;
    u128.lo = (biased_offset << kNumCallBits) + num_calls;

    HWY_DASSERT(BiasedOffset() == biased_offset);
    HWY_DASSERT(NumCalls() == num_calls);
    HWY_DASSERT(Duration() == duration);
  }

  void Add(uint64_t num_calls, uint64_t duration) {
    const uint64_t biased_offset = BiasedOffset();
    (void)biased_offset;

    u128.lo += num_calls;
    u128.hi += duration;

    HWY_DASSERT(biased_offset == BiasedOffset());
  }

  // For fast sorting by duration, which must therefore be the hi element.
  // lo holds BiasedOffset and NumCalls.
  uint128_t u128;
};
static_assert(sizeof(Accumulator) == 16, "Wrong Accumulator size");

template <typename T>
inline T ClampedSubtract(const T minuend, const T subtrahend) {
  if (subtrahend > minuend) {
    return 0;
  }
  return minuend - subtrahend;
}

// Per-thread call graph (stack) and Accumulator for each zone.
class Results {
 public:
  Results() {
    ZeroBytes(nodes_, sizeof(nodes_));
    ZeroBytes(zones_, sizeof(zones_));
  }

  // Used for computing overhead when this thread encounters its first Zone.
  // This has no observable effect apart from increasing "analyze_elapsed_".
  uint64_t ZoneDuration(const Packet* packets) {
    HWY_DASSERT(depth_ == 0);
    HWY_DASSERT(num_zones_ == 0);
    AnalyzePackets(packets, 2);
    const uint64_t duration = zones_[0].Duration();
    zones_[0].Set(1, 0, 0);  // avoids triggering biased_offset = 0 checks
    HWY_DASSERT(depth_ == 0);
    num_zones_ = 0;
    return duration;
  }

  void SetSelfOverhead(const uint64_t self_overhead) {
    self_overhead_ = self_overhead;
  }

  void SetChildOverhead(const uint64_t child_overhead) {
    child_overhead_ = child_overhead;
  }

  // Draw all required information from the packets, which can be discarded
  // afterwards. Called whenever this thread's storage is full.
  void AnalyzePackets(const Packet* packets, const size_t num_packets) {
    namespace hn = HWY_NAMESPACE;
    const uint64_t t0 = hn::timer::Start();

    for (size_t i = 0; i < num_packets; ++i) {
      const Packet p = packets[i];
      // Entering a zone
      if (p.BiasedOffset() != Packet::kOffsetBias) {
        HWY_DASSERT(depth_ < kMaxDepth);
        nodes_[depth_].packet = p;
        HWY_DASSERT(p.BiasedOffset() != 0);
        nodes_[depth_].child_total = 0;
        ++depth_;
        continue;
      }

      HWY_DASSERT(depth_ != 0);
      const Node& node = nodes_[depth_ - 1];
      // Masking correctly handles unsigned wraparound.
      const uint64_t duration =
          (p.Timestamp() - node.packet.Timestamp()) & Packet::kTimestampMask;
      const uint64_t self_duration = ClampedSubtract(
          duration, self_overhead_ + child_overhead_ + node.child_total);

      UpdateOrAdd(node.packet.BiasedOffset(), 1, self_duration);
      --depth_;

      // Deduct this nested node's time from its parent's self_duration.
      if (depth_ != 0) {
        nodes_[depth_ - 1].child_total += duration + child_overhead_;
      }
    }

    const uint64_t t1 = hn::timer::Stop();
    analyze_elapsed_ += t1 - t0;
  }

  // Incorporates results from another thread. Call after all threads have
  // exited any zones.
  void Assimilate(Results& other) {
    namespace hn = HWY_NAMESPACE;
    const uint64_t t0 = hn::timer::Start();
    HWY_DASSERT(depth_ == 0);
    HWY_DASSERT(other.depth_ == 0);

    for (size_t i = 0; i < other.num_zones_; ++i) {
      const Accumulator& zone = other.zones_[i];
      UpdateOrAdd(zone.BiasedOffset(), zone.NumCalls(), zone.Duration());
    }
    other.num_zones_ = 0;
    const uint64_t t1 = hn::timer::Stop();
    analyze_elapsed_ += t1 - t0 + other.analyze_elapsed_;
  }

  // Single-threaded.
  void Print() {
    namespace hn = HWY_NAMESPACE;
    const uint64_t t0 = hn::timer::Start();
    MergeDuplicates();

    // Sort by decreasing total (self) cost.
    // VQSort(&zones_[0].u128, num_zones_, SortDescending());
    std::sort(zones_, zones_ + num_zones_,
              [](const Accumulator& z1, const Accumulator& z2) {
                return z1.Duration() > z2.Duration();
              });

    const double inv_freq = 1.0 / platform::InvariantTicksPerSecond();

    const char* string_origin = StringOrigin::Get();
    for (size_t i = 0; i < num_zones_; ++i) {
      const Accumulator& z = zones_[i];
      const size_t num_calls = z.NumCalls();
      const double duration = static_cast<double>(z.Duration());
      printf("%-40s: %10zu x %15.0f = %9.6f\n",
             string_origin + z.BiasedOffset(), num_calls, duration / num_calls,
             duration * inv_freq);
    }
    num_zones_ = 0;

    const uint64_t t1 = hn::timer::Stop();
    analyze_elapsed_ += t1 - t0;
    printf("Total analysis [s]: %f\n",
           static_cast<double>(analyze_elapsed_) * inv_freq);
  }

 private:
  // Updates an existing Accumulator (uniquely identified by biased_offset) or
  // adds one if this is the first time this thread analyzed that zone.
  // Uses a self-organizing list data structure, which avoids dynamic memory
  // allocations and is far faster than unordered_map.
  void UpdateOrAdd(const size_t biased_offset, const uint64_t num_calls,
                   const uint64_t duration) {
    HWY_DASSERT(biased_offset != 0);
    HWY_DASSERT(biased_offset < (1ULL << Packet::kOffsetBits));

    // Special case for first zone: (maybe) update, without swapping.
    if (num_zones_ != 0 && zones_[0].BiasedOffset() == biased_offset) {
      zones_[0].Add(num_calls, duration);
      return;
    }

    // Look for a zone with the same offset.
    for (size_t i = 1; i < num_zones_; ++i) {
      if (zones_[i].BiasedOffset() == biased_offset) {
        zones_[i].Add(num_calls, duration);
        // Swap with predecessor (more conservative than move to front,
        // but at least as successful).
        const Accumulator prev = zones_[i - 1];
        zones_[i - 1] = zones_[i];
        zones_[i] = prev;
        return;
      }
    }

    // Not found; create a new Accumulator.
    HWY_DASSERT(num_zones_ < kMaxZones);
    zones_[num_zones_].Set(biased_offset, num_calls, duration);
    ++num_zones_;
  }

  // Each instantiation of a function template seems to get its own copy of
  // __func__ and GCC doesn't merge them. An N^2 search for duplicates is
  // acceptable because we only expect a few dozen zones.
  void MergeDuplicates() {
    const char* string_origin = StringOrigin::Get();
    for (size_t i = 0; i < num_zones_; ++i) {
      const size_t biased_offset = zones_[i].BiasedOffset();
      const char* name = string_origin + biased_offset;
      // Separate num_calls from biased_offset so we can add them together.
      uint64_t num_calls = zones_[i].NumCalls();

      // Add any subsequent duplicates to num_calls and total_duration.
      for (size_t j = i + 1; j < num_zones_;) {
        if (!strcmp(name, string_origin + zones_[j].BiasedOffset())) {
          num_calls += zones_[j].NumCalls();
          zones_[i].Add(0, zones_[j].Duration());
          // j was the last zone, so we are done.
          if (j == num_zones_ - 1) break;
          // Replace current zone with the last one, and check it next.
          zones_[j] = zones_[--num_zones_];
        } else {  // Name differed, try next Accumulator.
          ++j;
        }
      }

      // Re-pack regardless of whether any duplicates were found.
      zones_[i].Set(biased_offset, num_calls, zones_[i].Duration());
    }
  }

  uint64_t analyze_elapsed_ = 0;
  uint64_t self_overhead_ = 0;
  uint64_t child_overhead_ = 0;

  size_t depth_ = 0;      // Number of active zones.
  size_t num_zones_ = 0;  // Number of retired zones.

  alignas(HWY_ALIGNMENT) Node nodes_[kMaxDepth];         // Stack
  alignas(HWY_ALIGNMENT) Accumulator zones_[kMaxZones];  // Self-organizing list
};

// Per-thread packet storage, dynamically allocated.
class ThreadSpecific {
  static constexpr size_t kBufferCapacity = HWY_ALIGNMENT / sizeof(Packet);

 public:
  // "name" is used to sanity-check offsets fit in kOffsetBits.
  explicit ThreadSpecific(const char* name)
      : max_packets_((PROFILER_THREAD_STORAGE << 20) / sizeof(Packet)),
        packets_(AllocateAligned<Packet>(max_packets_)),
        num_packets_(0),
        string_origin_(StringOrigin::Get()) {
    // Even in optimized builds, verify that this zone's name offset fits
    // within the allotted space. If not, UpdateOrAdd is likely to overrun
    // zones_[]. Checking here on the cold path (only reached once per thread)
    // is cheap, but it only covers one zone.
    const size_t biased_offset = name - string_origin_;
    HWY_ASSERT(biased_offset < (1ULL << Packet::kOffsetBits));
  }

  // Depends on Zone => defined below.
  void ComputeOverhead();

  void WriteEntry(const char* name, const uint64_t timestamp) {
    HWY_DASSERT(name >= string_origin_);
    const size_t biased_offset = static_cast<size_t>(name - string_origin_);
    Write(Packet::Make(biased_offset, timestamp));
  }

  void WriteExit(const uint64_t timestamp) {
    const size_t biased_offset = Packet::kOffsetBias;
    Write(Packet::Make(biased_offset, timestamp));
  }

  void AnalyzeRemainingPackets() {
    // Ensures prior weakly-ordered streaming stores are globally visible.
    FlushStream();

    // Storage full => empty it.
    if (num_packets_ + buffer_size_ > max_packets_) {
      results_.AnalyzePackets(packets_.get(), num_packets_);
      num_packets_ = 0;
    }
    CopyBytes(buffer_, packets_.get() + num_packets_,
              buffer_size_ * sizeof(Packet));
    num_packets_ += buffer_size_;

    results_.AnalyzePackets(packets_.get(), num_packets_);
    num_packets_ = 0;
  }

  Results& GetResults() { return results_; }

 private:
  // Overwrites "to" while attempting to bypass the cache (read-for-ownership).
  // Both pointers must be aligned.
  static void StreamCacheLine(const uint64_t* HWY_RESTRICT from,
                              uint64_t* HWY_RESTRICT to) {
#if HWY_COMPILER_CLANG
    for (size_t i = 0; i < HWY_ALIGNMENT / sizeof(uint64_t); ++i) {
      __builtin_nontemporal_store(from[i], to + i);
    }
#else
    hwy::CopyBytes(from, to, HWY_ALIGNMENT);
#endif
  }

  // Write packet to buffer/storage, emptying them as needed.
  void Write(const Packet packet) {
    // Buffer full => copy to storage.
    if (buffer_size_ == kBufferCapacity) {
      // Storage full => empty it.
      if (num_packets_ + kBufferCapacity > max_packets_) {
        results_.AnalyzePackets(packets_.get(), num_packets_);
        num_packets_ = 0;
      }
      // This buffering halves observer overhead and decreases the overall
      // runtime by about 3%. Casting is safe because the first member is u64.
      StreamCacheLine(
          reinterpret_cast<const uint64_t*>(buffer_),
          reinterpret_cast<uint64_t*>(packets_.get() + num_packets_));
      num_packets_ += kBufferCapacity;
      buffer_size_ = 0;
    }
    buffer_[buffer_size_] = packet;
    ++buffer_size_;
  }

  // Write-combining buffer to avoid cache pollution. Must be the first
  // non-static member to ensure cache-line alignment.
  Packet buffer_[kBufferCapacity];
  size_t buffer_size_ = 0;

  const size_t max_packets_;
  // Contiguous storage for zone enter/exit packets.
  AlignedFreeUniquePtr<Packet[]> packets_;
  size_t num_packets_;
  // Cached here because we already read this cache line on zone entry/exit.
  const char* string_origin_;
  Results results_;
};

class ThreadList {
 public:
  // Called from any thread.
  ThreadSpecific* Add(const char* name) {
    const size_t index = num_threads_.fetch_add(1, std::memory_order_relaxed);
    HWY_DASSERT(index < kMaxThreads);

    ThreadSpecific* ts = MakeUniqueAligned<ThreadSpecific>(name).release();
    threads_[index].store(ts, std::memory_order_release);
    return ts;
  }

  // Single-threaded.
  void PrintResults() {
    const auto acq = std::memory_order_acquire;
    const size_t num_threads = num_threads_.load(acq);

    ThreadSpecific* main = threads_[0].load(acq);
    main->AnalyzeRemainingPackets();

    for (size_t i = 1; i < num_threads; ++i) {
      ThreadSpecific* ts = threads_[i].load(acq);
      ts->AnalyzeRemainingPackets();
      main->GetResults().Assimilate(ts->GetResults());
    }

    if (num_threads != 0) {
      main->GetResults().Print();
    }
  }

 private:
  // Owning pointers.
  alignas(64) std::atomic<ThreadSpecific*> threads_[kMaxThreads];
  std::atomic<size_t> num_threads_{0};
};

// RAII zone enter/exit recorder constructed by the ZONE macro; also
// responsible for initializing ThreadSpecific.
class Zone {
 public:
  // "name" must be a string literal (see StringOrigin::Get).
  HWY_NOINLINE explicit Zone(const char* name) {
    HWY_FENCE;
    ThreadSpecific* HWY_RESTRICT thread_specific = StaticThreadSpecific();
    if (HWY_UNLIKELY(thread_specific == nullptr)) {
      // Ensure the CPU supports our timer.
      char cpu[100];
      if (!platform::HaveTimerStop(cpu)) {
        HWY_ABORT("CPU %s is too old for PROFILER_ENABLED=1, exiting", cpu);
      }

      thread_specific = StaticThreadSpecific() = Threads().Add(name);
      // Must happen after setting StaticThreadSpecific, because ComputeOverhead
      // also calls Zone().
      thread_specific->ComputeOverhead();
    }

    // (Capture timestamp ASAP, not inside WriteEntry.)
    HWY_FENCE;
    const uint64_t timestamp = HWY_NAMESPACE::timer::Start();
    thread_specific->WriteEntry(name, timestamp);
  }

  HWY_NOINLINE ~Zone() {
    HWY_FENCE;
    const uint64_t timestamp = HWY_NAMESPACE::timer::Stop();
    StaticThreadSpecific()->WriteExit(timestamp);
    HWY_FENCE;
  }

  // Call exactly once after all threads have exited all zones.
  static void PrintResults() { Threads().PrintResults(); }

 private:
  // Returns reference to the thread's ThreadSpecific pointer (initially null).
  // Function-local static avoids needing a separate definition.
  static ThreadSpecific*& StaticThreadSpecific() {
    static thread_local ThreadSpecific* thread_specific;
    return thread_specific;
  }

  // Returns the singleton ThreadList. Non time-critical.
  static ThreadList& Threads() {
    static ThreadList threads_;
    return threads_;
  }
};

// Creates a zone starting from here until the end of the current scope.
// Timestamps will be recorded when entering and exiting the zone.
// "name" must be a string literal, which is ensured by merging with "".
#define PROFILER_ZONE(name)      \
  HWY_FENCE;                     \
  const hwy::Zone zone("" name); \
  HWY_FENCE

// Creates a zone for an entire function (when placed at its beginning).
// Shorter/more convenient than ZONE.
#define PROFILER_FUNC             \
  HWY_FENCE;                      \
  const hwy::Zone zone(__func__); \
  HWY_FENCE

#define PROFILER_PRINT_RESULTS hwy::Zone::PrintResults

inline void ThreadSpecific::ComputeOverhead() {
  namespace hn = HWY_NAMESPACE;
  // Delay after capturing timestamps before/after the actual zone runs. Even
  // with frequency throttling disabled, this has a multimodal distribution,
  // including 32, 34, 48, 52, 59, 62.
  uint64_t self_overhead;
  {
    const size_t kNumSamples = 32;
    uint32_t samples[kNumSamples];
    for (size_t idx_sample = 0; idx_sample < kNumSamples; ++idx_sample) {
      const size_t kNumDurations = 1024;
      uint32_t durations[kNumDurations];

      for (size_t idx_duration = 0; idx_duration < kNumDurations;
           ++idx_duration) {
        {
          PROFILER_ZONE("Dummy Zone (never shown)");
        }
        const uint64_t duration = results_.ZoneDuration(buffer_);
        buffer_size_ = 0;
        durations[idx_duration] = static_cast<uint32_t>(duration);
        HWY_DASSERT(num_packets_ == 0);
      }
      robust_statistics::CountingSort(durations, kNumDurations);
      samples[idx_sample] = robust_statistics::Mode(durations, kNumDurations);
    }
    // Median.
    robust_statistics::CountingSort(samples, kNumSamples);
    self_overhead = samples[kNumSamples / 2];
    if (PROFILER_PRINT_OVERHEAD) {
      printf("Overhead: %.0f\n", static_cast<double>(self_overhead));
    }
    results_.SetSelfOverhead(self_overhead);
  }

  // Delay before capturing start timestamp / after end timestamp.
  const size_t kNumSamples = 32;
  uint32_t samples[kNumSamples];
  for (size_t idx_sample = 0; idx_sample < kNumSamples; ++idx_sample) {
    const size_t kNumDurations = 16;
    uint32_t durations[kNumDurations];
    for (size_t idx_duration = 0; idx_duration < kNumDurations;
         ++idx_duration) {
      const size_t kReps = 10000;
      // Analysis time should not be included => must fit within buffer.
      HWY_DASSERT(kReps * 2 < max_packets_);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      const uint64_t t0 = hn::timer::Start();
      for (size_t i = 0; i < kReps; ++i) {
        PROFILER_ZONE("Dummy");
      }
      FlushStream();
      const uint64_t t1 = hn::timer::Stop();
      HWY_DASSERT(num_packets_ + buffer_size_ == kReps * 2);
      buffer_size_ = 0;
      num_packets_ = 0;
      const uint64_t avg_duration = (t1 - t0 + kReps / 2) / kReps;
      durations[idx_duration] =
          static_cast<uint32_t>(ClampedSubtract(avg_duration, self_overhead));
    }
    robust_statistics::CountingSort(durations, kNumDurations);
    samples[idx_sample] = robust_statistics::Mode(durations, kNumDurations);
  }
  robust_statistics::CountingSort(samples, kNumSamples);
  const uint64_t child_overhead = samples[9 * kNumSamples / 10];
  if (PROFILER_PRINT_OVERHEAD) {
    printf("Child overhead: %.0f\n", static_cast<double>(child_overhead));
  }
  results_.SetChildOverhead(child_overhead);
}

#pragma pack(pop)

}  // namespace hwy

#endif  // PROFILER_ENABLED || HWY_IDE

#if !PROFILER_ENABLED && !HWY_IDE
#define PROFILER_ZONE(name)
#define PROFILER_FUNC
#define PROFILER_PRINT_RESULTS()
#endif

#endif  // HIGHWAY_HWY_PROFILER_H_
