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

#include "absl/base/optimization.h"

#include "gtest/gtest.h"
#include "absl/types/optional.h"

namespace {

// Tests for the ABSL_PREDICT_TRUE and ABSL_PREDICT_FALSE macros.
// The tests only verify that the macros are functionally correct - i.e. code
// behaves as if they weren't used. They don't try to check their impact on
// optimization.

TEST(PredictTest, PredictTrue) {
  EXPECT_TRUE(ABSL_PREDICT_TRUE(true));
  EXPECT_FALSE(ABSL_PREDICT_TRUE(false));
  EXPECT_TRUE(ABSL_PREDICT_TRUE(1 == 1));
  EXPECT_FALSE(ABSL_PREDICT_TRUE(1 == 2));

  if (ABSL_PREDICT_TRUE(false)) ADD_FAILURE();
  if (!ABSL_PREDICT_TRUE(true)) ADD_FAILURE();

  EXPECT_TRUE(ABSL_PREDICT_TRUE(true) && true);
  EXPECT_TRUE(ABSL_PREDICT_TRUE(true) || false);
}

TEST(PredictTest, PredictFalse) {
  EXPECT_TRUE(ABSL_PREDICT_FALSE(true));
  EXPECT_FALSE(ABSL_PREDICT_FALSE(false));
  EXPECT_TRUE(ABSL_PREDICT_FALSE(1 == 1));
  EXPECT_FALSE(ABSL_PREDICT_FALSE(1 == 2));

  if (ABSL_PREDICT_FALSE(false)) ADD_FAILURE();
  if (!ABSL_PREDICT_FALSE(true)) ADD_FAILURE();

  EXPECT_TRUE(ABSL_PREDICT_FALSE(true) && true);
  EXPECT_TRUE(ABSL_PREDICT_FALSE(true) || false);
}

TEST(PredictTest, OneEvaluation) {
  // Verify that the expression is only evaluated once.
  int x = 0;
  if (ABSL_PREDICT_TRUE((++x) == 0)) ADD_FAILURE();
  EXPECT_EQ(x, 1);
  if (ABSL_PREDICT_FALSE((++x) == 0)) ADD_FAILURE();
  EXPECT_EQ(x, 2);
}

TEST(PredictTest, OperatorOrder) {
  // Verify that operator order inside and outside the macro behaves well.
  // These would fail for a naive '#define ABSL_PREDICT_TRUE(x) x'
  EXPECT_TRUE(ABSL_PREDICT_TRUE(1 && 2) == true);
  EXPECT_TRUE(ABSL_PREDICT_FALSE(1 && 2) == true);
  EXPECT_TRUE(!ABSL_PREDICT_TRUE(1 == 2));
  EXPECT_TRUE(!ABSL_PREDICT_FALSE(1 == 2));
}

TEST(PredictTest, Pointer) {
  const int x = 3;
  const int *good_intptr = &x;
  const int *null_intptr = nullptr;
  EXPECT_TRUE(ABSL_PREDICT_TRUE(good_intptr));
  EXPECT_FALSE(ABSL_PREDICT_TRUE(null_intptr));
  EXPECT_TRUE(ABSL_PREDICT_FALSE(good_intptr));
  EXPECT_FALSE(ABSL_PREDICT_FALSE(null_intptr));
}

TEST(PredictTest, Optional) {
  // Note: An optional's truth value is the value's existence, not its truth.
  absl::optional<bool> has_value(false);
  absl::optional<bool> no_value;
  EXPECT_TRUE(ABSL_PREDICT_TRUE(has_value));
  EXPECT_FALSE(ABSL_PREDICT_TRUE(no_value));
  EXPECT_TRUE(ABSL_PREDICT_FALSE(has_value));
  EXPECT_FALSE(ABSL_PREDICT_FALSE(no_value));
}

class ImplictlyConvertibleToBool {
 public:
  explicit ImplictlyConvertibleToBool(bool value) : value_(value) {}
  operator bool() const {  // NOLINT(google-explicit-constructor)
    return value_;
  }

 private:
  bool value_;
};

TEST(PredictTest, ImplicitBoolConversion) {
  const ImplictlyConvertibleToBool is_true(true);
  const ImplictlyConvertibleToBool is_false(false);
  if (!ABSL_PREDICT_TRUE(is_true)) ADD_FAILURE();
  if (ABSL_PREDICT_TRUE(is_false)) ADD_FAILURE();
  if (!ABSL_PREDICT_FALSE(is_true)) ADD_FAILURE();
  if (ABSL_PREDICT_FALSE(is_false)) ADD_FAILURE();
}

class ExplictlyConvertibleToBool {
 public:
  explicit ExplictlyConvertibleToBool(bool value) : value_(value) {}
  explicit operator bool() const { return value_; }

 private:
  bool value_;
};

TEST(PredictTest, ExplicitBoolConversion) {
  const ExplictlyConvertibleToBool is_true(true);
  const ExplictlyConvertibleToBool is_false(false);
  if (!ABSL_PREDICT_TRUE(is_true)) ADD_FAILURE();
  if (ABSL_PREDICT_TRUE(is_false)) ADD_FAILURE();
  if (!ABSL_PREDICT_FALSE(is_true)) ADD_FAILURE();
  if (ABSL_PREDICT_FALSE(is_false)) ADD_FAILURE();
}

}  // namespace
