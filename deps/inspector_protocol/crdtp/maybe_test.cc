// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maybe.h"

#include <string>
#include <vector>

#include "test_platform.h"

namespace crdtp {

// =============================================================================
// detail::PtrMaybe, detail::ValueMaybe, templates for optional
// pointers / values which are used in ../lib/Forward_h.template.
// =============================================================================
TEST(PtrMaybeTest, SmokeTest) {
  detail::PtrMaybe<std::vector<uint32_t>> example;
  EXPECT_FALSE(example.has_value());
  std::unique_ptr<std::vector<uint32_t>> v(new std::vector<uint32_t>);
  v->push_back(42);
  v->push_back(21);
  example = std::move(v);
  EXPECT_TRUE(example.has_value());
  EXPECT_THAT(example.value(), testing::ElementsAre(42, 21));
  std::vector<uint32_t> out = *std::move(example);
  EXPECT_TRUE(example.has_value());
  EXPECT_THAT(*example, testing::IsEmpty());
  EXPECT_THAT(out, testing::ElementsAre(42, 21));
}

TEST(ValueMaybeTest, SmokeTest) {
  detail::ValueMaybe<int32_t> example;
  EXPECT_FALSE(example.has_value());
  EXPECT_EQ(-1, example.value_or(-1));
  example = 42;
  EXPECT_TRUE(example.has_value());
  EXPECT_EQ(42, example.value());
  int32_t out = *std::move(example);
  EXPECT_EQ(out, 42);
}

}  // namespace crdtp
