// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/platform.h"

#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/platform.h"
#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/lsan-page-allocator.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/platform.h"

#if defined(CPPGC_CAGED_HEAP)
#include "src/heap/cppgc/caged-heap.h"
#endif  // defined(CPPGC_CAGED_HEAP)

namespace cppgc {
namespace internal {

namespace {

PageAllocator* g_page_allocator = nullptr;

PageAllocator& CreateAllocatorIfNeeded(PageAllocator* page_allocator) {
  if (!page_allocator) {
    static v8::base::LeakyObject<v8::base::PageAllocator>
        default_page_allocator;
    page_allocator = default_page_allocator.get();
  }
#if defined(LEAK_SANITIZER)
  // If lsan is enabled, override the given allocator with the custom lsan
  // allocator.
  static v8::base::LeakyObject<v8::base::LsanPageAllocator> lsan_page_allocator(
      page_allocator);
  page_allocator = lsan_page_allocator.get();
#endif  // LEAK_SANITIZER
  return *page_allocator;
}

}  // namespace

void Fatal(const std::string& reason, const SourceLocation& loc) {
#ifdef DEBUG
  V8_Fatal(loc.FileName(), static_cast<int>(loc.Line()), "%s", reason.c_str());
#else   // !DEBUG
  V8_Fatal("%s", reason.c_str());
#endif  // !DEBUG
}

void FatalOutOfMemoryHandler::operator()(const std::string& reason,
                                         const SourceLocation& loc) const {
  if (custom_handler_) {
    (*custom_handler_)(reason, loc, heap_);
    FATAL("Custom out of memory handler should not have returned");
  }
#ifdef DEBUG
  V8_Fatal(loc.FileName(), static_cast<int>(loc.Line()),
           "Oilpan: Out of memory (%s)", reason.c_str());
#else   // !DEBUG
  V8_Fatal("Oilpan: Out of memory");
#endif  // !DEBUG
}

void FatalOutOfMemoryHandler::SetCustomHandler(Callback* callback) {
  custom_handler_ = callback;
}

FatalOutOfMemoryHandler& GetGlobalOOMHandler() {
  static FatalOutOfMemoryHandler oom_handler;
  return oom_handler;
}

PageAllocator& GetGlobalPageAllocator() {
  CHECK_NOT_NULL(g_page_allocator);
  return *g_page_allocator;
}

}  // namespace internal

TracingController* Platform::GetTracingController() {
  static v8::base::LeakyObject<TracingController> tracing_controller;
  return tracing_controller.get();
}

void InitializeProcess(PageAllocator* page_allocator,
                       size_t desired_heap_size) {
#if defined(V8_USE_ADDRESS_SANITIZER) && defined(V8_HOST_ARCH_64_BIT)
  // Retrieve asan's internal shadow memory granularity and check that Oilpan's
  // object alignment/sizes are multiple of this granularity. This is needed to
  // perform poisoness checks.
  size_t shadow_scale;
  __asan_get_shadow_mapping(&shadow_scale, nullptr);
  DCHECK(shadow_scale);
  const size_t poisoning_granularity = 1 << shadow_scale;
  CHECK_EQ(0u, internal::kAllocationGranularity % poisoning_granularity);
#endif

  auto& allocator = internal::CreateAllocatorIfNeeded(page_allocator);

  CHECK(!internal::g_page_allocator);
  internal::GlobalGCInfoTable::Initialize(allocator);
#if defined(CPPGC_CAGED_HEAP)
  internal::CagedHeap::InitializeIfNeeded(allocator, desired_heap_size);
#endif  // defined(CPPGC_CAGED_HEAP)
  internal::g_page_allocator = &allocator;
}

void ShutdownProcess() { internal::g_page_allocator = nullptr; }

}  // namespace cppgc
