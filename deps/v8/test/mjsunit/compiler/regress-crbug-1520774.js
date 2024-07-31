// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --jit-fuzzing

const v2 = {};
const v5 = new Uint32Array(1000);
for (const v6 of v5) {
  let v7 = 0;
  do {
    v7++;
  } while ((() => {
    const v10 = v7 < 1;
    new Float32Array();
    const v13 = v2.Intl;
    try { v13.supportedValuesOf(); } catch (e) {}
    return v10;
  })())
}
