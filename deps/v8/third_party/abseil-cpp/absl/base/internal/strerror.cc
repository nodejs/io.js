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

#include "absl/base/internal/strerror.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

#include "absl/base/internal/errno_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

const char* StrErrorAdaptor(int errnum, char* buf, size_t buflen) {
#if defined(_WIN32)
  int rc = strerror_s(buf, buflen, errnum);
  buf[buflen - 1] = '\0';  // guarantee NUL termination
  if (rc == 0 && strncmp(buf, "Unknown error", buflen) == 0) *buf = '\0';
  return buf;
#else
  // The type of `ret` is platform-specific; both of these branches must compile
  // either way but only one will execute on any given platform:
  auto ret = strerror_r(errnum, buf, buflen);
  if (std::is_same<decltype(ret), int>::value) {
    // XSI `strerror_r`; `ret` is `int`:
    if (ret) *buf = '\0';
    return buf;
  } else {
    // GNU `strerror_r`; `ret` is `char *`:
    return reinterpret_cast<const char*>(ret);
  }
#endif
}

std::string StrErrorInternal(int errnum) {
  char buf[100];
  const char* str = StrErrorAdaptor(errnum, buf, sizeof buf);
  if (*str == '\0') {
    snprintf(buf, sizeof buf, "Unknown error %d", errnum);
    str = buf;
  }
  return str;
}

// kSysNerr is the number of errors from a recent glibc. `StrError()` falls back
// to `StrErrorAdaptor()` if the value is larger than this.
constexpr int kSysNerr = 135;

std::array<std::string, kSysNerr>* NewStrErrorTable() {
  auto* table = new std::array<std::string, kSysNerr>;
  for (size_t i = 0; i < table->size(); ++i) {
    (*table)[i] = StrErrorInternal(static_cast<int>(i));
  }
  return table;
}

}  // namespace

std::string StrError(int errnum) {
  absl::base_internal::ErrnoSaver errno_saver;
  static const auto* table = NewStrErrorTable();
  if (errnum >= 0 && static_cast<size_t>(errnum) < table->size()) {
    return (*table)[static_cast<size_t>(errnum)];
  }
  return StrErrorInternal(errnum);
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
