// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const v2 = new Int8Array(110);
function f3() {
}
function f7() {
    const v8 = [];
    const v9 = v8.f3;
    v2["toLocaleString"](v9, ...v8, f3(v9, ...v2, v9));
}
const v13 = %PrepareFunctionForOptimization(f7);
f7();
f7();
