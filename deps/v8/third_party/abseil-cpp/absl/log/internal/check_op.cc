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

#include <cstring>
#include <ostream>
#include <string>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/debugging/leak_check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#else
#include <strings.h>  // for strcasecmp, but msvc does not have this header
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

#define ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(x) \
  template absl::Nonnull<const char*> MakeCheckOpString( \
      x, x, absl::Nonnull<const char*>)
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(bool);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(int64_t);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(uint64_t);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(float);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(double);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(char);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(unsigned char);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const std::string&);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const absl::string_view&);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const signed char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const unsigned char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING(const void*);
#undef ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING

CheckOpMessageBuilder::CheckOpMessageBuilder(
    absl::Nonnull<const char*> exprtext) {
  stream_ << exprtext << " (";
}

std::ostream& CheckOpMessageBuilder::ForVar2() {
  stream_ << " vs. ";
  return stream_;
}

absl::Nonnull<const char*> CheckOpMessageBuilder::NewString() {
  stream_ << ")";
  // There's no need to free this string since the process is crashing.
  return absl::IgnoreLeak(new std::string(std::move(stream_).str()))->c_str();
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
#define DEFINE_CHECK_STROP_IMPL(name, func, expected)                          \
  absl::Nullable<const char*> Check##func##expected##Impl(                     \
      absl::Nullable<const char*> s1, absl::Nullable<const char*> s2,          \
      absl::Nonnull<const char*> exprtext) {                                   \
    bool equal = s1 == s2 || (s1 && s2 && !func(s1, s2));                      \
    if (equal == expected) {                                                   \
      return nullptr;                                                          \
    } else {                                                                   \
      /* There's no need to free this string since the process is crashing. */ \
      return absl::IgnoreLeak(new std::string(absl::StrCat(exprtext, " (", s1, \
                                                           " vs. ", s2, ")"))) \
          ->c_str();                                                           \
    }                                                                          \
  }
DEFINE_CHECK_STROP_IMPL(CHECK_STREQ, strcmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRNE, strcmp, false)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASEEQ, strcasecmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASENE, strcasecmp, false)
#undef DEFINE_CHECK_STROP_IMPL

namespace detect_specialization {

StringifySink::StringifySink(std::ostream& os) : os_(os) {}

void StringifySink::Append(absl::string_view text) { os_ << text; }

void StringifySink::Append(size_t length, char ch) {
  for (size_t i = 0; i < length; ++i) os_.put(ch);
}

void AbslFormatFlush(StringifySink* sink, absl::string_view text) {
  sink->Append(text);
}

}  // namespace detect_specialization

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
