// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax
// Flags: --no-enable-experimental-regexp-engine
// Flags: --enable-experimental-regexp-engine-on-excessive-backtracks
// Flags: --regexp-tier-up --regexp-tier-up-ticks 1

// We should report accurate results on patterns for which irregexp suffers
// from catastrophic backtracking.
let regexp = new RegExp("a+".repeat(100) + "x");
let match = "a".repeat(100) + "x";
let subject = match.repeat(3);

// First for the irregexp interpreter:
assertArrayEquals([match], regexp.exec(subject));
// Now for native irregexp:
assertArrayEquals([match], regexp.exec(subject));

// Now the same again with String.replace and a replacement function to
// exercise the RegExpGlobalExecRunner.
regexp = new RegExp(regexp.source, "g");
assertEquals("", subject.replace(regexp, function () { return ""; }));
assertEquals("", subject.replace(regexp, function () { return ""; }));

// Now the same again with String.replace and a replacement string to
// exercise the global irregexp execution mode.
regexp = new RegExp(regexp.source, "g");
assertEquals("", subject.replace(regexp, ""));
assertEquals("", subject.replace(regexp, ""));

// If an explicit backtrack limit is larger than the default, then we should
// take the default limit.
regexp = %NewRegExpWithBacktrackLimit(regexp.source, "", 1000000000)
assertArrayEquals([match], regexp.exec(subject));
assertArrayEquals([match], regexp.exec(subject));

// If the experimental engine can't handle a regexp with an explicit backtrack
// limit, we should abort and return null on excessive backtracking.
regexp = %NewRegExpWithBacktrackLimit(regexp.source + "(?=a)", "", 100)
assertEquals(null, regexp.exec(subject));
assertEquals(null, regexp.exec(subject));
