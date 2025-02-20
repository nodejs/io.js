// Copyright 2023 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Modified from BSD-licensed code
// Copyright (c) the JPEG XL Project Authors. All rights reserved.
// See https://github.com/libjxl/libjxl/blob/main/LICENSE.

#ifndef HIGHWAY_HWY_CONTRIB_THREAD_POOL_THREAD_POOL_H_
#define HIGHWAY_HWY_CONTRIB_THREAD_POOL_THREAD_POOL_H_

// IWYU pragma: begin_exports
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>  // snprintf

#include <array>
#include <new>
#include <thread>  //NOLINT
// IWYU pragma: end_exports

#include <atomic>
#include <vector>

#include "hwy/aligned_allocator.h"  // HWY_ALIGNMENT
#include "hwy/base.h"
#include "hwy/cache_control.h"  // Pause
#include "hwy/contrib/thread_pool/futex.h"
#include "hwy/contrib/thread_pool/topology.h"

// Temporary NOINLINE for profiling.
#define HWY_POOL_INLINE HWY_NOINLINE

#ifndef HWY_POOL_SETRANGE_INLINE
#if HWY_ARCH_ARM
// Workaround for invalid codegen on Arm (begin_ is larger than expected).
#define HWY_POOL_SETRANGE_INLINE HWY_NOINLINE
#else
#define HWY_POOL_SETRANGE_INLINE
#endif
#endif  // HWY_POOL_SETRANGE_INLINE

namespace hwy {

// Generates a random permutation of [0, size). O(1) storage.
class ShuffledIota {
 public:
  ShuffledIota() : coprime_(1) {}  // for PoolWorker
  explicit ShuffledIota(uint32_t coprime) : coprime_(coprime) {}

  // Returns the next after `current`, using an LCG-like generator.
  uint32_t Next(uint32_t current, const Divisor& divisor) const {
    HWY_DASSERT(current < divisor.GetDivisor());
    // (coprime * i + current) % size, see https://lemire.me/blog/2017/09/18/.
    return divisor.Remainder(current + coprime_);
  }

  // Returns true if a and b have no common denominator except 1. Based on
  // binary GCD. Assumes a and b are nonzero. Also used in tests.
  static bool CoprimeNonzero(uint32_t a, uint32_t b) {
    const size_t trailing_a = Num0BitsBelowLS1Bit_Nonzero32(a);
    const size_t trailing_b = Num0BitsBelowLS1Bit_Nonzero32(b);
    // If both have at least one trailing zero, they are both divisible by 2.
    if (HWY_MIN(trailing_a, trailing_b) != 0) return false;

    // If one of them has a trailing zero, shift it out.
    a >>= trailing_a;
    b >>= trailing_b;

    for (;;) {
      // Swap such that a >= b.
      const uint32_t tmp_a = a;
      a = HWY_MAX(tmp_a, b);
      b = HWY_MIN(tmp_a, b);

      // When the smaller number is 1, they were coprime.
      if (b == 1) return true;

      a -= b;
      // a == b means there was a common factor, so not coprime.
      if (a == 0) return false;
      a >>= Num0BitsBelowLS1Bit_Nonzero32(a);
    }
  }

  // Returns another coprime >= `start`, or 1 for small `size`.
  // Used to seed independent ShuffledIota instances.
  static uint32_t FindAnotherCoprime(uint32_t size, uint32_t start) {
    if (size <= 2) {
      return 1;
    }

    // Avoids even x for even sizes, which are sure to be rejected.
    const uint32_t inc = (size & 1) ? 1 : 2;

    for (uint32_t x = start | 1; x < start + size * 16; x += inc) {
      if (CoprimeNonzero(x, static_cast<uint32_t>(size))) {
        return x;
      }
    }

    HWY_ABORT("unreachable");
  }

  uint32_t coprime_;
};

// We want predictable struct/class sizes so we can reason about cache lines.
#pragma pack(push, 1)

enum class PoolWaitMode : uint32_t { kBlock, kSpin };

// Worker's private working set.
class PoolWorker {  // HWY_ALIGNMENT bytes
  static constexpr size_t kMaxVictims = 4;

 public:
  PoolWorker(size_t thread, size_t num_workers) {
    wait_mode_ = PoolWaitMode::kBlock;
    num_victims_ = static_cast<uint32_t>(HWY_MIN(kMaxVictims, num_workers));

    const Divisor div_workers(static_cast<uint32_t>(num_workers));

    // Increase gap between coprimes to reduce collisions.
    const uint32_t coprime = ShuffledIota::FindAnotherCoprime(
        static_cast<uint32_t>(num_workers),
        static_cast<uint32_t>((thread + 1) * 257 + thread * 13));
    const ShuffledIota shuffled_iota(coprime);

    // To simplify WorkerRun, our own thread is the first to 'steal' from.
    victims_[0] = static_cast<uint32_t>(thread);
    for (uint32_t i = 1; i < num_victims_; ++i) {
      victims_[i] = shuffled_iota.Next(victims_[i - 1], div_workers);
      HWY_DASSERT(victims_[i] != thread);
    }

    (void)padding_;
  }
  ~PoolWorker() = default;

  void SetWaitMode(PoolWaitMode wait_mode) {
    wait_mode_.store(wait_mode, std::memory_order_release);
  }
  PoolWaitMode WorkerGetWaitMode() const {
    return wait_mode_.load(std::memory_order_acquire);
  }

  hwy::Span<const uint32_t> Victims() const {
    return hwy::Span<const uint32_t>(victims_.data(),
                                     static_cast<size_t>(num_victims_));
  }

  // Called from main thread in Plan().
  HWY_POOL_SETRANGE_INLINE void SetRange(uint64_t begin, uint64_t end) {
    const auto rel = std::memory_order_release;
    begin_.store(begin, rel);
    end_.store(end, rel);
  }

  // Returns the STL-style end of this worker's assigned range.
  uint64_t WorkerGetEnd() const { return end_.load(std::memory_order_acquire); }

  // Returns the next task to execute. If >= WorkerGetEnd(), it must be skipped.
  uint64_t WorkerReserveTask() {
    return begin_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t> begin_;
  std::atomic<uint64_t> end_;  // only changes during SetRange

  std::atomic<PoolWaitMode> wait_mode_;  // (32-bit)
  uint32_t num_victims_;                 // <= kPoolMaxVictims
  std::array<uint32_t, kMaxVictims> victims_;

  uint8_t padding_[HWY_ALIGNMENT - 16 - 8 - sizeof(victims_)];
};
static_assert(sizeof(PoolWorker) == HWY_ALIGNMENT, "");

// Modified by main thread, shared with all workers.
class PoolTasks {  // 32 bytes
  // Signature of the (internal) function called from workers(s) for each
  // `task` in the [`begin`, `end`) passed to Run(). Closures (lambdas) do not
  // receive the first argument, which points to the lambda object.
  typedef void (*RunFunc)(const void* opaque, uint64_t task, size_t thread_id);

  // Calls closure(task, thread). Signature must match RunFunc.
  template <class Closure>
  static void CallClosure(const void* opaque, uint64_t task, size_t thread) {
    (*reinterpret_cast<const Closure*>(opaque))(task, thread);
  }

 public:
  // Called from main thread in Plan().
  template <class Closure>
  void Store(const Closure& closure, uint64_t begin, uint64_t end) {
    const auto rel = std::memory_order_release;
    func_.store(static_cast<RunFunc>(&CallClosure<Closure>), rel);
    opaque_.store(reinterpret_cast<const void*>(&closure), rel);
    begin_.store(begin, rel);
    end_.store(end, rel);
  }

  RunFunc WorkerGet(uint64_t& begin, uint64_t& end, const void*& opaque) const {
    const auto acq = std::memory_order_acquire;
    begin = begin_.load(acq);
    end = end_.load(acq);
    opaque = opaque_.load(acq);
    return func_.load(acq);
  }

 private:
  std::atomic<RunFunc> func_;
  std::atomic<const void*> opaque_;
  std::atomic<uint64_t> begin_;
  std::atomic<uint64_t> end_;
};

// Modified by main thread, shared with all workers.
class PoolCommands {  // 16 bytes
  static constexpr uint32_t kInitial = 0;
  static constexpr uint32_t kMask = 0xF;  // for command, rest is ABA counter.
  static constexpr size_t kShift = hwy::CeilLog2(kMask);

 public:
  static constexpr uint32_t kTerminate = 1;
  static constexpr uint32_t kWork = 2;
  static constexpr uint32_t kNop = 3;

  // Workers must initialize their copy to this so that they wait for the first
  // command as intended.
  static uint32_t WorkerInitialSeqCmd() { return kInitial; }

  // Sends `cmd` to all workers.
  void Broadcast(uint32_t cmd) {
    HWY_DASSERT(cmd <= kMask);
    const uint32_t epoch = ++epoch_;
    const uint32_t seq_cmd = (epoch << kShift) | cmd;
    seq_cmd_.store(seq_cmd, std::memory_order_release);

    // Wake any worker whose wait_mode_ is or was kBlock.
    WakeAll(seq_cmd_);

    // Workers are either starting up, or waiting for a command. Either way,
    // they will not miss this command, so no need to wait for them here.
  }

  // Returns the command, i.e., one of the public constants, e.g., kTerminate.
  uint32_t WorkerWaitForNewCommand(PoolWaitMode wait_mode,
                                   uint32_t& prev_seq_cmd) {
    uint32_t seq_cmd;
    if (HWY_LIKELY(wait_mode == PoolWaitMode::kSpin)) {
      seq_cmd = SpinUntilDifferent(prev_seq_cmd, seq_cmd_);
    } else {
      seq_cmd = BlockUntilDifferent(prev_seq_cmd, seq_cmd_);
    }
    prev_seq_cmd = seq_cmd;
    return seq_cmd & kMask;
  }

 private:
  static HWY_INLINE uint32_t SpinUntilDifferent(
      const uint32_t prev_seq_cmd, std::atomic<uint32_t>& current) {
    for (;;) {
      hwy::Pause();
      const uint32_t seq_cmd = current.load(std::memory_order_acquire);
      if (seq_cmd != prev_seq_cmd) return seq_cmd;
    }
  }

  // Counter for ABA-proofing WorkerWaitForNewCommand. Stored next to seq_cmd_
  // because both are written at the same time by the main thread. Sharding this
  // 4x (one per cache line) is not helpful.
  uint32_t epoch_{0};
  std::atomic<uint32_t> seq_cmd_{kInitial};
};

// Modified by main thread AND workers.
// TODO(janwas): more scalable tree
class alignas(HWY_ALIGNMENT) PoolBarrier {  // 4 * HWY_ALIGNMENT bytes
  static constexpr size_t kU64PerCacheLine = HWY_ALIGNMENT / sizeof(uint64_t);

 public:
  void Reset() {
    for (size_t i = 0; i < 4; ++i) {
      num_finished_[i * kU64PerCacheLine].store(0, std::memory_order_release);
    }
  }

  void WorkerArrive(size_t thread) {
    const size_t i = (thread & 3);
    num_finished_[i * kU64PerCacheLine].fetch_add(1, std::memory_order_release);
  }

  // Spin until all have called Arrive(). Note that workers spin for a new
  // command, not the barrier itself.
  HWY_POOL_INLINE void WaitAll(size_t num_workers) {
    const auto acq = std::memory_order_acquire;
    for (;;) {
      hwy::Pause();
      const uint64_t sum = num_finished_[0 * kU64PerCacheLine].load(acq) +
                           num_finished_[1 * kU64PerCacheLine].load(acq) +
                           num_finished_[2 * kU64PerCacheLine].load(acq) +
                           num_finished_[3 * kU64PerCacheLine].load(acq);
      if (sum == num_workers) break;
    }
  }

 private:
  // Sharded to reduce contention. Four counters, each in their own cache line.
  std::atomic<uint64_t> num_finished_[4 * kU64PerCacheLine];
};

// All mutable pool and worker state.
struct alignas(HWY_ALIGNMENT) PoolMem {
  PoolWorker& Worker(size_t thread) {
    return *reinterpret_cast<PoolWorker*>(reinterpret_cast<uint8_t*>(&barrier) +
                                          sizeof(barrier) +
                                          thread * sizeof(PoolWorker));
  }

  PoolTasks tasks;
  PoolCommands commands;
  // barrier is more write-heavy, hence keep in another cache line.
  uint8_t padding[HWY_ALIGNMENT - sizeof(tasks) - sizeof(commands)];

  PoolBarrier barrier;
  static_assert(sizeof(barrier) % HWY_ALIGNMENT == 0, "");

  // Followed by `num_workers` PoolWorker.
};

// Aligned allocation and initialization of variable-length PoolMem.
class PoolMemOwner {
 public:
  explicit PoolMemOwner(size_t num_threads)
      // The main thread also participates.
      : num_workers_(num_threads + 1) {
    const size_t size = sizeof(PoolMem) + num_workers_ * sizeof(PoolWorker);
    bytes_ = hwy::AllocateAligned<uint8_t>(size);
    HWY_ASSERT(bytes_);
    mem_ = new (bytes_.get()) PoolMem();

    for (size_t thread = 0; thread < num_workers_; ++thread) {
      new (&mem_->Worker(thread)) PoolWorker(thread, num_workers_);
    }

    // Publish non-atomic stores in mem_ - that is the only shared state workers
    // access before they call WorkerWaitForNewCommand.
    std::atomic_thread_fence(std::memory_order_release);
  }

  ~PoolMemOwner() {
    for (size_t thread = 0; thread < num_workers_; ++thread) {
      mem_->Worker(thread).~PoolWorker();
    }
    mem_->~PoolMem();
  }

  size_t NumWorkers() const { return num_workers_; }

  PoolMem* Mem() const { return mem_; }

 private:
  const size_t num_workers_;  // >= 1
  // Aligned allocation ensures we do not straddle cache lines.
  hwy::AlignedFreeUniquePtr<uint8_t[]> bytes_;
  PoolMem* mem_;
};

// Plans and executes parallel-for loops with work-stealing. No synchronization
// because there is no mutable shared state.
class ParallelFor {  // 0 bytes
  // A prior version of this code attempted to assign only as much work as a
  // thread will actually use. As with OpenMP's 'guided' strategy, we assigned
  // remaining/(k*num_threads) in each iteration. Although the worst-case
  // imbalance is bounded, this required several rounds of work allocation, and
  // the atomic counter did not scale to > 30 threads.
  //
  // We now use work stealing instead, where already-finished threads look for
  // and perform work from others, as if they were that thread. This deals with
  // imbalances as they arise, but care is required to reduce contention. We
  // randomize the order in which threads choose victims to steal from.
  //
  // Results: across 10K calls Run(), we observe a mean of 5.1 tasks per
  // thread, and standard deviation 0.67, indicating good load-balance.

 public:
  // Make preparations for workers to later run `closure(i)` for all `i` in
  // `[begin, end)`. Called from the main thread; workers are initializing or
  // spinning for a command. Returns false if there are no tasks or workers.
  template <class Closure>
  static bool Plan(uint64_t begin, uint64_t end, size_t num_workers,
                   const Closure& closure, PoolMem& mem) {
    // If there are no tasks, we are done.
    HWY_DASSERT(begin <= end);
    const size_t num_tasks = static_cast<size_t>(end - begin);
    if (HWY_UNLIKELY(num_tasks == 0)) return false;

    // If there are no workers, run all tasks already on the main thread without
    // the overhead of planning.
    if (HWY_UNLIKELY(num_workers <= 1)) {
      for (uint64_t task = begin; task < end; ++task) {
        closure(task, /*thread=*/0);
      }
      return false;
    }

    // Store for later retrieval by all workers in WorkerRun. Must happen after
    // the loop above because it may be re-entered by concurrent threads.
    mem.tasks.Store(closure, begin, end);

    // Assigning all remainders to the last thread causes imbalance. We instead
    // give one more to each thread whose index is less.
    const size_t remainder = num_tasks % num_workers;
    const size_t min_tasks = num_tasks / num_workers;

    uint64_t task = begin;
    for (size_t thread = 0; thread < num_workers; ++thread) {
      const uint64_t my_end = task + min_tasks + (thread < remainder);
      mem.Worker(thread).SetRange(task, my_end);
      task = my_end;
    }
    HWY_DASSERT(task == end);
    return true;
  }

  // Must be called for each `thread` in [0, num_workers), but only if
  // Plan returned true.
  static HWY_POOL_INLINE void WorkerRun(const size_t thread, size_t num_workers,
                                        PoolMem& mem) {
    // Nonzero, otherwise Plan returned false and this should not be called.
    HWY_DASSERT(num_workers != 0);
    HWY_DASSERT(thread < num_workers);

    const PoolTasks& tasks = mem.tasks;

    uint64_t begin, end;
    const void* opaque;
    const auto func = tasks.WorkerGet(begin, end, opaque);

    // Special case for <= 1 task per worker - avoid any shared state.
    if (HWY_UNLIKELY(end <= begin + num_workers)) {
      const uint64_t task = begin + thread;
      if (HWY_LIKELY(task < end)) {
        func(opaque, task, thread);
      }
      return;
    }

    // For each worker in random order, attempt to do all their work.
    for (uint32_t victim : mem.Worker(thread).Victims()) {
      PoolWorker* other_worker = &mem.Worker(victim);

      // Until all of other_worker's work is done:
      const uint64_t other_end = other_worker->WorkerGetEnd();
      for (;;) {
        // On x86 this generates a LOCK prefix, but that is only expensive if
        // there is actually contention, which is unlikely because we shard the
        // counters, threads do not quite proceed in lockstep due to memory
        // traffic, and stealing happens in semi-random order.
        uint64_t task = other_worker->WorkerReserveTask();

        // The worker that first sets `task` to `other_end` exits this loop.
        // After that, `task` can be incremented up to `num_workers - 1` times,
        // once per other worker.
        HWY_DASSERT(task < other_end + num_workers);

        if (HWY_UNLIKELY(task >= other_end)) {
          hwy::Pause();  // Reduce coherency traffic while stealing.
          break;
        }
        // `thread` is the one we are actually running on; this is important
        // because it is the TLS index for user code.
        func(opaque, task, thread);
      }
    }
  }
};

#pragma pack(pop)

// Sets the name of the current thread to the format string `format`, which must
// include %d for `thread`. Currently only implemented for pthreads (*nix and
// OSX); Windows involves throwing an exception.
static inline void SetThreadName(const char* format, int thread) {
#if HWY_OS_LINUX
  char buf[16] = {};  // Linux limit, including \0
  const int chars_written = snprintf(buf, sizeof(buf), format, thread);
  HWY_ASSERT(0 < chars_written &&
             chars_written <= static_cast<int>(sizeof(buf) - 1));
  HWY_ASSERT(0 == pthread_setname_np(pthread_self(), buf));
#else
  (void)format;
  (void)thread;
#endif
}

// Highly efficient parallel-for, intended for workloads with thousands of
// fork-join regions which consist of calling tasks[t](i) for a few hundred i,
// using dozens of threads.
//
// To reduce scheduling overhead, we assume that tasks are statically known and
// that threads do not schedule new work themselves. This allows us to avoid
// queues and only store a counter plus the current task. The latter is a
// pointer to a lambda function, without the allocation/indirection required for
// std::function.
//
// To reduce fork/join latency, we use an efficient barrier, optionally
// support spin-waits via SetWaitMode, and avoid any mutex/lock.
//
// To eliminate false sharing and enable reasoning about cache line traffic, the
// worker state uses a single aligned allocation.
//
// For load-balancing, we use work stealing in random order.
class ThreadPool {
  static void ThreadFunc(size_t thread, size_t num_workers, PoolMem* mem) {
    HWY_DASSERT(thread < num_workers);
    SetThreadName("worker%03zu", static_cast<int>(thread));

    // Ensure mem is ready to use (synchronize with PoolMemOwner's fence).
    std::atomic_thread_fence(std::memory_order_acquire);

    PoolWorker& worker = mem->Worker(thread);
    PoolCommands& commands = mem->commands;
    uint32_t prev_seq_cmd = PoolCommands::WorkerInitialSeqCmd();

    for (;;) {
      const PoolWaitMode wait_mode = worker.WorkerGetWaitMode();
      const uint32_t command =
          commands.WorkerWaitForNewCommand(wait_mode, prev_seq_cmd);
      if (HWY_UNLIKELY(command == PoolCommands::kTerminate)) {
        return;  // exits thread
      } else if (HWY_LIKELY(command == PoolCommands::kWork)) {
        ParallelFor::WorkerRun(thread, num_workers, *mem);
        mem->barrier.WorkerArrive(thread);
      } else if (command == PoolCommands::kNop) {
        // do nothing - used to change wait mode
      } else {
        HWY_DASSERT(false);  // unknown command
      }
    }
  }

 public:
  // This typically includes hyperthreads, hence it is a loose upper bound.
  // -1 because these are in addition to the main thread.
  static size_t MaxThreads() {
    LogicalProcessorSet lps;
    // This is OS dependent, but more accurate if available because it takes
    // into account restrictions set by cgroups or numactl/taskset.
    if (GetThreadAffinity(lps)) {
      return lps.Count() - 1;
    }
    return static_cast<size_t>(std::thread::hardware_concurrency() - 1);
  }

  // `num_threads` should not exceed `MaxThreads()`. If `num_threads` <= 1,
  // Run() runs only on the main thread. Otherwise, we launch `num_threads - 1`
  // threads because the main thread also participates.
  explicit ThreadPool(size_t num_threads) : owner_(num_threads) {
    (void)busy_;  // unused in non-debug builds, avoid warning
    const size_t num_workers = owner_.NumWorkers();

    // Launch threads without waiting afterwards: they will receive the next
    // PoolCommands once ready.
    threads_.reserve(num_workers - 1);
    for (size_t thread = 0; thread < num_workers - 1; ++thread) {
      threads_.emplace_back(ThreadFunc, thread, num_workers, owner_.Mem());
    }
  }

  // Waits for all threads to exit.
  ~ThreadPool() {
    PoolMem& mem = *owner_.Mem();
    mem.commands.Broadcast(PoolCommands::kTerminate);  // requests threads exit

    for (std::thread& thread : threads_) {
      HWY_ASSERT(thread.joinable());
      thread.join();
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator&(const ThreadPool&) = delete;

  // Returns number of PoolWorker, i.e., one more than the largest `thread`
  // argument. Useful for callers that want to allocate thread-local storage.
  size_t NumWorkers() const { return owner_.NumWorkers(); }

  // `mode` is initially `kBlock`, which means futex. Switching to `kSpin`
  // reduces fork-join overhead especially when there are many calls to `Run`,
  // but wastes power when waiting over long intervals. Inexpensive, OK to call
  // multiple times, but not concurrently with any `Run`.
  void SetWaitMode(PoolWaitMode mode) {
    // Run must not be active, otherwise we may overwrite the previous command
    // before it is seen by all workers.
    HWY_DASSERT(busy_.fetch_add(1) == 0);

    PoolMem& mem = *owner_.Mem();

    // For completeness/consistency, set on all workers, including the main
    // thread, even though it will never wait for a command.
    for (size_t thread = 0; thread < owner_.NumWorkers(); ++thread) {
      mem.Worker(thread).SetWaitMode(mode);
    }

    // Send a no-op command so that workers wake as soon as possible. Skip the
    // expensive barrier - workers may miss this command, but it is fine for
    // them to wake up later and get the next actual command.
    mem.commands.Broadcast(PoolCommands::kNop);

    HWY_DASSERT(busy_.fetch_add(-1) == 1);
  }

  // parallel-for: Runs `closure(task, thread)` on worker thread(s) for every
  // `task` in `[begin, end)`. Note that the unit of work should be large
  // enough to amortize the function call overhead, but small enough that each
  // worker processes a few tasks. Thus each `task` is usually a loop.
  //
  // Not thread-safe - concurrent calls to `Run` in the same ThreadPool are
  // forbidden unless NumWorkers() == 0. We check for that in debug builds.
  template <class Closure>
  void Run(uint64_t begin, uint64_t end, const Closure& closure) {
    const size_t num_workers = NumWorkers();
    PoolMem& mem = *owner_.Mem();

    if (HWY_LIKELY(ParallelFor::Plan(begin, end, num_workers, closure, mem))) {
      // Only check if we are going to fork/join.
      HWY_DASSERT(busy_.fetch_add(1) == 0);

      mem.barrier.Reset();
      mem.commands.Broadcast(PoolCommands::kWork);

      // Also perform work on main thread instead of busy-waiting.
      const size_t thread = num_workers - 1;
      ParallelFor::WorkerRun(thread, num_workers, mem);
      mem.barrier.WorkerArrive(thread);

      mem.barrier.WaitAll(num_workers);

      HWY_DASSERT(busy_.fetch_add(-1) == 1);
    }
  }

  // Can pass this as init_closure when no initialization is needed.
  // DEPRECATED, better to call the Run() overload without the init_closure arg.
  static bool NoInit(size_t /*num_threads*/) { return true; }  // DEPRECATED

  // DEPRECATED equivalent of NumWorkers. Note that this is not the same as the
  // ctor argument because num_threads = 0 has the same effect as 1.
  size_t NumThreads() const { return NumWorkers(); }  // DEPRECATED

  // DEPRECATED prior interface with 32-bit tasks and first calling
  // `init_closure(num_threads)`. Instead, perform any init before this, calling
  // NumWorkers() for an upper bound on the thread indices, then call the
  // other overload.
  template <class InitClosure, class RunClosure>
  bool Run(uint64_t begin, uint64_t end, const InitClosure& init_closure,
           const RunClosure& run_closure) {
    if (!init_closure(NumThreads())) return false;
    Run(begin, end, run_closure);
    return true;
  }

  // Only for use in tests.
  PoolMem& InternalMem() const { return *owner_.Mem(); }

 private:
  // Unmodified after ctor, but cannot be const because we call thread::join().
  std::vector<std::thread> threads_;

  PoolMemOwner owner_;

  // In debug builds, detects if functions are re-entered; always present so
  // that the memory layout does not change.
  std::atomic<int> busy_{0};
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_THREAD_POOL_THREAD_POOL_H_
