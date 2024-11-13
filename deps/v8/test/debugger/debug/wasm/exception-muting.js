// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test setting "Never Pause Here" breakpoint in Wasm.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const body_a = [
  kExprUnreachable,  // unreachable
];
const fun_a = builder.addFunction('a', kSig_v_v).addBody(body_a).exportFunc();

const instance = builder.instantiate();

Debug = debug.Debug;

const expected_breaks = [
  `$a:1:${fun_a.body_offset}`,       // break in a at unreachable
];

let error;
function onBreak(event, exec_state, data) {
  try {
    assertFalse(event == Debug.DebugEvent.Break, 'no breakpoints trigger');
    if (event != Debug.DebugEvent.Exception) return;
    if (error) return;
    const pos =
        [data.functionName(), data.sourceLine(), data.sourceColumn()].join(':');
    const loc = [pos, data.sourceLineText()].join(':');
    print(`Break at ${loc}`);
    assertTrue(expected_breaks.length > 0, 'expecting more breaks');
    const expected_pos = expected_breaks.shift();
    assertEquals(expected_pos, pos);
  } catch (e) {
    if (!error) error = e;
  }
}

function testWrapper() {
  print('Running a().');
  try {
    instance.exports.a();
    print('Exited normally???');
    error = new Error('Function should exit with Wasm trap');
  } catch(e) {
    print('Exception thrown');
  }
  print('Returned from wasm.');
}

Debug.setBreakOnException();
Debug.setListener(onBreak);
testWrapper();
assertEquals(0, expected_breaks.length, 'all breaks were hit');
const breakpoint_a = Debug.setBreakPoint(instance.exports.a, 0, 0, 'false');
testWrapper();
if (error) throw error;
