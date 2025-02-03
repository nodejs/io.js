// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function string_char_code(s, c) {
  return String.fromCharCode(73) + s.charCodeAt(1) + s.codePointAt(1);
}

%PrepareFunctionForOptimization(string_char_code);
assertEquals("I5530474565", string_char_code("a\u{12345}c", 1));
%OptimizeFunctionOnNextCall(string_char_code);
assertEquals("I5530474565", string_char_code("a\u{12345}c", 1));
assertOptimized(string_char_code);
