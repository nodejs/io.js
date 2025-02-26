// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let v0 = [1,-2];
const v1 = `
    --v0;
`;
let v3 = eval;
v3(v1);
function f5() {
    return f5;
}
v3 ^= f5;
