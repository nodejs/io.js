// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertEquals(1n, (-1n) ** 0n);
assertEquals(-1n, (-1n) ** 1n);
assertEquals(1n, (-1n) ** 2n);
assertEquals(-1n, (-1n) ** 3n);
assertEquals(1n, (-1n) ** 4n);
// Multi-digit exponents.
assertEquals(1n, (-1n) ** (2n ** 80n));
assertEquals(-1n, (-1n) ** ((2n ** 80n) + 1n));

assertEquals(1n, 0n ** 0n);
assertEquals(0n, 0n ** 1n);
assertEquals(0n, 0n ** 23n);

assertEquals(1n, 1n ** 0n);
assertEquals(1n, 1n ** 1n);
assertEquals(1n, 99n ** 0n);

assertEquals(2n, 2n ** 1n);
assertEquals(4n, 2n ** 2n);
assertEquals(8n, 2n ** 3n);
assertEquals(16n, 2n ** 4n);
assertEquals(151115727451828646838272n, 2n ** 77n);

assertEquals(3n, 3n ** 1n);
assertEquals(9n, 3n ** 2n);
assertEquals(27n, 3n ** 3n);
assertEquals(81n, 3n ** 4n);
assertEquals(243n, 3n ** 5n);
assertEquals(30903154382632612361920641803529n, 3n ** 66n);

assertEquals(1n, (-2n) ** 0n);
assertEquals(-2n, (-2n) ** 1n);
assertEquals(4n, (-2n) ** 2n);
assertEquals(-8n, (-2n) ** 3n);
assertEquals(16n, (-2n) ** 4n);
assertEquals(-32n, (-2n) ** 5n);

assertEquals(1n, (-3n) ** 0n);
assertEquals(-3n, (-3n) ** 1n);
assertEquals(9n, (-3n) ** 2n);
assertEquals(-27n, (-3n) ** 3n);
assertEquals(81n, (-3n) ** 4n);
assertEquals(-243n, (-3n) ** 5n);

assertThrows(() => 3n ** -2n, RangeError);  // Negative exponent.
assertThrows(() => 2n ** (1024n ** 4n), RangeError);  // Too big.

// Mixed types
assertThrows(() => 1n ** 1, TypeError);
assertThrows(() => 1 ** 1n, TypeError);
