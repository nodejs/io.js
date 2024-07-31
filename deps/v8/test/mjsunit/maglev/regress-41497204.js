// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo() {
  let o = { value: 1.5 };
  // {x} should be a freshly allocated HeapNumber and not alias {o.value}.
  let x = o.value;
  inc(o);
  return x;
}

function inc(o) {
  o.value++;
}

%PrepareFunctionForOptimization(foo);
assertEquals(foo(), 1.5);
assertEquals(foo(), 1.5);
%OptimizeMaglevOnNextCall(foo);
assertEquals(foo(), 1.5);
