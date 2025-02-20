// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy-feedback-allocation --clone-object-sidestep-transitions
// Flags: --verify-heap --allow-natives-syntax

const o = {};
o.d = 512;
const p = { p:42 };

function f() {
  const copy = { __proto__: p, ...o };
  const dummy = { ...copy };
  %HeapObjectVerify(dummy);
  copy.x = 55;
}
f();
o.d = undefined;
f();
