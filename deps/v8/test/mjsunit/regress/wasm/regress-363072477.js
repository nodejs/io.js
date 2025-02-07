// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $struct4 = builder.addStruct([], kNoSuperType, false);
let $sig5 = builder.addType(kSig_i_iii);
let $sig7 = builder.addType(makeSig([], [kWasmNullRef]));

let $func19 = builder.addFunction(undefined, $sig7).addBody([
    kGCPrefix, kExprStructNewDefault, $struct4,
    kGCPrefix, kExprRefCastNull, kStructRefCode,
    kGCPrefix, kExprBrOnCast, 0b11 /* nullable -> nullable */, 0, kAnyRefCode, kNullRefCode,
    kExprDrop,
    kExprUnreachable,
  ]);

const instance = builder.instantiate();
