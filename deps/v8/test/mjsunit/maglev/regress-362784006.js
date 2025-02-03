// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --invocation-count-for-maglev-osr=1 --invocation-count-for-maglev=1
// Flags: --minimum-invocations-after-ic-update=1 --deopt-every-n-times=250
// Flags: --single-threaded --verify-heap

// The flags ensure that we verify the heap just before we deopt the first
// Maglev OSR synchronously.
// --deopt-every-n-times > 0 forces MaterializeHeapObjects to call GC.

let v0 = 1;
let vn = 1000;
for (let i1 = v0; i1 <= vn; i1 += v0) {
    v0 = v0 && i1;
    let v8 = "";
    [v8,] = v8;
}
