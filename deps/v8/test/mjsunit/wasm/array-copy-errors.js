// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");


(function TestArrayCopyErrorReasons() {
  var builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  // The array.copy has two different reasons to trap. (null dereference, oob)
  // All compilers should emit the same reason in all cases. Which one doesn't
  // really matter and could be changed in the future, e.g. if it improves
  // generated code.
  builder.addFunction("tgtNullSrcNull", kSig_v_v)
    .addBody([
      kExprRefNull, array,  // target array (nullptr)
      kExprI32Const, 2,     // target index
      kExprRefNull, array,  // source array (nullptr)
      kExprI32Const, 2,     // source index
      kExprI32Const, 2,     // length
      kGCPrefix, kExprArrayCopy, array, array,
  ]).exportFunc();

  builder.addFunction("tgtOobSrcNull", kSig_v_v)
    .addBody([
      kExprI32Const, 4, kGCPrefix, kExprArrayNewDefault, array,  // target array
      kExprI32Const, 42,    // target index (out of bounds)
      kExprRefNull, array,  // source array (nullptr)
      kExprI32Const, 2,     // source index
      kExprI32Const, 2,     // length
      kGCPrefix, kExprArrayCopy, array, array,
  ]).exportFunc();

  builder.addFunction("tgtNullSrcOob", kSig_v_v)
    .addBody([
      kExprRefNull, array,  // target array (nullptr)
      kExprI32Const, 2,     // target index
      kExprI32Const, 4, kGCPrefix, kExprArrayNewDefault, array,  // source array
      kExprI32Const, 42,    // source index (out of bounds)
      kExprI32Const, 2,     // length
      kGCPrefix, kExprArrayCopy, array, array,
  ]).exportFunc();

  builder.addFunction("tgtOobSrcOob", kSig_v_v)
    .addBody([
      kExprI32Const, 4, kGCPrefix, kExprArrayNewDefault, array,  // target array
      kExprI32Const, 42,    // target index (out of bounds)
      kExprI32Const, 4, kGCPrefix, kExprArrayNewDefault, array,  // source array
      kExprI32Const, 42,    // source index (out of bounds)
      kExprI32Const, 2,     // length
      kGCPrefix, kExprArrayCopy, array, array,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertTraps(kTrapNullDereference, wasm.tgtNullSrcNull);
  assertTraps(kTrapArrayOutOfBounds, wasm.tgtOobSrcNull);
  assertTraps(kTrapNullDereference, wasm.tgtNullSrcOob);
  assertTraps(kTrapArrayOutOfBounds, wasm.tgtOobSrcOob);
})();
