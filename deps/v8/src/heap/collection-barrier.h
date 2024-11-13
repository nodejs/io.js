// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_COLLECTION_BARRIER_H_
#define V8_HEAP_COLLECTION_BARRIER_H_

#include <atomic>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/platform/mutex.h"
#include "src/heap/local-heap.h"
#include "src/logging/counters.h"

namespace v8 {
namespace internal {

class Heap;

// This class stops and resumes all background threads waiting for GC.
class CollectionBarrier {
 public:
  CollectionBarrier(
      Heap* heap, std::shared_ptr<v8::TaskRunner> foreground_task_runner);

  // Returns true when collection was requested.
  bool WasGCRequested();

  // Requests a GC from the main thread. Returns whether GC was successfully
  // requested. Requesting a GC can fail when isolate shutdown was already
  // initiated.
  bool TryRequestGC();

  // Resumes all threads waiting for GC when tear down starts.
  void NotifyShutdownRequested();

  // Stops the TimeToCollection timer when starting the GC.
  void StopTimeToCollectionTimer();

  // Resumes threads waiting for collection.
  void ResumeThreadsAwaitingCollection();

  // Cancels collection if one was requested and resumes threads waiting for GC.
  void CancelCollectionAndResumeThreads();

  // This is the method use by background threads to request and wait for GC.
  // Returns whether a GC was performed.
  bool AwaitCollectionBackground(LocalHeap* local_heap);

 private:
  Heap* heap_;
  base::Mutex mutex_;
  base::ConditionVariable cv_wakeup_;
  base::ElapsedTimer timer_;

  // Flag that main thread checks whether a GC was requested from the background
  // thread.
  std::atomic<bool> collection_requested_{false};

  // This flag is used to detect whether to block for the GC. Only set if the
  // main thread was actually running and is unset when GC resumes background
  // threads.
  bool block_for_collection_ = false;

  // Set to true when a GC was performed, false in case it was canceled because
  // the main thread parked itself without running the GC.
  bool collection_performed_ = false;

  // Will be set as soon as Isolate starts tear down.
  bool shutdown_requested_ = false;

  // Used to post tasks on the main thread.
  std::shared_ptr<v8::TaskRunner> foreground_task_runner_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_COLLECTION_BARRIER_H_
