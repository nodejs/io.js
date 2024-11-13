// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Some edge cases that unibrow got wrong

assertEquals("𐐘", "𐑀".toUpperCase());
assertEquals("𐑀", "𐐘".toLowerCase());
assertEquals("σ", "Σ".toLowerCase());

// Some different paths in the ICU case conversion fastpath

assertEquals("σς", "\u03A3\u03A3".toLowerCase());
// Expand sharp s in latin1 fastpath
assertEquals("ASSB", "A\u00DFB".toUpperCase());
assertEquals("AB", "Ab".toUpperCase());
// Find first uppercase in fastpath
// Input length < a machine word size
assertEquals("ab", "ab".toLowerCase());
assertEquals("ab", "aB".toLowerCase());
assertEquals("AÜ", "aü".toUpperCase());
assertEquals("AÜ", "AÜ".toUpperCase());
assertEquals("aü", "aü".toLowerCase());
assertEquals("aü", "aÜ".toLowerCase());
assertEquals("aü", "AÜ".toLowerCase());
assertEquals("aü", "AÜ".toLowerCase());

// Input length >= a machine word size
assertEquals("abcdefghij", "abcdefghij".toLowerCase());
assertEquals("abcdefghij", "abcdefghiJ".toLowerCase());
assertEquals("abçdefghij", "abçdefghiJ".toLowerCase());
assertEquals("abçdefghij", "abÇdefghiJ".toLowerCase());
assertEquals("abcdefghiá", "abcdeFghiá".toLowerCase());
assertEquals("abcdefghiá", "abcdeFghiÁ".toLowerCase());

assertEquals("ABCDEFGHIJ", "ABCDEFGHIJ".toUpperCase());
assertEquals("ABCDEFGHIJ", "ABCDEFGHIj".toUpperCase());
assertEquals("ABÇDEFGHIJ", "ABÇDEFGHIj".toUpperCase());
assertEquals("ABÇDEFGHIJ", "ABçDEFGHIj".toUpperCase());
assertEquals("ABCDEFGHIÁ", "ABCDEfGHIÁ".toUpperCase());
assertEquals("ABCDEFGHIÁ", "ABCDEfGHIá".toUpperCase());


// Starts with fastpath, but switches to full Unicode path
// U+00FF is uppercased to U+0178.
assertEquals("AŸ", "aÿ".toUpperCase());
// U+00B5 (µ) is uppercased to U+039C (Μ)
assertEquals("AΜ", "aµ".toUpperCase());

// Buffer size increase
assertEquals("CSSBẶ", "cßbặ".toUpperCase());
assertEquals("FIFLFFIFFL", "\uFB01\uFB02\uFB03\uFB04".toUpperCase());
assertEquals("ABCÀCSSA", "abcàcßa".toUpperCase());
assertEquals("ABCDEFGHIÀCSSA", "ABCDEFGHIàcßa".toUpperCase());
assertEquals("ABCDEFGHIÀCSSA", "abcdeFghiàcßa".toUpperCase());

// OneByte input with buffer size increase: non-fast path
assertEquals("ABCSS", "abCß".toLocaleUpperCase("tr"));

// More comprehensive tests for "tr", "az" and "lt" are in
// test262/intl402/Strings/*

// Buffer size decrease with a single locale or locale list.
// In Turkic (tr, az), U+0307 preceded by Capital Letter I is dropped.
assertEquals("abci", "aBcI\u0307".toLocaleLowerCase("tr"));
assertEquals("abci", "aBcI\u0307".toLocaleLowerCase("az"));
assertEquals("abci", "aBcI\u0307".toLocaleLowerCase(["tr", "en"]));

// Cons string
assertEquals("abcijkl", ("aBcI" + "\u0307jkl").toLocaleLowerCase("tr"));
assertEquals("abcijkl",
             ("aB" + "cI" + "\u0307j" + "kl").toLocaleLowerCase("tr"));
assertEquals("abci\u0307jkl", ("aBcI" + "\u0307jkl").toLocaleLowerCase("en"));
assertEquals("abci\u0307jkl",
             ("aB" + "cI" + "\u0307j" + "kl").toLocaleLowerCase("en"));
assertEquals("abci\u0307jkl",
             ("aB" + "cI" + "\u0307j" + "kl").toLocaleLowerCase("fil"));
assertEquals("abci\u0307jkl", ("aBcI" + "\u0307jkl").toLowerCase());
assertEquals("abci\u0307jkl",
             ("aB" + "cI" + "\u0307j" + "kl").toLowerCase());
assertEquals("[object arraybuffer]",
    (new String(new ArrayBuffer())).toLocaleLowerCase("fil"));
assertEquals("[OBJECT ARRAYBUFFER]",
    (new String(new ArrayBuffer())).toLocaleUpperCase("fil"));

assertEquals("abcde", ("a" + "b" + "cde").toLowerCase());
assertEquals("ABCDE", ("a" + "b" + "cde").toUpperCase());
assertEquals("abcde", ("a" + "b" + "cde").toLocaleLowerCase());
assertEquals("ABCDE", ("a" + "b" + "cde").toLocaleUpperCase());
assertEquals("abcde", ("a" + "b" + "cde").toLocaleLowerCase("en"));
assertEquals("ABCDE", ("a" + "b" + "cde").toLocaleUpperCase("en"));
assertEquals("abcde", ("a" + "b" + "cde").toLocaleLowerCase("fil"));
assertEquals("ABCDE", ("a" + "b" + "cde").toLocaleUpperCase("fil"));
assertEquals("abcde", ("a" + "b" + "cde").toLocaleLowerCase("longlang"));
assertEquals("ABCDE", ("a" + "b" + "cde").toLocaleUpperCase("longlang"));

// "tr" and "az" should behave identically.
assertEquals("aBcI\u0307".toLocaleLowerCase("tr"),
             "aBcI\u0307".toLocaleLowerCase("az"));
// What matters is the first locale in the locale list.
assertEquals("aBcI\u0307".toLocaleLowerCase(["tr", "en", "fr"]),
             "aBcI\u0307".toLocaleLowerCase("tr"));
assertEquals("aBcI\u0307".toLocaleLowerCase(["en", "tr", "az"]),
             "aBcI\u0307".toLocaleLowerCase("en"));
assertEquals("aBcI\u0307".toLocaleLowerCase(["en", "tr", "az"]),
             "aBcI\u0307".toLowerCase());

// An empty locale list is the same as the default locale. Try these tests
// under Turkish and Greek locale.
assertEquals("aBcI\u0307".toLocaleLowerCase([]),
             "aBcI\u0307".toLocaleLowerCase());
assertEquals("aBcI\u0307".toLocaleLowerCase([]),
             "aBcI\u0307".toLocaleLowerCase(Intl.GetDefaultLocale));
assertEquals("άόύώ".toLocaleUpperCase([]), "άόύώ".toLocaleUpperCase());
assertEquals("άόύώ".toLocaleUpperCase([]),
             "άόύώ".toLocaleUpperCase(Intl.GetDefaultLocale));


// English/root locale keeps U+0307 (combining dot above).
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase("en"));
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase("en-GB"));
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase(["en", "tr"]));
assertEquals("abci\u0307", "aBcI\u0307".toLowerCase());

// Anything other than 'tr' and 'az' behave like root for U+0307.
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase("fil"));
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase("zh-Hant-TW"));

// Up to 8 chars are allowed for the primary language tag in BCP 47.
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase("longlang"));
assertEquals("ABCI\u0307", "aBcI\u0307".toLocaleUpperCase("longlang"));
assertEquals("abci\u0307", "aBcI\u0307".toLocaleLowerCase(["longlang", "tr"]));
assertEquals("ABCI\u0307", "aBcI\u0307".toLocaleUpperCase(["longlang", "tr"]));
assertThrows(() => "abc".toLocaleLowerCase("longlang2"), RangeError);
assertThrows(() => "abc".toLocaleUpperCase("longlang2"), RangeError);
assertThrows(() => "abc".toLocaleLowerCase(["longlang2", "en"]), RangeError);
assertThrows(() => "abc".toLocaleUpperCase(["longlang2", "en"]), RangeError);

// Greek uppercasing: not covered by intl402/String/*, yet. Tonos (U+0301) and
// other diacritic marks are dropped.  See
// http://bugs.icu-project.org/trac/ticket/5456#comment:19 for more examples.
// See also http://bugs.icu-project.org/trac/ticket/12845 .
assertEquals("Α", "α\u0301".toLocaleUpperCase("el"));
assertEquals("Α", "α\u0301".toLocaleUpperCase("el-GR"));
assertEquals("Α", "α\u0301".toLocaleUpperCase("el-Grek"));
assertEquals("Α", "α\u0301".toLocaleUpperCase("el-Grek-GR"));
assertEquals("Α", "ά".toLocaleUpperCase("el"));
assertEquals("ΑΟΫΩ", "άόύώ".toLocaleUpperCase("el"));
assertEquals("ΑΟΥ\u0308Ω", "α\u0301ο\u0301υ\u0301ω\u0301".toLocaleUpperCase("el"));
assertEquals("ΑΟΫΩ", "άόύώ".toLocaleUpperCase("el"));
assertEquals("ΟΕ", "Ό\u1f15".toLocaleUpperCase("el"));
assertEquals("ΟΕ", "Ο\u0301ε\u0314\u0301".toLocaleUpperCase("el"));
assertEquals("ΡΩΜΕΪΚΑ", "ρωμέικα".toLocaleUpperCase("el"));
assertEquals("ΜΑΪΟΥ, ΤΡΟΛΕΪ", "Μαΐου, τρόλεϊ".toLocaleUpperCase("el"));
assertEquals("ΤΟ ΕΝΑ Ή ΤΟ ΑΛΛΟ.", "Το ένα ή το άλλο.".toLocaleUpperCase("el"));

// Input and output are identical.
assertEquals("αβγδε", "αβγδε".toLocaleLowerCase("el"));
assertEquals("ΑΒΓΔΕ", "ΑΒΓΔΕ".toLocaleUpperCase("el"));
assertEquals("ΑΒΓΔΕАБ𝐀𝐁", "ΑΒΓΔΕАБ𝐀𝐁".toLocaleUpperCase("el"));
assertEquals("ABCDEÂÓḴ123", "ABCDEÂÓḴ123".toLocaleUpperCase("el"));
// ASCII-only or Latin-1 only: 1-byte
assertEquals("ABCDE123", "ABCDE123".toLocaleUpperCase("el"));
assertEquals("ABCDEÂÓ123", "ABCDEÂÓ123".toLocaleUpperCase("el"));

// To make sure that the input string is not overwritten in place.
var strings = ["abCdef", "αβγδε", "άόύώ", "аб"];
for (var s  of strings) {
  var backupAsArray = s.split("");
  var uppered = s.toLocaleUpperCase("el");
  assertEquals(s, backupAsArray.join(""));
}

// In other locales, U+0301 is preserved.
assertEquals("Α\u0301Ο\u0301Υ\u0301Ω\u0301",
             "α\u0301ο\u0301υ\u0301ω\u0301".toLocaleUpperCase("en"));
assertEquals("Α\u0301Ο\u0301Υ\u0301Ω\u0301",
             "α\u0301ο\u0301υ\u0301ω\u0301".toUpperCase());

// Plane 1; Deseret and Warang Citi Script.
assertEquals("\u{10400}\u{118A0}", "\u{10428}\u{118C0}".toUpperCase());
assertEquals("\u{10428}\u{118C0}", "\u{10400}\u{118A0}".toLowerCase());
// Mathematical Bold {Capital, Small} Letter A do not change.
assertEquals("\u{1D400}\u{1D41A}", "\u{1D400}\u{1D41A}".toUpperCase());
assertEquals("\u{1D400}\u{1D41A}", "\u{1D400}\u{1D41A}".toLowerCase());
// Plane 1; New characters in Unicode 8.0
assertEquals("\u{10C80}", "\u{10CC0}".toUpperCase());
assertEquals("\u{10CC0}", "\u{10C80}".toLowerCase());
assertEquals("\u{10C80}", "\u{10CC0}".toLocaleUpperCase());
assertEquals("\u{10CC0}", "\u{10C80}".toLocaleLowerCase());
assertEquals("\u{10C80}", "\u{10CC0}".toLocaleUpperCase(["tr"]));
assertEquals("\u{10C80}", "\u{10CC0}".toLocaleUpperCase(["tr"]));
assertEquals("\u{10CC0}", "\u{10C80}".toLocaleLowerCase());

// check fast path for Latin-1 supplement (U+00A0 ~ U+00FF)
var latin1Suppl = "\u00A0¡¢£¤¥¦§¨©ª«¬\u00AD®°±²³´µ¶·¸¹º»¼½¾¿" +
    "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
var latin1SupplLowercased = "\u00A0¡¢£¤¥¦§¨©ª«¬\u00AD®°±²³´µ¶·¸¹º»¼½¾¿" +
    "àáâãäåæçèéêëìíîïðñòóôõö×øùúûüýþßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";
var latin1SupplUppercased = "\u00A0¡¢£¤¥¦§¨©ª«¬\u00AD®°±²³´\u039C¶·¸¹º»¼½¾¿" +
    "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞSSÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ÷ØÙÚÛÜÝÞ\u0178";

assertEquals(latin1SupplLowercased, latin1Suppl.toLowerCase());
assertEquals(latin1SupplUppercased, latin1Suppl.toUpperCase());
assertEquals(latin1SupplLowercased, latin1Suppl.toLocaleLowerCase("de"));
assertEquals(latin1SupplUppercased, latin1Suppl.toLocaleUpperCase("de"));
assertEquals(latin1SupplLowercased, latin1Suppl.toLocaleLowerCase("el"));
assertEquals(latin1SupplUppercased, latin1Suppl.toLocaleUpperCase("el"));
assertEquals(latin1SupplUppercased, latin1Suppl.toLocaleUpperCase("tr"));
assertEquals(latin1SupplLowercased, latin1Suppl.toLocaleLowerCase("tr"));
assertEquals(latin1SupplUppercased, latin1Suppl.toLocaleUpperCase("az"));
assertEquals(latin1SupplLowercased, latin1Suppl.toLocaleLowerCase("az"));
assertEquals(latin1SupplUppercased, latin1Suppl.toLocaleUpperCase("lt"));
// Lithuanian need to have a dot-above for U+00CC(Ì) and U+00CD(Í) when
// lowercasing.
assertEquals("\u00A0¡¢£¤¥¦§¨©ª«¬\u00AD®°±²³´µ¶·¸¹º»¼½¾¿" +
    "àáâãäåæçèéêëi\u0307\u0300i\u0307\u0301îïðñòóôõö×øùúûüýþß" +
    "àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ",
    latin1Suppl.toLocaleLowerCase("lt"));
