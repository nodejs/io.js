// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_MAGLEV

#include "test/unittests/maglev/maglev-test.h"

#include "src/execution/isolate.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {
namespace maglev {

MaglevTest::MaglevTest()
    : TestWithNativeContextAndZone(kCompressGraphZone),
      broker_(isolate(), zone(), v8_flags.trace_heap_broker, CodeKind::MAGLEV),
      broker_scope_(&broker_, isolate(), zone()),
      current_broker_(&broker_) {
  if (!PersistentHandlesScope::IsActive(isolate())) {
    persistent_scope_.emplace(isolate());
  }
  broker()->SetTargetNativeContextRef(isolate()->native_context());
}

MaglevTest::~MaglevTest() {
  if (persistent_scope_) {
    persistent_scope_->Detach();
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV
