// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

let BigIntCtors = [BigInt64Array, BigUint64Array];
let NonBigIntCtors = [Int8Array,
                      Uint8Array,
                      Uint8ClampedArray,
                      Int16Array,
                      Uint16Array,
                      Int32Array,
                      Uint32Array,
                      Float16Array,
                      Float32Array,
                      Float64Array];

function assertThrowsCannotMixBigInt(cb) {
  assertThrows(cb, TypeError, /Cannot mix BigInt/);
}

for (let bigIntTA of BigIntCtors) {
  for (let nonBigIntTA of NonBigIntCtors) {
    assertThrowsCannotMixBigInt(() => { new bigIntTA(new nonBigIntTA(0)); });
    assertThrowsCannotMixBigInt(() => { new bigIntTA(new nonBigIntTA(1)); });

    assertThrowsCannotMixBigInt(() => { new nonBigIntTA(new bigIntTA(0)); });
    assertThrowsCannotMixBigInt(() => { new nonBigIntTA(new bigIntTA(1)); });
  }
}
