// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

d8.file.execute('test/mjsunit/const-tracking-let-other-script.js');

// This will use the StoreGlobal bytecode.
function write(newA) {
  a = newA;
}

%PrepareFunctionForOptimization(write);
%CompileBaseline(write);

write(1);
assertUnoptimized(read);
assertEquals(1, read());
