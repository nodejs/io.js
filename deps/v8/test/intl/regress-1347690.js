// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparison to empty string could return zero for certain Unicode character.

// In all locales, some Unicode characters are ignorable.
// Unicode in C0
assertEquals(0, (new Intl.Collator('en')).compare("","\u0001"));
// SOFT HYPHEN
assertEquals(0, (new Intl.Collator('en')).compare("","\u00AD"));
// ARABIC SIGN SAMVAT
assertEquals(0, (new Intl.Collator('en')).compare("","\u0604"));

assertEquals(0, (new Intl.Collator('en')).compare("","\u0001\u0002\u00AD\u0604"));

// Default Thai collation ignores punctuation.
assertEquals(0, (new Intl.Collator('th')).compare(""," "));
assertEquals(0, (new Intl.Collator('th')).compare("","*"));
assertEquals(0, (new Intl.Collator('th', {ignorePunctuation: true})).compare(""," "));
assertEquals(0, (new Intl.Collator('th', {ignorePunctuation: true})).compare("","*"));
assertEquals(-1, (new Intl.Collator('th', {ignorePunctuation: false})).compare(""," "));
assertEquals(-1, (new Intl.Collator('th', {ignorePunctuation: false})).compare("","*"));

assertEquals(-1, (new Intl.Collator('en')).compare(""," "));
assertEquals(-1, (new Intl.Collator('en')).compare("","*"));
assertEquals(0, (new Intl.Collator('en', {ignorePunctuation: true})).compare(""," "));
assertEquals(0, (new Intl.Collator('en', {ignorePunctuation: true})).compare("","*"));
assertEquals(-1, (new Intl.Collator('en', {ignorePunctuation: false})).compare(""," "));
assertEquals(-1, (new Intl.Collator('en', {ignorePunctuation: false})).compare("","*"));
