// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function dbg(x) {
  debugger;
}

function foo() {
  arguments[0];
  dbg();
}

function bar() {
  var t = { a : 1 };
  dbg();
  return t.a;
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
foo(1);
foo(1);
bar(1);
bar(1);
%OptimizeFunctionOnNextCall(foo);
%OptimizeFunctionOnNextCall(bar);

var Debug = debug.Debug;
Debug.setListener(function(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  for (var i = 0; i < exec_state.frameCount(); i++) {
    var f = exec_state.frame(i);
    for (var j = 0; j < f.localCount(); j++) {
      if (f.localValue(j).isUnavailable()) {
        print(`${f.localName(j)} is unavailable`);
      } else {
        print("'" + f.localName(j) + "' = " + f.localValue(j).value());
      }
    }
  }
});

foo(1);
bar(1);
