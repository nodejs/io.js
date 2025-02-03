// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class C1 {}
const v3 = new C1();

const foo = {
  "not_const": new C1(),
  3: 1,
  127: 1,
};

// Overflow the transitions
for (var i = 0; i < 400; ++i) {
  foo.__proto__ = {};
}

// Mutate const field
foo.not_const = 9;


// Overflow transitions of jsfunction object
Function.x = 1;
function foo2(){};

// Overflow the transitions
for (var i = 0; i < 400; ++i) {
  foo2.__proto__ = {};
}
