// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function string_char_at(s) {
  return s.charAt(2);
}

%PrepareFunctionForOptimization(string_char_at);
assertEquals("c", string_char_at("abcdef"));
%OptimizeFunctionOnNextCall(string_char_at);
assertEquals("c", string_char_at("abcdef"));
assertOptimized(string_char_at);
