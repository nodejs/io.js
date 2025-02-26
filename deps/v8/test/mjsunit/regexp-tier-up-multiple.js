// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier-up behavior differs between slow and fast paths in
// RegExp.prototype.replace with a function as an argument.
// Flags: --regexp-tier-up --regexp-tier-up-ticks=5
// Flags: --allow-natives-syntax --no-force-slow-path --no-regexp-interpret-all
// Flags: --no-enable-experimental-regexp-engine
//
// Concurrent compiles can trigger interrupts which would cause regexp
// re-execution and thus mess with test expectations below.
// Flags: --no-concurrent-recompilation

const kLatin1 = true;
const kUnicode = false;

function CheckRegexpNotYetCompiled(regexp) {
  assertFalse(%RegexpHasBytecode(regexp, kLatin1) &&
              %RegexpHasNativeCode(regexp, kLatin1));
  assertFalse(%RegexpHasBytecode(regexp, kUnicode) &&
              %RegexpHasNativeCode(regexp, kUnicode));
}

// Testing RegExp.test method which calls into Runtime_RegExpExec.
let re = new RegExp('^.$');
CheckRegexpNotYetCompiled(re);

// Testing first five executions of regexp with one-byte string subject.
for (var i = 0; i < 5; i++) {
  re.test("a");
  assertTrue(%RegexpHasBytecode(re, kLatin1));
  assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
              !%RegexpHasNativeCode(re, kUnicode));
}
// Testing the tier-up to native code.
re.test("a");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re,kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re,kUnicode));
re.test("a");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re,kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re,kUnicode));
// Testing that the regexp will compile to native code for two-byte string
// subject as well, because we have a single tick counter for both string
// representations.
re.test("π");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re,kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            %RegexpHasNativeCode(re,kUnicode));

// Testing String.replace method for non-global regexps.
var subject = "a1111";
re = /\w1/;
CheckRegexpNotYetCompiled(re);

for (var i = 0; i < 5; i++) {
  subject.replace(re, "x");
  assertTrue(%RegexpHasBytecode(re, kLatin1));
  assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
              !%RegexpHasNativeCode(re, kUnicode));
}

subject.replace(re, "x");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re, kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re, kUnicode));

// This regexp will match, so it will execute five times, and tier-up.
re_g = /\w/g;
CheckRegexpNotYetCompiled(re_g);
subject.replace(re_g, "x");
assertTrue(!%RegexpHasBytecode(re_g, kLatin1) &&
            %RegexpHasNativeCode(re_g, kLatin1));
assertTrue(!%RegexpHasBytecode(re_g, kUnicode) &&
            !%RegexpHasNativeCode(re_g, kUnicode));

// Testing String.replace method for global regexps with a function as a
// parameter. This will tier-up eagerly and compile to native code right
// away, even though the regexp is only executed once.
function f() { return "x"; }
re_g = /\w2/g;
CheckRegexpNotYetCompiled(re_g);
subject.replace(re_g, f);
assertTrue(!%RegexpHasBytecode(re_g, kLatin1) &&
            %RegexpHasNativeCode(re_g, kLatin1));
assertTrue(!%RegexpHasBytecode(re_g, kUnicode) &&
            !%RegexpHasNativeCode(re_g, kUnicode));
