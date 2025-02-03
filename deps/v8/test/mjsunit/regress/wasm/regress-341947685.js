// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.exportMemoryAs("mem", 0);

function GenerateInput(which) {
  switch (which) {
    case 0:
      // 16 lanes with "2" as 8-bit int.
      return wasmS128Const(new Array(16).fill(2));
    case 1:
      // 16 lanes with "3" as 8-bit int.
      return wasmS128Const(new Array(16).fill(3));
    case 2:
      // 4 lanes with "1000" as 32-bit int.
      return [
        ...wasmI32Const(1000),
        kSimdPrefix, kExprI32x4Splat,
      ];
    default:
      assertUnreachable();
  }
}

function MakeFunctionBody(variant) {
  let body = [];
  // Create some register pressure.
  let kNumDummyValues = 50;
  for (let i = 0; i < kNumDummyValues; i++) {
    body.push(
      kExprI32Const, i,
      kSimdPrefix, kExprI8x16Splat,
    );
  }
  // Prepare input values. For simplicity, we always store them in locals.
  for (let i = 0; i < 3; i++) {
    body.push(
      ...GenerateInput(i),
      kExprLocalSet, i,
    );
  }
  // Memory offset where the result will be stored later.
  body.push(
    kExprI32Const, 0,
  );

  // Get the inputs to the actual operation. Load those from locals that
  // the Liftoff register allocator should consider "in use"; materialize
  // one afresh that the register allocator should consider "reusable".
  for (let i = 0; i < 3; i++) {
    if (variant == i) {
      body.push(...GenerateInput(i));
    } else {
      body.push(kExprLocalGet, i);
    }
  }

  // Compute the result and store it in memory.
  // First intermediate result: 16 products with value 2*3 == 6.
  // Second intermediate result: groups of four neighboring products
  // are added up, so we get four sums with value 6+6+6+6 == 24 each.
  // Final result: 24 is added onto 1000, in four 32-bit lanes.
  body.push(
    kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,
    kSimdPrefix, kExprS128StoreMem, 0, 0,
  );

  // Drop the dummy values.
  for (let i = 0; i < kNumDummyValues; i++) {
    body.push(kExprDrop);
  }
  return body;
}

for (let i = 0; i < 3; i++) {
  let body = MakeFunctionBody(i);
  builder.addFunction(`main${i}`, kSig_v_v).exportFunc()
    .addLocals(kWasmS128, 3)
    .addBody(body);
}

let instance = builder.instantiate();
let dv = new DataView(instance.exports.mem.buffer);
for (let i = 0; i < 3; i++) {
  instance.exports[`main${i}`]();
  assertEquals(1024, dv.getInt32(0, true));
}
