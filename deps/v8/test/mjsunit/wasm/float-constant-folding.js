// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function() {
  print("F32: sNaN - 0 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32Sub0", kSig_i_i).addBody(
      [ kExprLocalGet, 0, kExprF32ReinterpretI32, kExprF32Const, 0x00, 0x00,
          0x00, 0x00, // 0.0
          kExprF32Sub, kExprI32ReinterpretF32, ]).exportFunc();
  var module = builder.instantiate();
  // F32Sub0(signalling_NaN)
  assertEquals(0x7fe00000, module.exports.F32Sub0(0x7fa00000));
})();

(function() {
  print("F32: -0 sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32Sub0", kSig_i_i).addBody(
      [ kExprF32Const, 0x00, 0x00, 0x00, 0x80, // 0.0
      kExprLocalGet, 0, kExprF32ReinterpretI32, kExprF32Sub,
          kExprI32ReinterpretF32, ]).exportFunc();
  var module = builder.instantiate();
  // F32Sub0(signalling_NaN)
  assertEquals(0x7fe00000, module.exports.F32Sub0(0x7fa00000));
})();

(function() {
  print("F32: sNaN - X = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32NaNSubX", kSig_i_i).addBody(
      [ kExprF32Const, 0x00, 0x00, 0xa0, 0x7f, kExprF32Const, 0x12, 0x34, 0x56,
          0x78, kExprF32Sub, kExprI32ReinterpretF32, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7fe00000, module.exports.F32NaNSubX());
})();

(function() {
  print("F32: X - sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32XSubNaN", kSig_i_i).addBody(
      [ kExprF32Const, 0x12, 0x34, 0x56, 0x78, kExprF32Const, 0x00, 0x00, 0xa0,
          0x7f, kExprF32Sub, kExprI32ReinterpretF32, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7fe00000, module.exports.F32XSubNaN());
})();

(function() {
  print("F64: X + sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32XAddNaN", kSig_i_i).addBody(
      [ kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Add, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F32XAddNaN());
})();

(function() {
  print("F64: sNaN - 0 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Sub0", kSig_i_i).addBody(
      [ kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF64Const, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, // 0.0
          kExprF64Sub, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Sub0());
})();

(function() {
  print("F64: -0 - sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Sub0", kSig_i_i).addBody(
      [ kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, // 0.0
      kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF64Sub, kExprI64ReinterpretF64,
          kExprI64Const, 32, kExprI64ShrU, kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Sub0());
})();

(function() {
  print("F64: sNaN - X = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64NaNSubX", kSig_i_i).addBody(
      [ kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Sub, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64NaNSubX());
})();

(function() {
  print("F64: X - sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64XSubNaN", kSig_i_i).addBody(
      [ kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Sub, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64XSubNaN());
})();

(function() {
  print("F64: sNaN * 1 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Mul1", kSig_i_i).addBody(
      [ kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF64Const, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0xf0, 0x3f, kExprF64Mul, kExprI64ReinterpretF64,
          kExprI64Const, 32, kExprI64ShrU, kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Mul1());
})();

(function() {
  print("F64: X * sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64XMulNaN", kSig_i_i).addBody(
      [ kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Mul, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64XMulNaN());
})();

(function() {
  print("F64: sNaN / 1 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Div1", kSig_i_i).addBody(
      [ kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF64Const, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0xf0, 0x3f, kExprF64Div, kExprI64ReinterpretF64,
          kExprI64Const, 32, kExprI64ShrU, kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Div1());
})();

(function() {
  print("F64: sNaN / -1 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Div1", kSig_i_i).addBody(
      [ kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF64Const, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0xf0, 0xbf, kExprF64Div, kExprI64ReinterpretF64,
          kExprI64Const, 32, kExprI64ShrU, kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Div1());
})();

(function() {
  print("F64: sNaN / -1 = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64Div1", kSig_i_i)
    .addBody([
      kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff, 0x00,
      kExprF64ReinterpretI64,
      kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xbf,
      kExprF64Div,
      kExprI64ReinterpretF64,
      kExprI64Const, 32,
      kExprI64ShrU,
      kExprI32ConvertI64,
            ])
            .exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64Div1());
})();

(function() {
  print("F64: X / sNaN = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64XDivNaN", kSig_i_i).addBody(
      [ kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Div, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64XDivNaN());
})();

(function() {
  print("F64: sNaN / X = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64NaNDivX", kSig_i_i).addBody(
      [ kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF64Const, 0xde, 0xbc, 0x0a, 0x89, 0x67, 0x45, 0x23, 0x01,
          kExprF64Div, kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64NaNDivX());
})();

(function() {
  print("F32ConvertF64(sNaN) = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F32ConvertF64X", kSig_i_i).addBody(
      [ kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x7f,
          kExprF32ConvertF64, kExprI32ReinterpretF32, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7fd00000, module.exports.F32ConvertF64X());
})();

(function() {
  print("F64ConvertF32(sNaN) = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64ConvertF32X", kSig_i_i).addBody(
      [ kExprF32Const, 0x00, 0x00, 0xa0, 0x7f, kExprF64ConvertF32,
          kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffc0000, module.exports.F64ConvertF32X());
})();

(function() {
  print("F64ConvertF32(F32ConvertF64(sNaN)) = qNaN");
  var builder = new WasmModuleBuilder();
  builder.addFunction("F64toF32toF64", kSig_i_i).addBody(
      [ kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf9, 0xff,
          0x00, kExprF64ReinterpretI64, kExprF32ConvertF64, kExprF64ConvertF32,
          kExprI64ReinterpretF64, kExprI64Const, 32, kExprI64ShrU,
          kExprI32ConvertI64, ]).exportFunc();
  var module = builder.instantiate();
  assertEquals(0x7ffa0000, module.exports.F64toF32toF64());
})();
