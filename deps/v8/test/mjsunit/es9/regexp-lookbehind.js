// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function checkEquals(array, subject, regexp) {
  // Do it once with the interpreter.
  var result = subject.match(regexp);
  assertEquals(array, result);
  // Do it again with the machine code.
  result = subject.match(regexp);
  assertEquals(array, result);
}

function checkExec(array, regexp, subject) {
  // Do it once with the interpreter.
  var result = regexp.exec(subject);
  assertEquals(array, result);
  // Do it again with the machine code.
  result = regexp.exec(subject);
  assertEquals(array, result);
}

function checkNull(subject, regexp) {
  // Do it once with the interpreter.
  var result = subject.match(regexp);
  assertNull(result);
  // Do it again with the machine code.
  result = subject.match(regexp);
  assertNull(result);
}

// Simple fixed-length matches.
checkEquals(["a"], "a", /^.(?<=a)/);
checkNull("b", /^.(?<=a)/);
checkEquals(["foo"], "foo1", /^f..(?<=.oo)/);
checkEquals(["foo"], "foo2", /^f\w\w(?<=\woo)/);
checkNull("boo", /^f\w\w(?<=\woo)/);
checkNull("fao", /^f\w\w(?<=\woo)/);
checkNull("foa", /^f\w\w(?<=\woo)/);
checkEquals(["def"], "abcdef", /(?<=abc)\w\w\w/);
checkEquals(["def"], "abcdef", /(?<=a.c)\w\w\w/);
checkEquals(["def"], "abcdef", /(?<=a\wc)\w\w\w/);
checkEquals(["cde"], "abcdef", /(?<=a[a-z])\w\w\w/);
checkEquals(["def"], "abcdef", /(?<=a[a-z][a-z])\w\w\w/);
checkEquals(["def"], "abcdef", /(?<=a[a-z]{2})\w\w\w/);
checkEquals(["bcd"], "abcdef", /(?<=a{1})\w\w\w/);
checkEquals(["cde"], "abcdef", /(?<=a{1}b{1})\w\w\w/);
checkEquals(["def"], "abcdef", /(?<=a{1}[a-z]{2})\w\w\w/);

// Variable-length matches.
checkEquals(["def"], "abcdef", /(?<=[a|b|c]*)[^a|b|c]{3}/);
checkEquals(["def"], "abcdef", /(?<=\w*)[^a|b|c]{3}/);

// Start of line matches.
checkEquals(["def"], "abcdef", /(?<=^abc)def/);
checkEquals(["def"], "abcdef", /(?<=^[a-c]{3})def/);
checkEquals(["def"], "abcabcdef", /(?<=^[a-c]{6})def/);
checkEquals(["def"], "xyz\nabcdef", /(?<=^[a-c]{3})def/m);
checkEquals(["ab", "cd", "efg"], "ab\ncd\nefg", /(?<=^)\w+/gm);
checkEquals(["ab", "cd", "efg"], "ab\ncd\nefg", /\w+(?<=$)/gm);
checkEquals(["ab", "cd", "efg"], "ab\ncd\nefg", /(?<=^)\w+(?<=$)/gm);
checkNull("abcdef", /(?<=^[^a-c]{3})def/);
checkNull("foooo", /"^foooo(?<=^o+)$/);
checkNull("foooo", /"^foooo(?<=^o*)$/);
checkEquals(["foo"], "foo", /^foo(?<=^fo+)$/);
checkEquals(["foooo"], "foooo", /^foooo(?<=^fo*)/);
checkEquals(["foo", "f"], "foo", /^(f)oo(?<=^\1o+)$/);
checkEquals(["foo", "f"], "foo", /^(f)oo(?<=^\1o+)$/i);
checkEquals(["foo\u1234", "f"], "foo\u1234", /^(f)oo(?<=^\1o+).$/i);
checkEquals(["def"], "abcdefdef", /(?<=^\w+)def/);
checkEquals(["def", "def"], "abcdefdef", /(?<=^\w+)def/g);

// Word boundary matches.
checkEquals(["def"], "abc def", /(?<=\b)[d-f]{3}/);
checkEquals(["def"], "ab cdef", /(?<=\B)\w{3}/);
checkEquals(["def"], "ab cdef", /(?<=\B)(?<=c(?<=\w))\w{3}/);
checkNull("abcdef", /(?<=\b)[d-f]{3}/);

// Negative lookbehind.
checkEquals(["abc"], "abcdef", /(?<!abc)\w\w\w/);
checkEquals(["abc"], "abcdef", /(?<!a.c)\w\w\w/);
checkEquals(["abc"], "abcdef", /(?<!a\wc)\w\w\w/);
checkEquals(["abc"], "abcdef", /(?<!a[a-z])\w\w\w/);
checkEquals(["abc"], "abcdef", /(?<!a[a-z]{2})\w\w\w/);
checkNull("abcdef", /(?<!abc)def/);
checkNull("abcdef", /(?<!a.c)def/);
checkNull("abcdef", /(?<!a\wc)def/);
checkNull("abcdef", /(?<!a[a-z][a-z])def/);
checkNull("abcdef", /(?<!a[a-z]{2})def/);
checkNull("abcdef", /(?<!a{1}b{1})cde/);
checkNull("abcdef", /(?<!a{1}[a-z]{2})def/);

// Capturing matches.
checkEquals(["def", "c"], "abcdef", /(?<=(c))def/);
checkEquals(["def", "bc"], "abcdef", /(?<=(\w{2}))def/);
checkEquals(["def", "bc", "c"], "abcdef", /(?<=(\w(\w)))def/);
checkEquals(["def", "a"], "abcdef", /(?<=(\w){3})def/);
checkEquals(["d", "bc", undefined], "abcdef", /(?<=(bc)|(cd))./);
checkEquals(["c", "a", undefined],
            "abcdef", /(?<=([ab]{1,2})\D|(abc))\w/);
checkEquals(["ab", "a", "b"], "abcdef", /\D(?<=([ab]+))(\w)/);
checkEquals(["c", "d"], "abcdef", /(?<=b|c)\w/g);
checkEquals(["cd", "ef"], "abcdef", /(?<=[b-e])\w{2}/g);

// Captures inside negative lookbehind. (They never capture.)
checkEquals(["de", undefined], "abcdef", /(?<!(^|[ab]))\w{2}/);

// Nested lookaround.
checkEquals(["ef"], "abcdef", /(?<=ab(?=c)\wd)\w\w/);
checkEquals(["ef", "bc"], "abcdef", /(?<=a(?=([^a]{2})d)\w{3})\w\w/);
checkEquals(["ef", "bc"],
            "abcdef", /(?<=a(?=([bc]{2}(?<!a{2}))d)\w{3})\w\w/);
checkNull("abcdef", /(?<=a(?=([bc]{2}(?<!a*))d)\w{3})\w\w/);
checkEquals(["faaa"], "faaao", /^faaao?(?<=^f[oa]+(?=o))/);

// Back references.
checkEquals(["b", "b", "bb"], "abb", /(.)(?<=(\1\1))/);
checkEquals(["B", "B", "bB"], "abB", /(.)(?<=(\1\1))/i);
checkEquals(["aB", "aB", "a"], "aabAaBa", /((\w)\w)(?<=\1\2\1)/i);
checkEquals(["Ba", "Ba", "a"], "aabAaBa", /(\w(\w))(?<=\1\2\1)/i);
checkEquals(["b", "b", "B"], "abaBbAa", /(?=(\w))(?<=(\1))./i);
checkEquals(["foo", "'", "foo"], "  'foo'  ", /(?<=(.))(\w+)(?=\1)/);
checkEquals(["foo", "\"", "foo"], "  \"foo\"  ", /(?<=(.))(\w+)(?=\1)/);
checkNull("  .foo\"  ", /(?<=(.))(\w+)(?=\1)/);
checkNull("ab", /(.)(?<=\1\1\1)/);
checkNull("abb", /(.)(?<=\1\1\1)/);
checkEquals(["b", "b"], "abbb", /(.)(?<=\1\1\1)/);
checkNull("ab", /(..)(?<=\1\1\1)/);
checkNull("abb", /(..)(?<=\1\1\1)/);
checkNull("aabb", /(..)(?<=\1\1\1)/);
checkNull("abab", /(..)(?<=\1\1\1)/);
checkNull("fabxbab", /(..)(?<=\1\1\1)/);
checkNull("faxabab", /(..)(?<=\1\1\1)/);
checkEquals(["ab", "ab"], "fababab", /(..)(?<=\1\1\1)/);

// Back references to captures inside the lookbehind.
checkEquals(["d", "C"], "abcCd", /(?<=\1(\w))d/i);
checkEquals(["d", "x"], "abxxd", /(?<=\1([abx]))d/);
checkEquals(["c", "ab"], "ababc", /(?<=\1(\w+))c/);
checkEquals(["c", "b"], "ababbc", /(?<=\1(\w+))c/);
checkNull("ababdc", /(?<=\1(\w+))c/);
checkEquals(["c", "abab"], "ababc", /(?<=(\w+)\1)c/);

// Alternations are tried left to right,
// and we do not backtrack into a lookbehind.
checkEquals(["xabcd", "cd", ""], "xabcd", /.*(?<=(..|...|....))(.*)/);
checkEquals(["xabcd", "bcd", ""], "xabcd", /.*(?<=(xx|...|....))(.*)/);
checkEquals(["xxabcd", "bcd", ""], "xxabcd", /.*(?<=(xx|...))(.*)/);
checkEquals(["xxabcd", "xx", "abcd"], "xxabcd", /.*(?<=(xx|xxx))(.*)/);

// We do not backtrack into a lookbehind.
// The lookbehind captures "abc" so that \1 does not match. We do not backtrack
// to capture only "bc" in the lookbehind.
checkNull("abcdbc", /(?<=([abc]+)).\1/);

// Greedy loop.
checkEquals(["c", "bbbbbb"], "abbbbbbc", /(?<=(b+))c/);
checkEquals(["c", "b1234"], "ab1234c", /(?<=(b\d+))c/);
checkEquals(["c", "b12b23b34"], "ab12b23b34c", /(?<=((?:b\d{2})+))c/);

// Sticky
var re1 = /(?<=^(\w+))def/g;
assertEquals(["def", "abc"], re1.exec("abcdefdef"));
assertEquals(["def", "abcdef"], re1.exec("abcdefdef"));
var re2 = /\Bdef/g;
assertEquals(["def"], re2.exec("abcdefdef"));
assertEquals(["def"], re2.exec("abcdefdef"));

// Misc
checkNull("abcdef", /(?<=$abc)def/);
checkEquals(["foo"], "foo", /^foo(?<=foo)$/);
checkEquals(["foo"], "foo", /^f.o(?<=foo)$/);
checkNull("fno", /^f.o(?<=foo)$/);
checkNull("foo", /^foo(?<!foo)$/);
checkNull("foo", /^f.o(?<!foo)$/);
assertEquals(["fno"], "fno".match(/^f.o(?<!foo)$/));
assertEquals(["foooo"], "foooo".match(/^foooo(?<=fo+)$/));
assertEquals(["foooo"], "foooo".match(/^foooo(?<=fo*)$/));
checkExec(["abc", "abc"], /(abc\1)/, "abc");
checkExec(["abc", "abc"], /(abc\1)/, "abc\u1234");
checkExec(["abc", "abc"], /(abc\1)/i, "abc");
checkExec(["abc", "abc"], /(abc\1)/i, "abc\u1234");
var oob_subject = "abcdefghijklmnabcdefghijklmn".substr(14);
checkNull(oob_subject, /(?=(abcdefghijklmn))(?<=\1)a/i);
checkNull(oob_subject, /(?=(abcdefghijklmn))(?<=\1)a/);
checkNull("abcdefgabcdefg".substr(1), /(?=(abcdefg))(?<=\1)/);

// Mutual recursive capture/back references
checkExec(["cacb", "a", ""], /(?<=a(.\2)b(\1)).{4}/, "aabcacbc");
checkExec(["b", "ac", "ac"], /(?<=a(\2)b(..\1))b/, "aacbacb");
checkExec(["x", "aa"], /(?<=(?:\1b)(aa))./, "aabaax");
checkExec(["x", "aa"], /(?<=(?:\1|b)(aa))./, "aaaax");

// Restricted syntax in Annex B 1.4.
assertThrows("/(?<=.)*/u", SyntaxError);
assertThrows("/(?<=.){1,2}/u", SyntaxError);
assertThrows("/(?<=.)*/", SyntaxError);
assertThrows("/(?<=.)?/", SyntaxError);
assertThrows("/(?<=.)+/", SyntaxError);
