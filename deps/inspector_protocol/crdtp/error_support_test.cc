// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "error_support.h"

#include <string>
#include <vector>

#include "test_platform.h"

namespace crdtp {
TEST(ErrorSupportTest, Empty) {
  ErrorSupport errors;
  EXPECT_TRUE(errors.Errors().empty());
}

TEST(ErrorSupportTest, Nesting) {
  ErrorSupport errors;
  // Enter field foo, inter element at index 42, enter field bar, and encounter
  // an error there ("something wrong").
  errors.Push();
  errors.SetName("foo");
  errors.Push();
  errors.SetIndex(42);
  errors.Push();
  errors.SetName("bar_sibling");
  errors.SetName("bar");
  errors.AddError("something wrong");
  errors.Pop();  // bar
  errors.Pop();  // 42
  // The common case is actually that we'll enter some field, set the name
  // or index, and leave without ever producing an error.
  errors.Push();
  errors.SetName("no_error_here");
  errors.Pop();  // no_error_here
  errors.Push();
  errors.SetName("bang");
  errors.AddError("one last error");
  errors.Pop();  // bang
  errors.Pop();  // foo
  std::string out(errors.Errors().begin(), errors.Errors().end());
  EXPECT_EQ("foo.42.bar: something wrong; foo.bang: one last error", out);
}
}  // namespace crdtp
