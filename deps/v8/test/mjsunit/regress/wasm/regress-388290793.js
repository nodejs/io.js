// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let mem = new WebAssembly.Memory({initial: 1n, maximum: 0x10n, address: 'i64'});    // reserve 0x10 * 0x10000
let mem2 = new WebAssembly.Memory({initial: 1n, maximum: 0x10n, address: 'i64'});

let builder = new WasmModuleBuilder();
let m = builder.addImportedMemory('import', 'mem', 1, 0x20, false, true);

let sig_v_ll = makeSig([kWasmI64, kWasmI64], []);

builder.addFunction('fn', sig_v_ll).addBody([
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  kExprI64StoreMem, 0x41, m, 0, // can index up to 0x20 * 0x10000
]).exportFunc();

let instance = builder.instantiate({import: {mem}});
let {fn} = instance.exports;

let mem2_u64a = new BigUint64Array(mem2.buffer);
assertEquals(0n, mem2_u64a[0]);
assertThrows(() => fn(0x100000n, 0x4242424242424242n));  // oob write into mem2?
assertEquals(0n, mem2_u64a[0]);
