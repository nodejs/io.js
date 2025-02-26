// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --expose-gc

// Helper to convert setTimeout into an awaitable promise.
function asyncTimeout(timeout) {
  return new Promise((resolve, reject)=>{
    setTimeout(resolve, timeout);
  })
}

function Foo() {}

function getX(o) { return o.x; }

(async function() {
  let o = new Foo();
  // Transition o:Foo to o:Foo{x}. This transition is important, as the o:Foo
  // map is the initial map for the Foo constructor, and so is strongly held by
  // it. We want o to be the only strong holder of its map.
  o.x = 42;
  %CompleteInobjectSlackTracking(new Foo());

  // Warm up 'getX' with 'Foo{x}' feedback for its o.x access.
  %PrepareFunctionForOptimization(getX);
  assertEquals(getX(o), 42);
  assertEquals(getX(o), 42);

  // Clear out 'o', which is the only strong holder of the Foo{x} map.
  let weak_o = new WeakRef(o);
  o = null;

  // Tick the message loop so that the weak ref can be collected.
  await asyncTimeout(0);

  // Collect the old 'o', which will also collect the 'Foo{x}' map.
  // We need to invoke GC asynchronously and wait for it to finish, so that
  // it doesn't need to scan the stack. Otherwise, some objects may not be
  // reclaimed because of conservative stack scanning and the test may not
  // work as intended.
  await gc({ type: 'major', execution: 'async' });

  // Make sure the old 'o' was collected.
  assertEquals(undefined, weak_o.deref());

  // Optimize the function with the current monomorphic 'Foo{x}' map o.x access,
  // where the 'Foo{x}' map is dead and therefore the map set is empty. Then,
  // create a new 'Foo{x}' object and pass that through. This compilation and
  // o.x access should still succeed despite the dead map.
  %OptimizeFunctionOnNextCall(getX);
  o = new Foo();
  o.x = 42;
  assertEquals(getX(o), 42);
})();
