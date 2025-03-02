// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --nolazy-feedback-allocation

const v1 = new Set();
for (let v2 = 0; v2 < 5; v2++) {
    function F3() {
        this.a = -869472841;
    }
    const v6 = new F3();
    function f7(a8) {
        const o12 = {
            ...a8,
            [a8]() {
            },
            __proto__: v1,
            "c": a8,
        };
        return o12;
    }
    var a = f7(v6);
    f7(a);
}
