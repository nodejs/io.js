// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_GARBAGE_COLLECTOR_H_
#define V8_HEAP_CPPGC_GARBAGE_COLLECTOR_H_

#include <optional>

#include "include/cppgc/common.h"
#include "src/heap/cppgc/heap-config.h"

namespace cppgc {
namespace internal {

// GC interface that allows abstraction over the actual GC invocation. This is
// needed to mock/fake GC for testing.
class GarbageCollector {
 public:
  // Executes a garbage collection specified in config.
  virtual void CollectGarbage(GCConfig) = 0;
  virtual void StartIncrementalGarbageCollection(GCConfig) = 0;

  // The current epoch that the GC maintains. The epoch is increased on every
  // GC invocation.
  virtual size_t epoch() const = 0;

  // Returns if the stack state is overridden.
  virtual std::optional<EmbedderStackState> overridden_stack_state() const = 0;

  // These virtual methods are also present in class HeapBase.
  virtual void set_override_stack_state(EmbedderStackState state) = 0;
  virtual void clear_overridden_stack_state() = 0;

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  virtual std::optional<int> UpdateAllocationTimeout() = 0;
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_GARBAGE_COLLECTOR_H_
