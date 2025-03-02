// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-externalize-string

(function TestIsWellFormed() {
  const short2ByteWellFormed = createExternalizableString(
      '\u1234'.padEnd(kExternalStringMinTwoByteLength, '\u1234'));
  const short2ByteIllFormed = createExternalizableString(
      '\uD83D'.padEnd(kExternalStringMinTwoByteLength, '\uD83D'));

  assertTrue(short2ByteWellFormed.isWellFormed());
  assertFalse(short2ByteIllFormed.isWellFormed());

  try {
    // Turn the strings into uncached external strings to hit the slow runtime
    // path.
    externalizeString(short2ByteWellFormed);
    externalizeString(short2ByteIllFormed);
  } catch (e) {
  }

  assertTrue(short2ByteWellFormed.isWellFormed());
  assertFalse(short2ByteIllFormed.isWellFormed());
})();

(function TestToWellFormed() {
  const short2ByteWellFormed = createExternalizableString(
      '\u1234'.padEnd(kExternalStringMinTwoByteLength, '\u1234'));
  const short2ByteIllFormed = createExternalizableString(
      '\uD83D'.padEnd(kExternalStringMinTwoByteLength, '\uD83D'));

  assertTrue(short2ByteWellFormed.isWellFormed());
  assertFalse(short2ByteIllFormed.isWellFormed());

  try {
    // Turn the strings into uncached external strings to hit the slow runtime
    // path.
    externalizeString(short2ByteWellFormed);
    externalizeString(short2ByteIllFormed);
  } catch (e) {
  }

  assertEquals(
      '\u1234'.padEnd(kExternalStringMinTwoByteLength, '\u1234'),
      short2ByteWellFormed.toWellFormed());
  // U+FFFD (REPLACEMENT CHARACTER)
  assertEquals(
      '\uFFFD'.padEnd(kExternalStringMinTwoByteLength, '\uFFFD'),
      short2ByteIllFormed.toWellFormed());
})();
