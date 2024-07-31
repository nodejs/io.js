// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <vector>

#include "gtest/gtest.h"
#include "third_party/abseil-cpp/absl/base/options.h"
#include "third_party/abseil-cpp/absl/container/fixed_array.h"
#include "third_party/abseil-cpp/absl/container/inlined_vector.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/span.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

namespace {

#if !ABSL_OPTION_HARDENED
# error "Define ABSL_OPTION_HARDENED to 1 in absl/base/options.h"
#endif

TEST(AbslHardeningTest, Optional) {
  absl::optional<int> optional;
  EXPECT_DEATH_IF_SUPPORTED(*optional, "");
}

TEST(AbslHardeningTest, StringView) {
  absl::string_view view("foo");
  EXPECT_DEATH_IF_SUPPORTED(view[4], "");
  EXPECT_DEATH_IF_SUPPORTED(view.remove_prefix(10), "");
  EXPECT_DEATH_IF_SUPPORTED(view.remove_suffix(10), "");

  absl::string_view empty("");
  EXPECT_DEATH_IF_SUPPORTED(empty.front(), "");
  EXPECT_DEATH_IF_SUPPORTED(empty.back(), "");
}

TEST(AbslHardeningTest, FixedArray) {
  absl::FixedArray<int, 4> fixed_array(0);
  EXPECT_DEATH_IF_SUPPORTED(fixed_array[1], "");
  EXPECT_DEATH_IF_SUPPORTED(fixed_array.front(), "");
  EXPECT_DEATH_IF_SUPPORTED(fixed_array.back(), "");
}

TEST(AbslHardeningTest, InlinedVector) {
  absl::InlinedVector<int, 10> inlined_vector;
  EXPECT_DEATH_IF_SUPPORTED(inlined_vector[1], "");
  EXPECT_DEATH_IF_SUPPORTED(inlined_vector.front(), "");
  EXPECT_DEATH_IF_SUPPORTED(inlined_vector.back(), "");
  EXPECT_DEATH_IF_SUPPORTED(
      inlined_vector.resize(inlined_vector.max_size() + 1), "");
  EXPECT_DEATH_IF_SUPPORTED(inlined_vector.pop_back(), "");

  auto it = inlined_vector.end();
  EXPECT_DEATH_IF_SUPPORTED(inlined_vector.erase(it), "");
}

TEST(AbslHardeningTest, Span) {
  std::vector<int> v;
  auto span = absl::Span<const int>(v);

  EXPECT_DEATH_IF_SUPPORTED(span.front(), "");
  EXPECT_DEATH_IF_SUPPORTED(span.back(), "");
  EXPECT_DEATH_IF_SUPPORTED(span.remove_prefix(10), "");
  EXPECT_DEATH_IF_SUPPORTED(span.remove_suffix(10), "");
  EXPECT_DEATH_IF_SUPPORTED(span[10], "");

  std::vector<int> v1 = {1, 2, 3, 4};
  EXPECT_DEATH_IF_SUPPORTED(absl::MakeSpan(&v1[2], &v1[0]), "");
  EXPECT_DEATH_IF_SUPPORTED(absl::MakeConstSpan(&v1[2], &v1[0]), "");
}

TEST(AbslHardeningTest, Variant) {
  absl::variant<int, std::string> variant = 5;
  EXPECT_DEATH_IF_SUPPORTED(absl::get<std::string>(variant), "");
  EXPECT_DEATH_IF_SUPPORTED(absl::get<1>(variant), "");
}

}  // namespace
