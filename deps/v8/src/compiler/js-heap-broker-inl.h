// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_HEAP_BROKER_INL_H_
#define V8_COMPILER_JS_HEAP_BROKER_INL_H_

#include "src/compiler/js-heap-broker.h"
#include "src/heap/parked-scope-inl.h"

namespace v8::internal::compiler {

V8_INLINE
JSHeapBroker::RecursiveMutexGuardIfNeeded::RecursiveMutexGuardIfNeeded(
    LocalIsolate* local_isolate, base::SpinningMutex* mutex,
    int* mutex_depth_address)
    : mutex_depth_address_(mutex_depth_address),
      initial_mutex_depth_(*mutex_depth_address_),
      mutex_guard_(local_isolate, mutex, initial_mutex_depth_ == 0) {
  (*mutex_depth_address_)++;
}

V8_INLINE JSHeapBroker::MapUpdaterGuardIfNeeded::MapUpdaterGuardIfNeeded(
    JSHeapBroker* broker)
    : RecursiveMutexGuardIfNeeded(broker -> local_isolate_or_isolate(),
                                  broker->isolate()->map_updater_access(),
                                  &broker->map_updater_mutex_depth_) {}

V8_INLINE JSHeapBroker::BoilerplateMigrationGuardIfNeeded::
    BoilerplateMigrationGuardIfNeeded(JSHeapBroker* broker)
    : RecursiveMutexGuardIfNeeded(
          broker -> local_isolate_or_isolate(),
          broker->isolate()->boilerplate_migration_access(),
          &broker->boilerplate_migration_mutex_depth_) {}

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_JS_HEAP_BROKER_INL_H_
