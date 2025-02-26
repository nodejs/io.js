// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// Testing instanceof and builtin continuations.
// (copy-pasted from test/mjsunit/maglev/nested-continuations.js)

function Foo() {}
Object.defineProperty(Foo, Symbol.hasInstance, { value: Math.round });

let foo = new Foo();

function test_instanceof_foo(f) {
  // `f instanceof Foo` runs `%ToBoolean(Foo[Symbol.hasInstance](f))`, where
  // `Foo[Symbol.hasInstance]` is `Math.round`.
  //
  // So with sufficient builtin inlining, this will call
  // `%ToBoolean(round(%ToNumber(f)))`, which will call `f.valueOf`. If this
  // deopts (which in this test it will), we need to make sure to both round it,
  // and then convert that rounded value to a boolean.
  return f instanceof Foo;
}

foo.valueOf = () => {
  %DeoptimizeFunction(test_instanceof_foo);
  // Return a value which, when rounded, has ToBoolean false, and when not
  // rounded, has ToBoolean true.
  return 0.2;
}

%PrepareFunctionForOptimization(test_instanceof_foo);
assertFalse(test_instanceof_foo(foo));
assertFalse(test_instanceof_foo(foo));

%OptimizeFunctionOnNextCall(test_instanceof_foo);
assertFalse(test_instanceof_foo(foo));
