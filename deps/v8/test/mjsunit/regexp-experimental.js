// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --default-to-experimental-regexp-engine

function Test(regexp, subject, expectedResult, expectedLastIndex) {
  assertEquals(%RegexpTypeTag(regexp), "EXPERIMENTAL");
  var result = regexp.exec(subject);
  if (result instanceof Array && expectedResult instanceof Array) {
    assertArrayEquals(expectedResult, result);
  } else {
    assertEquals(expectedResult, result);
  }
  assertEquals(expectedLastIndex, regexp.lastIndex);
}

function AssertUnsupported(regexp) {
  assertNotEquals(%RegexpTypeTag(regexp), 'EXPERIMENTAL');
}

// The empty regexp.
Test(new RegExp(""), "asdf", [""], 0);

// Plain patterns without special operators.
Test(/asdf1/, "123asdf1xyz", ["asdf1"], 0);
// Escaped operators, otherwise plain string:
Test(/\*\.\(\[\]\?/, "123*.([]?123", ["*.([]?"], 0);
// Some two byte values:
Test(/쁰d섊/, "123쁰d섊abc", ["쁰d섊"], 0);
// A pattern with surrogates but without unicode flag:
Test(/💩f/, "123💩f", ["💩f"], 0);

// Disjunctions.
Test(/asdf|123/, "xyz123asdf", ["123"], 0);
Test(/asdf|123|fj|f|a/, "da123", ["a"], 0);
Test(/|123/, "123", [""], 0);

// Character ranges.
Test(/[abc]/, "123asdf", ["a"], 0);
Test(/[0-9]/, "asdf123xyz", ["1"], 0);
Test(/[^0-9]/, "123!xyz", ["!"], 0);
Test(/\w\d/, "?a??a3!!!", ["a3"], 0);
// [💩] without unicode flag is a character range matching one of the two
// surrogate characters that make up 💩.  The leading surrogate is 0xD83D.
Test(/[💩]/, "f💩", [String.fromCodePoint(0xD83D)], 0);

// Greedy and non-greedy quantifiers.
Test(/x*/, "asdfxk", [""], 0);
Test(/xx*a/, "xxa", ["xxa"], 0);
Test(/x*[xa]/, "xxaa", ["xxa"], 0);
Test(/x*?[xa]/, "xxaa", ["x"], 0);
Test(/x*?a/, "xxaa", ["xxa"], 0);
Test(/x+a/, "axxa", ["xxa"], 0);
Test(/x+?[ax]/, "axxa", ["xx"], 0);
Test(/xx?[xa]/, "xxaa", ["xxa"], 0);
Test(/xx??[xa]/, "xxaa", ["xx"], 0);
Test(/xx??a/, "xxaa", ["xxa"], 0);
Test(/x{4}/, "xxxxxxxxx", ["xxxx"], 0);
Test(/x{4,}/, "xxxxxxxxx", ["xxxxxxxxx"], 0);
Test(/x{4,}?/, "xxxxxxxxx", ["xxxx"], 0);
Test(/x{2,4}/, "xxxxxxxxx", ["xxxx"], 0);
Test(/x{2,4}?/, "xxxxxxxxx", ["xx"], 0);

// Non-capturing groups and nested operators.
Test(/(?:)/, "asdf", [""], 0);
Test(/(?:asdf)/, "123asdfxyz", ["asdf"], 0);
Test(/(?:asdf)|123/, "xyz123asdf", ["123"], 0);
Test(/asdf(?:[0-9]|(?:xy|x)*)*/, "kkkasdf5xyx8xyyky", ["asdf5xyx8xy"], 0);

// Capturing groups.
Test(/()/, "asdf", ["", ""], 0);
Test(/(123)/, "asdf123xyz", ["123", "123"], 0);
Test(/asdf(123)xyz/, "asdf123xyz", ["asdf123xyz", "123"], 0);
Test(/(123|xyz)/, "123", ["123", "123"], 0);
Test(/(123|xyz)/, "xyz", ["xyz", "xyz"], 0);
Test(/(123)|(xyz)/, "123", ["123", "123", undefined], 0);
Test(/(123)|(xyz)/, "xyz", ["xyz", undefined, "xyz"], 0);
Test(/(?:(123)|(xyz))*/, "xyz123", ["xyz123", "123", undefined], 0);
Test(/((123)|(xyz)*)*/, "xyz123xyz", ["xyz123xyz", "xyz", undefined, "xyz"], 0);

// Assertions.
Test(/asdf\b/, "asdf---", ["asdf"], 0);
Test(/asdf\b/, "asdfg", null, 0);
Test(/asd[fg]\B/, "asdf asdgg", ["asdg"], 0);
Test(/^asd[fg]/, "asdf asdgg", ["asdf"], 0);
Test(/asd[fg]$/, "asdf asdg", ["asdg"], 0);

// The global flag.
Test(/asdf/g, "fjasdfkkasdf", ["asdf"], 6);

// The sticky flag.
var r = /asdf/y;
r.lastIndex = 2;
Test(r, "fjasdfkkasdf", ["asdf"], 6);

// The multiline flag.
Test(/^a/m, "x\na", ["a"], 0);
Test(/x$/m, "x\na", ["x"], 0);

// The dotall flag.
Test(/asdf.xyz/s, 'asdf\nxyz', ['asdf\nxyz'], 0);

// Lookbehinds.
Test(/ab(?<=a(?<=a)b)c/, 'abc', ['abc'], 0);
Test(/ab(?<=a(?<=a)b)(c)/, 'abc', ['abc', 'c'], 0);

// Negative lookbehind.
Test(/ab(?<=b)c/, 'abc', ['abc'], 0);
Test(/ab(?<=a(?<!b)b)c/, 'abc', ['abc'], 0);
Test(/ab(?<=a(?<!(b))b)c/, 'abc', ['abc', undefined], 0);
Test(/ab(?<=a(?<!b)b)(c)/, 'abc', ['abc', 'c'], 0);
Test(/ab(?<=a(?<!(b))b)(c)/, 'abc', ['abc', undefined, 'c'], 0);

// Global and Sticky flags are not yet supported in combination with lookbehinds
AssertUnsupported(/ab(?<=b)c/g);
AssertUnsupported(/ab(?<=b)c/y);

// Capture reset with quantifiers
Test(/(?:(a)|b)*/, 'aba', ['aba', 'a'], 0);
Test(/(?:(a)|b)+/, 'aba', ['aba', 'a'], 0);
Test(/(?:(a)|b){2,8}/, 'aba', ['aba', 'a'], 0);
Test(/(?:(a)|b)*/, 'abb', ['abb', undefined], 0);
Test(/(?:(a)|b)+/, 'abb', ['abb', undefined], 0);
Test(/(?:(a)|b){2,8}/, 'abb', ['abb', undefined], 0);

Test(/(?:(?:(a)|b)+|c)*/, 'abc', ['abc', undefined], 0);
Test(/(?:(?:(a)|b)+?|c)+/, 'aba', ['aba', 'a'], 0);
Test(/(?:(?:(a)|b){2,}|c)*/, 'abcabc', ['abcabc', undefined], 0);
Test(/(?:(?:(a)|b){1}|c){3,4}/, 'abcabc', ['abca', 'a'], 0);
Test(/(?:(?:(a)|b)*?|c)+?/, 'abcaac', ['', undefined], 0);
Test(/(?:(?:(a)|b)+|c){3}/, 'bbcbac', ['bbcba', 'a'], 0);

// This test allows to detect abnormal memory usage. Itself cannot check the
// consumption or trigger an out-of-memory error, but it creates a detectable
// anomaly.
Test(/(((((a?)+)+)+)+)b/, ('a'.repeat(100000)), null, 0);
