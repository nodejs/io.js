// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/strings-storage.h"

#include <cstdio>

#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using StringsStorageWithIsolate = TestWithIsolate;

bool StringEq(const char* left, const char* right) {
  return strcmp(left, right) == 0;
}

TEST_F(StringsStorageWithIsolate, GetNameFromString) {
  StringsStorage storage;

  // One char strings are canonical on the v8 heap so use a 2 char string here.
  DirectHandle<String> str =
      isolate()->factory()->NewStringFromAsciiChecked("xy");
  const char* stored_str = storage.GetName(*str);
  CHECK(StringEq("xy", stored_str));

  // The storage should de-duplicate the underlying char arrays and return the
  // exact same pointer for equivalent input strings.
  const char* stored_str_twice = storage.GetName(*str);
  CHECK_EQ(stored_str, stored_str_twice);

  // Even if the input string was a different one on the v8 heap, if the char
  // array is the same, it should be de-duplicated.
  DirectHandle<String> str2 =
      isolate()->factory()->NewStringFromAsciiChecked("xy");
  CHECK_NE(*str, *str2);
  const char* stored_str_thrice = storage.GetName(*str2);
  CHECK_EQ(stored_str_twice, stored_str_thrice);
}

TEST_F(StringsStorageWithIsolate, GetNameFromSymbol) {
  StringsStorage storage;

  DirectHandle<Symbol> symbol = isolate()->factory()->NewSymbol();
  const char* stored_symbol = storage.GetName(*symbol);
  CHECK(StringEq("<symbol>", stored_symbol));

  DirectHandle<Symbol> symbol2 = isolate()->factory()->NewSymbol();
  CHECK_NE(*symbol, *symbol2);
  const char* stored_symbol2 = storage.GetName(*symbol2);
  CHECK_EQ(stored_symbol, stored_symbol2);
}

TEST_F(StringsStorageWithIsolate, GetConsName) {
  StringsStorage storage;

  DirectHandle<String> str =
      isolate()->factory()->NewStringFromAsciiChecked("xy");

  const char* empty_prefix_str = storage.GetConsName("", *str);
  CHECK(StringEq("xy", empty_prefix_str));

  const char* get_str = storage.GetConsName("get ", *str);
  CHECK(StringEq("get xy", get_str));
}

TEST_F(StringsStorageWithIsolate, GetNameFromInt) {
  StringsStorage storage;

  const char* stored_str = storage.GetName(0);
  CHECK(StringEq("0", stored_str));

  stored_str = storage.GetName(2147483647);
  CHECK(StringEq("2147483647", stored_str));

  stored_str = storage.GetName(std::numeric_limits<int>::min());
  char str_negative_int[12];
  snprintf(str_negative_int, sizeof(str_negative_int), "%d",
           std::numeric_limits<int>::min());
  CHECK(StringEq(str_negative_int, stored_str));
}

TEST_F(StringsStorageWithIsolate, Format) {
  StringsStorage storage;

  const char* xy = "xy";
  const char* stored_str = storage.GetFormatted("%s", xy);
  CHECK(StringEq("xy", stored_str));
  // Check that the string is copied.
  CHECK_NE(xy, stored_str);

  const char* formatted_str = storage.GetFormatted("%s / %s", xy, xy);
  CHECK(StringEq("xy / xy", formatted_str));

  // A different format specifier that results in the same string should share
  // the string in storage.
  const char* formatted_str2 = storage.GetFormatted("%s", "xy / xy");
  CHECK_EQ(formatted_str, formatted_str2);
}

TEST_F(StringsStorageWithIsolate, FormatAndGetShareStorage) {
  StringsStorage storage;

  DirectHandle<String> str =
      isolate()->factory()->NewStringFromAsciiChecked("xy");
  const char* stored_str = storage.GetName(*str);

  const char* formatted_str = storage.GetFormatted("%s", "xy");
  CHECK_EQ(stored_str, formatted_str);
}

TEST_F(StringsStorageWithIsolate, Refcounting) {
  StringsStorage storage;

  const char* a = storage.GetCopy("12");
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());

  const char* b = storage.GetCopy("12");
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());

  // Ensure that we deduplicate the string.
  CHECK_EQ(a, b);

  CHECK(storage.Release(a));
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());

  CHECK(storage.Release(b));
  CHECK_EQ(storage.GetStringCountForTesting(), 0);
  CHECK_EQ(0, storage.GetStringSize());

#if !DEBUG
  CHECK(!storage.Release("12"));
#endif  // !DEBUG

  // Verify that other constructors refcount as intended.
  const char* c = storage.GetFormatted("%d", 12);
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());

  const char* d = storage.GetName(12);
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());

  CHECK_EQ(c, d);

  CHECK(storage.Release(c));
  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(2, storage.GetStringSize());
  CHECK(storage.Release(d));
  CHECK_EQ(storage.GetStringCountForTesting(), 0);
  CHECK_EQ(0, storage.GetStringSize());

  CHECK(!storage.Release("12"));
}

TEST_F(StringsStorageWithIsolate, InvalidRelease) {
  StringsStorage storage;

  // If we attempt to release a string not being managed by the StringsStorage,
  // return false.
  CHECK(!storage.Release("12"));
}

TEST_F(StringsStorageWithIsolate, CopyAndConsShareStorage) {
  StringsStorage storage;

  DirectHandle<String> str =
      isolate()->factory()->NewStringFromAsciiChecked("foo");

  const char* copy_str = storage.GetCopy("get foo");
  const char* cons_str = storage.GetConsName("get ", *str);

  CHECK_EQ(storage.GetStringCountForTesting(), 1);
  CHECK_EQ(copy_str, cons_str);
}

}  // namespace internal
}  // namespace v8
