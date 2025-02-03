// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_INTERPRETER_INTRINSICS_H_
#define V8_INTERPRETER_INTERPRETER_INTRINSICS_H_

#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {
namespace interpreter {

// List of supported intrisics, with upper case name, lower case name and
// expected number of arguments (-1 denoting argument count is variable).
#define INTRINSICS_LIST(V)                                             \
  V(AsyncFunctionAwait, async_function_await_caught, 2)                \
  V(AsyncFunctionEnter, async_function_enter, 2)                       \
  V(AsyncFunctionReject, async_function_reject, 2)                     \
  V(AsyncFunctionResolve, async_function_resolve, 2)                   \
  V(AsyncGeneratorAwait, async_generator_await_caught, 2)              \
  V(AsyncGeneratorReject, async_generator_reject, 2)                   \
  V(AsyncGeneratorResolve, async_generator_resolve, 3)                 \
  V(AsyncGeneratorYieldWithAwait, async_generator_yield_with_await, 2) \
  V(CreateJSGeneratorObject, create_js_generator_object, 2)            \
  V(GeneratorGetResumeMode, generator_get_resume_mode, 1)              \
  V(GeneratorClose, generator_close, 1)                                \
  V(GetImportMetaObject, get_import_meta_object, 0)                    \
  V(CopyDataProperties, copy_data_properties, 2)                       \
  V(CopyDataPropertiesWithExcludedPropertiesOnStack,                   \
    copy_data_properties_with_excluded_properties_on_stack, -1)        \
  V(CreateIterResultObject, create_iter_result_object, 2)              \
  V(CreateAsyncFromSyncIterator, create_async_from_sync_iterator, 1)

class IntrinsicsHelper {
 public:
  enum class IntrinsicId {
#define DECLARE_INTRINSIC_ID(name, lower_case, count) k##name,
    INTRINSICS_LIST(DECLARE_INTRINSIC_ID)
#undef DECLARE_INTRINSIC_ID
        kIdCount
  };
  static_assert(static_cast<uint32_t>(IntrinsicId::kIdCount) <= kMaxUInt8);

  V8_EXPORT_PRIVATE static bool IsSupported(Runtime::FunctionId function_id);
  static IntrinsicId FromRuntimeId(Runtime::FunctionId function_id);
  static Runtime::FunctionId ToRuntimeId(IntrinsicId intrinsic_id);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(IntrinsicsHelper);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_INTERPRETER_INTRINSICS_H_
