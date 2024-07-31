// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmEqRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct])
    .exportFunc();
  builder.addFunction('passObj', makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([kExprLocalGet, 0])
    .exportFunc();
  return builder.instantiate({});
})();

let obj = instance.exports.createStruct(123);
// The struct is opaque and doesn't have any observable properties.
assertFalse(obj instanceof Object);
assertEquals([], Object.getOwnPropertyNames(obj));
// It can be passed as externref without any observable change.
let passObj = instance.exports.passObj;
let obj2 = passObj(obj);
assertFalse(obj2 instanceof Object);
assertEquals([], Object.getOwnPropertyNames(obj2));
assertSame(obj, obj2);
// A JavaScript object can be passed as externref.
let jsObject = {"hello": "world"};
assertSame(jsObject, passObj(jsObject));
