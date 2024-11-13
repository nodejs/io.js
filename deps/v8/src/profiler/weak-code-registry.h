// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_WEAK_CODE_REGISTRY_H_
#define V8_PROFILER_WEAK_CODE_REGISTRY_H_

#include <vector>

#include "src/execution/isolate.h"
#include "src/objects/objects.h"
#include "src/profiler/profile-generator.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE WeakCodeRegistry {
 public:
  struct Listener {
    virtual void OnHeapObjectDeletion(CodeEntry* entry) = 0;
  };

  explicit WeakCodeRegistry(Isolate* isolate) : isolate_(isolate) {}
  ~WeakCodeRegistry() { Clear(); }

  void Track(CodeEntry* entry, DirectHandle<AbstractCode> code);

  // Removes all dead code objects from the registry, invoking the provided
  // listener for each new CodeEntry that is no longer referenced on the heap
  // (if set).
  void Sweep(Listener* listener);

  // Removes all heap object tracking from stored CodeEntries.
  void Clear();

 private:
  Isolate* const isolate_;
  // Invariant: Entries will always be removed here before the
  // InstructionStreamMap is destroyed. CodeEntries should not be freed while
  // their heap objects exist.
  std::vector<CodeEntry*> entries_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_WEAK_CODE_REGISTRY_H_
