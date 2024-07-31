// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Debug = debug.Debug
var exception = null;
var break_count = 0;
const expected_breaks = 9;

var expected_values =
  [ReferenceError, ReferenceError, 0, 0, 0, 0, 1,
   ReferenceError, ReferenceError];

function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break) {
      assertTrue(exec_state.frameCount() != 0, "FAIL: Empty stack trace");
      var frameMirror = exec_state.frame(0);

      var v = null;;
      try {
        v = frameMirror.evaluate('i').value();
      } catch(e) {
        v = e;
      }
      frameMirror.allScopes();
      var source = frameMirror.sourceLineText();
      print("paused at: " + source);
      assertTrue(source.indexOf("// Break " + break_count + ".") > 0,
                 "Unexpected pause at: " + source + "\n" +
                 "Expected: // Break " + break_count + ".");
      if (expected_values[break_count] === ReferenceError) {
        assertTrue(v instanceof ReferenceError);
      } else {
        assertSame(expected_values[break_count], v);
      }
      ++break_count;

      if (break_count !== expected_breaks) {
        exec_state.prepareStep(Debug.StepAction.StepInto);
        print("Next step prepared");
      }
    }
  } catch(e) {
    exception = e;
    print(e, e.stack);
  }
};

Debug.setListener(listener);

var sum = 0;
(function (){
  'use strict';

  debugger; // Break 0.

  for (let i=0; // Break 1.
       i < 1;   // Break 2.  // Break 6.
       i++) {   // Break 5.
    let key = i; // Break 3.
    sum += key;   // Break 4.
  }
}()); // Break 7.

assertNull(exception); // Break 8.
assertEquals(expected_breaks, break_count);

Debug.setListener(null);
