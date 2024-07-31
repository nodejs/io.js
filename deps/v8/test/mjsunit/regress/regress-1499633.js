// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags:

function test(x) {
   return (x % x) / -1e-15;
}

for (let v9 = 1; v9 < 25; v9++) {
    assertEquals(-0, test(v9));
}
