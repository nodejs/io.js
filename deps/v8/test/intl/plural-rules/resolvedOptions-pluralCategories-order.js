// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test the order of elements in the array pluralCategories
// returned by resolvedOptions()

assertArrayEquals(
    ["zero", "one", "two", "few", "many", "other"],
    (new Intl.PluralRules("ar")).resolvedOptions().pluralCategories,
    "ar");

assertArrayEquals(
    ["one", "two", "few", "other"],
    (new Intl.PluralRules("sl")).resolvedOptions().pluralCategories,
    "sl");

assertArrayEquals(
    ["one", "two", "other"],
    (new Intl.PluralRules("he")).resolvedOptions().pluralCategories,
    "he");

assertArrayEquals(
    ["one", "few", "many", "other"],
    (new Intl.PluralRules("ru")).resolvedOptions().pluralCategories,
    "ru");

assertArrayEquals(
    ["one", "two", "other"],
    (new Intl.PluralRules("se")).resolvedOptions().pluralCategories,
    "se");

assertArrayEquals(
    ["zero", "one", "two", "few", "many", "other"],
    (new Intl.PluralRules("cy")).resolvedOptions().pluralCategories,
    "cy");

assertArrayEquals(
    ["one", "two", "few", "many", "other"],
    (new Intl.PluralRules("br")).resolvedOptions().pluralCategories,
    "br");
