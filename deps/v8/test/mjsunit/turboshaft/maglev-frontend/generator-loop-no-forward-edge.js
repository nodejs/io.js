// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(o) {
  o.x; // No feedback for this load --> will be an unconditional deopt in the
       // Maglev graph. As a result, the while-loop below won't have a forward
       // edge.
  let i = 0;
  while (true) { yield i++; }
}

let gen = foo({ x : 1 });

for (let i = 0; i < 20000; i++) {
  // OSR will eventually kick in.
  assertEquals(i, gen.next().value);
}
