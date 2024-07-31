// Copyright 2020 The Abseil Authors.
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

#include <cxxabi.h>
#include <execinfo.h>

#include <algorithm>
#include <cstring>

#include "absl/base/internal/raw_logging.h"
#include "absl/debugging/internal/demangle.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

void InitializeSymbolizer(const char*) {}

namespace debugging_internal {
namespace {

static std::string GetSymbolString(absl::string_view backtrace_line) {
  // Example Backtrace lines:
  // 0   libimaging_shared.dylib             0x018c152a
  // _ZNSt11_Deque_baseIN3nik7mediadb4PageESaIS2_EE17_M_initialize_mapEm + 3478
  //
  // or
  // 0   libimaging_shared.dylib             0x0000000001895c39
  // _ZN3nik4util19register_shared_ptrINS_3gpu7TextureEEEvPKvS5_ + 39
  //
  // or
  // 0   mysterious_app                      0x0124000120120009 main + 17
  auto address_pos = backtrace_line.find(" 0x");
  if (address_pos == absl::string_view::npos) return std::string();
  absl::string_view symbol_view = backtrace_line.substr(address_pos + 1);

  auto space_pos = symbol_view.find(" ");
  if (space_pos == absl::string_view::npos) return std::string();
  symbol_view = symbol_view.substr(space_pos + 1);  // to mangled symbol

  auto plus_pos = symbol_view.find(" + ");
  if (plus_pos == absl::string_view::npos) return std::string();
  symbol_view = symbol_view.substr(0, plus_pos);  // strip remainng

  return std::string(symbol_view);
}

}  // namespace
}  // namespace debugging_internal

bool Symbolize(const void* pc, char* out, int out_size) {
  if (out_size <= 0 || pc == nullptr) {
    out = nullptr;
    return false;
  }

  // This allocates a char* array.
  char** frame_strings = backtrace_symbols(const_cast<void**>(&pc), 1);

  if (frame_strings == nullptr) return false;

  std::string symbol = debugging_internal::GetSymbolString(frame_strings[0]);
  free(frame_strings);

  char tmp_buf[1024];
  if (debugging_internal::Demangle(symbol.c_str(), tmp_buf, sizeof(tmp_buf))) {
    size_t len = strlen(tmp_buf);
    if (len + 1 <= static_cast<size_t>(out_size)) {  // +1 for '\0'
      assert(len < sizeof(tmp_buf));
      memmove(out, tmp_buf, len + 1);
    }
  } else {
    strncpy(out, symbol.c_str(), static_cast<size_t>(out_size));
  }

  if (out[out_size - 1] != '\0') {
    // strncpy() does not '\0' terminate when it truncates.
    static constexpr char kEllipsis[] = "...";
    size_t ellipsis_size =
        std::min(sizeof(kEllipsis) - 1, static_cast<size_t>(out_size) - 1);
    memcpy(out + out_size - ellipsis_size - 1, kEllipsis, ellipsis_size);
    out[out_size - 1] = '\0';
  }

  return true;
}

ABSL_NAMESPACE_END
}  // namespace absl
