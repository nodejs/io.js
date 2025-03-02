// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo(a) {
  // Doing random things to add entries in the feedback vector before the
  // GetTemplateObject entry.
  a + 3;
  a - 3;

  if (a == 5) {
    // Feedback not collected. Maglev will insert a GetTemplateObject followed
    // by an unconditional deopt.
    return ````;
  } else {
    return 17;
  }
}

%PrepareFunctionForOptimization(foo);
assertEquals(17, foo(42));
assertEquals(17, foo(42));

%OptimizeFunctionOnNextCall(foo);
assertEquals(17, foo(42));
assertThrows(() => foo(5), TypeError, '"" is not a function');
