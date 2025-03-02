// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This benchmark was ported from
// https://github.com/mathiasbynens/string-prototype-replace-regexp-benchmark.

d8.file.execute("base.js");

var str;
var re;

function StringReplace1() {
  str.replace(re, "");
}

function StringReplace2() {
  str.replace(re, "xyz");
}

function StringReplace3() {
  str.replace(re, "x$1yz");
}

function FunctionReplace1() {
  str.replace(re, String);
}

function Replace1Setup() {
  re = /[Cz]/;
  str = createHaystack();
}

function Replace2Setup() {
  re = /[Cz]/g;
  str = createHaystack();
}

function Replace3Setup() {
  re = /([Cz])/;
  str = createHaystack();
}

function Replace4Setup() {
  re = /([Cz])/g;
  str = createHaystack();
}

var benchmarks = [ [ StringReplace1, Replace1Setup],
                   [ StringReplace1, Replace2Setup],
                   [ StringReplace2, Replace1Setup],
                   [ StringReplace2, Replace2Setup],
                   [ StringReplace3, Replace3Setup],
                   [ StringReplace3, Replace4Setup],
                   [ FunctionReplace1, Replace3Setup],
                   [ FunctionReplace1, Replace4Setup],
                 ];
