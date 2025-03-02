// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

glo4 = 4;

function write_glo4(x) { glo4 = x }

%PrepareFunctionForOptimization(write_glo4);
write_glo4(4);
assertEquals(4, glo4);

// At this point, glo4 has cell type Constant.

%OptimizeFunctionOnNextCall(write_glo4);
write_glo4(4);
assertEquals(4, glo4);
assertOptimized(write_glo4);

// Deopting when calling the function with an input that isn't 4.
write_glo4(6);
assertEquals(6, glo4);
assertUnoptimized(write_glo4);
