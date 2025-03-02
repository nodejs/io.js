// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is slow; disable stress flags.

// Overwrite the value for --noverify-heap which the test runner already set to
// true before. Due to flag contradiction checking, this requires
// --allow-overwriting-for-next-flag to avoid an error.

// Flags: --allow-overwriting-for-next-flag --noverify-heap
// Flags: --no-stress-incremental-marking --no-stress-concurrent-allocation
// Flags: --no-stress-compaction

class TestObject {
  constructor(ix) {
    this.ix = ix;
  }
};

const objectCount = 1 << 21;
const objects = new Array(objectCount);
for (let i = 0; i < objectCount; ++i) {
  objects[i] = new TestObject(i);
}

const w = new WeakMap();

for (let i = 0; i < objectCount; ++i) {
  w.set(objects[i], i);
}

for (let i = 0; i < objectCount; ++i) {
  assertEquals(i, w.get(objects[i]));
}
