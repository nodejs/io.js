// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const kMemtypeSize64 = 8;
const kMemtypeSize32 = 4;
const kMemtypeSize16 = 2;
const kMemtypeSize8 = 1;

function Add(a, b) { return a + b; }
function Sub(a, b) { return a - b; }
function And(a, b) { return a & b; }
function Or(a, b) { return a | b; }
function Xor(a, b) { return a ^ b; }
function Exchange(a, b) { return b; }

let maxSize = 10;
let memory = new WebAssembly.Memory({initial: 1, maximum: maxSize, shared: true});

function GetAtomicBinOpFunction(wasmExpression, alignment, offset) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, maxSize, "shared");
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      wasmExpression, alignment, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module,
        {m: {imported_mem: memory}});
  return instance.exports.main;
}

function GetI64AtomicBinOpFunction(wasmExpression, alignment, offset) {
  const kSig_l_il = makeSig([kWasmI32, kWasmI64], [kWasmI64]);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, maxSize, "shared");
  builder.addFunction("main", kSig_l_il)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      wasmExpression, alignment, offset
    ])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module,
        {m: {imported_mem: memory}});
  return instance.exports.main;
}

function GetAtomicCmpExchangeFunction(wasmExpression, alignment, offset) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, maxSize, "shared");
  builder.addFunction("main", kSig_i_iii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kAtomicPrefix,
      wasmExpression, alignment, ...wasmSignedLeb(offset, 5)])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module,
        {m: {imported_mem: memory}});
  return instance.exports.main;
}

function GetAtomicLoadFunction(wasmExpression, alignment, offset) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, maxSize, "shared");
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kAtomicPrefix,
      wasmExpression, alignment, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module,
        {m: {imported_mem: memory}});
  return instance.exports.main;
}

function GetAtomicStoreFunction(wasmExpression, alignment, offset) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, maxSize, "shared");
  builder.addFunction("main", kSig_v_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      wasmExpression, alignment, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module,
        {m: {imported_mem: memory}});
  return instance.exports.main;
}

function VerifyBoundsCheck64(func, memtype_size) {
  const kPageSize = 65536;
  // Test out of bounds at boundary
  for (let i = memory.buffer.byteLength - memtype_size + 1;
       i < memory.buffer.byteLength + memtype_size + 4; i++) {
    assertTrapsOneOf(
      // If an underlying platform uses traps for a bounds check,
      // kTrapUnalignedAccess will be thrown before kTrapMemOutOfBounds.
      // Otherwise, kTrapMemOutOfBounds will be first.
      [kTrapMemOutOfBounds, kTrapUnalignedAccess],
      () => func(i, 5n, 10n)
    );
  }
  // Test out of bounds at maximum + 1
  assertTraps(kTrapMemOutOfBounds, () => func((maxSize + 1) * kPageSize, 5n, 1n));
}

function VerifyBoundsCheck(func, memtype_size) {
  const kPageSize = 65536;
  // Test out of bounds at boundary
  for (let i = memory.buffer.byteLength - memtype_size + 1;
       i < memory.buffer.byteLength + memtype_size + 4; i++) {
    assertTrapsOneOf(
      // If an underlying platform uses traps for a bounds check,
      // kTrapUnalignedAccess will be thrown before kTrapMemOutOfBounds.
      // Otherwise, kTrapMemOutOfBounds will be first.
      [kTrapMemOutOfBounds, kTrapUnalignedAccess],
      () => func(i, 5, 10)
    );
  }
  // Test out of bounds at maximum + 1
  assertTraps(kTrapMemOutOfBounds, () => func((maxSize + 1) * kPageSize, 5, 1));
}

// Test many elements in the small range, make bigger steps later. This is still
// O(2^n), but takes 213 steps to reach 2^32.
const inc = i => i + Math.floor(i/10) + 1;

function Test32Op(operation, func) {
  let i32 = new Uint32Array(memory.buffer);
  for (let i = 0; i < i32.length; i = inc(i)) {
    let expected = 0x9cedf00d;
    let value = 0x11111111;
    i32[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize32, value) >>> 0);
    assertEquals(operation(expected, value) >>> 0, i32[i]);
  }
  VerifyBoundsCheck(func, kMemtypeSize32);
}

function Test16Op(operation, func) {
  let i16 = new Uint16Array(memory.buffer);
  for (let i = 0; i < i16.length; i = inc(i)) {
    let expected = 0xd00d;
    let value = 0x1111;
    i16[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize16, value));
    assertEquals(operation(expected, value), i16[i]);
  }
  VerifyBoundsCheck(func, kMemtypeSize16);
}

function Test8Op(operation, func) {
  let i8 = new Uint8Array(memory.buffer);
  for (let i = 0; i < i8.length; i = inc(i)) {
    let expected = 0xbe;
    let value = 0x12;
    i8[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize8, value));
    assertEquals(operation(expected, value), i8[i]);
  }
  VerifyBoundsCheck(func, kMemtypeSize8, 10);
}

function Test64Op(operation, func) {
  let i64 = new BigUint64Array(memory.buffer);
  for (let i = 0; i < i64.length; i = inc(i)) {
    let expected = 987659876543210n;
    let value = 111111111111111n;
    i64[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize64, value));
    assertEquals(operation(expected, value), i64[i]);
  }
  VerifyBoundsCheck64(func, kMemtypeSize64);
}

function Test32Op64(operation, func) {
  let i32 = new Uint32Array(memory.buffer);
  for (let i = 0; i < i32.length; i = inc(i)) {
    let expected = 123456;
    let value = 111111n;
    i32[i] = expected;
    assertEquals(expected, Number(func(i * kMemtypeSize32, value)));
    assertEquals(operation(expected, Number(value)), i32[i]);
  }
  VerifyBoundsCheck64(func, kMemtypeSize32);
}

function Test16Op64(operation, func) {
  let i16 = new Uint16Array(memory.buffer);
  for (let i = 0; i < i16.length; i = inc(i)) {
    let expected = 0xd00d;
    let value = 0x1111n;
    i16[i] = expected;
    assertEquals(expected, Number(func(i * kMemtypeSize16, value)));
    assertEquals(operation(expected, Number(value)), i16[i]);
  }
  VerifyBoundsCheck64(func, kMemtypeSize16);
}

function Test8Op64(operation, func) {
  let i8 = new Uint8Array(memory.buffer);
  for (let i = 0; i < i8.length; i = inc(i)) {
    let expected = 0xbe;
    let value = 0x12n;
    i8[i] = expected;
    assertEquals(expected, Number(func(i * kMemtypeSize8, value)));
    assertEquals(operation(expected, Number(value)), i8[i]);
  }
  VerifyBoundsCheck64(func, kMemtypeSize8, 10);
}

(function TestAtomicAdd() {
  print(arguments.callee.name);
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd, 2, 0);
  Test32Op(Add, wasmAdd);
})();

(function TestAtomicAdd16U() {
  print(arguments.callee.name);
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd16U, 1, 0);
  Test16Op(Add, wasmAdd);
})();

(function TestAtomicAdd8U() {
  print(arguments.callee.name);
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd8U, 0, 0);
  Test8Op(Add, wasmAdd);
})();

(function TestI64AtomicAdd() {
  print(arguments.callee.name);
  let wasmAdd = GetI64AtomicBinOpFunction(kExprI64AtomicAdd, 3, 0);
  Test64Op(Add, wasmAdd);
})();

(function TestI64AtomicAdd32U() {
  print(arguments.callee.name);
  let wasmAdd = GetI64AtomicBinOpFunction(kExprI64AtomicAdd32U, 2, 0);
  Test32Op64(Add, wasmAdd);
})();

(function TestI64AtomicAdd16U() {
  print(arguments.callee.name);
  let wasmAdd = GetI64AtomicBinOpFunction(kExprI64AtomicAdd16U, 1, 0);
  Test16Op64(Add, wasmAdd);
})();

(function TestI64AtomicAdd8U() {
  print(arguments.callee.name);
  let wasmAdd = GetI64AtomicBinOpFunction(kExprI64AtomicAdd8U, 0, 0);
  Test8Op64(Add, wasmAdd);
})();

(function TestAtomicSub() {
  print(arguments.callee.name);
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub, 2, 0);
  Test32Op(Sub, wasmSub);
})();

(function TestAtomicSub16U() {
  print(arguments.callee.name);
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub16U, 1, 0);
  Test16Op(Sub, wasmSub);
})();

(function TestAtomicSub8U() {
  print(arguments.callee.name);
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub8U, 0, 0);
  Test8Op(Sub, wasmSub);
})();

(function TestI64AtomicSub() {
  print(arguments.callee.name);
  let wasmSub = GetI64AtomicBinOpFunction(kExprI64AtomicSub, 3, 0);
  Test64Op(Sub, wasmSub);
})();

(function TestI64AtomicSub32U() {
  print(arguments.callee.name);
  let wasmSub = GetI64AtomicBinOpFunction(kExprI64AtomicSub32U, 2, 0);
  Test32Op64(Sub, wasmSub);
})();

(function TestI64AtomicSub16U() {
  print(arguments.callee.name);
  let wasmSub = GetI64AtomicBinOpFunction(kExprI64AtomicSub16U, 1, 0);
  Test16Op64(Sub, wasmSub);
})();

(function TestI64AtomicSub8U() {
  print(arguments.callee.name);
  let wasmSub = GetI64AtomicBinOpFunction(kExprI64AtomicSub8U, 0, 0);
  Test8Op64(Sub, wasmSub);
})();

(function TestAtomicAnd() {
  print(arguments.callee.name);
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd, 2, 0);
  Test32Op(And, wasmAnd);
})();

(function TestAtomicAnd16U() {
  print(arguments.callee.name);
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd16U, 1, 0);
  Test16Op(And, wasmAnd);
})();

(function TestAtomicAnd8U() {
  print(arguments.callee.name);
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd8U, 0, 0);
  Test8Op(And, wasmAnd);
})();

(function TestI64AtomicAnd() {
  print(arguments.callee.name);
  let wasmAnd = GetI64AtomicBinOpFunction(kExprI64AtomicAnd, 3, 0);
  Test64Op(And, wasmAnd);
})();

(function TestI64AtomicAnd32U() {
  print(arguments.callee.name);
  let wasmAnd = GetI64AtomicBinOpFunction(kExprI64AtomicAnd32U, 2, 0);
  Test32Op64(And, wasmAnd);
})();

(function TestI64AtomicAnd16U() {
  print(arguments.callee.name);
  let wasmAnd = GetI64AtomicBinOpFunction(kExprI64AtomicAnd16U, 1, 0);
  Test16Op64(And, wasmAnd);
})();

(function TestI64AtomicAnd8U() {
  print(arguments.callee.name);
  let wasmAnd = GetI64AtomicBinOpFunction(kExprI64AtomicAnd8U, 0, 0);
  Test8Op64(And, wasmAnd);
})();

(function TestAtomicOr() {
  print(arguments.callee.name);
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr, 2, 0);
  Test32Op(Or, wasmOr);
})();

(function TestAtomicOr16U() {
  print(arguments.callee.name);
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr16U, 1, 0);
  Test16Op(Or, wasmOr);
})();

(function TestAtomicOr8U() {
  print(arguments.callee.name);
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr8U, 0, 0);
  Test8Op(Or, wasmOr);
})();

(function TestI64AtomicOr() {
  print(arguments.callee.name);
  let wasmOr = GetI64AtomicBinOpFunction(kExprI64AtomicOr, 3, 0);
  Test64Op(Or, wasmOr);
})();

(function TestI64AtomicOr32U() {
  print(arguments.callee.name);
  let wasmOr = GetI64AtomicBinOpFunction(kExprI64AtomicOr32U, 2, 0);
  Test32Op64(Or, wasmOr);
})();

(function TestI64AtomicOr16U() {
  print(arguments.callee.name);
  let wasmOr = GetI64AtomicBinOpFunction(kExprI64AtomicOr16U, 1, 0);
  Test16Op64(Or, wasmOr);
})();

(function TestI64AtomicOr8U() {
  print(arguments.callee.name);
  let wasmOr = GetI64AtomicBinOpFunction(kExprI64AtomicOr8U, 0, 0);
  Test8Op64(Or, wasmOr);
})();

(function TestAtomicXor() {
  print(arguments.callee.name);
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor, 2, 0);
  Test32Op(Xor, wasmXor);
})();

(function TestAtomicXor16U() {
  print(arguments.callee.name);
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor16U, 1, 0);
  Test16Op(Xor, wasmXor);
})();

(function TestAtomicXor8U() {
  print(arguments.callee.name);
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor8U, 0, 0);
  Test8Op(Xor, wasmXor);
})();

(function TestI64AtomicXor() {
  print(arguments.callee.name);
  let wasmXor = GetI64AtomicBinOpFunction(kExprI64AtomicXor, 3, 0);
  Test64Op(Xor, wasmXor);
})();

(function TestI64AtomicXor32U() {
  print(arguments.callee.name);
  let wasmXor = GetI64AtomicBinOpFunction(kExprI64AtomicXor32U, 2, 0);
  Test32Op64(Xor, wasmXor);
})();

(function TestI64AtomicXor16U() {
  print(arguments.callee.name);
  let wasmXor = GetI64AtomicBinOpFunction(kExprI64AtomicXor16U, 1, 0);
  Test16Op64(Xor, wasmXor);
})();

(function TestI64AtomicXor8U() {
  print(arguments.callee.name);
  let wasmXor = GetI64AtomicBinOpFunction(kExprI64AtomicXor8U, 0, 0);
  Test8Op64(Xor, wasmXor);
})();

(function TestAtomicExchange() {
  print(arguments.callee.name);
  let wasmExchange = GetAtomicBinOpFunction(kExprI32AtomicExchange, 2, 0);
  Test32Op(Exchange, wasmExchange);
})();

(function TestAtomicExchange16U() {
  print(arguments.callee.name);
  let wasmExchange = GetAtomicBinOpFunction(kExprI32AtomicExchange16U, 1, 0);
  Test16Op(Exchange, wasmExchange);
})();

(function TestAtomicExchange8U() {
  print(arguments.callee.name);
  let wasmExchange = GetAtomicBinOpFunction(kExprI32AtomicExchange8U, 0, 0);
  Test8Op(Exchange, wasmExchange);
})();

(function TestI64AtomicExchange() {
  print(arguments.callee.name);
  let wasmExchange = GetI64AtomicBinOpFunction(kExprI64AtomicExchange, 3, 0);
  Test64Op(Exchange, wasmExchange);
})();

(function TestI64AtomicExchange32U() {
  print(arguments.callee.name);
  let wasmExchange = GetI64AtomicBinOpFunction(kExprI64AtomicExchange32U, 2, 0);
  Test32Op64(Exchange, wasmExchange);
})();

(function TestI64AtomicExchange16U() {
  print(arguments.callee.name);
  let wasmExchange = GetI64AtomicBinOpFunction(kExprI64AtomicExchange16U, 1, 0);
  Test16Op64(Exchange, wasmExchange);
})();

(function TestI64AtomicExchange8U() {
  print(arguments.callee.name);
  let wasmExchange = GetI64AtomicBinOpFunction(kExprI64AtomicExchange8U, 0, 0);
  Test8Op64(Exchange, wasmExchange);
})();


function TestCmpExchange(func, buffer, params, size, offset = 0) {
  for (let i = 0; i + (offset / size) < buffer.length; i = inc(i)) {
    for (let j = 0; j < params.length; j++) {
      for (let k = 0; k < params.length; k++) {
        buffer[i + (offset / size)] = params[j];
        let loaded = func(i * size, params[k], params[j]) >>> 0;
        let expected = (params[k] == loaded) ? params[j] : loaded;
        assertEquals(loaded, params[j]);
        assertEquals(expected, buffer[i + (offset / size)]);
      }
    }
  }
  VerifyBoundsCheck(func, size);
}

(function TestAtomicCompareExchange() {
  print(arguments.callee.name);
  // Offset is big enough to not fit in a 12-bit immediate on arm64, but small
  // enough to fit in the maxSize wasm pages.
  const offset = 0x1234;
  let wasmCmpExchange =
      GetAtomicCmpExchangeFunction(kExprI32AtomicCompareExchange, 2, offset);
  let i32 = new Uint32Array(memory.buffer);
  let params = [0x00000001, 0x00000555, 0x00099999, 0xffffffff];
  TestCmpExchange(wasmCmpExchange, i32, params, kMemtypeSize32, offset);
})();

(function TestAtomicCompareExchange16U() {
  print(arguments.callee.name);
  let wasmCmpExchange =
      GetAtomicCmpExchangeFunction(kExprI32AtomicCompareExchange16U, 1, 0);
  let i16 = new Uint16Array(memory.buffer);
  let params = [0x0001, 0x0555, 0x9999];
  TestCmpExchange(wasmCmpExchange, i16, params, kMemtypeSize16);
})();

(function TestAtomicCompareExchange8U() {
  print(arguments.callee.name);
  let wasmCmpExchange =
      GetAtomicCmpExchangeFunction(kExprI32AtomicCompareExchange8U, 0, 0);
  let i8 = new Uint8Array(memory.buffer);
  let params = [0x01, 0x0d, 0xf9];
  TestCmpExchange(wasmCmpExchange, i8, params, kMemtypeSize8);
})();

function TestLoad(func, buffer, value, size) {
  for (let i = 0; i < buffer.length; i = inc(i)) {
    buffer[i] = value;
    assertEquals(value, func(i * size) >>> 0);
  }
  VerifyBoundsCheck(func, size);
}

(function TestAtomicLoad() {
  print(arguments.callee.name);
  let wasmLoad = GetAtomicLoadFunction(kExprI32AtomicLoad, 2, 0);
  let i32 = new Uint32Array(memory.buffer);
  let value = 0xacedaced;
  TestLoad(wasmLoad, i32, value, kMemtypeSize32);
})();

(function TestAtomicLoad16U() {
  print(arguments.callee.name);
  let wasmLoad = GetAtomicLoadFunction(kExprI32AtomicLoad16U, 1, 0);
  let i16 = new Uint16Array(memory.buffer);
  let value = 0xaced;
  TestLoad(wasmLoad, i16, value, kMemtypeSize16);
})();

(function TestAtomicLoad8U() {
  print(arguments.callee.name);
  let wasmLoad = GetAtomicLoadFunction(kExprI32AtomicLoad8U, 0, 0);
  let i8 = new Uint8Array(memory.buffer);
  let value = 0xac;
  TestLoad(wasmLoad, i8, value, kMemtypeSize8);
})();

function TestStore(func, buffer, value, size) {
  for (let i = 0; i < buffer.length; i = inc(i)) {
    func(i * size, value)
    assertEquals(value, buffer[i]);
  }
  VerifyBoundsCheck(func, size);
}

(function TestAtomicStore() {
  print(arguments.callee.name);
  let wasmStore = GetAtomicStoreFunction(kExprI32AtomicStore, 2, 0);
  let i32 = new Uint32Array(memory.buffer);
  let value = 0xacedaced;
  TestStore(wasmStore, i32, value, kMemtypeSize32);
})();

(function TestAtomicStore16U() {
  print(arguments.callee.name);
  let wasmStore = GetAtomicStoreFunction(kExprI32AtomicStore16U, 1, 0);
  let i16 = new Uint16Array(memory.buffer);
  let value = 0xaced;
  TestStore(wasmStore, i16, value, kMemtypeSize16);
})();

(function TestAtomicStore8U() {
  print(arguments.callee.name);
  let wasmStore = GetAtomicStoreFunction(kExprI32AtomicStore8U, 0, 0);
  let i8 = new Uint8Array(memory.buffer);
  let value = 0xac;
  TestCmpExchange(wasmStore, i8, value, kMemtypeSize8);
})();

(function TestAtomicLoadStoreOffset() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let memory = new WebAssembly.Memory({
    initial: 16, maximum: 128, shared: true});
  builder.addImportedMemory("m", "imported_mem", 16, 128, "shared");
  builder.addFunction("loadStore", kSig_i_v)
    .addBody([
      kExprI32Const, 16,
      kExprI32Const, 20,
      kAtomicPrefix,
      kExprI32AtomicStore, 2, 0xFC, 0xFF, 0x3a,
      kExprI32Const, 16,
      kAtomicPrefix,
      kExprI32AtomicLoad, 2, 0xFC, 0xFF, 0x3a])
    .exportAs("loadStore");
  builder.addFunction("storeOob", kSig_v_v)
    .addBody([
      kExprI32Const, 16,
      kExprI32Const, 20,
      kAtomicPrefix,
      kExprI32AtomicStore, 2, 0xFC, 0xFF, 0xFF, 0x3a])
    .exportAs("storeOob");
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = (new WebAssembly.Instance(module,
        {m: {imported_mem: memory}}));
  let buf = memory.buffer;
  assertEquals(20, instance.exports.loadStore());
  assertTraps(kTrapMemOutOfBounds, instance.exports.storeOob);
})();

(function TestAtomicOpinLoop() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let memory = new WebAssembly.Memory({
    initial: 16, maximum: 128, shared: true});
  builder.addImportedMemory("m", "imported_mem", 16, 128, "shared");
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprI32Const, 16,
        kExprI32Const, 20,
        kAtomicPrefix,
        kExprI32AtomicStore, 2, 0,
        kExprI32Const, 16,
        kAtomicPrefix,
        kExprI32AtomicLoad, 2, 0,
        kExprReturn,
      kExprEnd,
      kExprI32Const, 0
    ])
    .exportFunc();
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = (new WebAssembly.Instance(module,
        {m: {imported_mem: memory}}));
  assertEquals(20, instance.exports.main());
})();

(function TestUnalignedAtomicAccesses() {
  print(arguments.callee.name);
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd, 2, 17);
  assertTraps(kTrapUnalignedAccess, () => wasmAdd(4, 1001));
  let wasmLoad = GetAtomicLoadFunction(kExprI32AtomicLoad16U, 1, 0);
  assertTraps(kTrapUnalignedAccess, () => wasmLoad(15));
  let wasmStore = GetAtomicStoreFunction(kExprI32AtomicStore, 2, 0);
  assertTraps(kTrapUnalignedAccess, () => wasmStore(22, 5));
  let wasmCmpExchange =
      GetAtomicCmpExchangeFunction(kExprI32AtomicCompareExchange, 2, 0x16);
  assertTraps(kTrapUnalignedAccess, () => wasmCmpExchange(11, 6, 5));

  // Building functions with bad alignment should fail to compile
  assertThrows(() => GetAtomicBinOpFunction(kExprI32AtomicSub16U, 3, 0),
      WebAssembly.CompileError);
})();

function CmpExchgLoop(opcode, alignment) {
  print("TestI64AtomicCompareExchangeLoop" + alignment);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem", 0, 2, "shared");
  builder.addFunction("main", makeSig([kWasmI32], []))
      .addLocals(kWasmI64, 2)
      .addBody([
        kExprLoop, kWasmVoid,
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kAtomicPrefix, opcode, alignment, 0,
          kExprLocalGet, 1,
          kExprI64Ne,
          kExprBrIf, 0,
          kExprEnd
      ])
      .exportFunc();
  let mem = new WebAssembly.Memory({initial: 2, maximum: 2, shared: true});
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {imported_mem: mem}});
}

(function TestAtomicCompareExchgLoop() {
  CmpExchgLoop(kExprI64AtomicCompareExchange, 3);
  CmpExchgLoop(kExprI64AtomicCompareExchange32U, 2);
  CmpExchgLoop(kExprI64AtomicCompareExchange16U, 1);
  CmpExchgLoop(kExprI64AtomicCompareExchange8U, 0);
})();

(function TestIllegalAtomicOp() {
  // Regression test for https://crbug.com/1381330.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_v_v).addBody([
    kAtomicPrefix, 0x90, 0x0f
  ]);
  assertEquals(false, WebAssembly.validate(builder.toBuffer()));
  assertThrows(
      () => builder.toModule(), WebAssembly.CompileError,
      /invalid atomic opcode: 0xfe790/);
})();

(function TestConsistentErrorReporting() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, 32);
  builder.addFunction("atomicStore", makeSig([kWasmI32], []))
    .addBody([
      kExprLocalGet, 0,
      kExprI64Const, 42, // A dummy value to be stored.
      kAtomicPrefix, kExprI64AtomicStore, /*align*/ 0x03, /*offset*/ 0x00,
    ]).exportFunc();
  builder.addFunction("atomicLoad", makeSig([kWasmI32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kAtomicPrefix, kExprI64AtomicLoad, /*align*/ 0x03, /*offset*/ 0x00,
    ]).exportFunc();
  builder.addFunction("atomicAdd", makeSig([kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 42,
      kAtomicPrefix, kExprI32AtomicAdd, /*align*/ 0x02, /*offset*/ 0x00,
    ]).exportFunc();
  builder.addFunction("atomicCompareExchange", makeSig([kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 12,
      kExprI32Const, 23,
      kAtomicPrefix, kExprI32AtomicCompareExchange,
      /*align*/ 0x02, /*offset*/ 0x00,
    ]).exportFunc();
  builder.addFunction("atomicWait", makeSig([kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 12,
      kExprI64Const, 23,
      kAtomicPrefix, kExprI32AtomicWait, /*align*/ 0x02, /*offset*/ 0x00,
    ]).exportFunc();
  builder.addFunction("atomicNotify", makeSig([kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 12,
      kAtomicPrefix, kExprAtomicNotify, /*align*/ 0x02, /*offset*/ 0x00,
    ]).exportFunc();


  const wasm = builder.instantiate().exports;

  let unaligned = 1;
  let oob = 1024 * 1024;
  for (let test of [
      "atomicStore", "atomicLoad", "atomicAdd", "atomicCompareExchange",
      "atomicWait", "atomicNotify"]) {
    print(`- test ${test}`);
    let fct = wasm[test];
    assertTraps(kTrapUnalignedAccess, () => fct(unaligned));
    assertTraps(kTrapMemOutOfBounds, () => fct(oob));
    // If an atomic operation is both unaligned and out of bounds, the unaligned
    // error is reported.
    assertTraps(kTrapUnalignedAccess, () => fct(unaligned + oob));
  }
})();
