// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

for (let v0 = 0; v0 < 5; v0++) {
    const o2 = {
    };
    const o3 = {
        "c": 3.0,
    };
    function f5(a6, a7) {
        Object.defineProperty(o2, "c", { configurable: true, enumerable: true, value: o3 });
        const o10 = {
            ...a7,
        };
    }
    const v11 = f5(undefined, o2);
    f5();
}
