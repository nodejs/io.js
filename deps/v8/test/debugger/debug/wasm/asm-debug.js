// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --validate-asm

Debug = debug.Debug

// Initialized in setup().
var exception;
var break_count;
var num_wasm_scripts;
var module;

var expected_stack_entries = [];

function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break) {
      ++break_count;
      // Request frame details.
      var num_frames = exec_state.frameCount();
      assertEquals(
          expected_stack_entries.length, num_frames, 'number of frames');
      print('Stack Trace (length ' + num_frames + '):');
      for (var i = 0; i < num_frames; ++i) {
        var frame = exec_state.frame(i);
        var line = frame.sourceLine();
        var column = frame.sourceColumn() + 1;
        var funcName = frame.func().name();
        print(
            '  [' + i + '] ' + funcName + ' (' + line + ':' + column + ')');
        assertEquals(
            expected_stack_entries[i][0], funcName, 'function name at ' + i);
        assertEquals(expected_stack_entries[i][1], line, 'line at ' + i);
        assertEquals(expected_stack_entries[i][2], column, 'column at ' + i);
      }
    }
  } catch (e) {
    print('exception: ' + e);
    exception = e;
  }
};

function generateWasmFromAsmJs(stdlib, foreign, heap) {
  'use asm';
  var debugger_fun = foreign.call_debugger;
  function callDebugger() {
    debugger_fun();
  }
  function redirectFun() {
    callDebugger();
  }
  return redirectFun;
}

function call_debugger() {
  debugger;
}

function setup() {
  exception = null;
  break_count = 0;
}

(function FrameInspection() {
  setup();
  var fun =
      generateWasmFromAsmJs(this, {'call_debugger': call_debugger}, undefined);
  expected_stack_entries = [
    ['call_debugger', 58, 3],    // --
    ['callDebugger', 49, 5],     // --
    ['redirectFun', 52, 5],      // --
    ['FrameInspection', 78, 3],  // --
    ['', 82, 3]
  ];
  Debug.setListener(listener);
  fun();
  Debug.setListener(null);
  assertEquals(1, break_count);
  if (exception) throw exception;
})();
