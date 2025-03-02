// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing --allow-natives-syntax

const v0 = `
    const v4 = Array(v2);
    class C5 {
        static o(a7, a8, a9) {
            v4.__proto__ = v2;
        }
    }
`;

assertThrowsAsync(%RuntimeEvaluateREPL(v0));
