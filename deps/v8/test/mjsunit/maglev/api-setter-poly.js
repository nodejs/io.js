// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function a() {
}

function b() {
}

Error.captureStackTrace(a);
Error.captureStackTrace(b);

function foo(f, s) {
  // This calls an API setter.
  f.stack = s;
}
%PrepareFunctionForOptimization(foo);

foo(a, 'stack1');
foo(b, 'stack2');
foo(a, 'stack3');

// Make the store polymorphic.
foo({}, 'unrelated');

%OptimizeMaglevOnNextCall(foo);

foo(a, 'stack4');
assertEquals('stack4', a.stack);
