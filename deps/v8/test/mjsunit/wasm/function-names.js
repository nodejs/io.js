// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();

var last_func_index = builder.addFunction("exec_unreachable", kSig_v_v)
  .addBody([kExprUnreachable]).index

var illegal_func_name = [0xff];
var func_names = [ "☠", illegal_func_name, "some math: (½)² = ¼", "" ];
var expected_names = ["exec_unreachable", "☠", null,
                      "some math: (½)² = ¼", "", "main"];

for (var func_name of func_names) {
  last_func_index = builder.addFunction(func_name, kSig_v_v)
    .addBody([kExprCallFunction, last_func_index]).index;
}

builder.addFunction("main", kSig_v_v)
  .addBody([kExprCallFunction, last_func_index])
  .exportFunc();

var module = builder.instantiate();

(function testFunctionNamesAsString() {
  var names = expected_names.concat(["testFunctionNamesAsString", null]);
  try {
    module.exports.main();
    assertFalse("should throw");
  } catch (e) {
    var lines = e.stack.split(/\r?\n/);
    lines.shift();
    assertEquals(names.length, lines.length);
    for (var i = 0; i < names.length; ++i) {
      var line = lines[i].trim();
      if (names[i] === null) continue;
      var printed_name = names[i];
      var expected_start = "at " + printed_name + " (";
      assertTrue(line.startsWith(expected_start),
          "should start with '" + expected_start + "': '" + line + "'");
    }
  }
})();

// For the remaining tests, collect the Callsite objects instead of just a
// string:
Error.prepareStackTrace = function(error, frames) {
  return frames;
};

(function testFunctionNamesAsCallSites() {
  var names = expected_names.concat(['testFunctionNamesAsCallSites', null]);
  try {
    module.exports.main();
    assertFalse('should throw');
  } catch (e) {
    assertEquals(names.length, e.stack.length, 'stack length');
    for (var i = 0; i < names.length; ++i) {
      assertEquals(
          names[i], e.stack[i].getFunctionName(), 'function name at ' + i);
    }
  }
})();
