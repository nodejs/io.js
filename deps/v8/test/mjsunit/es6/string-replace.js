// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var pattern = {
  [Symbol.replace]: (string, newValue) => string + newValue,
  toString: () => "c"
};
// Check object coercible fails.
assertThrows(() => String.prototype.replace.call(null, pattern, "x"),
             TypeError);
// Override is called.
assertEquals("abcdex", "abcde".replace(pattern, "x"));
// Non-callable override.
pattern[Symbol.replace] = "dumdidum";
assertThrows(() => "abcde".replace(pattern, "x"), TypeError);
// Null override.
pattern[Symbol.replace] = null;
assertEquals("abXde", "abcde".replace(pattern, "X"));

assertEquals("[Symbol.replace]", RegExp.prototype[Symbol.replace].name);

assertThrows(
    () => { RegExp.prototype[Symbol.replace].call("xyz", "foo", "a"); },
    TypeError,
    "Method RegExp.prototype.@@replace called on incompatible receiver xyz");
