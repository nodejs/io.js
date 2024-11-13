// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --jit-fuzzing --efficiency-mode

function f1( a3) {
    try { a3(); } catch (e) {}
    class C6 {
    }
    const v7 = new C6();
    function f9(a10, a11) {
        a11.propertyIsEnumerable();
        const t6 = a11.constructor;
        const v14 = new t6();
        v14.constructor;
    }
    f9(7, v7);
    for (let v17 = 0; v17 < 25; v17++) {
        const o18 = {
            "deleteProperty": f9,
        };
        const t17 = o18.deleteProperty;
        t17(3653, o18);
        o18.deleteProperty(v17, 2);
    }
}
f1(f1, f1);
f1();
