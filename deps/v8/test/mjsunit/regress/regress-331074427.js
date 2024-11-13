// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kMaxStringLengths = [
  (1 << 28) - 16,  // 32 bit
  (1 << 29) - 24,  // 64 bit
];

let exception;
for (let length of kMaxStringLengths) {
  let long_string = '"' + new Array(length - 2).join('x');
  try {
    String.prototype.link(long_string);
  } catch (e) {
    exception = e;
    break;
  }
}

assertException(exception, RangeError, 'Invalid string length');
