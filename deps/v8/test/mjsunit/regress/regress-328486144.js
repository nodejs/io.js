// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --assert-types

const v0 = [];
for (let v1 = 0; v1 < 25; v1++) {
    v0[v1] = [];
}
function f3() {
    const v8 = Object("A");
    o = v8;
}
v0.sort(f3);
