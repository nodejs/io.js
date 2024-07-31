// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".
let kSig_d_r = makeSig([kWasmExternRef], [kWasmF64]);
let kSig_l_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI64]);
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_f_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF32]);
let kSig_d_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF64]);
let kSig_v_rili = makeSig([kWasmExternRef, kWasmI32, kWasmI64, kWasmI32], []);
let kSig_v_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], []);
let kSig_v_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32], []);
let kSig_v_rifi = makeSig([kWasmExternRef, kWasmI32, kWasmF32, kWasmI32], []);
let kSig_v_ridi = makeSig([kWasmExternRef, kWasmI32, kWasmF64, kWasmI32], []);

let kImports = {
  DataView: {
    getBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.getBigInt64),
    getBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.getBigUint64),
    getFloat32Import:
        Function.prototype.call.bind(DataView.prototype.getFloat32),
    getFloat64Import:
        Function.prototype.call.bind(DataView.prototype.getFloat64),
    getInt8Import: Function.prototype.call.bind(DataView.prototype.getInt8),
    getInt16Import: Function.prototype.call.bind(DataView.prototype.getInt16),
    getInt32Import: Function.prototype.call.bind(DataView.prototype.getInt32),
    getUint8Import: Function.prototype.call.bind(DataView.prototype.getUint8),
    getUint16Import: Function.prototype.call.bind(DataView.prototype.getUint16),
    getUint32Import: Function.prototype.call.bind(DataView.prototype.getUint32),
    setBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.setBigInt64),
    setBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.setBigUint64),
    setFloat32Import:
        Function.prototype.call.bind(DataView.prototype.setFloat32),
    setFloat64Import:
        Function.prototype.call.bind(DataView.prototype.setFloat64),
    setInt8Import: Function.prototype.call.bind(DataView.prototype.setInt8),
    setInt16Import: Function.prototype.call.bind(DataView.prototype.setInt16),
    setInt32Import: Function.prototype.call.bind(DataView.prototype.setInt32),
    setUint8Import: Function.prototype.call.bind(DataView.prototype.setUint8),
    setUint16Import: Function.prototype.call.bind(DataView.prototype.setUint16),
    setUint32Import: Function.prototype.call.bind(DataView.prototype.setUint32),
    byteLengthImport: Function.prototype.call.bind(
        Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength').get)
  },
};

function MakeInstance() {
  let builder = new WasmModuleBuilder();

  let kDataViewGetBigInt64 =
      builder.addImport('DataView', 'getBigInt64Import', kSig_l_rii);
  let kDataViewGetBigUint64 =
      builder.addImport('DataView', 'getBigUint64Import', kSig_l_rii);
  let kDataViewGetFloat32 =
      builder.addImport('DataView', 'getFloat32Import', kSig_f_rii);
  let kDataViewGetFloat64 =
      builder.addImport('DataView', 'getFloat64Import', kSig_d_rii);
  let kDataViewGetInt8 =
      builder.addImport('DataView', 'getInt8Import', kSig_i_ri);
  let kDataViewGetInt16 =
      builder.addImport('DataView', 'getInt16Import', kSig_i_rii);
  let kDataViewGetInt32 =
      builder.addImport('DataView', 'getInt32Import', kSig_i_rii);
  let kDataViewGetUint8 =
      builder.addImport('DataView', 'getUint8Import', kSig_i_ri);
  let kDataViewGetUint16 =
      builder.addImport('DataView', 'getUint16Import', kSig_i_rii);
  let kDataViewGetUint32 =
      builder.addImport('DataView', 'getUint32Import', kSig_i_rii);
  let kDataViewSetBigInt64 =
      builder.addImport('DataView', 'setBigInt64Import', kSig_v_rili);
  let kDataViewSetBigUint64 =
      builder.addImport('DataView', 'setBigUint64Import', kSig_v_rili);
  let kDataViewSetFloat32 =
      builder.addImport('DataView', 'setFloat32Import', kSig_v_rifi);
  let kDataViewSetFloat64 =
      builder.addImport('DataView', 'setFloat64Import', kSig_v_ridi);
  let kDataViewSetInt8 =
      builder.addImport('DataView', 'setInt8Import', kSig_v_rii);
  let kDataViewSetInt16 =
      builder.addImport('DataView', 'setInt16Import', kSig_v_riii);
  let kDataViewSetInt32 =
      builder.addImport('DataView', 'setInt32Import', kSig_v_riii);
  let kDataViewSetUint8 =
      builder.addImport('DataView', 'setUint8Import', kSig_v_rii);
  let kDataViewSetUint16 =
      builder.addImport('DataView', 'setUint16Import', kSig_v_riii);
  let kDataViewSetUint32 =
      builder.addImport('DataView', 'setUint32Import', kSig_v_riii);
  let kDataViewByteLength =
      builder.addImport('DataView', 'byteLengthImport', kSig_d_r);

  return builder.instantiate(kImports);
}

let instance = MakeInstance();
