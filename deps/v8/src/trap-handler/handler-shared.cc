// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PLEASE READ BEFORE CHANGING THIS FILE!
//
// This file contains code that is used both inside and outside the out of
// bounds trap handler. Because this code runs in a trap handler context,
// use extra care when modifying this file. Here are some rules to follow.
//
// 1. Do not introduce any new external dependencies. This file needs
//    to be self contained so it is easy to audit everything that a
//    trap handler might do.
//
// 2. Any changes must be reviewed by someone from the crash reporting
//    or security team. See OWNERS for suggested reviewers.
//
// For more information, see https://goo.gl/yMeyUY.

#include "src/trap-handler/trap-handler-internal.h"

namespace v8 {
namespace internal {
namespace trap_handler {

// We declare this as int rather than bool as a workaround for a glibc bug, in
// which the dynamic loader cannot handle executables whose TLS area is only
// 1 byte in size; see https://sourceware.org/bugzilla/show_bug.cgi?id=14898.
thread_local int g_thread_in_wasm_code;

static_assert(sizeof(g_thread_in_wasm_code) > 1,
              "sizeof(thread_local_var) must be > 1, see "
              "https://sourceware.org/bugzilla/show_bug.cgi?id=14898");

size_t gNumCodeObjects = 0;
CodeProtectionInfoListEntry* gCodeObjects = nullptr;
std::atomic_size_t gRecoveredTrapCount = {0};
std::atomic<uintptr_t> gLandingPad = {0};

#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
std::atomic_flag MetadataLock::spinlock_ = ATOMIC_FLAG_INIT;
#else
std::atomic_flag MetadataLock::spinlock_;
#endif

MetadataLock::MetadataLock() {
  if (g_thread_in_wasm_code) {
    abort();
  }

  while (spinlock_.test_and_set(std::memory_order_acquire)) {
  }
}

MetadataLock::~MetadataLock() {
  if (g_thread_in_wasm_code) {
    abort();
  }

  spinlock_.clear(std::memory_order_release);
}

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8
