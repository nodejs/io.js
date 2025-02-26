// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/test-utils.h"

#include "include/libplatform/libplatform.h"
#include "include/v8-isolate.h"
#include "src/api/api-inl.h"
#include "src/base/platform/time.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8 {

namespace {
// counter_lookup_callback doesn't pass through any state information about
// the current Isolate, so we have to store the current counter map somewhere.
// Fortunately tests run serially, so we can just store it in a static global.
CounterMap* kCurrentCounterMap = nullptr;
}  // namespace

std::unique_ptr<CppHeap> IsolateWrapper::cpp_heap_;

IsolateWrapper::IsolateWrapper(CountersMode counters_mode,
                               bool use_statically_set_cpp_heap)
    : array_buffer_allocator_(
          v8::ArrayBuffer::Allocator::NewDefaultAllocator()) {
  CHECK_NULL(kCurrentCounterMap);

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = array_buffer_allocator_.get();
  if (use_statically_set_cpp_heap) {
    create_params.cpp_heap = cpp_heap_.release();
  }

  if (counters_mode == kEnableCounters) {
    counter_map_ = std::make_unique<CounterMap>();
    kCurrentCounterMap = counter_map_.get();

    create_params.counter_lookup_callback = [](const char* name) {
      CHECK_NOT_NULL(kCurrentCounterMap);
      // If the name doesn't exist in the counter map, operator[] will default
      // initialize it to zero.
      return &(*kCurrentCounterMap)[name];
    };
  } else {
    create_params.counter_lookup_callback = [](const char* name) -> int* {
      return nullptr;
    };
  }

  isolate_ = v8::Isolate::New(create_params);
  CHECK_NOT_NULL(isolate());
}

IsolateWrapper::~IsolateWrapper() {
  v8::Platform* platform = internal::V8::GetCurrentPlatform();
  CHECK_NOT_NULL(platform);
  isolate_->Enter();
  while (platform::PumpMessageLoop(platform, isolate())) continue;
  isolate_->Exit();
  isolate_->Dispose();
  if (counter_map_) {
    CHECK_EQ(kCurrentCounterMap, counter_map_.get());
    kCurrentCounterMap = nullptr;
  } else {
    CHECK_NULL(kCurrentCounterMap);
  }
}

namespace internal {

SaveFlags::SaveFlags() {
  // For each flag, save the current flag value.
#define FLAG_MODE_APPLY(ftype, ctype, nam, def, cmt) \
  SAVED_##nam = v8_flags.nam.value();
#include "src/flags/flag-definitions.h"
#undef FLAG_MODE_APPLY
}

SaveFlags::~SaveFlags() {
  // For each flag, set back the old flag value if it changed (don't write the
  // flag if it didn't change, to keep TSAN happy).
#define FLAG_MODE_APPLY(ftype, ctype, nam, def, cmt) \
  if (SAVED_##nam != v8_flags.nam.value()) {         \
    v8_flags.nam = SAVED_##nam;                      \
  }
#include "src/flags/flag-definitions.h"  // NOLINT
#undef FLAG_MODE_APPLY
}

}  // namespace internal
}  // namespace v8
