// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier-up behavior differs between slow and fast paths in
// RegExp.prototype.replace with a function as an argument.
// Flags: --regexp-tier-up --regexp-tier-up-ticks=1
// Flags: --allow-natives-syntax --no-force-slow-path --no-regexp-interpret-all
// Flags: --no-enable-experimental-regexp-engine

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

// Testing first execution of regexp with one-byte string subject.
re.test("a");
assertTrue(%RegexpHasBytecode(re, kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re, kUnicode));
// Testing second execution of regexp now with a two-byte string subject.
// This will compile to native code because we have a single tick counter
// for both string representations.
re.test("π");
assertTrue(%RegexpHasBytecode(re, kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            %RegexpHasNativeCode(re,kUnicode));
// Testing tier-up when we're back to executing the regexp with a one byte
// string.
re.test("6");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re,kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            %RegexpHasNativeCode(re,kUnicode));
re.test("7");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re,kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            %RegexpHasNativeCode(re,kUnicode));

// Testing String.replace method for non-global regexps.
var subject = "a11";
re = /\w1/;
CheckRegexpNotYetCompiled(re);

subject.replace(re, "x");
assertTrue(%RegexpHasBytecode(re, kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re, kUnicode));
subject.replace(re, "x");
assertTrue(!%RegexpHasBytecode(re, kLatin1) &&
            %RegexpHasNativeCode(re, kLatin1));
assertTrue(!%RegexpHasBytecode(re, kUnicode) &&
            !%RegexpHasNativeCode(re, kUnicode));

// Testing String.replace method for global regexps.
// This regexp will match, so it will execute twice, and tier-up.
let re_g = /\w1/g;
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

// Testing eager tier-up for very long strings.
let dna = "ATCG".repeat(251);

re_g = />.*\n|\n/;
CheckRegexpNotYetCompiled(re_g);

dna = dna.replace(re_g,"");
assertTrue(!%RegexpHasBytecode(re_g, kLatin1) &&
            %RegexpHasNativeCode(re_g, kLatin1));
assertTrue(!%RegexpHasBytecode(re_g, kUnicode) &&
            !%RegexpHasNativeCode(re_g, kUnicode));
