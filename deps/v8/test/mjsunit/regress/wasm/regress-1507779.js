// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation

// This tests an issue where we created phis for rtts in Turboshaft, but then
// assumed every rtt is an RttCanonOp. This is fixed by resolving Phis to
// identical RttCanonOps to their argument.
// To reproduce we need:
// - A loop with an allocation and an identical allocation after the loop. The
//   loop allocation has to dominate the outer allocation, but not its copies
//   after unrolling.
// - An inner loop which gets eliminated by peeling. This is required so that
//   the outer loop is not peeled, but then is unrolled.
// This way the outer allocation's RttCanonOp will be value-eliminated to the
// loop allocation, which will then be duplicated during unrolling, which will
// result in the outer allocation using a Phi as rtt.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

let struct_type = builder.addStruct([makeField(kWasmI32, true)]);

builder.addFunction("main", makeSig([kWasmI32], [wasmRefNullType(struct_type),
                                                 wasmRefNullType(struct_type)]))
  .addLocals(wasmRefNullType(struct_type), 1)
  .addLocals(kWasmI32, 2)
  .addBody([
    kExprLoop, kWasmVoid,
      kExprLocalGet, 0, kExprI32Const, 0, kExprI32LtS, kExprBrIf, 0,
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_type,
      kExprLocalSet, 1,
      kExprI32Const, 0, kExprLocalSet, 3,
      kExprLoop, kWasmVoid,
        kExprLocalGet, 3, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 3,
        kExprLocalGet, 3, kExprI32Const, 1, kExprI32LtS,
        kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 2, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 2,
      kExprLocalGet, 2, kExprI32Const, 10, kExprI32LtS,
      kExprBrIf, 0,
    kExprEnd,
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
    kExprLocalGet, 1
  ])

builder.instantiate();
