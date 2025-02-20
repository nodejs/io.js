// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --no-wasm-loop-unrolling --no-wasm-loop-peeling

// This test case creates nested loops which contain struct.get calls that
// can potentially be load-eliminated as they load from a struct allocation at
// the function entry. The inner-most loop contains a call which cannot be
// inlined invalidating the eliminated loads in the loop resulting in a required
// revisitation of the loop body.
// Due to a bug in this revisitation algorithm, this resulted in 2^n loop visits
// of the innermost loop for this particular case where n is the nesting depth
// of loops.
// Due to the SnapshotTable storing the state as deltas between previous states,
// every revisit results in more data being accumulated in the SnapshotTable
// finally reaching an out of memory for the zone as it seems to be limited to
// 4 GB.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Parameter to configure the nesting of loops.
// Note that initially 22 nested loops where enough to trigger the oom situation
// described above, so 30 should reliably reproduce this issue or run into
// timeouts due to the exponential runtime.
const nestedLoopCount = 30;

(function WasmLoadElimination() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmAnyRef, true);
  let struct = builder.addStruct([
    makeField(wasmRefType(array), true)
  ]);

  let sig = makeSig([wasmRefType(struct)], [kWasmI32]);
  // An imported function that can not be inlined causing unknown side effects
  // and invaliding all previous (mutable) loads.
  let innerFct = builder.addImport('m', 'nonInlineableCallee', sig)

  let local_index = 1;
  // This function generates nested ops containing struct.get instructions that
  // can be eliminated on the forward edge but get invalidated when reaching
  // the call of the imported function in the innermost loop.
  let inlineCallees = (count) => {
    let local_1 = local_index++;
    let local_2 = local_index++;
    if (count == 0) {
      return [
        kExprLocalGet, local_1,
        kExprRefAsNonNull,
        kExprCallFunction, innerFct,
      ];
    }
    return [
      kExprLoop, kWasmVoid,
        kExprLocalGet, local_1,
        kGCPrefix, kExprStructGet, struct, 0,
        kExprLocalSet, local_2,
        kExprI32Const, 1,
        kExprIf, kWasmVoid,
          kExprLocalGet, local_2,
          kExprI32Const, 43, // dummy array index
          kGCPrefix, kExprArrayGet, array,
          kGCPrefix, kExprRefCast, struct,
          ...inlineCallees(count - 1),
          kExprDrop,
          kExprBr, 1,
        kExprEnd,
      kExprEnd,
      kExprI32Const, 1,
    ];
  };

  let fct = builder.addFunction("loadEliminationLoop", sig)
    .addBody([
      kGCPrefix, kExprArrayNewFixed, array, 0,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 1,
      ...inlineCallees(nestedLoopCount),
    ])
    .exportFunc();
  for (let i = 0; i < nestedLoopCount + 1; ++i) {
    fct.addLocals(wasmRefNullType(struct), 1)
       .addLocals(wasmRefType(array), 1)
  }

  let instance = builder.instantiate({m: {nonInlineableCallee: () => 1}});
  // Calling it with a JS object will already fail on the boundary which is fine
  // as we eagerly compile and do not care about the runtime behavior.
  // (This program doesn't make much sense.)
  assertThrows(() => instance.exports.loadEliminationLoop({}), TypeError);
})();
