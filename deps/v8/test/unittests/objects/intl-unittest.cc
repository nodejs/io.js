// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_INTL_SUPPORT

#include "src/objects/intl-objects.h"
#include "src/objects/js-break-iterator.h"
#include "src/objects/js-collator-inl.h"
#include "src/objects/js-date-time-format.h"
#include "src/objects/js-list-format.h"
#include "src/objects/js-number-format.h"
#include "src/objects/js-plural-rules.h"
#include "src/objects/js-relative-time-format.h"
#include "src/objects/js-segmenter.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "unicode/coll.h"

namespace v8 {
namespace internal {

using IntlTest = TestWithContext;

// This operator overloading enables CHECK_EQ to be used with
// std::vector<NumberFormatSpan>
bool operator==(const NumberFormatSpan& lhs, const NumberFormatSpan& rhs) {
  return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}
template <typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(
    std::basic_ostream<_CharT, _Traits>& self, const NumberFormatSpan& part) {
  return self << "{" << part.field_id << "," << part.begin_pos << ","
              << part.end_pos << "}";
}

void test_flatten_regions_to_parts(
    const std::vector<NumberFormatSpan>& regions,
    const std::vector<NumberFormatSpan>& expected_parts) {
  std::vector<NumberFormatSpan> mutable_regions = regions;
  std::vector<NumberFormatSpan> parts = FlattenRegionsToParts(&mutable_regions);
  CHECK_EQ(expected_parts, parts);
}

TEST_F(IntlTest, FlattenRegionsToParts) {
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 10),
          NumberFormatSpan(1, 2, 8),
          NumberFormatSpan(2, 2, 4),
          NumberFormatSpan(3, 6, 8),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 2),
          NumberFormatSpan(2, 2, 4),
          NumberFormatSpan(1, 4, 6),
          NumberFormatSpan(3, 6, 8),
          NumberFormatSpan(-1, 8, 10),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 1),
          NumberFormatSpan(0, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
          NumberFormatSpan(-1, 0, 1),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
      });
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 10),
          NumberFormatSpan(1, 0, 1),
          NumberFormatSpan(2, 0, 2),
          NumberFormatSpan(3, 0, 3),
          NumberFormatSpan(4, 0, 4),
          NumberFormatSpan(5, 0, 5),
          NumberFormatSpan(15, 5, 10),
          NumberFormatSpan(16, 6, 10),
          NumberFormatSpan(17, 7, 10),
          NumberFormatSpan(18, 8, 10),
          NumberFormatSpan(19, 9, 10),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(1, 0, 1),
          NumberFormatSpan(2, 1, 2),
          NumberFormatSpan(3, 2, 3),
          NumberFormatSpan(4, 3, 4),
          NumberFormatSpan(5, 4, 5),
          NumberFormatSpan(15, 5, 6),
          NumberFormatSpan(16, 6, 7),
          NumberFormatSpan(17, 7, 8),
          NumberFormatSpan(18, 8, 9),
          NumberFormatSpan(19, 9, 10),
      });

  //              :          4
  //              :      22 33    3
  //              :      11111   22
  // input regions:     0000000  111
  //              :     ------------
  // output parts:      0221340--231
  test_flatten_regions_to_parts(
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(-1, 0, 12),
          NumberFormatSpan(0, 0, 7),
          NumberFormatSpan(1, 9, 12),
          NumberFormatSpan(1, 1, 6),
          NumberFormatSpan(2, 9, 11),
          NumberFormatSpan(2, 1, 3),
          NumberFormatSpan(3, 10, 11),
          NumberFormatSpan(3, 4, 6),
          NumberFormatSpan(4, 5, 6),
      },
      std::vector<NumberFormatSpan>{
          NumberFormatSpan(0, 0, 1),
          NumberFormatSpan(2, 1, 3),
          NumberFormatSpan(1, 3, 4),
          NumberFormatSpan(3, 4, 5),
          NumberFormatSpan(4, 5, 6),
          NumberFormatSpan(0, 6, 7),
          NumberFormatSpan(-1, 7, 9),
          NumberFormatSpan(2, 9, 10),
          NumberFormatSpan(3, 10, 11),
          NumberFormatSpan(1, 11, 12),
      });
}

TEST_F(IntlTest, GetStringOption) {
  Handle<JSObject> options = i_isolate()->factory()->NewJSObjectWithNullProto();
  {
    // No value found
    std::unique_ptr<char[]> result = nullptr;
    Maybe<bool> found =
        GetStringOption(i_isolate(), options, "foo", std::vector<const char*>{},
                        "service", &result);
    CHECK(!found.FromJust());
    CHECK_NULL(result);
  }

  Handle<String> key = i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  LookupIterator it(i_isolate(), options, key);
  CHECK(Object::SetProperty(&it, Handle<Smi>(Smi::FromInt(42), i_isolate()),
                            StoreOrigin::kMaybeKeyed,
                            Just(ShouldThrow::kThrowOnError))
            .FromJust());

  {
    // Value found
    std::unique_ptr<char[]> result = nullptr;
    Maybe<bool> found =
        GetStringOption(i_isolate(), options, "foo", std::vector<const char*>{},
                        "service", &result);
    CHECK(found.FromJust());
    CHECK_NOT_NULL(result);
    CHECK_EQ(0, strcmp("42", result.get()));
  }

  {
    // No expected value in values array
    std::unique_ptr<char[]> result = nullptr;
    Maybe<bool> found =
        GetStringOption(i_isolate(), options, "foo",
                        std::vector<const char*>{"bar"}, "service", &result);
    CHECK(i_isolate()->has_exception());
    CHECK(found.IsNothing());
    CHECK_NULL(result);
    i_isolate()->clear_exception();
  }

  {
    // Expected value in values array
    std::unique_ptr<char[]> result = nullptr;
    Maybe<bool> found =
        GetStringOption(i_isolate(), options, "foo",
                        std::vector<const char*>{"42"}, "service", &result);
    CHECK(found.FromJust());
    CHECK_NOT_NULL(result);
    CHECK_EQ(0, strcmp("42", result.get()));
  }
}

TEST_F(IntlTest, GetBoolOption) {
  Handle<JSObject> options = i_isolate()->factory()->NewJSObjectWithNullProto();
  {
    bool result = false;
    Maybe<bool> found =
        GetBoolOption(i_isolate(), options, "foo", "service", &result);
    CHECK(!found.FromJust());
    CHECK(!result);
  }

  Handle<String> key = i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  {
    LookupIterator it(i_isolate(), options, key);
    Handle<Object> false_value =
        handle(i::ReadOnlyRoots(i_isolate()).false_value(), i_isolate());
    Object::SetProperty(i_isolate(), options, key, false_value,
                        StoreOrigin::kMaybeKeyed,
                        Just(ShouldThrow::kThrowOnError))
        .Assert();
    bool result = false;
    Maybe<bool> found =
        GetBoolOption(i_isolate(), options, "foo", "service", &result);
    CHECK(found.FromJust());
    CHECK(!result);
  }

  {
    LookupIterator it(i_isolate(), options, key);
    Handle<Object> true_value =
        handle(i::ReadOnlyRoots(i_isolate()).true_value(), i_isolate());
    Object::SetProperty(i_isolate(), options, key, true_value,
                        StoreOrigin::kMaybeKeyed,
                        Just(ShouldThrow::kThrowOnError))
        .Assert();
    bool result = false;
    Maybe<bool> found =
        GetBoolOption(i_isolate(), options, "foo", "service", &result);
    CHECK(found.FromJust());
    CHECK(result);
  }
}

TEST_F(IntlTest, GetAvailableLocales) {
  std::set<std::string> locales;

  locales = JSV8BreakIterator::GetAvailableLocales();
  CHECK(locales.count("en-US"));
  CHECK(!locales.count("abcdefg"));

  locales = JSCollator::GetAvailableLocales();
  CHECK(locales.count("en-US"));

  locales = JSDateTimeFormat::GetAvailableLocales();
  CHECK(locales.count("en-US"));

  locales = JSListFormat::GetAvailableLocales();
  CHECK(locales.count("en-US"));

  locales = JSNumberFormat::GetAvailableLocales();
  CHECK(locales.count("en-US"));

  locales = JSPluralRules::GetAvailableLocales();
  CHECK(locales.count("en"));

  locales = JSRelativeTimeFormat::GetAvailableLocales();
  CHECK(locales.count("en-US"));

  locales = JSSegmenter::GetAvailableLocales();
  CHECK(locales.count("en-US"));
  CHECK(!locales.count("abcdefg"));
}

// Tests that the LocaleCompare fast path and generic path return the same
// comparison results for all ASCII strings.
TEST_F(IntlTest, StringLocaleCompareFastPath) {
  // We compare all single-char strings of printable ASCII characters.
  std::vector<Handle<String>> ascii_strings;
  for (int c = 0; c <= 0x7F; c++) {
    if (!std::isprint(c)) continue;
    ascii_strings.push_back(
        i_isolate()->factory()->LookupSingleCharacterStringFromCode(c));
  }

  Handle<JSFunction> collator_constructor = Handle<JSFunction>(
      JSFunction::cast(
          i_isolate()->context()->native_context()->intl_collator_function()),
      i_isolate());
  Handle<Map> constructor_map =
      JSFunction::GetDerivedMap(i_isolate(), collator_constructor,
                                collator_constructor)
          .ToHandleChecked();
  Handle<Object> options(ReadOnlyRoots(i_isolate()).undefined_value(),
                         i_isolate());
  static const char* const kMethodName = "StringLocaleCompareFastPath";

  // For all fast locales, exhaustively compare within the printable ASCII
  // range.
  const std::set<std::string>& locales = JSCollator::GetAvailableLocales();
  for (const std::string& locale : locales) {
    Handle<String> locale_string =
        i_isolate()->factory()->NewStringFromAsciiChecked(locale.c_str());

    if (Intl::CompareStringsOptionsFor(i_isolate()->AsLocalIsolate(),
                                       locale_string, options) !=
        Intl::CompareStringsOptions::kTryFastPath) {
      continue;
    }

    Handle<JSCollator> collator =
        JSCollator::New(i_isolate(), constructor_map, locale_string, options,
                        kMethodName)
            .ToHandleChecked();

    for (size_t i = 0; i < ascii_strings.size(); i++) {
      Handle<String> lhs = ascii_strings[i];
      for (size_t j = i + 1; j < ascii_strings.size(); j++) {
        Handle<String> rhs = ascii_strings[j];
        CHECK_EQ(
            Intl::CompareStrings(i_isolate(), *collator->icu_collator()->raw(),
                                 lhs, rhs, Intl::CompareStringsOptions::kNone),
            Intl::CompareStrings(i_isolate(), *collator->icu_collator()->raw(),
                                 lhs, rhs,
                                 Intl::CompareStringsOptions::kTryFastPath));
      }
    }
  }
}

TEST_F(IntlTest, IntlMathematicalValueFromString) {
  struct TestCase {
    bool is_nan;
    bool is_minus_zero;
    bool is_negative;
    bool is_negative_infinity;
    bool is_positive_infinity;
    bool is_mathematical_value;
    const char* string;
  } cases[] = {
      {false, false, false, false, false, true, "+1"},
      {false, false, false, false, false, true,
       "+1234567890123456789012345678901234567890"},
      {false, false, false, false, true, false,
       "+1234567890123456789012345678901234567890e987654321"},
      {false, false, false, false, true, false,
       "    +1234567890123456789012345678901234567890e987654321  "},
      {true, false, false, false, false, false,
       "    +12   345 67  "},  // space between digit is invalid
      {true, false, false, false, false, false,
       "    -12   345 67  "},  // space between digit is invalid
      {false, false, false, false, false, true,
       "1234567890123456789012345678901234567890"},
      {false, false, false, false, false, true,
       "+.1234567890123456789012345678901234567890"},
      {false, false, false, false, false, true,
       ".1234567890123456789012345678901234567890"},
      {false, false, false, false, false, true, ".1234567890123456789e123"},
      {false, false, false, false, false, true, ".1234567890123456789E123"},
      {false, false, false, false, false, true, ".1234567890123456789e+123"},
      {false, false, false, false, false, true, ".1234567890123456789E+123"},
      {false, false, false, false, false, true, ".1234567890123456789e-0123"},
      {false, false, false, false, false, true, ".1234567890123456789E-0123"},
      {false, false, false, false, false, true,
       "1234567890123456789012345678901234567.890"},
      {false, false, false, false, false, true,
       "1234567890123456789012345678901234567890."},
      {true, false, false, false, false, false,
       "1234567.90123456789012345678901234567.890"},  // two '.'
      {true, false, false, false, false, false,
       ".1234567890123456789e12.3"},  // two '.'
      {false, false, true, false, false, true, "-1"},
      {false, false, true, false, false, true, "-1e33  "},
      {false, false, true, false, false, true, "  -0.21e33"},
      {false, false, false, false, false, true, "  0.21e33"},
      {false, true, false, false, false, false, "-0"},
      {false, false, false, false, false, true, "1"},
      {false, false, true, false, false, true, "  -1234.567e-20  "},
      {false, true, false, false, false, false, "  -1234.567e-9876  "},
      {false, false, false, false, true, false, "  Infinity "},
      {false, false, true, true, false, false, "        -Infinity "},
      {true, false, false, false, false, false, "yz"},  // not digits
      {false, false, true, false, false, true,
       "  -12345678901234567890122345.6778901234567890e234 "},
      {false, false, false, false, false, true,
       "  12345678901234567890122345.6778901234567890e-234 "},
      {false, false, false, false, false, true, "  0b01010001 "},
      {false, false, false, false, false, true, "  0B01010001 "},
      {true, false, false, false, false, false,
       "  -0b01010001 "},  // invalid binary becaues of -
      {true, false, false, false, false, false,
       "  -0B01010001 "},  // invalid binary becaues of -
      {true, false, false, false, false, false,
       "  0b01010002 "},  // invalid binary becaues of 2
      {true, false, false, false, false, false,
       "  0B01010003 "},  // invalid binary becaues of 3
      {false, false, false, false, false, true, "  0o01234567 "},
      {false, false, false, false, false, true, "  0O76543210 "},
      {true, false, false, false, false, false,
       "  -0o01234567 "},  // invalid oct becaues of -
      {true, false, false, false, false, false,
       "  -0O76543210 "},  // invalid oct becaues of -
      {true, false, false, false, false, false,
       "  0o012345678 "},  // invalid oct becaues of 8
      {true, false, false, false, false, false,
       "  0O765432108 "},  // invalid oct becaues of 8
      {false, false, false, false, false, true, "  0x123456789aBcDeF "},
      {false, false, false, false, false, true, "  0X123456789AbCdEf "},
      {true, false, false, false, false, false,
       "  -0x123456789aBcDeF "},  // invalid hex because of -
      {true, false, false, false, false, false,
       "  -0X123456789AbCdEf "},  // invalid hex because of -
      {true, false, false, false, false, false,
       "  0x012345678xyz "},  // invalid hex because xyz
      {true, false, false, false, false, false,
       "  0X765432108xyz "},  // invalid hex because xyz
  };
  for (auto& cas : cases) {
    IntlMathematicalValue x =
        IntlMathematicalValue::From(
            i_isolate(),
            i_isolate()->factory()->NewStringFromAsciiChecked(cas.string))
            .ToChecked();
    CHECK_EQ(x.IsNaN(), cas.is_nan);
  }
}

TEST_F(IntlTest, IntlMathematicalValueFromBigInt) {
  struct TestCase {
    bool is_negative;
    const char* bigint_string;
  } cases[] = {
      {false, "12"},
      {false, "12345678901234567890123456789012345678901234567890"},
      {true, "-12345678901234567890123456789012345678901234567890"},
      {false, "0"},
      {true, "-20"},
  };
  for (auto& cas : cases) {
    printf("%s\n", cas.bigint_string);
    Handle<String> str =
        i_isolate()->factory()->NewStringFromAsciiChecked(cas.bigint_string);
    IntlMathematicalValue x =
        IntlMathematicalValue::From(
            i_isolate(), BigInt::FromObject(i_isolate(), str).ToHandleChecked())
            .ToChecked();
    CHECK_EQ(x.IsNaN(), false);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_INTL_SUPPORT
