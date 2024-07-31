// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbo-optimize-apply --turbofan
// Flags: --no-always-turbofan

// These tests do not work well if we flush the feedback vector, which causes
// deoptimization.
// Flags: --no-stress-flush-code --no-flush-bytecode

// Some of the tests rely on optimizing/deoptimizing at predictable moments, so
// this is not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

// Tests for optimization of CallWithSpread and CallWithArrayLike.

// Test JSCallReducer::ReduceJSCallWithArrayLike.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y, z) {
    return sum_js.apply(null, ["", x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertOptimized(foo);
  assertFalse(sum_js_got_interpreted);
})();

// Test using receiver
(function () {
  function bar() {
    return this.gaga;
  }
  function foo(receiver) {
    return bar.apply(receiver, [""]);
  }

  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  var receiver = { gaga: 42 };
  assertEquals(42, foo(receiver));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(42, foo(receiver));
  assertOptimized(foo);
})();

// Test with holey array.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y) {
    return sum_js.apply(null, ["",x,,y]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('AundefinedB', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('AundefinedB', foo('A', 'B'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with holey-double array.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + (c ? c : .0) + d;
  }
  function foo(x, y) {
    return sum_js.apply(null, [3.14, x, , y]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(45.31, foo(16.11, 26.06));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);

  // We optimize in the next call and  sum_js stays inlined.
  assertEquals(45.31, foo(16.11, 26.06));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test deopt when array size changes.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y, z) {
    let a = ["", x, y, z];
    a.push('*');
    return sum_js.apply(null, a);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  // Here array size changes.
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  // Here it should deoptimize.
  assertEquals('abc', foo('a', 'b', 'c'));
  assertUnoptimized(foo);
  assertTrue(sum_js_got_interpreted);
  // Now speculation mode prevents the optimization.
  %PrepareFunctionForOptimization(foo);
  %OptimizeFunctionOnNextCall(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with FixedDoubleArray.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y, z) {
    return sum_js.apply(null, [3.14, x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(56.34, foo(11.03, 16.11, 26.06));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals(56.34, foo(11.03, 16.11, 26.06));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with empty array.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42;
  }
  function foo() {
    return fortytwo.apply(null, []);
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(42, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(42, foo());
  assertFalse(got_interpreted);
  assertOptimized(foo);
})();

// Test with empty array that changes size.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42 + arguments.length;
  }

  var len = 2;
  function foo() {
    let args = []
    for (var i = 0; i < len; i++) { args.push(1); }
    let result = fortytwo.apply(null, args);
    return result;
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(44, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(44, foo());
  assertTrue(got_interpreted);
  assertOptimized(foo);

  len = 0;
  assertEquals(42, foo());
  assertFalse(got_interpreted);
  assertOptimized(foo);
})();

// Test with FixedDoubleArray and Math.min/max.
(function () {
  "use strict";
  function arrayMin(val) {
    return Math.min.apply(Math, val);
  }
  function arrayMax(val) {
    return Math.max.apply(Math, val);
  }

  %PrepareFunctionForOptimization(arrayMin);
  %PrepareFunctionForOptimization(arrayMin);
  assertEquals(11.03, arrayMin([11.03, 16.11, 26.06]));

  %PrepareFunctionForOptimization(arrayMax);
  %PrepareFunctionForOptimization(arrayMax);
  assertEquals(26.06, arrayMax([11.03, 16.11, 26.06]));
  %OptimizeFunctionOnNextCall(arrayMin);
  %OptimizeFunctionOnNextCall(arrayMax);

  assertEquals(11.03, arrayMin([11.03, 16.11, 26.06]));
  assertEquals(26.06, arrayMax([11.03, 16.11, 26.06]));

  assertOptimized(arrayMin);
  assertOptimized(arrayMax);

})();

// Test with holey double array and Math.min/max.
(function () {
  "use strict";
  function arrayMin(val) {
    return Math.min.apply(Math, val);
  }
  function arrayMax(val) {
    return Math.max.apply(Math, val);
  }

  %PrepareFunctionForOptimization(arrayMin);
  %PrepareFunctionForOptimization(arrayMin);
  assertEquals(NaN, arrayMin([11.03, 16.11, , 26.06]));

  %PrepareFunctionForOptimization(arrayMax);
  %PrepareFunctionForOptimization(arrayMax);
  assertEquals(NaN, arrayMax([11.03, 16.11, , 26.06]));
  %OptimizeFunctionOnNextCall(arrayMin);
  %OptimizeFunctionOnNextCall(arrayMax);

  assertEquals(NaN, arrayMin([11.03, 16.11, , 26.06]));
  assertEquals(NaN, arrayMax([11.03, 16.11, , 26.06]));

  assertOptimized(arrayMin);
  assertOptimized(arrayMax);

})();

// Test Reflect.apply().
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y ,z) {
    return Reflect.apply(sum_js, null, ["", x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test Reflect.apply() with empty array.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42;
  }
  function foo() {
    return Reflect.apply(fortytwo, null, []);
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(42, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(42, foo());
  assertFalse(got_interpreted);
  assertOptimized(foo);
})();

// Test Reflect.apply() with empty array that changes size.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42 + arguments.length;
  }

  var len = 2;
  function foo() {
    let args = []
    for (var i = 0; i < len; i++) { args.push(1); }
    let result = Reflect.apply(fortytwo, null, args);
    return result;
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(44, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(44, foo());
  assertTrue(got_interpreted);
  assertOptimized(foo);

  len = 0;
  assertEquals(42, foo());
  assertFalse(got_interpreted);
  assertOptimized(foo);
})();

// Test JSCallReducer::ReduceJSCallWithSpread.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y, z) {
    const numbers = ["", x, y, z];
    return sum_js(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test spread call with empty array.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    const args = [];
    return sum_js(x, y, z, ...args);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test spread call with empty array that changes size.
(function () {
  "use strict";
  var max_got_interpreted = true;
  function max() {
    max_got_interpreted = %IsBeingInterpreted();
    return Math.max(...arguments);
  }

  var len = 2;
  function foo(x, y, z) {
    let args = [];
    for (var i = 0; i < len; i++) { args.push(4 + i); }
    return max(x, y, z, ...args);
  }

  %PrepareFunctionForOptimization(max);
  %PrepareFunctionForOptimization(foo);
  assertEquals(5, foo(1, 2, 3));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(max_got_interpreted);
  assertEquals(5, foo(1, 2, 3));
  assertTrue(max_got_interpreted);
  assertOptimized(foo);

  len = 0;
  assertEquals(3, foo(1, 2, 3));
  assertFalse(max_got_interpreted);
  assertOptimized(foo);
})();

// Test spread call with more args.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d, e, f, g) {
    assertEquals(7, arguments.length);
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + e + f + g;
  }
  function foo(x, y, z) {
    const numbers = ["", z, y, x];
    return sum_js(x, y, z, ...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abccba', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abccba', foo('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test on speculative optimization of introduced JSCall (array_size != 0).
(function () {
  "use strict";
  var F = Math.max;
  var len;
  function foo(x, y, z) {
    var args = [z];
    for (var i = 0; i < len; i++) { args.push(0); }
    return F(x, y, ...args);
  }
  function foo1(x, y, z) {
    var args = [z];
    for (var i = 0; i < len; i++) { args.push(0); }
    return F(x, y, ...args);
  }

  // Optimize JSCallWithSpread and Math.max
  len = 0;
  %PrepareFunctionForOptimization(foo);
  assertEquals(3, foo(1, 2, 3));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(3, foo(1, 2, 3));
  assertOptimized(foo);
  // Deoptimize when input of Math.max is not number
  foo('a', 'b', 3);
  assertUnoptimized(foo);

  // Optimize JSCallWithSpread and Math.max
  len = 2;
  %PrepareFunctionForOptimization(foo1);
  assertEquals(3, foo1(1, 2, 3));
  %OptimizeFunctionOnNextCall(foo1);
  assertEquals(3, foo1(1, 2, 3));
  //Deoptimize when array length changes
  assertUnoptimized(foo1);
})();

// Test on speculative optimization of introduced JSCall (array_size == 0).
(function () {
  "use strict";
  var F = Math.max;
  var len;
  function foo(x, y, z) {
    var args = [];
    for (var i = 0; i < len; i++) { args.push(z); }
    return F(x, y, ...args);
  }
  function foo1(x, y, z) {
    var args = [];
    for (var i = 0; i < len; i++) { args.push(z); }
    return F(x, y, ...args);
  }

  // Optimize JSCallWithSpread and Math.max
  len = 0;
  %PrepareFunctionForOptimization(foo);
  assertEquals(2, foo(1, 2, 3));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(2, foo(1, 2, 3));
  assertOptimized(foo);
  // Deoptimzie when input of Math.max is not number
  foo('a', 'b', 3);
  assertUnoptimized(foo);

  // Optimize JSCallWithSpread and Math.max
  len = 2;
  %PrepareFunctionForOptimization(foo1);
  assertEquals(3, foo1(1, 2, 3));
  %OptimizeFunctionOnNextCall(foo1);
  assertEquals(3, foo1(1, 2, 3));
  assertOptimized(foo1);
  // No Deoptimization when array length changes
  foo(1, 2, 3);
  assertUnoptimized(foo);
})();

// Test apply on JSCreateClosure.
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function foo_closure() {
    return function(a, b, c, d) {
      sum_got_interpreted = %IsBeingInterpreted();
      return a + b + c + d;
    }
  }
  const _foo_closure = foo_closure();
  %PrepareFunctionForOptimization(_foo_closure);

  function foo(x, y, z) {
    return foo_closure().apply(null, ["", x, y, z]);
  }

  %PrepareFunctionForOptimization(foo_closure);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo_closure);
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply with JSBoundFunction
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e;
  }
  const f = sum_js.bind({ x: 26 }, 11, 3);
  function foo(a, b, c) {
    return f.apply(null, [a, b, c]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(166, foo(40, 42, 44));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals(166, foo(40, 42, 44));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply with nested bindings
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e;
  }
  const f = sum_js.bind({ x: 26 }, 11).bind({ y: 4 }, 3);
  function foo(x, y, z) {
    return f.apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(166, foo(40, 42, 44));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals(166, foo(40, 42, 44));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply on bound function (JSCreateBoundFunction).
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d;
  }
  function foo(x, y, z) {
    return sum_js.bind({ x: 42 }).apply(null, ["", x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('42abc', foo('a', 'b', 'c'));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals('42abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply on bound function (JSCreateBoundFunction) with args.
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e, f) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e + f;
  }
  function foo(x, y, z) {
    return sum_js.bind({ x: 3 }, 11, 31).apply(null, ["", x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('45abc', foo('a', 'b', 'c'));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals('45abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test call with array-like under-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + arguments.length;
  }
  function foo(x, y) {
    return sum_js.apply(null, ["", x, y]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('ABundefined3', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('ABundefined3', foo('A', 'B'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with array-like over-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + arguments.length;
  }
  function foo(v, w, x, y, z) {
    return sum_js.apply(null, ["", v, w, x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc6', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc6', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with spread under-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + arguments.length;
  }
  function foo(x, y) {
    const numbers = ["", x, y];
    return sum_js(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('ABundefined3', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('ABundefined3', foo('A', 'B'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with spread over-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + arguments.length;
  }
  function foo(v, w, x, y, z) {
    const numbers = ["", v, w, x, y, z];
    return sum_js(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc6', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc6', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test calling function that has rest parameters.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, ...moreArgs) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + moreArgs[0] + moreArgs[1] + moreArgs[2] + moreArgs[3];
  }
  function foo(v, w, x, y, z) {
    return sum_js.apply(null, ["", v, w, x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abcde', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abcde', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with 'arguments'.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo() {
    return sum_js.apply(null, arguments);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js_got_interpreted);

  // The call is not inlined with CreateArguments.
  %OptimizeFunctionOnNextCall(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with inlined calls.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d;
  }
  function foo(x, y, z) {
    return sum_js.apply(null, ["", x, y, z]);
  }
  function bar(a, b, c) {
    return foo(c, b, a);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  %PrepareFunctionForOptimization(bar);
  assertEquals('cba', bar('a', 'b', 'c'));
  assertTrue(sum_js_got_interpreted);

  // Optimization also works if the call is in an inlined function.
  %OptimizeFunctionOnNextCall(bar);
  assertEquals('cba', bar('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(bar);
})();
