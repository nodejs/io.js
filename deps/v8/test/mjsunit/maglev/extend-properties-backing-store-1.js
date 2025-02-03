// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

const s = new Set();

function foo(o) {
  // One of these stores will exhaust the slack and we need to add a property
  // backing store.
  o.b1 = 0;
  o.b2 = 0;
  o.b3 = 0;
}
%PrepareFunctionForOptimization(foo);

let o1 = {a: 1}; // One in-object property.

// Make the StoreIC in `foo` polymorphic.
foo(o1);
foo({a: 0, b: 2});

%OptimizeMaglevOnNextCall(foo);
foo({a: 1, b: 3});

let o2 = {a: 1};
s.add(o2);  // Force creating the hash.

foo(o2);
// Assert that the hash value was preseved when adding the properties backing
// store.
assertTrue(s.has(o2));
