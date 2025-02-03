// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --expose-gc

function Bar() {
    this.x = 42;
}
function foo() {
    var x = new Bar();
    x.__defineSetter__( 0, function() { x; });
    gc(); // Clear the feedback map in the Bar access.
}

%PrepareFunctionForOptimization(Bar);
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();
