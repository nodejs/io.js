// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction("isNull", makeSig([kWasmAnyRef], [kWasmI32]))
  .addBody([kExprLocalGet, 0, kExprRefIsNull]).exportFunc();

const instance = builder.instantiate({});
// JS can't directly do anything with %WasmNull as it does not support handling
// wasm null values (accessing most properties excluding the map will crash).
// So we internalize it in wasm and check for null.
assertEquals(1, instance.exports.isNull(%WasmNull()));
