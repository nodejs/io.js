// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f5(a6, a7, a8) {
    const o10 = {
        "a": a8,
        __proto__: a8,
        7: a7,
        3472: a6,
        "f": a8,
    };
}
f5();
f5(0, 216, Int8Array);
f5(10, 0, -65535);
