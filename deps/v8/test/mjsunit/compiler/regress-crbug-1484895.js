// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-maglev --jit-fuzzing

const v0 = `tan`;
const v3 = new Float64Array(3270);
for (const v4 in v3) {
    const v6 = 9.137894809324841 | v0;
    let v7 = v6 / v6;
    const v8 = -v7;
    -(v7++ ||v8 | v8);
}
