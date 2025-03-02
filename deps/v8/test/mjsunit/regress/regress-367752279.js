// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

let log = "";
function foo(replacement) {
  gc();
  const o = JSON.parse('{"p":8, "q":4294967295}', function (k, v, context) {
    log += JSON.stringify(context);
    if (k === 'p') {
      this.q = replacement;
    }
  });
  return {p:1, q:replacement};
}
%PrepareFunctionForOptimization(foo);
foo({});
foo(1);
let firstLog = log;
log = "";
%OptimizeFunctionOnNextCall(foo);
foo({});
foo(1);
assertEquals(firstLog, log);
