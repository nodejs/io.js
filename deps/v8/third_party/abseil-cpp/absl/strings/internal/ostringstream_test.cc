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

#include <ios>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(OStringStream, IsOStream) {
  static_assert(
      std::is_base_of<std::ostream, absl::strings_internal::OStringStream>(),
      "");
}

TEST(OStringStream, ConstructNullptr) {
  absl::strings_internal::OStringStream strm(nullptr);
  EXPECT_EQ(nullptr, strm.str());
}

TEST(OStringStream, ConstructStr) {
  std::string s = "abc";
  {
    absl::strings_internal::OStringStream strm(&s);
    EXPECT_EQ(&s, strm.str());
  }
  EXPECT_EQ("abc", s);
}

TEST(OStringStream, Destroy) {
  std::unique_ptr<std::string> s(new std::string);
  absl::strings_internal::OStringStream strm(s.get());
  s.reset();
}

TEST(OStringStream, MoveConstruct) {
  std::string s = "abc";
  {
    absl::strings_internal::OStringStream strm1(&s);
    strm1 << std::hex << 16;
    EXPECT_EQ(&s, strm1.str());
    absl::strings_internal::OStringStream strm2(std::move(strm1));
    strm2 << 16;  // We should still be in base 16.
    EXPECT_EQ(&s, strm2.str());
  }
  EXPECT_EQ("abc1010", s);
}

TEST(OStringStream, MoveAssign) {
  std::string s = "abc";
  {
    absl::strings_internal::OStringStream strm1(&s);
    strm1 << std::hex << 16;
    EXPECT_EQ(&s, strm1.str());
    absl::strings_internal::OStringStream strm2(nullptr);
    strm2 = std::move(strm1);
    strm2 << 16;  // We should still be in base 16.
    EXPECT_EQ(&s, strm2.str());
  }
  EXPECT_EQ("abc1010", s);
}

TEST(OStringStream, Str) {
  std::string s1;
  absl::strings_internal::OStringStream strm(&s1);
  const absl::strings_internal::OStringStream& c_strm(strm);

  static_assert(std::is_same<decltype(strm.str()), std::string*>(), "");
  static_assert(std::is_same<decltype(c_strm.str()), const std::string*>(), "");

  EXPECT_EQ(&s1, strm.str());
  EXPECT_EQ(&s1, c_strm.str());

  strm.str(&s1);
  EXPECT_EQ(&s1, strm.str());
  EXPECT_EQ(&s1, c_strm.str());

  std::string s2;
  strm.str(&s2);
  EXPECT_EQ(&s2, strm.str());
  EXPECT_EQ(&s2, c_strm.str());

  strm.str(nullptr);
  EXPECT_EQ(nullptr, strm.str());
  EXPECT_EQ(nullptr, c_strm.str());
}

TEST(OStreamStream, WriteToLValue) {
  std::string s = "abc";
  {
    absl::strings_internal::OStringStream strm(&s);
    EXPECT_EQ("abc", s);
    strm << "";
    EXPECT_EQ("abc", s);
    strm << 42;
    EXPECT_EQ("abc42", s);
    strm << 'x' << 'y';
    EXPECT_EQ("abc42xy", s);
  }
  EXPECT_EQ("abc42xy", s);
}

TEST(OStreamStream, WriteToRValue) {
  std::string s = "abc";
  absl::strings_internal::OStringStream(&s) << "";
  EXPECT_EQ("abc", s);
  absl::strings_internal::OStringStream(&s) << 42;
  EXPECT_EQ("abc42", s);
  absl::strings_internal::OStringStream(&s) << 'x' << 'y';
  EXPECT_EQ("abc42xy", s);
}

}  // namespace
