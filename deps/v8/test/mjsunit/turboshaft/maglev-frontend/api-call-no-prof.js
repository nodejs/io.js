// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-maglev-inline-api-calls
// Flags: --no-rcs

function g() { }

function main() {
  var err = new Error();
  return err.stack;
}

Error.prepareStackTrace = function() { return "The stack trace\n"; };

%PrepareFunctionForOptimization(main);
let stack = main();
%OptimizeFunctionOnNextCall(main);
assertEquals(stack, main());
assertOptimized(main);
