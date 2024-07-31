// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --allow-natives-syntax --maglev-inline-api-calls

function main() {
  var v0 = [function f4() {}];
  var err = new Error();
  err.stack;
  try {
    v0();
  } catch (e) {
    print(e);
    e.stack;
  }
}

Error.prepareStackTrace = function(v1, v2) {
  gc();
};

%PrepareFunctionForOptimization(main);
main();
%OptimizeMaglevOnNextCall(main);
main();
