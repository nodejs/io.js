// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --trace-gc-object-stats

var obj = {
  a: 1,
  b: 2,
  q: 3
};
obj[44] = 44;
Object.defineProperty(obj, "c", {
  get: () => 7,
  enumerable: true
});
obj2 = JSON.parse(JSON.stringify(obj));
gc();
assertEquals(obj, obj2);
