// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_
#define V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_

#include <utility>
#include <vector>

#include "include/cppgc/prefinalizer.h"

namespace cppgc {
namespace internal {

class HeapBase;

struct PreFinalizer final {
  using Callback = PrefinalizerRegistration::Callback;

  void* object;
  Callback callback;

  bool operator==(const PreFinalizer& other) const;
};

class PreFinalizerHandler final {
 public:
  explicit PreFinalizerHandler(HeapBase& heap);

  void RegisterPrefinalizer(PreFinalizer pre_finalizer);

  void InvokePreFinalizers();

  bool IsInvokingPreFinalizers() const { return is_invoking_; }

  void NotifyAllocationInPrefinalizer(size_t);
  size_t ExtractBytesAllocatedInPrefinalizers() {
    return std::exchange(bytes_allocated_in_prefinalizers, 0);
  }

 private:
  // Checks that the current thread is the thread that created the heap.
  bool CurrentThreadIsCreationThread();

  // Pre-finalizers are called in the reverse order in which they are
  // registered by the constructors (including constructors of Mixin
  // objects) for an object, by processing the ordered_pre_finalizers_
  // back-to-front.
  std::vector<PreFinalizer> ordered_pre_finalizers_;
  std::vector<PreFinalizer>* current_ordered_pre_finalizers_;

  HeapBase& heap_;
  bool is_invoking_ = false;

  // Counter of bytes allocated during prefinalizers.
  size_t bytes_allocated_in_prefinalizers = 0u;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_PREFINALIZER_HANDLER_H_
