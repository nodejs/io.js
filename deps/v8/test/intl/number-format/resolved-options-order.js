// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// https://tc39.es/ecma402/#table-numberformat-resolvedoptions-properties
assertEquals(
    "locale," +
    "numberingSystem," +
    "style," +
    "minimumIntegerDigits," +
    "minimumFractionDigits," +
    "maximumFractionDigits," +
    "useGrouping," +
    "notation," +
    "signDisplay," +
    "roundingIncrement," +
    "roundingMode," +
    "roundingPriority," +
    "trailingZeroDisplay",
    Object.keys((new Intl.NumberFormat("en")).resolvedOptions()).join(","));
