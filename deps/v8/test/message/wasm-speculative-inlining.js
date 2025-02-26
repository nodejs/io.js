// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-inlining-call-indirect --allow-natives-syntax
// Flags: --trace-wasm-inlining --wasm-dynamic-tiering
// Explicitly set Turboshaft since the tracing output is slightly different in
// TurboFan (which is also on its way out anyway).
// Explicitly enable dynamic tiering, since we need Liftoff to collect feedback
// for speculative optimizations.

d8.file.execute('test/mjsunit/mjsunit.js');
d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function BuildTest(call_type, callee_count) {
  print(
      'Test ' + call_type + ' speculative inlining with ' +
      callee_count + ' different callees during feedback collection.');
  let builder = new WasmModuleBuilder();

  let sig_index = builder.addType(kSig_i_i);

  let main = builder.addFunction("main", kSig_i_ii).exportFunc();
  if (call_type === "call_ref") {
    main.addBody([
      kExprLocalGet, 0,

      kExprLocalGet, 1,
      kExprTableGet, kTableZero,
      kGCPrefix, kExprRefCast, sig_index,

      kExprCallRef, sig_index,
    ]);
  } else if (call_type === "call_indirect") {
    main.addBody([
      kExprLocalGet, 0,

      kExprLocalGet, 1,
      kExprCallIndirect, sig_index, kTableZero,
    ]);
  } else if (call_type === "return_call_indirect") {
    main.addBody([
      kExprLocalGet, 0,

      kExprLocalGet, 1,
      kExprReturnCallIndirect, sig_index, kTableZero,
    ]);
  }

  let callee = [];
  // Add one more function than what will be called before tier-up below,
  // for also testing mis-speculation.
  for (let i = 0; i < callee_count + 1; i++) {
    callee.push(builder.addFunction("callee" + i, sig_index)
      .addBody([kExprLocalGet, 0, kExprI32Const, i, kExprI32Add]));
  }
  builder.appendToTable(callee.map(f => f.index));

  let instance = builder.instantiate();

  for (let i = 0; i < 10; i++) {
    // Call each of the `callee`s except the last one to collect feedback.
    for (let j = 0; j < callee_count; j++) {
      assertEquals(10 + j, instance.exports.main(10, j));
    }
  };
  %WasmTierUpFunction(instance.exports.main);

  // Tier-up is done, and inlining should have happened (in the monomorphic and
  // polymorphic cases). The result should be correct in any case.
  for (let j = 0; j < callee_count; j++) {
    assertEquals(10 + j, instance.exports.main(10, j));
  }

  // Now, run with a `callee` that was never seen before tier-up.
  // While this is not inlined, it should still return the correct result.
  assertEquals(10 + callee_count, instance.exports.main(10, callee_count));
}

const kMaxPolymorphism = 4;

(function CallRefUninitializedFeedback() { BuildTest("call_ref", 0); })();
(function CallRefMonomorphic() { BuildTest("call_ref", 1); })();
(function CallRefPolymorphic() { BuildTest("call_ref", kMaxPolymorphism); })();
(function CallRefMegamorphic() { BuildTest("call_ref", kMaxPolymorphism + 1); })();

(function CallIndirectUninitializedFeedback() { BuildTest("call_indirect", 0); })();
(function CallIndirectMonomorphic() { BuildTest("call_indirect", 1); })();
(function CallIndirectPolymorphic() { BuildTest("call_indirect", kMaxPolymorphism); })();
(function CallIndirectMegamorphic() { BuildTest("call_indirect", kMaxPolymorphism + 1); })();

(function ReturnCallIndirectUninitializedFeedback() {
  BuildTest("return_call_indirect", 0);
})();
(function ReturnCallIndirectMonomorphic() {
  BuildTest('return_call_indirect', 1);
})();
(function ReturnCallIndirectPolymorphic() {
  BuildTest('return_call_indirect', kMaxPolymorphism);
})();
(function ReturnCallIndirectMegamorphic() {
  BuildTest('return_call_indirect', kMaxPolymorphism + 1);
})();
