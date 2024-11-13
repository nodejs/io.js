// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy-feedback-allocation

class C {};
let o = {};
o.__defineSetter__("foo", C);

function f(o) {
  try {
    o.foo = 44;
  } catch (e) {
  }
}

f(o);
f(o);
