// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test the order of the .*Digits in resolvedOptions()
// [[MinimumIntegerDigits]]     "minimumIntegerDigits"
// [[MinimumFractionDigits]]    "minimumFractionDigits"
// [[MaximumFractionDigits]]    "maximumFractionDigits"
// [[MinimumSignificantDigits]] "minimumSignificantDigits"
// [[MaximumSignificantDigits]] "maximumSignificantDigits"
let keys = Object.keys(
    (new Intl.NumberFormat(undefined, {notation:"compact"})
     .resolvedOptions()));

assertTrue(keys.indexOf("minimumIntegerDigits") <
           keys.indexOf("minimumFractionDigits",
           "minimumIntegerDigits should be before minimumFractionDigits"));
assertTrue(keys.indexOf("minimumFractionDigits") <
           keys.indexOf("maximumFractionDigits"),
           "minimumFractionDigits should be before maximumFractionDigits");
assertTrue(keys.indexOf("maximumFractionDigits") <
           keys.indexOf("minimumSignificantDigits"),
           "maximumFractionDigits should be before minimumSignificantDigits");
assertTrue(keys.indexOf("minimumSignificantDigits") <
           keys.indexOf("maximumSignificantDigits"),
           "minimumSignificantDigits should be before maximumSignificantDigits");
