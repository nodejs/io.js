// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/block-instrumentation-reducer.h"

#include "src/handles/handles-inl.h"
#include "src/roots/roots-inl.h"

namespace v8::internal::compiler::turboshaft {

namespace detail {

Handle<HeapObject> CreateCountersArray(Isolate* isolate) {
  return Handle<HeapObject>::New(
      ReadOnlyRoots(isolate).basic_block_counters_marker(), isolate);
}

}  // namespace detail

}  // namespace v8::internal::compiler::turboshaft
