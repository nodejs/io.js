// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging
// Flags: --no-stress-wasm-stack-switching

// --stress-wasm-stack-switching affects the behavior of the JSPI tests below,
// so we skip them under this stress variant to only check the intended behavior
// and avoid false negatives.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestSuspendJSFramesTraps() {
  // The call stack of this test looks like:
  // export1 -> import1 -> export2 -> import2
  // Where export1 is "promising" and import2 is "suspending". Returning a
  // promise from import2 should trap because of the JS import in the middle.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let import1_index = builder.addImport("m", "import1", kSig_i_v);
  let import2_index = builder.addImport("m", "import2", kSig_i_v);
  builder.addFunction("export1", kSig_i_v)
      .addBody([
          // export1 -> import1 (unwrapped)
          kExprCallFunction, import1_index,
      ]).exportFunc();
  builder.addFunction("export2", kSig_i_v)
      .addBody([
          // export2 -> import2 (suspending)
          kExprCallFunction, import2_index,
      ]).exportFunc();
  let instance;
  function import1() {
    // import1 -> export2 (unwrapped)
    instance.exports.export2();
  }
  function import2() {
    return Promise.resolve(0);
  }
  import2 = new WebAssembly.Suspending(import2);
  instance = builder.instantiate(
      {'m':
        {'import1': import1,
         'import2': import2
        }});
  // export1 (promising)
  let wrapper = WebAssembly.promising(instance.exports.export1);
  assertThrowsAsync(wrapper(), WebAssembly.SuspendError,
      /trying to suspend JS frames/);
})();

(function TestSwitchingToTheCentralStackForRuntime() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let table = builder.addTable(kWasmExternRef, 1);
  let array_index = builder.addArray(kWasmI32, true);
  let new_space_full_index = builder.addImport('m', 'new_space_full', kSig_v_v);
  builder.addFunction("test", kSig_i_r)
      .addBody([
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kNumericPrefix, kExprTableGrow, table.index]).exportFunc();
  builder.addFunction("test2", kSig_i_r)
      .addBody([
        kExprI32Const, 1]).exportFunc();
  builder.addFunction("test3", kSig_l_v)
      .addBody([
        kExprCallFunction, new_space_full_index,
        ...wasmI64Const(0)
        ]).exportFunc();
  builder.addFunction("test4", kSig_v_v)
      .addBody([
        kExprCallFunction, new_space_full_index,
        kExprI32Const, 1,
        kGCPrefix, kExprArrayNewDefault, array_index,
        kExprDrop]).exportFunc();
  function new_space_full() {
    %SimulateNewspaceFull();
  }
  let instance = builder.instantiate({m: {new_space_full}});
  let wrapper = WebAssembly.promising(instance.exports.test);
  let wrapper2 = WebAssembly.promising(instance.exports.test2);
  let wrapper3 = WebAssembly.promising(instance.exports.test3);
  let wrapper4 = WebAssembly.promising(instance.exports.test4);
  function switchesToCS(fn) {
    const beforeCall = %WasmSwitchToTheCentralStackCount();
    fn();
    return %WasmSwitchToTheCentralStackCount() - beforeCall;
  }

  // Calling exported functions from the central stack.
  // Fails under --stress-wasm-stack-switching, because the unwrapped exports
  // switch to a secondary stack.
  // assertEquals(0, switchesToCS(() => instance.exports.test({})));
  // assertEquals(0, switchesToCS(() => instance.exports.test2({})));
  // assertEquals(0, switchesToCS(() => instance.exports.test3({})));
  // assertEquals(0, switchesToCS(() => instance.exports.test4({})));

  // Runtime call to table.grow.
  switchesToCS(wrapper);
  // No runtime calls.
  switchesToCS(wrapper2);
  // Runtime call to allocate the bigint.
  switchesToCS(wrapper3);
  // Runtime call for array.new.
  switchesToCS(wrapper4);
  %CheckIsOnCentralStack();
})();
