// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-isolate-operations-win.h"

#include "src/common/assert-scope.h"
#include "src/heap/read-only-spaces.h"
#include "src/logging/log.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

// static
EtwIsolateOperations* EtwIsolateOperations::instance = nullptr;

// virtual
void EtwIsolateOperations::SetEtwCodeEventHandler(Isolate* isolate,
                                                  uint32_t options) {
  isolate->v8_file_logger()->SetEtwCodeEventHandler(options);
}

// virtual
void EtwIsolateOperations::ResetEtwCodeEventHandler(Isolate* isolate) {
  isolate->v8_file_logger()->ResetEtwCodeEventHandler();
}

// virtual
FilterETWSessionByURLResult
EtwIsolateOperations::RunFilterETWSessionByURLCallback(
    Isolate* isolate, const std::string& payload) {
  // We should not call back into V8 from the RunFilterETWSessionByURLCallback
  // callback.
  DisallowJavascriptExecution no_js(isolate);
  return isolate->RunFilterETWSessionByURLCallback(payload);
}

// virtual
void EtwIsolateOperations::RequestInterrupt(Isolate* isolate,
                                            InterruptCallback callback,
                                            void* data) {
  isolate->RequestInterrupt(callback, data);
}

// virtual
bool EtwIsolateOperations::HeapReadOnlySpaceWritable(Isolate* isolate) {
  return isolate->heap()->read_only_space()->writable();
}

// virtual
std::optional<Tagged<GcSafeCode>>
EtwIsolateOperations::HeapGcSafeTryFindCodeForInnerPointer(Isolate* isolate,
                                                           Address address) {
  return isolate->heap()->GcSafeTryFindCodeForInnerPointer(address);
}

// static
EtwIsolateOperations* EtwIsolateOperations::Instance() {
  static EtwIsolateOperations etw_isolate_operations;
  if (!instance) {
    instance = &etw_isolate_operations;
  }

  return instance;
}

// static
void EtwIsolateOperations::SetInstanceForTesting(
    EtwIsolateOperations* etw_isolate_operations) {
  instance = etw_isolate_operations;
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
