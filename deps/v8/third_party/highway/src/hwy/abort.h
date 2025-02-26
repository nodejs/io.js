// Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause

#ifndef HIGHWAY_HWY_ABORT_H_
#define HIGHWAY_HWY_ABORT_H_

#include "hwy/highway_export.h"

namespace hwy {

// Interface for custom abort handler
typedef void (*AbortFunc)(const char* file, int line,
                          const char* formatted_err);

// Retrieve current abort handler
// Returns null if no abort handler registered, indicating Highway should print and abort
HWY_DLLEXPORT AbortFunc& GetAbortFunc();

// Sets a new abort handler and returns the previous abort handler
// If this handler does not do the aborting itself Highway will use its own abort mechanism
// which allows this to be used to customize the handling of the error itself.
// Returns null if no previous abort handler registered
HWY_DLLEXPORT AbortFunc SetAbortFunc(AbortFunc func);

}  // namespace hwy

#endif  // HIGHWAY_HWY_ABORT_H_
