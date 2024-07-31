// Copyright 2017 The Abseil Authors.
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

#include "absl/strings/internal/ostringstream.h"

#include <cassert>
#include <cstddef>
#include <ios>
#include <streambuf>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

OStringStream::Streambuf::int_type OStringStream::Streambuf::overflow(int c) {
  assert(str_);
  if (!std::streambuf::traits_type::eq_int_type(
          c, std::streambuf::traits_type::eof()))
    str_->push_back(static_cast<char>(c));
  return 1;
}

std::streamsize OStringStream::Streambuf::xsputn(const char* s,
                                                 std::streamsize n) {
  assert(str_);
  str_->append(s, static_cast<size_t>(n));
  return n;
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
