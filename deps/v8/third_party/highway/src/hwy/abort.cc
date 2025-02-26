// Copyright 2019 Google LLC
// Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause

#include "hwy/abort.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "hwy/base.h"

#if HWY_IS_ASAN || HWY_IS_MSAN || HWY_IS_TSAN
#include "sanitizer/common_interface_defs.h"  // __sanitizer_print_stack_trace
#endif

namespace hwy {

namespace {
std::string GetBaseName(std::string const& file_name) {
  auto last_slash = file_name.find_last_of("/\\");
  return file_name.substr(last_slash + 1);
}
}  // namespace

HWY_DLLEXPORT AbortFunc& GetAbortFunc() {
  static AbortFunc func;
  return func;
}

HWY_DLLEXPORT AbortFunc SetAbortFunc(AbortFunc func) {
  const AbortFunc prev = GetAbortFunc();
  GetAbortFunc() = func;
  return prev;
}

HWY_DLLEXPORT HWY_NORETURN void HWY_FORMAT(3, 4)
    Abort(const char* file, int line, const char* format, ...) {
  char buf[800];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  AbortFunc handler = GetAbortFunc();
  if (handler != nullptr) {
    handler(file, line, buf);
  } else {
    fprintf(stderr, "Abort at %s:%d: %s\n", GetBaseName(file).data(), line,
            buf);
  }

// If compiled with any sanitizer, they can also print a stack trace.
#if HWY_IS_ASAN || HWY_IS_MSAN || HWY_IS_TSAN
  __sanitizer_print_stack_trace();
#endif  // HWY_IS_*
  fflush(stderr);

// Now terminate the program:
#if HWY_ARCH_RISCV
  exit(1);  // trap/abort just freeze Spike.
#elif HWY_IS_DEBUG_BUILD && !HWY_COMPILER_MSVC && !HWY_ARCH_ARM
  // Facilitates breaking into a debugger, but don't use this in non-debug
  // builds because it looks like "illegal instruction", which is misleading.
  // Also does not work on Arm.
  __builtin_trap();
#else
  abort();  // Compile error without this due to HWY_NORETURN.
#endif
}

}  // namespace hwy
