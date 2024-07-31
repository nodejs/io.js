// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-inlining
// Flags: --maglev-escape-analysis

var d = false;

function bar() {
  if (d) {
    // This shoud access 'o' defined in foo, but its allocation was
    // removed since there was no use.
    throw foo_inlined.arguments[0].x;
  }
}

function foo_inlined() {
  bar();
}

function foo() {
  var o = {x: 1};
  foo_inlined(o);
};

%PrepareFunctionForOptimization(foo_inlined);
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);

// This does not crash, but it messes the stack up!
d = true;
try { foo(); } catch (e) {}

// We force some deopt that causes the crash.

function deopt() {}
function crash() {
  deopt(); // Deopt, since no feedback.
};

%PrepareFunctionForOptimization(crash);
%OptimizeMaglevOnNextCall(crash);


crash(); // Deopt and crash!!
