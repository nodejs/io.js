// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
d8.file.execute('test/mjsunit/mjsunit.js');

let b1 = new WasmModuleBuilder();
let $a8 = b1.addArray(kWasmI8, true, kNoSuperType, true);
let a8ref = wasmRefType($a8);
b1.addFunction("make_array", makeSig([], [a8ref])).exportFunc()
  .addBody([
    kExprI32Const, 10,  // length
    kGCPrefix, kExprArrayNewDefault, $a8,
  ]);

let instance1 = b1.instantiate();
let array = instance1.exports.make_array();

let b2 = new WasmModuleBuilder();
let $struct0 = b2.addStruct([makeField(kWasmI32, true)]);
let struct0ref = wasmRefType($struct0);
let tag0_sig = b2.addType(makeSig([struct0ref], []));
let $tag0 = b2.addTag(tag0_sig);
let create_exn = b2.addImport("", "create_exn", kSig_v_v);
b2.addExportOfKind("tag0", kExternalTag, $tag0);
b2.addFunction("main", kSig_v_v).exportFunc().addBody([
    kExprCallFunction, create_exn,
]);

let exported_tag;
let instance2 = b2.instantiate({
  "": {
    create_exn: () => {
      assertThrows(() => new WebAssembly.Exception(exported_tag, [array]),
          TypeError,
          /object is not a subtype of expected type/);
    },
  }
});
exported_tag = instance2.exports.tag0;
instance2.exports.main();
