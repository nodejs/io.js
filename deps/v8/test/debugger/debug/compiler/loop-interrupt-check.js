// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-maglev --no-stress-concurrent-inlining
// Flags: --invocation-count-for-turbofan=3000

// This test checks that loop interrupt stack checks are properly detected are
// processed.

// Note that interrupt requests are not handled at the same time in Maglev and
// Turbofan: in Maglev, they are not directly checked but we instead rely on the
// fact budget interrupt will perform stack check interrupt, which in Turbofan,
// they are checked at every loop iteration.

var Debug = debug.Debug;

var trigger_count = 0;
var called_from;
Debug.setListener(function (event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    called_from = exec_state.frames[0].functionName;
    trigger_count += 1;
  }
});

function g(x) {
  if (x == 0) {
    %ScheduleBreak();
  }
  return 7;
}

function loop_interrupt_check_f(x) {
  let r = 0;
  for (let i = 0; i < 10; i++) {
    if (i == 0) {
      // Schedules an interrupt if {x} is 0.
      r += g(x);
    } else {
      r += g(1);
    }
  }
  return r;
}

%PrepareFunctionForOptimization(loop_interrupt_check_f);
assertEquals(70, loop_interrupt_check_f(1));
%OptimizeFunctionOnNextCall(loop_interrupt_check_f);
assertEquals(70, loop_interrupt_check_f(1));
assertOptimized(loop_interrupt_check_f);

assertEquals(0, trigger_count);
assertEquals(70, loop_interrupt_check_f(0));
assertEquals(1, trigger_count);
assertEquals("loop_interrupt_check_f", called_from);
assertOptimized(loop_interrupt_check_f);
