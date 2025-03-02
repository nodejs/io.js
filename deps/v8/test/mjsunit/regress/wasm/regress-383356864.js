// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-wasm-loop-unrolling

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $struct0 =
  builder.addStruct([makeField(kWasmI32, true)], kNoSuperType, true);
let $struct1 =
  builder.addStruct([makeField(kWasmExternRef, true)], kNoSuperType, true);
let $sig2 = builder.addType(makeSig([kWasmAnyRef], []));
let $sig3 = builder.addType(kSig_v_i);
let $sig4 = builder.addType(makeSig([kWasmAnyRef], [kWasmExternRef]));
let external_func0 = builder.addImport('js', 'external_func', $sig2);
let doit1 = builder.addFunction(undefined, $sig3).exportAs('doit');
let read2 = builder.addFunction(undefined, $sig4).exportAs('read');

doit1.addLocals(kWasmAnyRef, 4)
  .addBody([
    kGCPrefix, kExprStructNewDefault, $struct0,
    kExprLocalSet, 1,
    kGCPrefix, kExprStructNewDefault, $struct0,
    kExprLocalSet, 2,
    kGCPrefix, kExprStructNewDefault, $struct0,
    kExprLocalSet, 3,
    kGCPrefix, kExprStructNewDefault, $struct1,
    kExprLocalSet, 4,
    kExprLoop, kWasmVoid,
      kExprLocalGet, 1,
      kGCPrefix, kExprRefCast, $struct0,
      kExprLocalGet, 0,
      kGCPrefix, kExprStructSet, $struct0, 0,
      kExprLocalGet, 1,
      kExprCallFunction, external_func0,
      kExprLocalGet, 2,
      kExprLocalSet, 1,
      kExprLocalGet, 3,
      kExprLocalSet, 2,
      kExprLocalGet, 4,
      kExprLocalSet, 3,
      kExprBr, 0,
    kExprEnd,
    kExprUnreachable,
  ]);

read2.addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprRefCast, $struct1,
    kGCPrefix, kExprStructGet, $struct1, 0,
  ]);

let call_count = 0;
let wasm_inst = builder.instantiate({
  "js": {
      "external_func": (ref) => {
        call_count += 1;
        if (call_count == 4) {
          fakeobj = wasm_inst.exports['read'](ref);
          throw 'unreachable';
        }
      }
  }
});

let doit = wasm_inst.exports['doit'];
%WasmTierUpFunction(doit);
assertTraps(kTrapIllegalCast, () => doit(0));
