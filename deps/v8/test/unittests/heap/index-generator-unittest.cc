// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/index-generator.h"

#include <optional>

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

TEST(IndexGeneratorTest, Empty) {
  IndexGenerator gen(0);

  EXPECT_EQ(std::nullopt, gen.GetNext());
}

TEST(IndexGeneratorTest, GetNext) {
  IndexGenerator gen(11);

  EXPECT_EQ(0U, gen.GetNext());
  EXPECT_EQ(5U, gen.GetNext());
  EXPECT_EQ(2U, gen.GetNext());
  EXPECT_EQ(8U, gen.GetNext());
  EXPECT_EQ(1U, gen.GetNext());
  EXPECT_EQ(3U, gen.GetNext());
  EXPECT_EQ(6U, gen.GetNext());
  EXPECT_EQ(9U, gen.GetNext());
  EXPECT_EQ(4U, gen.GetNext());
  EXPECT_EQ(7U, gen.GetNext());
  EXPECT_EQ(10U, gen.GetNext());
  EXPECT_EQ(std::nullopt, gen.GetNext());
}

}  // namespace internal
}  // namespace v8
