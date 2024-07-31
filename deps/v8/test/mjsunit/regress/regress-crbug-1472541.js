// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignore-unhandled-promises --single-threaded

try {
    for (let v9 = 0; v9 < 5; v9++) {
        const v24 = new Uint8Array([0,97,115,109,1,0,0,0,v9,5,v9,96,297564919,297564919,127]);
        WebAssembly.instantiate(v24);
    }
} catch {}
