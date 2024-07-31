// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let one = { valueOf() { Array.prototype[1] = 1; } };
let ar = new Uint8ClampedArray([one,,]);
assertEquals(ar.length, 2);
assertEquals(ar[0], 0);
assertEquals(ar[1], 0);
