// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_ISOLATE_OPERATIONS_WIN_H_
#define V8_DIAGNOSTICS_ETW_ISOLATE_OPERATIONS_WIN_H_

#include <optional>
#include <string>

#include "include/v8-callbacks.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "src/api/api.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

class V8_EXPORT_PRIVATE EtwIsolateOperations {
 public:
  virtual void SetEtwCodeEventHandler(Isolate* isolate, uint32_t options);
  virtual void ResetEtwCodeEventHandler(Isolate* isolate);

  virtual FilterETWSessionByURLResult RunFilterETWSessionByURLCallback(
      Isolate* isolate, const std::string& payload);
  virtual void RequestInterrupt(Isolate* isolate, InterruptCallback callback,
                                void* data);
  virtual bool HeapReadOnlySpaceWritable(Isolate* isolate);
  virtual std::optional<Tagged<GcSafeCode>>
  HeapGcSafeTryFindCodeForInnerPointer(Isolate* isolate, Address address);

  static EtwIsolateOperations* Instance();
  static void SetInstanceForTesting(
      EtwIsolateOperations* etw_isolate_operations);

 private:
  static EtwIsolateOperations* instance;
};

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_ISOLATE_OPERATIONS_WIN_H_
