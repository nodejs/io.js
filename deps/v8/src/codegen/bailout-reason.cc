// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/bailout-reason.h"
#include "src/base/logging.h"

namespace v8 {
namespace internal {

#define ERROR_MESSAGES_TEXTS(C, T) T,

const char* GetBailoutReason(BailoutReason reason) {
  // Currently, the BailoutReason is read from the SharedFunctionInfo object
  // inside the sandbox and must therefore be considered untrusted. As such, it
  // needs to be validated here.
  static_assert(std::is_unsigned_v<std::underlying_type_t<BailoutReason>>);
  SBXCHECK_LT(reason, BailoutReason::kLastErrorMessage);
  DCHECK_GE(reason, BailoutReason::kNoReason);
  static const char* error_messages_[] = {
      BAILOUT_MESSAGES_LIST(ERROR_MESSAGES_TEXTS)};
  return error_messages_[static_cast<int>(reason)];
}

const char* GetAbortReason(AbortReason reason) {
  DCHECK_LT(reason, AbortReason::kLastErrorMessage);
  DCHECK_GE(reason, AbortReason::kNoReason);
  static const char* error_messages_[] = {
      ABORT_MESSAGES_LIST(ERROR_MESSAGES_TEXTS)};
  return error_messages_[static_cast<int>(reason)];
}

bool IsValidAbortReason(int reason_id) {
  return reason_id >= static_cast<int>(AbortReason::kNoReason) &&
         reason_id < static_cast<int>(AbortReason::kLastErrorMessage);
}

#undef ERROR_MESSAGES_TEXTS
}  // namespace internal
}  // namespace v8
