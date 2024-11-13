// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev-extend-properties-backing-store

const v1 = new Set();
const v4 = new Float32Array(2284);
for (const v5 of v4) {
    const v6 = 2284 >> v1;
    const v7 = -v6;
    const v8 = /abc|def|ghi+/ygs;
    v8.e = v8;
    v6 << v7;
}
