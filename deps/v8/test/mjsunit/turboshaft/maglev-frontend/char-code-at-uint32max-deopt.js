// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function charCodeAt(idx) {
  return "abc".charCodeAt(idx);
}

%PrepareFunctionForOptimization(charCodeAt);
assertSame(97, charCodeAt(0.0));
%OptimizeFunctionOnNextCall(charCodeAt);
assertSame(97, charCodeAt(0.0));
assertOptimized(charCodeAt);

// Using max-uint32+1 as index. This should trigger a deopt (and should
// definitely not wrap around and be considered as 0!).
assertSame(NaN, charCodeAt(4294967296));
assertUnoptimized(charCodeAt);
