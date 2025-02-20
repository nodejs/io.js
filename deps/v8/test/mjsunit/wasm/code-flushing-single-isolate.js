// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('f1', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();
builder.addFunction('f2', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();
builder.addFunction('f3', kSig_i_i).addBody([kExprLocalGet, 0]).exportFunc();

const exports = builder.instantiate().exports;

exports.f1(1);
exports.f2(2);
exports.f3(3);

assertTrue(%IsLiftoffFunction(exports.f1));
assertTrue(%IsLiftoffFunction(exports.f2));
assertTrue(%IsLiftoffFunction(exports.f3));

assertTrue(%FlushLiftoffCode() > 0);

assertTrue(%IsUncompiledWasmFunction(exports.f1));
assertTrue(%IsUncompiledWasmFunction(exports.f2));
assertTrue(%IsUncompiledWasmFunction(exports.f3));

exports.f1(1);
exports.f2(2);
exports.f3(3);

%WasmTierUpFunction(exports.f3);

assertTrue(%FlushLiftoffCode() > 0);

assertTrue(%IsUncompiledWasmFunction(exports.f1));
assertTrue(%IsUncompiledWasmFunction(exports.f2));
assertTrue(%IsTurboFanFunction(exports.f3));
