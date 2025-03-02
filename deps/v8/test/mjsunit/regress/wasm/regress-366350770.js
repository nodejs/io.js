// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();
builder.addFunction("main", kSig_i_i).addBody([
    kExprI32Const, 1,
    kExprLocalGet, 0,
    kExprI32DivU,      // Aborts Liftoff compilation on armv7 without sudiv.
    kExprUnreachable,  // Ran into a DCHECK after aborted compilation.
  ]).exportAs("main");
let instance = builder.instantiate();
assertThrows(
    () => instance.exports.main(1), WebAssembly.RuntimeError, 'unreachable');
