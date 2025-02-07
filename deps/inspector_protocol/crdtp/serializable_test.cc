// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <string>

#include "serializable.h"
#include "test_platform.h"

namespace crdtp {
// =============================================================================
// Serializable - An object to be emitted as a sequence of bytes.
// =============================================================================

namespace {
// Tests ::Serialize (which invokes ::AppendSerialized).
class SimpleExample : public Serializable {
 public:
  explicit SimpleExample(const std::vector<uint8_t>& contents)
      : contents_(contents) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    out->insert(out->end(), contents_.begin(), contents_.end());
  }

 private:
  std::vector<uint8_t> contents_;
};
}  // namespace

TEST(SerializableTest, YieldsContents) {
  std::vector<uint8_t> contents = {1, 2, 3};
  SimpleExample foo(contents);
  foo.AppendSerialized(&contents);  // Yields contents by appending.
  EXPECT_THAT(contents, testing::ElementsAre(1, 2, 3, 1, 2, 3));
  // Yields contents by returning.
  EXPECT_THAT(foo.Serialize(), testing::ElementsAre(1, 2, 3));
}
}  // namespace crdtp
