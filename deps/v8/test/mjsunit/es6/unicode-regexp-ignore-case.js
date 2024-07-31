// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Non-unicode use toUpperCase mappings.
assertFalse(/[\u00e5]/i.test("\u212b"));
assertFalse(/[\u212b]/i.test("\u00e5\u1234"));
assertFalse(/[\u212b]/i.test("\u00e5"));

assertTrue("\u212b".toLowerCase() == "\u00e5");
assertTrue("\u00c5".toLowerCase() == "\u00e5");
assertTrue("\u00e5".toUpperCase() == "\u00c5");

// Unicode uses case folding mappings.
assertTrue(/\u00e5/ui.test("\u212b"));
assertTrue(/\u00e5/ui.test("\u00c5"));
assertTrue(/\u00e5/ui.test("\u00e5"));
assertTrue(/\u00e5/ui.test("\u212b"));
assertTrue(/\u00c5/ui.test("\u00e5"));
assertTrue(/\u00c5/ui.test("\u212b"));
assertTrue(/\u00c5/ui.test("\u00c5"));
assertTrue(/\u212b/ui.test("\u00c5"));
assertTrue(/\u212b/ui.test("\u00e5"));
assertTrue(/\u212b/ui.test("\u212b"));

// Non-BMP.
assertFalse(/\u{10400}/i.test("\u{10428}"));
assertTrue(/\u{10400}/ui.test("\u{10428}"));
assertTrue(/\ud801\udc00/ui.test("\u{10428}"));
assertTrue(/[\u{10428}]/ui.test("\u{10400}"));
assertTrue(/[\ud801\udc28]/ui.test("\u{10400}"));
assertEquals(["\uff21\u{10400}"],
             /[\uff40-\u{10428}]+/ui.exec("\uff21\u{10400}abc"));
assertEquals(["abc"], /[^\uff40-\u{10428}]+/ui.exec("\uff21\u{10400}abc\uff23"));
assertEquals(["\uff53\u24bb"],
             /[\u24d5-\uff33]+/ui.exec("\uff54\uff53\u24bb\u24ba"));

// Full mappings are ignored.
assertFalse(/\u00df/ui.test("SS"));
assertFalse(/\u1f8d/ui.test("\u1f05\u03b9"));

// Simple mappings work.
assertTrue(/\u1f8d/ui.test("\u1f85"));

// Common mappings work.
assertTrue(/\u1f6b/ui.test("\u1f63"));

// Back references.
assertEquals(["\u00e5\u212b\u00c5", "\u00e5"],
             /(.)\1\1/ui.exec("\u00e5\u212b\u00c5"));
assertEquals(["\u{118aa}\u{118ca}", "\u{118aa}"],
             /(.)\1/ui.exec("\u{118aa}\u{118ca}"));

// Misc.
assertTrue(/\u00e5\u00e5\u00e5/ui.test("\u212b\u00e5\u00c5"));
assertTrue(/AB\u{10400}/ui.test("ab\u{10428}"));

// Non-Latin1 maps to Latin1.
assertEquals(["s"], /^\u017F/ui.exec("s"));
assertEquals(["s"], /^\u017F/ui.exec("s\u1234"));
assertEquals(["as"], /^a[\u017F]/ui.exec("as"));
assertEquals(["as"], /^a[\u017F]/ui.exec("as\u1234"));
