// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const GB = 1024 * 1024 * 1024;
const SRC_OFFSET = 4294970000n; // 0x100000a90n
const SRC_OFFSET_LEB = [0x90, 0x95, 0x80, 0x80, 0x10];
const DST_OFFSET = 4294970160n;
const DST_OFFSET_LEB = [0xb0, 0x96, 0x80, 0x80, 0x10];

const builder = new WasmModuleBuilder();
builder.addMemory64(5 * GB / kPageSize);
builder.exportMemoryAs('memory');

// Here we make a global of type v128 to be the target
// for loading lanes and the source for storing lanes.
var g = builder.addGlobal(
  kWasmS128, true, false,
  [kSimdPrefix, kExprS128Const,
   1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0]);

for (let i = 0; i < 4; ++i) {
  builder.addFunction(`load_lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Load32Lane, 0, 0, i,
                kExprGlobalSet, g.index])
      .exportFunc();

  builder.addFunction(`store_lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Store32Lane, 0, 0, i])
      .exportFunc();

  builder.addFunction(`Load_Lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Load32Lane, 0, ...SRC_OFFSET_LEB, i,
                kExprGlobalSet, g.index])
      .exportFunc();

  builder.addFunction(`Store_Lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Store32Lane, 0, ...DST_OFFSET_LEB, i])
      .exportFunc();
}

(function TestLoadStoreLaneExternalOffset(){
  print(arguments.callee.name);

  var instance = builder.instantiate({});
  var buffer = instance.exports.memory.buffer;

  var src_view = new Uint32Array(buffer, Number(SRC_OFFSET), 4);
  var dst_view = new Uint32Array(buffer, Number(DST_OFFSET), 4);
  var values = [ 0x01234567, 0x89abcdef, 0x76543210, 0xfedcba98 ];
  var expected_values = [ 0, 0, 0, 0 ];
  src_view.set(values, 0);

  for (let i = 0n; i < 4n; ++i) {
    expected_values[i] = values[i];
    const offset = 4n * i;
    instance.exports[`load_lane_${i}`](SRC_OFFSET + offset);
    instance.exports[`store_lane_${i}`](DST_OFFSET + offset);
    assertEquals(expected_values, Array.from(dst_view.values()));
  }
})();

(function TestLoadStoreLaneInternalOffset(){
  print(arguments.callee.name);

  var instance = builder.instantiate({});
  var buffer = instance.exports.memory.buffer;

  var src_view = new Uint32Array(buffer, Number(SRC_OFFSET), 4);
  var dst_view = new Uint32Array(buffer, Number(DST_OFFSET), 4);
  var values = [ 0x01234567, 0x89abcdef, 0x76543210, 0xfedcba98 ];
  var expected_values = [ 0, 0, 0, 0 ];
  src_view.set(values, 0);

  for (let i = 0n; i < 4n; ++i) {
    expected_values[i] = values[i];
    const offset = 4n * i;
    instance.exports[`Load_Lane_${i}`](offset);
    instance.exports[`Store_Lane_${i}`](offset);
    assertEquals(expected_values, Array.from(dst_view.values()));
  }
})();
