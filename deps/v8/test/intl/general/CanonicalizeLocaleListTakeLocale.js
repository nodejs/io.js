// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Test NumberFormat will accept Intl.Locale as first parameter, or
// as in the array.

let tag = "zh-Hant-TW-u-nu-thai"
let l = new Intl.Locale(tag);

var nf;
// Test with String
assertDoesNotThrow(() => nf = new Intl.NumberFormat(tag));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with Array with one String
assertDoesNotThrow(() => nf = new Intl.NumberFormat([tag]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with Array with two String
assertDoesNotThrow(() => nf = new Intl.NumberFormat([tag, "en"]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with a Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat(l));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with an Array of one Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat([l]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with an Array of one Locale and a Sring
assertDoesNotThrow(() => nf = new Intl.NumberFormat([l, "en"]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test DateTimeFormat
var df;
assertDoesNotThrow(() => df = new Intl.DateTimeFormat(tag));
assertEquals(tag, df.resolvedOptions().locale);
assertDoesNotThrow(() => df = new Intl.DateTimeFormat([tag]));
assertEquals(tag, df.resolvedOptions().locale);

// Test RelativeTimeFormat
var rtf;
assertDoesNotThrow(() => rtf = new Intl.RelativeTimeFormat(tag));
assertEquals(tag, rtf.resolvedOptions().locale);
assertDoesNotThrow(() => rtf = new Intl.RelativeTimeFormat([tag]));
assertEquals(tag, rtf.resolvedOptions().locale);

// Test ListFormat
tag = "zh-Hant-TW"
var lf;
assertDoesNotThrow(() => lf = new Intl.ListFormat(tag));
assertEquals(tag, lf.resolvedOptions().locale);
assertDoesNotThrow(() => lf = new Intl.ListFormat([tag]));
assertEquals(tag, lf.resolvedOptions().locale);

// Test Collator
var col;
assertDoesNotThrow(() => col = new Intl.Collator(tag));
assertEquals(tag, lf.resolvedOptions().locale);
assertDoesNotThrow(() => col = new Intl.Collator([tag]));
assertEquals(tag, lf.resolvedOptions().locale);

// Test monkey patching won't impact the result.

class MyLocale extends Intl.Locale {
  constructor(tag, options) {
    super(tag, options);
  }
  toString() {
    // this should not get called.
    fail("toString should not be called")
  }
}

let myLocale = new MyLocale(tag);

// Test with a Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat(myLocale));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with an Array of one Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat([myLocale]));
assertEquals(tag, nf.resolvedOptions().locale);

var res = Intl.getCanonicalLocales(myLocale);
assertEquals(1, res.length);
assertEquals(tag, res[0]);

res = Intl.getCanonicalLocales([myLocale, "fr"]);
assertEquals(2, res.length);
assertEquals(tag, res[0]);
assertEquals("fr", res[1]);

res = Intl.getCanonicalLocales(["fr", myLocale]);
assertEquals(2, res.length);
assertEquals("fr", res[0]);
assertEquals(tag, res[1]);
