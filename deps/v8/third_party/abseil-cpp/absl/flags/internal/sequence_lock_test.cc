// Copyright 2020 The Abseil Authors.
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
#include "absl/flags/internal/sequence_lock.h"

#include <algorithm>
#include <atomic>
#include <thread>  // NOLINT(build/c++11)
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/container/fixed_array.h"
#include "absl/time/clock.h"

namespace {

namespace flags = absl::flags_internal;

class ConcurrentSequenceLockTest
    : public testing::TestWithParam<std::tuple<int, int>> {
 public:
  ConcurrentSequenceLockTest()
      : buf_bytes_(std::get<0>(GetParam())),
        num_threads_(std::get<1>(GetParam())) {}

 protected:
  const int buf_bytes_;
  const int num_threads_;
};

TEST_P(ConcurrentSequenceLockTest, ReadAndWrite) {
  const int buf_words =
      flags::AlignUp(buf_bytes_, sizeof(uint64_t)) / sizeof(uint64_t);

  // The buffer that will be protected by the SequenceLock.
  absl::FixedArray<std::atomic<uint64_t>> protected_buf(buf_words);
  for (auto& v : protected_buf) v = -1;

  flags::SequenceLock seq_lock;
  std::atomic<bool> stop{false};
  std::atomic<int64_t> bad_reads{0};
  std::atomic<int64_t> good_reads{0};
  std::atomic<int64_t> unsuccessful_reads{0};

  // Start a bunch of threads which read 'protected_buf' under the sequence
  // lock. The main thread will concurrently update 'protected_buf'. The updates
  // always consist of an array of identical integers. The reader ensures that
  // any data it reads matches that pattern (i.e. the reads are not "torn").
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads_; i++) {
    threads.emplace_back([&]() {
      absl::FixedArray<char> local_buf(buf_bytes_);
      while (!stop.load(std::memory_order_relaxed)) {
        if (seq_lock.TryRead(local_buf.data(), protected_buf.data(),
                             buf_bytes_)) {
          bool good = true;
          for (const auto& v : local_buf) {
            if (v != local_buf[0]) good = false;
          }
          if (good) {
            good_reads.fetch_add(1, std::memory_order_relaxed);
          } else {
            bad_reads.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          unsuccessful_reads.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  while (unsuccessful_reads.load(std::memory_order_relaxed) < num_threads_) {
    absl::SleepFor(absl::Milliseconds(1));
  }
  seq_lock.MarkInitialized();

  // Run a maximum of 5 seconds. On Windows, the scheduler behavior seems
  // somewhat unfair and without an explicit timeout for this loop, the tests
  // can run a long time.
  absl::Time deadline = absl::Now() + absl::Seconds(5);
  for (int i = 0; i < 100 && absl::Now() < deadline; i++) {
    absl::FixedArray<char> writer_buf(buf_bytes_);
    for (auto& v : writer_buf) v = i;
    seq_lock.Write(protected_buf.data(), writer_buf.data(), buf_bytes_);
    absl::SleepFor(absl::Microseconds(10));
  }
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : threads) t.join();
  ASSERT_GE(good_reads, 0);
  ASSERT_EQ(bad_reads, 0);
}

// Simple helper for generating a range of thread counts.
// Generates [low, low*scale, low*scale^2, ...high)
// (even if high is between low*scale^k and low*scale^(k+1)).
std::vector<int> MultiplicativeRange(int low, int high, int scale) {
  std::vector<int> result;
  for (int current = low; current < high; current *= scale) {
    result.push_back(current);
  }
  result.push_back(high);
  return result;
}

#ifndef ABSL_HAVE_THREAD_SANITIZER
const int kMaxThreads = absl::base_internal::NumCPUs();
#else
// With TSAN, a lot of threads contending for atomic access on the sequence
// lock make this test run too slowly.
const int kMaxThreads = std::min(absl::base_internal::NumCPUs(), 4);
#endif

// Return all of the interesting buffer sizes worth testing:
// powers of two and adjacent values.
std::vector<int> InterestingBufferSizes() {
  std::vector<int> ret;
  for (int v : MultiplicativeRange(1, 128, 2)) {
    ret.push_back(v);
    if (v > 1) {
      ret.push_back(v - 1);
    }
    ret.push_back(v + 1);
  }
  return ret;
}

INSTANTIATE_TEST_SUITE_P(
    TestManyByteSizes, ConcurrentSequenceLockTest,
    testing::Combine(
        // Buffer size (bytes).
        testing::ValuesIn(InterestingBufferSizes()),
        // Number of reader threads.
        testing::ValuesIn(MultiplicativeRange(1, kMaxThreads, 2))));

// Simple single-threaded test, parameterized by the size of the buffer to be
// protected.
class SequenceLockTest : public testing::TestWithParam<int> {};

TEST_P(SequenceLockTest, SingleThreaded) {
  const int size = GetParam();
  absl::FixedArray<std::atomic<uint64_t>> protected_buf(
      flags::AlignUp(size, sizeof(uint64_t)) / sizeof(uint64_t));

  flags::SequenceLock seq_lock;
  seq_lock.MarkInitialized();

  std::vector<char> src_buf(size, 'x');
  seq_lock.Write(protected_buf.data(), src_buf.data(), size);

  std::vector<char> dst_buf(size, '0');
  ASSERT_TRUE(seq_lock.TryRead(dst_buf.data(), protected_buf.data(), size));
  ASSERT_EQ(src_buf, dst_buf);
}
INSTANTIATE_TEST_SUITE_P(TestManyByteSizes, SequenceLockTest,
                         // Buffer size (bytes).
                         testing::Range(1, 128));

}  // namespace
