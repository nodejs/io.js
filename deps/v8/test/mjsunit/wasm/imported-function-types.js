// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var exporting_module = (function() {
  var builder = new WasmModuleBuilder();

  var binaryType = builder.addType(kSig_i_ii);
  var unaryType = builder.addType(kSig_i_i);

  builder.addFunction("func1", makeSig([wasmRefType(binaryType)], [kWasmI32]))
    .addBody([kExprI32Const, 42, kExprI32Const, 12, kExprLocalGet, 0,
              kExprCallRef, binaryType])
    .exportFunc();

  builder.addFunction("func2", makeSig([wasmRefType(unaryType)], [kWasmI32]))
    .addBody([kExprI32Const, 42, kExprLocalGet, 0, kExprCallRef, unaryType])
    .exportFunc();

  return builder.instantiate({});
})();

var importing_module = function(imported_function) {
  var builder = new WasmModuleBuilder();

  var unaryType = builder.addType(kSig_i_i);

  builder.addImport("other", "func",
    makeSig([wasmRefType(unaryType)], [kWasmI32]));

  return builder.instantiate({other: {func: imported_function}});
};

// Same form/different index should be fine.
importing_module(exporting_module.exports.func2);
// Same index/different form should throw.
assertThrows(
    () => importing_module(exporting_module.exports.func1),
    WebAssembly.LinkError,
    /imported function does not match the expected type/);
