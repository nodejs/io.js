// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

Debug = debug.Debug

var exception = null;
var break_count = 0;

const expected_frames = [
  // func-name; wasm?; pos; line; col
  ['call_debugger', false],        // --
  ['$wasm_2', true, 56, 1, 60],    // --
  ['$wasm_1', true, 52, 1, 55],    // --
  ['testFrameInspection', false],  // --
  ['', false]
];

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  ++break_count;
  try {
    var frame_count = exec_state.frameCount();
    assertEquals(expected_frames.length, frame_count, 'frame count');

    for (var i = 0; i < frame_count; ++i) {
      var frame = exec_state.frame(i);
      assertEquals(expected_frames[i][0], frame.func().name(), 'name at ' + i);
      if (expected_frames[i][1]) {  // wasm frame?
        assertEquals(expected_frames[i][3], frame.sourceLine(), 'line at ' + i);
        assertEquals(expected_frames[i][4], frame.sourceColumn(),
            'column at ' + i);
      }
    }
  } catch (e) {
    exception = e;
  }
};

var builder = new WasmModuleBuilder();

// wasm_1 calls wasm_2 on offset 2.
// wasm_2 calls call_debugger on offset 1.

builder.addImport("mod", 'func', kSig_v_v);

builder.addFunction('wasm_1', kSig_v_v)
    .addBody([kExprNop, kExprCallFunction, 2])
    .exportAs('main');

builder.addFunction('wasm_2', kSig_v_v).addBody([kExprCallFunction, 0]);

function call_debugger() {
  debugger;
}

var module = builder.instantiate({mod: {func: call_debugger}});

(function testFrameInspection() {
  Debug.setListener(listener);
  module.exports.main();
  Debug.setListener(null);

  assertEquals(1, break_count);
  if (exception) throw exception;
})();
