// Copyright 2022 The Abseil Authors.
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

#include "absl/log/internal/check_op.h"

#include <string.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#else
#include <strings.h>  // for strcasecmp, but msvc does not have this header
#endif

#include <sstream>
#include <string>

#include "absl/base/config.h"
#include "absl/strings/str_cat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

#define ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(x) \
  template std::string* MakeCheckOpString(x, x, const char*)
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(bool);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(int64_t);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(uint64_t);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(float);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(double);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(char);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(unsigned char);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const std::string&);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const absl::string_view&);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const char*);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const signed char*);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const unsigned char*);
ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const void*);
#undef ABSL_LOGGING_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING

CheckOpMessageBuilder::CheckOpMessageBuilder(const char* exprtext) {
  stream_ << exprtext << " (";
}

std::ostream& CheckOpMessageBuilder::ForVar2() {
  stream_ << " vs. ";
  return stream_;
}

std::string* CheckOpMessageBuilder::NewString() {
  stream_ << ")";
  return new std::string(stream_.str());
}

void MakeCheckOpValueString(std::ostream& os, const char v) {
  if (v >= 32 && v <= 126) {
    os << "'" << v << "'";
  } else {
    os << "char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream& os, const signed char v) {
  if (v >= 32 && v <= 126) {
    os << "'" << v << "'";
  } else {
    os << "signed char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream& os, const unsigned char v) {
  if (v >= 32 && v <= 126) {
    os << "'" << v << "'";
  } else {
    os << "unsigned char value " << int{v};
  }
}

void MakeCheckOpValueString(std::ostream& os, const void* p) {
  if (p == nullptr) {
    os << "(null)";
  } else {
    os << p;
  }
}

// Helper functions for string comparisons.
#define DEFINE_CHECK_STROP_IMPL(name, func, expected)                      \
  std::string* Check##func##expected##Impl(const char* s1, const char* s2, \
                                           const char* exprtext) {         \
    bool equal = s1 == s2 || (s1 && s2 && !func(s1, s2));                  \
    if (equal == expected) {                                               \
      return nullptr;                                                      \
    } else {                                                               \
      return new std::string(                                              \
          absl::StrCat(exprtext, " (", s1, " vs. ", s2, ")"));             \
    }                                                                      \
  }
DEFINE_CHECK_STROP_IMPL(CHECK_STREQ, strcmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRNE, strcmp, false)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASEEQ, strcasecmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASENE, strcasecmp, false)
#undef DEFINE_CHECK_STROP_IMPL

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
