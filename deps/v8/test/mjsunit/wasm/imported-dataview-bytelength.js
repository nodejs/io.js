// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --mock-arraybuffer-allocator

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kSig_d_r = makeSig([kWasmExternRef], [kWasmF64]);

let kImports = {
  DataView: {
    byteLengthImport: Function.prototype.call.bind(
        Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength').get)
  },
};

function MakeInstance() {
  let builder = new WasmModuleBuilder();

  let kDataViewByteLength =
      builder.addImport('DataView', 'byteLengthImport', kSig_d_r);
  builder.addFunction('byteLength', kSig_d_r).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, kDataViewByteLength,
  ]);

  return builder.instantiate(kImports);
}

const kLength = 8 * 1024 * 1024 * 1024;

(function TestByteLengthLargeAlloc() {
  print(arguments.callee.name);
  let instance = MakeInstance()

  let big_array = new Int8Array(kLength);
  let big_dataview = new DataView(big_array.buffer);

  assertEquals(kLength, big_dataview.byteLength);
  assertEquals(kLength, instance.exports.byteLength(big_dataview));
})();

(function TestByteLengthLargeAllocLengthTrackingDataViewGSAB() {
  print(arguments.callee.name);
  let instance = MakeInstance();

  let buffer = new SharedArrayBuffer(kLength, {maxByteLength: 2 * kLength});
  let big_dataview = new DataView(buffer);

  assertEquals(kLength, instance.exports.byteLength(big_dataview));
})();
