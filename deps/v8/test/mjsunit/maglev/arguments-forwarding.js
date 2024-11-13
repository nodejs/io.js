// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function optimize(f, a) {
  %PrepareFunctionForOptimization(f);
  f(...a);
  f(...a);
  %OptimizeFunctionOnNextCall(f);
  return f(...a);
}
%NeverOptimizeFunction(optimize);

// Top level versions

// Sloppy arguments with no mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Sloppy arguments with mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Strict arguments.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Sloppy arguments with no mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 42);
})();

// Sloppy arguments with mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 42);
})();

// Sloppy arguments with mapped count with mapped update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    y = 42;
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 42);
})();

// Strict arguments with updates before call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  assertEquals(optimize(foo, [1, 2, 3]), 42);
})();

// Sloppy arguments with no mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Sloppy arguments with mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Strict arguments with updates after call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 2);
})();

// Sloppy arguments with no mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 44);
})();

// Sloppy arguments with mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 44);
})();

// Strict arguments with updates after call in a loop.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  assertEquals(optimize(foo, [1, 2, 3]), 44);
})();


// Inlined versions

// Sloppy arguments with no mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Strict arguments.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with no mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with mapped count with mapped update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    y = 42;
    return bar.apply(this, arguments);
  }
function top() {
  return foo(1, 2, 3);
}
%PrepareFunctionForOptimization(foo);
assertEquals(optimize(top, []), 42);
})();

// Strict arguments with updates before call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with no mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Strict arguments with updates after call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with no mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();

// Sloppy arguments with mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();

// Strict arguments with updates after call in a loop.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();

// Inline function propagating inlined arguments.

// Sloppy arguments with no mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with mapped count.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Strict arguments.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with no mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with mapped count with update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with mapped count with mapped update before call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    y = 42;
    return bar.apply(this, arguments);
  }
function top() {
  return foo(1, 2, 3);
}
%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
assertEquals(optimize(top, []), 42);
})();

// Strict arguments with updates before call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    arguments[1] = 42;
    return bar.apply(this, arguments);
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 42);
})();

// Sloppy arguments with no mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with mapped count with update after call.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Strict arguments with updates after call.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = bar.apply(this, arguments);
    arguments[1] = 42;
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 2);
})();

// Sloppy arguments with no mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo() {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();

// Sloppy arguments with mapped count with update after call in a loop.
(function() {
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();

// Strict arguments with updates after call in a loop.
(function() {
  "use strict";
  function bar(x, y) { return y; }
  function foo(x, y) {
    let r = 0;
    for (let i = 0; i < 2; i++) {
      r += bar.apply(this, arguments);
      arguments[1] = 42;
    }
    return r;
  }
  function top() {
    return foo(1, 2, 3);
  }
  %PrepareFunctionForOptimization(bar);
  %PrepareFunctionForOptimization(foo);
  assertEquals(optimize(top, []), 44);
})();
