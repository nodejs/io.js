// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_MATERIALIZED_OBJECT_STORE_H_
#define V8_DEOPTIMIZER_MATERIALIZED_OBJECT_STORE_H_

#include <vector>

#include "src/handles/handles.h"

namespace v8 {
namespace internal {

class FixedArray;
class Isolate;

class MaterializedObjectStore {
 public:
  explicit MaterializedObjectStore(Isolate* isolate) : isolate_(isolate) {}

  Handle<FixedArray> Get(Address fp);
  void Set(Address fp, DirectHandle<FixedArray> materialized_objects);
  bool Remove(Address fp);

 private:
  Isolate* isolate() const { return isolate_; }
  Handle<FixedArray> GetStackEntries();
  Handle<FixedArray> EnsureStackEntries(int size);

  int StackIdToIndex(Address fp);

  Isolate* isolate_;
  std::vector<Address> frame_fps_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_MATERIALIZED_OBJECT_STORE_H_
