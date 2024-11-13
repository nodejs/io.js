// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --no-lazy-feedback-allocation --invocation-count-for-maglev=4
// Flags: --minimum-invocations-after-ic-update=5

function callbackFn(elem, index) {
    const target = [42];
    function cb(elem, index, arr) {
        arr.pop();
        return undefined;
    }
    target.forEach(cb);
    this.console.profile();
    return elem;
}
const float_arr = new Float64Array(200);
float_arr.findIndex(callbackFn);
