// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-lazy-compilation

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
builder.addFunction('f1', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();
builder.addFunction('f2', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();
builder.addFunction('f3', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();

const exports = builder.instantiate().exports;

exports.f1(1);
exports.f2(2);
exports.f3(3);

%FlushLiftoffCode();

exports.f1(1);
exports.f2(2);
exports.f3(3);

%WasmTierUpFunction(exports.f3);

%FlushLiftoffCode();

exports.f1(1);
exports.f2(2);
exports.f3(3);
