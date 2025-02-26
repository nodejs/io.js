// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_DEBUG_WIN_H_
#define V8_DIAGNOSTICS_ETW_DEBUG_WIN_H_

#include <iostream>

#include "src/flags/flags.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE EtwTraceDebug {
 public:
  static EtwTraceDebug info;
};

std::ostream& operator<<(std::ostream& os, const EtwTraceDebug&);

#undef ETWTRACEDBG
#ifdef DEBUG
#define ETWTRACEDBG v8_flags.etw_trace_debug&& std::cout << EtwTraceDebug::info
#else
#define ETWTRACEDBG 0 && std::cout
#endif

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_DEBUG_WIN_H_
