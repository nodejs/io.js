// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let slot;

function foo() {
  return function inner(a) {
    slot = +a;
    return 1 / slot;
  }
}

function mk_foo() {
  let f = eval('(' + foo.toString() + ')');
  return f();
}

for (let i = 0; i < 3; i++) {
  let f = mk_foo();
  assertEquals(Infinity, f(0));
  assertEquals(-Infinity, f(-0));
}
