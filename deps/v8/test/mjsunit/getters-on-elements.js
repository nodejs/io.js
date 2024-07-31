// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --noalways-turbofan --turbofan --no-assert-types

// It's nice to run this in other browsers too.
var standalone = false;
if (standalone) {
  assertTrue = function(val) {
    if (val != true) {
      print("FAILURE");
    }
  }

  assertFalse = function(val) {
    if (val != false) {
      print("FAILURE");
    }
  }

  assertEquals = function(expected, val) {
    if (expected !== val) {
      print("FAILURE");
    }
  }

  empty_func = function(name) { }
  assertUnoptimized = empty_func;
  assertOptimized = empty_func;

  prepareForOptimize = emtpy_func;
  optimize = empty_func;
  clearFunctionTypeFeedback = empty_func;
  deoptimizeFunction = empty_func;
} else {
  optimize = function(name) {
    %OptimizeFunctionOnNextCall(name);
  }
  prepareForOptimize = function(name) {
    %PrepareFunctionForOptimization(name);
  }
  clearFunctionTypeFeedback = function(name) {
    %ClearFunctionFeedback(name);
  }
  deoptimizeFunction = function(name) {
    %DeoptimizeFunction(name);
  }
}

function base_getter_test(create_func) {
  var calls = 0;

  // Testcase: setter in prototype chain
  foo = function(a) { var x = a[0]; return x + 3; }
  var a = create_func();
  var ap = [];
  ap.__defineGetter__(0, function() { calls++; return 0; });

  prepareForOptimize(foo);
  foo(a);
  assertUnoptimized(foo);
  // Smi and Double elements transition the KeyedLoadIC to Generic state
  // here, because they miss twice with the same map when loading the hole.
  // For HOLEY_ELEMENTS, however, the IC knows how to convert the hole
  // to undefined if the prototype is the original array prototype, so it
  // stays monomorphic for now...
  foo(a);
  foo(a);
  delete a[0];

  assertEquals(0, calls);
  a.__proto__ = ap;
  // ...and later becomes polymorphic when it sees a second map. Optimized
  // code will therefore inline the elements access, and deopt right away
  // when it loads the hole from index [0].
  // Possible solutions:
  // - remove the convert_hole_to_undefined flag from the IC, to force it
  //   into generic state for all elements kinds. Cost: slower ICs in code
  //   that doesn't get optimized.
  // - teach Turbofan about the same trick: for holey elements with the
  //   original array prototype, convert hole to undefined inline. Cost:
  //   larger optimized code size, because the loads for different maps with
  //   the same elements kind can no longer be consolidated if they handle
  //   the hole differently.
  // - call "foo" twice after setting a.__proto__ and before optimizing it;
  //   this is the simplest fix so let's do that for now.
  foo(a);
  assertEquals(1, calls);
  foo(a);
  assertEquals(2, calls);
  optimize(foo);
  foo(a);
  assertEquals(3, calls);
  assertOptimized(foo);

  // Testcase: getter "deep" in prototype chain.
  clearFunctionTypeFeedback(foo);
  deoptimizeFunction(foo);
  clearFunctionTypeFeedback(foo);
  calls = 0;

  a = create_func();
  var ap2 = [];
  a.__proto__ = ap2;
  foo(a);
  foo(a);
  foo(a);
  delete a[0];

  assertEquals(0, calls);

  ap2.__proto__ = ap;  // "sneak" in a callback.
  // The sneak case should be caught by unoptimized code too.
  assertUnoptimized(foo);
  foo(a);
  foo(a);
  foo(a);
  assertEquals(3, calls);

  // Testcase: getter added after optimization (feedback is monomorphic)
  clearFunctionTypeFeedback(foo);
  deoptimizeFunction(foo);
  clearFunctionTypeFeedback(foo);
  calls = 0;

  a = create_func();
  ap2 = [];
  a.__proto__ = ap2;
  prepareForOptimize(foo);
  foo(a);
  foo(a);
  foo(a);
  optimize(foo);
  foo(a);
  assertOptimized(foo);
  delete a[0];
  ap2.__proto__ = ap;
  foo(a);
  assertOptimized(foo);  // getters don't require deopt on shape change.
  assertEquals(1, calls);

  // Testcase: adding additional getters to a prototype chain that already has
  // one shouldn't deopt anything.
  clearFunctionTypeFeedback(foo);
  calls = 0;

  a = create_func();
  a.__proto__ = ap2;
  bar = function(a) { return a[3] + 600; }
  prepareForOptimize(bar);
  bar(a);
  bar(a);
  bar(a);
  optimize(bar);
  bar(a);
  assertOptimized(bar);
  assertEquals(0, calls);
  delete a[3];
  ap2.__defineGetter__(3, function() { calls++; return 0; });
  bar(a);
  assertOptimized(bar);
  assertEquals(1, calls);

  // Reset the state of foo and bar.
  clearFunctionTypeFeedback(foo);
  deoptimizeFunction(foo);
  clearFunctionTypeFeedback(foo);

  clearFunctionTypeFeedback(bar);
  deoptimizeFunction(bar);
  clearFunctionTypeFeedback(bar);
}

// Verify that map transitions don't confuse us.
create_func_smi = function() { return [,,,,,,5]; }
create_func_double = function() { return [,,,,,,5.5]; }
create_func_fast = function() { return [,,,,,,true]; }

var cf = [create_func_smi,
          create_func_double,
          create_func_fast];

for(var c = 0; c < cf.length; c++) {
  base_getter_test(cf[c]);
}

// A special test for LoadKeyedHoleMode. Ensure that optimized is generated
// which sets ALLOW_RETURN_HOLE, then add a setter on the prototype that should
// cause the function to deoptimize.

var a = [3.5,,,3.5];
fun = function(a) { return a[0] + 5.5; }
prepareForOptimize(fun);
fun(a);
fun(a);
fun(a);  // should have a monomorphic KeyedLoadIC.
optimize(fun);
fun(a);
assertOptimized(fun);

// Maglev and TF differs here: Maglev will not try to silence a holey nan
// if it hasn't seen one.
if (isTurboFanned(fun)) {
  // returning undefined shouldn't phase us.
  delete a[0];
  fun(a);
  assertOptimized(fun);
}

// but messing up the prototype chain will.
a.__proto__ = [];
fun(a);
assertUnoptimized(fun);

// Construct a non-trivial prototype chain.
var a = [3.5,,,,3.5];
var ap = [,,3.5];
ap.__proto__ = a.__proto__;
a.__proto__ = ap;
prepareForOptimize(fun);
fun(a);
optimize(fun);
fun(a);
assertOptimized(fun);

var calls = 0;
delete a[0];
ap.__defineGetter__(0, function() { calls++; return 0; });
fun(a);
assertEquals(1, calls);
assertUnoptimized(fun);
