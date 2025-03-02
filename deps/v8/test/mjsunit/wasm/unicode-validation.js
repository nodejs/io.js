// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function toByteArray(s) {
  var arr = [];
  for (var i = 0; i < s.length; ++i) {
    arr.push(s.charCodeAt(i) & 0xff);
  }
  return arr;
}

function toString(arr) {
  if (typeof arr === "string") return arr;
  var s = "";
  for (var b of arr) s += String.fromCharCode(b);
  return s;
}

function toUTF8(arr) {
  if (typeof arr === "string" || arr === undefined) return arr;
  return decodeURIComponent(escape(toString(arr)));
}

function isValidUtf8(arr) {
  if (typeof arr === "string" || arr === undefined) return true;
  try {
    var s = toUTF8(arr);
    for (var i = 0; i < s.length; ++i)
      if ((s.charCodeAt(i) & 0xfffe) == 0xfffe)
        return false;
    return true;
  } catch (e) {
    if (e instanceof URIError) return false;
    throw e;
  }
}

function checkImportsAndExports(imported_module_name, imported_function_name,
    internal_function_name, exported_function_name, shouldThrow) {
  var builder = new WasmModuleBuilder();

  builder.addImport(imported_module_name, imported_function_name,
      kSig_v_v);

  builder.addFunction(internal_function_name, kSig_v_v)
    .addBody([kExprCallFunction, 0])
    .exportAs(exported_function_name);

  // Consistency check: does javascript agree with our shouldThrow annotation?
  assertEquals(shouldThrow,
      !isValidUtf8(imported_module_name) ||
          !isValidUtf8(imported_function_name) ||
          !isValidUtf8(exported_function_name),
      "JavaScript does not agree with our shouldThrow expectation");

  if (!shouldThrow) {
    imported_module_name = toUTF8(imported_module_name);
    imported_function_name = toUTF8(imported_function_name);
  }

  var ffi = new Object();
  if (imported_function_name === undefined) {
    ffi[imported_module_name] = function() { };
  } else {
    ffi[imported_module_name] = new Object();
    ffi[imported_module_name][imported_function_name] = function() { };
  }

  var hasThrown = true;
  try {
    builder.instantiate(ffi);
    hasThrown = false;
  } catch (err) {
    if (!shouldThrow) print(err);
    assertTrue(shouldThrow, "Should not throw error on valid names");
    assertTrue(err instanceof Error, "exception should be an Error");
    assertContains("UTF-8", err.toString());
  }
  assertEquals(shouldThrow, hasThrown,
      "Should throw validation error on invalid names");
}

function checkImportedModuleName(name, shouldThrow) {
  checkImportsAndExports(name, "imp", "func", undefined, shouldThrow);
}

function checkImportedFunctionName(name, shouldThrow) {
  checkImportsAndExports("module", name, "func", "func", shouldThrow);
}

function checkExportedFunctionName(name, shouldThrow) {
  checkImportsAndExports("module", "func", "func", name, shouldThrow);
}

function checkInternalFunctionName(name) {
  checkImportsAndExports("module", "func", name, "func", false);
}

function checkAll(name, shouldThrow) {
  checkImportedModuleName(name, shouldThrow);
  checkImportedFunctionName(name, shouldThrow);
  checkExportedFunctionName(name, shouldThrow);
  checkInternalFunctionName(name);
}

checkAll("ascii", false);
checkAll("some math: (½)² = ¼", false);
checkAll("中国历史系列条目\n北", false);
checkAll(toByteArray("\xef\xb7\x8f"), false);
checkAll(toByteArray("a\xc2\x81\xe1\x80\xbf\xf1\x80\xa0\xbf"), false);
checkAll(toByteArray("\xff"), true);
checkAll(toByteArray("\xed\xa0\x8f"), true);        // surrogate code points
checkAll(toByteArray("\xe0\x82\x80"), true);        // overlong sequence
checkAll(toByteArray("\xf4\x90\x80\x80"), true);    // beyond limit: U+110000
checkAll(toByteArray("with\x00null"), false);
