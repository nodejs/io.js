// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --interrupt-budget=100

var caught = 0;

function runManyTimes(f) {
  for (let i = 0; i < 1000; ++i) {
    try {
      // Seems to be important that this is inside a try catch.
      f();
    } catch (e) {
      assertUnreachable();
    }
  }
}

let A = {
  get foo() {
    return 0;
  },
  set foo(v) {
  }
};

let B = {
  __proto__: A,

  aa() {
    "use strict";
    super.foo;
  },
  bb() {
    "use strict";
    super.foo = 55;
  }
};

var superAccessingFunc = B.aa;
var superAccessingFunc2 = B.bb;

runManyTimes(function () {
  try {
    superAccessingFunc();
    superAccessingFunc2();
  } catch (e) {
    caught++;
  }
});

assertEquals(0, caught);
