// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=2

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function Regress343772336() {
  var builder = new WasmModuleBuilder();

  let sig_index = builder.addType(kSig_v_v);
  let import_index = builder.addImport("m", "f", sig_index);
  builder.addExport("f", import_index);
  builder.addFunction("main", sig_index)
    .addBody([])         // --
    .exportAs("main");

  let instance = builder.instantiate({m: {f: () => {}}});

  // Tier up the wrapper for this signature compiled for a re-exported import.
  for (var i = 0; i < 3; i += 1) {
    instance.exports.f();
  }
  // Fails because the tier-up incorrectly replaced the wrapper for main as
  // well.
  instance.exports.main();
})();
