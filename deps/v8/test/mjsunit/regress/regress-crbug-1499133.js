// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f0() {
    let v2 = 1;
    for (let i3 = v2; i3 >= v2 && i3 < 1e10; i3 += v2) {
        v2 = v2 && i3;
    }
}
f0();
