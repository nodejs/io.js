// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-experimental-wasm-stringref
// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function instantiateModuleWithStringRef() {
  // Build a WebAssembly module which uses stringref features.
  const builder = new WasmModuleBuilder();
  builder.addFunction("main",
                      makeSig([kWasmStringRef], [kWasmStringRef]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();
  return builder.instantiate();
}

// Due to --noexperimental-wasm-stringref stringrefs are not supported.
assertThrows(instantiateModuleWithStringRef, WebAssembly.CompileError);
// Disable imported strings explicitly.
%SetWasmImportedStringsEnabled(false);
assertThrows(instantiateModuleWithStringRef, WebAssembly.CompileError);
// Enable imported strings explicitly.
%SetWasmImportedStringsEnabled(true);
let str = "Hello World!";
assertSame(str, instantiateModuleWithStringRef().exports.main(str));
