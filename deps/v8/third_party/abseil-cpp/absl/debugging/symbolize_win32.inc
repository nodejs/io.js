// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// See "Retrieving Symbol Information by Address":
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680578(v=vs.85).aspx

#include <windows.h>

// MSVC header dbghelp.h has a warning for an ignored typedef.
#pragma warning(push)
#pragma warning(disable:4091)
#include <dbghelp.h>
#pragma warning(pop)

#pragma comment(lib, "dbghelp.lib")

#include <algorithm>
#include <cstring>

#include "absl/base/internal/raw_logging.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

static HANDLE process = NULL;

void InitializeSymbolizer(const char*) {
  if (process != nullptr) {
    return;
  }
  process = GetCurrentProcess();

  // Symbols are not loaded until a reference is made requiring the
  // symbols be loaded. This is the fastest, most efficient way to use
  // the symbol handler.
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
  if (!SymInitialize(process, nullptr, true)) {
    // GetLastError() returns a Win32 DWORD, but we assign to
    // unsigned long long to simplify the ABSL_RAW_LOG case below.  The uniform
    // initialization guarantees this is not a narrowing conversion.
    const unsigned long long error{GetLastError()};  // NOLINT(runtime/int)
    ABSL_RAW_LOG(FATAL, "SymInitialize() failed: %llu", error);
  }
}

bool Symbolize(const void* pc, char* out, int out_size) {
  if (out_size <= 0) {
    return false;
  }
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buf);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  if (!SymFromAddr(process, reinterpret_cast<DWORD64>(pc), nullptr, symbol)) {
    return false;
  }
  const size_t out_size_t = static_cast<size_t>(out_size);
  strncpy(out, symbol->Name, out_size_t);
  if (out[out_size_t - 1] != '\0') {
    // strncpy() does not '\0' terminate when it truncates.
    static constexpr char kEllipsis[] = "...";
    size_t ellipsis_size =
        std::min(sizeof(kEllipsis) - 1, out_size_t - 1);
    memcpy(out + out_size_t - ellipsis_size - 1, kEllipsis, ellipsis_size);
    out[out_size_t - 1] = '\0';
  }
  return true;
}

ABSL_NAMESPACE_END
}  // namespace absl
