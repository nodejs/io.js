// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fuzzing --no-testing-d8-test-runner

const v2 = -2147483649;
for (let v3 = 0; v3 < 5; v3++) {
    const v5 = 2147483648 ^ 9007199254740990;v2 *v3 >>> 2147483647 >>> v5 & -65537;
    const v12 = %OptimizeOsr();
}
