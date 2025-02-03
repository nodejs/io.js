// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/torque/utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace torque {

TEST(TorqueUtils, FileUriDecodeIllegal) {
  EXPECT_EQ(FileUriDecode("http://wrong.scheme"), std::nullopt);
  EXPECT_EQ(FileUriDecode("file://wrong-escape%"), std::nullopt);
  EXPECT_EQ(FileUriDecode("file://another-wrong-escape%a"), std::nullopt);
  EXPECT_EQ(FileUriDecode("file://no-hex-escape%0g"), std::nullopt);
}

TEST(TorqueUtils, FileUriDecode) {
#ifdef V8_OS_WIN
  EXPECT_EQ(FileUriDecode("file:///c%3A/torque/base.tq").value(),
            "c:/torque/base.tq");
  EXPECT_EQ(FileUriDecode("file:///d%3a/lower/hex.txt").value(),
            "d:/lower/hex.txt");
#else
  EXPECT_EQ(FileUriDecode("file:///some/src/file.tq").value(),
            "/some/src/file.tq");
#endif
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
