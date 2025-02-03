// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_LOCAL_ISOLATE_INL_H_
#define V8_EXECUTION_LOCAL_ISOLATE_INL_H_

#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

Address LocalIsolate::cage_base() const { return isolate_->cage_base(); }

Address LocalIsolate::code_cage_base() const {
  return isolate_->code_cage_base();
}

ReadOnlyHeap* LocalIsolate::read_only_heap() const {
  return isolate_->read_only_heap();
}

Tagged<Object> LocalIsolate::root(RootIndex index) const {
  DCHECK(RootsTable::IsImmortalImmovable(index));
  return isolate_->root(index);
}

Handle<Object> LocalIsolate::root_handle(RootIndex index) const {
  DCHECK(RootsTable::IsImmortalImmovable(index));
  return isolate_->root_handle(index);
}

template <typename Callback>
V8_INLINE void LocalIsolate::ExecuteMainThreadWhileParked(Callback callback) {
  heap_.ExecuteMainThreadWhileParked(callback);
}

template <typename Callback>
V8_INLINE void LocalIsolate::ParkIfOnBackgroundAndExecute(Callback callback) {
  if (is_main_thread()) {
    callback();
  } else {
    heap_.ExecuteBackgroundThreadWhileParked(callback);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_LOCAL_ISOLATE_INL_H_
