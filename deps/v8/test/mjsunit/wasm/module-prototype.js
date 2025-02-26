// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-js-source-phase-imports

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function makeBuilder() {
  var builder = new WasmModuleBuilder();

  builder.addFunction("f", kSig_v_v)
    .addBody([])
    .exportFunc();
  return builder;
}

assertEquals(
  Object.getPrototypeOf(WebAssembly.Module.prototype),
  Object.prototype);

var builder = makeBuilder();
var module = new WebAssembly.Module(builder.toBuffer());
var module_prototype = Object.getPrototypeOf(module);
assertEquals(module_prototype, WebAssembly.Module.prototype);
assertTrue(module instanceof WebAssembly.Module);
assertEquals(module[Symbol.toStringTag], "WebAssembly.Module");
