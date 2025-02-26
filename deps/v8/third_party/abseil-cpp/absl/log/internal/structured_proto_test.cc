//
// Copyright 2024 The Abseil Authors.
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

#include "absl/log/internal/structured_proto.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

using ::testing::TestWithParam;

struct StructuredProtoTestCase {
  std::string test_name;
  StructuredProtoField field;
  std::vector<char> expected_encoded_field;
};

using StructuredProtoTest = TestWithParam<StructuredProtoTestCase>;

TEST_P(StructuredProtoTest, Encoding) {
  const StructuredProtoTestCase& test_case = GetParam();

  // Greater than or equal to since BufferSizeForStructuredProtoField() is just
  // an estimate of the data size and not an exact measurement.
  ASSERT_GE(BufferSizeForStructuredProtoField(test_case.field),
            test_case.expected_encoded_field.size());

  std::vector<char> buf;
  buf.resize(1024);

  absl::Span<char> buf_span(buf);
  EXPECT_TRUE(EncodeStructuredProtoField(test_case.field, buf_span));
  size_t encoded_field_size = buf.size() - buf_span.size();

  ASSERT_EQ(encoded_field_size, test_case.expected_encoded_field.size());
  buf.resize(encoded_field_size);
  EXPECT_EQ(buf, test_case.expected_encoded_field);
}

INSTANTIATE_TEST_SUITE_P(
    StructuredProtoTestSuiteInstantiation,
    StructuredProtoTest,  // This is the name of your parameterized test
    testing::ValuesIn<StructuredProtoTestCase>({
        {
            "Varint",
            {
                42,
                StructuredProtoField::Value{
                    absl::in_place_type<StructuredProtoField::Varint>,
                    int32_t{23},
                },
            },
            {'\xD0', '\x02', '\x17'},
        },
        {
            "I64",
            {
                42,
                StructuredProtoField::Value{
                    absl::in_place_type<StructuredProtoField::I64>,
                    int64_t{23},
                },
            },
            {'\xD1', '\x02', '\x17', '\x00', '\x00', '\x00', '\x00', '\x00',
             '\x00', '\x00'},
        },
        {
            "LengthDelimited",
            {
                42,
                // Use a string_view so the terminating NUL is excluded.
                absl::string_view("Hello"),
            },
            {'\xD2', '\x02', '\x05', 'H', 'e', 'l', 'l', 'o'},
        },
        {
            "I32",
            {
                42,
                StructuredProtoField::Value{
                    absl::in_place_type<StructuredProtoField::I32>,
                    int32_t{23},
                },
            },
            {'\xD5', '\x02', '\x17', '\x00', '\x00', '\x00'},
        },
    }),
    [](const testing::TestParamInfo<StructuredProtoTest::ParamType>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
