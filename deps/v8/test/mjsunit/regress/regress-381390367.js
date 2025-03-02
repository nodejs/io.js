// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags:

function f0() {
    try {
        f0();
    } catch(e2) {
    }
    const v3 = `
        this.console.profile();
        for (let i9 = 0, i10 = 10000; i9 < i10; ++i9) {
        }
    `;
    return eval(v3);
}
f0();
