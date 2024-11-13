// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-turboshaft-loop-unrolling --expose-gc --enable-inspector

var Debug = debug.Debug;

Debug.setListener(function (event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    gc();
  }
});

function foo(b) {
  %ScheduleBreak();

  // Allocating a first object.
  let o1 = { x : 42 };

  // Inserting a loop, in order to get a JSStackCheck.
  let v = 0;
  for (; v < 42; v = (v + 2) * 2) {}

  // Allocating a second object. This should not get allocation-folded with the
  // first allocation, because of the JSStackCheck's GC in the middle.
  let o2 = { x : o1, y : v };

  return o2;
}

%PrepareFunctionForOptimization(foo);
assertEquals({ x : { x : 42 }, y : 60 }, foo());
assertEquals({ x : { x : 42 }, y : 60 }, foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals({ x : { x : 42 }, y : 60 }, foo());
