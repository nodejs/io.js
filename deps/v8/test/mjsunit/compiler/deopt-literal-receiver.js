// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// Flags: --allow-natives-syntax --expose-gc

function g(b) {
  if (b) {
    // We now remove the only strong pointer to `global_object.y` that we had
    // and trigger a GC. This should still not free `global_object.y`, since the
    // DeoptimizationLiteralArray for `f` holds a weak pointer to it, but the GC
    // will treat it as a strong pointer because `f` is on the stack.
    global_object = undefined;
    gc();
  }
}
%NeverOptimizeFunction(g);


global_object = { y : { this_is_the_name : 155 } };

function f(x, b) {
  // {global_object} is constant, which means that {global_object.y} will be
  // constant-folded.
  let o = [ global_object.y, 42 ];

  // {o} will be escape-analyzed away, and {global_object.y} (a JSReceiver) will
  // flow into a FrameState, which will lead the code object holding a weak
  // pointer to it in its DeoptimizationLiteralArray.

  g(b);

  return o[1] + x;
}

%PrepareFunctionForOptimization(f);
assertEquals(84, f(42, false));
%OptimizeFunctionOnNextCall(f);
assertEquals(84, f(42, false));
// And now trigger the deopt.
assertEquals(84, f(42, true));
