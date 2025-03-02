// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --maglev-extend-properties-backing-store

// TODO(dmercadier): this test is exactly the same as
// test/mjsunit/maglev/extend-properties-backing-store-2.js, but with a
// %OptimizeFunctionOnNextCall instead of the %OptimizeMaglevOnNextCall.
// Consider unifying the 2 tests.

function addProperties(o) {
  // Add enough properties to exhaust the slack, so that adding the property
  // in `foo` will need to grow the property backing store.
  o.a1 = 1;
  o.a2 = 2;
  o.a3 = 3;
  o.a4 = 4;
  o.a5 = 5;
  o.a6 = 6;
  o.a7 = 7;
}

const s = new Set();

function foo(o) {
  o.b = 2;
}
%PrepareFunctionForOptimization(foo);

let o1 = {};
addProperties(o1);

// Make the StoreIC in `foo` polymorphic.
foo(o1);
foo({a: 0, b: 2});

%OptimizeFunctionOnNextCall(foo);
foo({a: 1, b: 3});

let o2 = {};
addProperties(o2);
s.add(o2);  // Force creating the hash.

foo(o2);
// Assert that the hash value was preseved when extending the properties
// backing store.
assertTrue(s.has(o2));
