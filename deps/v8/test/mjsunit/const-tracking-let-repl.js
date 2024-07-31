// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

%RuntimeEvaluateREPL('let a = 42;');

// read() uses LdaGlobal / StaGlobal bytecodes for accessing the top-level
// `let` variable.
%RuntimeEvaluateREPL('function read() { ++a; return a; }');

%PrepareFunctionForOptimization(read);
assertEquals(43, read());

%OptimizeFunctionOnNextCall(read);
assertEquals(44, read());
assertOptimized(read);

// `a` was not embedded as a constant to `read`, so changing it won't deopt
// `read`.
a = 5;

assertOptimized(read);
assertEquals(6, read());
