// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-source-phase-imports --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function makeBuilder() {
  var builder = new WasmModuleBuilder();

  builder.addFunction("f", kSig_v_v)
    .addBody([])
    .exportFunc();
  return builder;
}

var AbstractModuleSource = %GetAbstractModuleSource();
assertEquals(
  Object.getPrototypeOf(WebAssembly.Module.prototype),
  AbstractModuleSource.prototype);
assertEquals(
  Object.getPrototypeOf(AbstractModuleSource.prototype),
  Object.prototype);

var ToStringTag = Object
  .getOwnPropertyDescriptor(
    AbstractModuleSource.prototype,
    Symbol.toStringTag,
  ).get;

var builder = makeBuilder();
var module = new WebAssembly.Module(builder.toBuffer());
assertEquals(Object.getPrototypeOf(module), WebAssembly.Module.prototype);
assertTrue(module instanceof WebAssembly.Module);
assertTrue(module instanceof AbstractModuleSource);
assertEquals(ToStringTag.call(module), "WebAssembly.Module");
