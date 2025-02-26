// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let getters = 0;
let result;

import('./modules-skip-1.mjs', { with: {
    get attr1() {
        getters++;
        return {};
    },
    get attr2() {
        getters++;
        return {};
    },
} }).then(
    () => assertUnreachable('Should have failed due to invalid attributes values'),
    error => result = error);

%PerformMicrotaskCheckpoint();

assertEquals(2, getters);
assertInstanceof(result, TypeError);
