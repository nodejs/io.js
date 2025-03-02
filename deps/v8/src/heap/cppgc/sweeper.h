// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_SWEEPER_H_
#define V8_HEAP_CPPGC_SWEEPER_H_

#include <memory>

#include "src/base/macros.h"
#include "src/base/platform/time.h"
#include "src/heap/cppgc/heap-config.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc::internal {

class HeapBase;
class ConcurrentSweeperTest;
class BaseSpace;

class V8_EXPORT_PRIVATE Sweeper final {
 public:
  class V8_EXPORT_PRIVATE SweepingOnMutatorThreadObserver {
   public:
    explicit SweepingOnMutatorThreadObserver(Sweeper&);
    virtual ~SweepingOnMutatorThreadObserver();

    virtual void Start() = 0;
    virtual void End() = 0;

   private:
    Sweeper& sweeper_;
  };

  static constexpr bool CanDiscardMemory() {
#if defined(V8_OS_WIN)
    // Discarding memory on Windows does not decommit the memory and does not
    // contribute to reduce the memory footprint. On the other hand, these
    // calls become expensive the more memory is allocated in the system and
    // can result in hangs. Thus, it is better to not discard on Windows.
    return false;
#else
    return CheckMemoryIsInaccessibleIsNoop();
#endif
  }

  explicit Sweeper(HeapBase&);
  ~Sweeper();

  Sweeper(const Sweeper&) = delete;
  Sweeper& operator=(const Sweeper&) = delete;

  // Starts sweeping. Assumes that the heap holds no linear allocation buffers.
  // Will not finish sweeping in case SweepingConfig::sweeping_type is
  // SweepingType::kAtomic but rely on the caller to finish sweeping
  // immediately.
  void Start(SweepingConfig);
  // Returns true when sweeping was finished and false if it was not running or
  // couldn't be finished due to being a recursive sweep call.
  bool FinishIfRunning();
  void FinishIfOutOfWork();
  // SweepForAllocationIfRunning sweeps the given `space` until a slot that can
  // fit an allocation of `min_wanted_size` bytes is found. Returns true if a
  // slot was found. Aborts after `max_duration`.
  bool SweepForAllocationIfRunning(BaseSpace* space, size_t min_wanted_size,
                                   v8::base::TimeDelta max_duration);

  bool IsSweepingOnMutatorThread() const;
  bool IsSweepingInProgress() const;

  // Assist with sweeping. Returns true if sweeping is done.
  bool PerformSweepOnMutatorThread(v8::base::TimeDelta max_duration,
                                   StatsCollector::ScopeId);

 private:
  void WaitForConcurrentSweepingForTesting();

  class SweeperImpl;

  HeapBase& heap_;
  std::unique_ptr<SweeperImpl> impl_;

  friend class ConcurrentSweeperTest;
};

}  // namespace cppgc::internal

#endif  // V8_HEAP_CPPGC_SWEEPER_H_
