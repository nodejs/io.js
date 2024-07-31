// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function NonBigIntRegressionTest() {
  const rab = new ArrayBuffer(1050, {"maxByteLength": 2000});
  const ta = new Uint8ClampedArray(rab);
  ta[Symbol.toPrimitive] = () => { rab.resize(0); return 0; };
  ta[916] = ta;
})();

(function BigIntRegressionTest() {
  const rab = new ArrayBuffer(8 * 100, {"maxByteLength": 8 * 200});
  const ta = new BigInt64Array(rab);
  ta[Symbol.toPrimitive] = () => { rab.resize(0); return 0n; };
  ta[1] = ta;
})();
