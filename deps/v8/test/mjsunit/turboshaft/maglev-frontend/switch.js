// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f_switch(x) {
  switch(x) {
    // Need at least v8_flags.switch_table_min_cases (= 6) cases so that the
    // bytecode optimizes this switch with a jumptable rather than generating
    // cascading ifs/elses.
    case 3: return 3;
    case 4: return 5;
    case 5: return 7;
    case 6: return 11;
    // hole between 6 and 9
    case 9: return 13;
    // hole between 9 and 13
    case 13: return 17;
    default: return 19;
  }
}

%PrepareFunctionForOptimization(f_switch);
assertEquals(3, f_switch(3));
assertEquals(5, f_switch(4));
assertEquals(7, f_switch(5));
assertEquals(11, f_switch(6));
assertEquals(13, f_switch(9));
assertEquals(17, f_switch(13));
// Testing holes/default case
assertEquals(19, f_switch(0));
assertEquals(19, f_switch(2));
assertEquals(19, f_switch(7));
assertEquals(19, f_switch(8));
assertEquals(19, f_switch(10));
assertEquals(19, f_switch(12));
assertEquals(19, f_switch(42));

%OptimizeFunctionOnNextCall(f_switch);
assertEquals(3, f_switch(3));
assertOptimized(f_switch);
assertEquals(5, f_switch(4));
assertEquals(7, f_switch(5));
assertEquals(11, f_switch(6));
assertEquals(13, f_switch(9));
assertEquals(17, f_switch(13));
assertOptimized(f_switch);
// Testing holes/default case
assertEquals(19, f_switch(0));
assertEquals(19, f_switch(2));
assertEquals(19, f_switch(7));
assertEquals(19, f_switch(8));
assertEquals(19, f_switch(10));
assertEquals(19, f_switch(12));
assertEquals(19, f_switch(42));
assertOptimized(f_switch);
