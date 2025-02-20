// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function f(short_str, long_str) {
  let cons_str = short_str + long_str;
  let seq_str = short_str + 'cd';
  if (seq_str == 'abcdcd') {
    return cons_str.length;
  } else {
    return seq_str[2];
  }
}

%PrepareFunctionForOptimization(f);
assertEquals(19, f("abcd", "abcdefghijklmno"));
assertEquals("c", f("abcde", "abcdefghijklmno"));
%OptimizeMaglevOnNextCall(f);
assertEquals(19, f("abcd", "abcdefghijklmno"));
assertEquals("c", f("abcde", "abcdefghijklmno"));
%OptimizeFunctionOnNextCall(f);
assertEquals(19, f("abcd", "abcdefghijklmno"));
assertEquals("c", f("abcde", "abcdefghijklmno"));
assertOptimized(f);
