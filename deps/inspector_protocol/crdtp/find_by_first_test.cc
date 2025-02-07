// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "find_by_first.h"
#include "test_platform.h"

namespace crdtp {
// =============================================================================
// FindByFirst - Efficient retrieval from a sorted vector.
// =============================================================================
TEST(FindByFirst, SpanBySpan) {
  std::vector<std::pair<span<uint8_t>, span<uint8_t>>> sorted_span_by_span = {
      {SpanFrom("foo1"), SpanFrom("bar1")},
      {SpanFrom("foo2"), SpanFrom("bar2")},
      {SpanFrom("foo3"), SpanFrom("bar3")},
  };
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("foo1"),
                              SpanFrom("not_found"));
    EXPECT_EQ("bar1", std::string(result.begin(), result.end()));
  }
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("foo3"),
                              SpanFrom("not_found"));
    EXPECT_EQ("bar3", std::string(result.begin(), result.end()));
  }
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("baz"),
                              SpanFrom("not_found"));
    EXPECT_EQ("not_found", std::string(result.begin(), result.end()));
  }
}

namespace {
class TestObject {
 public:
  explicit TestObject(const std::string& message) : message_(message) {}

  const std::string& message() const { return message_; }

 private:
  std::string message_;
};
}  // namespace

TEST(FindByFirst, ObjectBySpan) {
  std::vector<std::pair<span<uint8_t>, std::unique_ptr<TestObject>>>
      sorted_object_by_span;
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo1"), std::make_unique<TestObject>("bar1")));
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo2"), std::make_unique<TestObject>("bar2")));
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo3"), std::make_unique<TestObject>("bar3")));
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("foo1"));
    ASSERT_TRUE(result);
    ASSERT_EQ("bar1", result->message());
  }
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("foo3"));
    ASSERT_TRUE(result);
    ASSERT_EQ("bar3", result->message());
  }
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("baz"));
    ASSERT_FALSE(result);
  }
}
}  // namespace crdtp
