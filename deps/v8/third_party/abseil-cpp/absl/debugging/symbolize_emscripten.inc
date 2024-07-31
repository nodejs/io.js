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
#include <emscripten.h>

#include <algorithm>
#include <cstring>

#include "absl/base/internal/raw_logging.h"
#include "absl/debugging/internal/demangle.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

extern "C" {
const char* emscripten_pc_get_function(const void* pc);
}

// clang-format off
EM_JS(bool, HaveOffsetConverter, (),
      { return typeof wasmOffsetConverter !== 'undefined'; });
// clang-format on

namespace absl {
ABSL_NAMESPACE_BEGIN

void InitializeSymbolizer(const char*) {
  if (!HaveOffsetConverter()) {
    ABSL_RAW_LOG(INFO,
                 "Symbolization unavailable. Rebuild with -sWASM=1 "
                 "and -sUSE_OFFSET_CONVERTER=1.");
  }
}

bool Symbolize(const void* pc, char* out, int out_size) {
  // Check if we have the offset converter necessary for pc_get_function.
  // Without it, the program will abort().
  if (!HaveOffsetConverter()) {
    return false;
  }
  if (pc == nullptr || out_size <= 0) {
    return false;
  }
  const char* func_name = emscripten_pc_get_function(pc);
  if (func_name == nullptr) {
    return false;
  }

  strncpy(out, func_name, out_size);

  if (out[out_size - 1] != '\0') {
    // strncpy() does not '\0' terminate when it truncates.
    static constexpr char kEllipsis[] = "...";
    int ellipsis_size = std::min<int>(sizeof(kEllipsis) - 1, out_size - 1);
    memcpy(out + out_size - ellipsis_size - 1, kEllipsis, ellipsis_size);
    out[out_size - 1] = '\0';
  }

  return true;
}

ABSL_NAMESPACE_END
}  // namespace absl
