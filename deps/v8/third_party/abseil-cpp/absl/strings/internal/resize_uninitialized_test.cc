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

#include "absl/strings/internal/resize_uninitialized.h"

#include "gtest/gtest.h"

namespace {

int resize_call_count = 0;
int append_call_count = 0;

// A mock string class whose only purpose is to track how many times its
// resize()/append() methods have been called.
struct resizable_string {
  using value_type = char;
  size_t size() const { return 0; }
  size_t capacity() const { return 0; }
  char& operator[](size_t) {
    static char c = '\0';
    return c;
  }
  void resize(size_t) { resize_call_count += 1; }
  void append(size_t, value_type) { append_call_count += 1; }
  void reserve(size_t) {}
  resizable_string& erase(size_t = 0, size_t = 0) { return *this; }
};

int resize_default_init_call_count = 0;
int append_default_init_call_count = 0;

// A mock string class whose only purpose is to track how many times its
// resize()/__resize_default_init()/append()/__append_default_init() methods
// have been called.
struct default_init_string {
  size_t size() const { return 0; }
  size_t capacity() const { return 0; }
  char& operator[](size_t) {
    static char c = '\0';
    return c;
  }
  void resize(size_t) { resize_call_count += 1; }
  void __resize_default_init(size_t) { resize_default_init_call_count += 1; }
  void __append_default_init(size_t) { append_default_init_call_count += 1; }
  void reserve(size_t) {}
  default_init_string& erase(size_t = 0, size_t = 0) { return *this; }
};

TEST(ResizeUninit, WithAndWithout) {
  resize_call_count = 0;
  append_call_count = 0;
  resize_default_init_call_count = 0;
  append_default_init_call_count = 0;
  {
    resizable_string rs;

    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
    EXPECT_FALSE(
        absl::strings_internal::STLStringSupportsNontrashingResize(&rs));
    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
    absl::strings_internal::STLStringResizeUninitialized(&rs, 237);
    EXPECT_EQ(resize_call_count, 1);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
    absl::strings_internal::STLStringResizeUninitializedAmortized(&rs, 1000);
    EXPECT_EQ(resize_call_count, 1);
    EXPECT_EQ(append_call_count, 1);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
  }

  resize_call_count = 0;
  append_call_count = 0;
  resize_default_init_call_count = 0;
  append_default_init_call_count = 0;
  {
    default_init_string rus;

    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
    EXPECT_TRUE(
        absl::strings_internal::STLStringSupportsNontrashingResize(&rus));
    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 0);
    EXPECT_EQ(append_default_init_call_count, 0);
    absl::strings_internal::STLStringResizeUninitialized(&rus, 237);
    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 1);
    EXPECT_EQ(append_default_init_call_count, 0);
    absl::strings_internal::STLStringResizeUninitializedAmortized(&rus, 1000);
    EXPECT_EQ(resize_call_count, 0);
    EXPECT_EQ(append_call_count, 0);
    EXPECT_EQ(resize_default_init_call_count, 1);
    EXPECT_EQ(append_default_init_call_count, 1);
  }
}

TEST(ResizeUninit, Amortized) {
  std::string str;
  size_t prev_cap = str.capacity();
  int cap_increase_count = 0;
  for (int i = 0; i < 1000; ++i) {
    absl::strings_internal::STLStringResizeUninitializedAmortized(&str, i);
    size_t new_cap = str.capacity();
    if (new_cap > prev_cap) ++cap_increase_count;
    prev_cap = new_cap;
  }
  EXPECT_LT(cap_increase_count, 50);
}

}  // namespace
