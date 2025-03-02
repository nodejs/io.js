// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestRefTest() {
  var builder = new WasmModuleBuilder();
  builder.startRecGroup();
  let structSuper = builder.addStruct([makeField(kWasmI32, true)]);
  let structSub = builder.addStruct([makeField(kWasmI32, true)], structSuper);
  let array = builder.addArray(kWasmI32);
  builder.endRecGroup();

  let fct =
  builder.addFunction('createStructSuper',
                      makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, structSuper,
      kGCPrefix, kExprExternConvertAny,
    ]).exportFunc();
  builder.addFunction('createStructSub', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, structSub,
      kGCPrefix, kExprExternConvertAny,
    ]).exportFunc();
  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1,
      kGCPrefix, kExprExternConvertAny,
    ]).exportFunc();
  builder.addFunction('createFuncRef', makeSig([], [kWasmFuncRef]))
    .addBody([
      kExprRefFunc, fct.index,
    ]).exportFunc();

  [
    ["StructSuper", structSuper],
    ["StructSub", structSub],
    ["Array", array],
    ["I31", kI31RefCode],
    ["AnyArray", kArrayRefCode],
    ["Struct", kStructRefCode],
    ["Eq", kEqRefCode],
    ["String", kStringRefCode],
    // 'ref.test any' is semantically the same as '!ref.is_null' here.
    ["Any", kAnyRefCode],
    ["None", kNullRefCode]
  ].forEach(([typeName, typeCode]) => {
    builder.addFunction(`refTest${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefTest, typeCode,
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefTestNull, typeCode,
    ]).exportFunc();

    builder.addFunction(`refCast${typeName}`,
                        makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, typeCode,
      kGCPrefix, kExprExternConvertAny,
    ]).exportFunc();

    builder.addFunction(`refCastNull${typeName}`,
                        makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, typeCode,
      kGCPrefix, kExprExternConvertAny,
    ]).exportFunc();

    builder.addFunction(`brOnCast${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprBlock, kWasmRef, typeCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprBrOnCast, 0b01, 0, kAnyRefCode, typeCode,
        kExprI32Const, 0,
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 1,
      kExprReturn,
    ]).exportFunc();
    builder.addFunction(`brOnCastNull${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprBlock, kWasmRefNull, typeCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprBrOnCast, 0b11, 0, kAnyRefCode, typeCode,
        kExprI32Const, 0,
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 1,
      kExprReturn,
    ]).exportFunc();
    builder.addFunction(`brOnCastFail${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprBlock, kAnyRefCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprBrOnCastFail, 0b01, 0, kAnyRefCode, typeCode,
        kExprI32Const, 0,
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 1,
      kExprReturn,
    ]).exportFunc();
    builder.addFunction(`brOnCastFailNull${typeName}`,
                        makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprBlock, kAnyRefCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprBrOnCastFail, 0b11, 0, kAnyRefCode, typeCode,
        kExprI32Const, 0,
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 1,
      kExprReturn,
    ]).exportFunc();
  });

  var instance = builder.instantiate();
  let wasm = instance.exports;
  // result: [ref.test, ref.test null]
  assertEquals([0, 1], wasm.refTestStructSuper(null));
  assertEquals([0, 0], wasm.refTestStructSuper(undefined));
  assertEquals([1, 1], wasm.refTestStructSuper(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStructSuper(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStructSuper(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStructSuper(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStructSuper(1));
  assertEquals([0, 0], wasm.refTestStructSuper({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestStructSuper('string'));

  assertEquals([0, 1], wasm.refTestStructSub(null));
  assertEquals([0, 0], wasm.refTestStructSub(undefined));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStructSub(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStructSub(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStructSub(1));
  assertEquals([0, 0], wasm.refTestStructSub({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestStructSub('string'));

  assertEquals([0, 1], wasm.refTestArray(null));
  assertEquals([0, 0], wasm.refTestArray(undefined));
  assertEquals([0, 0], wasm.refTestArray(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestArray(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestArray(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestArray(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestArray(1));
  assertEquals([0, 0], wasm.refTestArray({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestArray('string'));

  assertEquals([0, 1], wasm.refTestI31(null));
  assertEquals([0, 0], wasm.refTestI31(undefined));
  assertEquals([0, 0], wasm.refTestI31(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestI31(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestI31(1));
  assertEquals([0, 0], wasm.refTestI31({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestI31('string'));

  assertEquals([0, 1], wasm.refTestAnyArray(null));
  assertEquals([0, 0], wasm.refTestAnyArray(undefined));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestAnyArray(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestAnyArray(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestAnyArray(1));
  assertEquals([0, 0], wasm.refTestAnyArray({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestAnyArray('string'));

  assertEquals([0, 1], wasm.refTestStruct(null));
  assertEquals([0, 0], wasm.refTestStruct(undefined));
  assertEquals([1, 1], wasm.refTestStruct(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestStruct(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestStruct(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestStruct(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestStruct(1));
  assertEquals([0, 0], wasm.refTestStruct({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestStruct('string'));

  assertEquals([0, 1], wasm.refTestString(null));
  assertEquals([0, 0], wasm.refTestString(undefined));
  assertEquals([0, 0], wasm.refTestString(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestString(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestString(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestString(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestString(1));
  assertEquals([0, 0], wasm.refTestString({'JavaScript': 'Object'}));
  assertEquals([1, 1], wasm.refTestString('string'));

  assertEquals([0, 1], wasm.refTestEq(null));
  assertEquals([0, 0], wasm.refTestEq(undefined));
  assertEquals([1, 1], wasm.refTestEq(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestEq(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestEq(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestEq(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestEq(1)); // ref.i31
  assertEquals([0, 0], wasm.refTestEq({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestEq('string'));

  assertEquals([0, 1], wasm.refTestAny(null));
  assertEquals([1, 1], wasm.refTestAny(undefined));
  assertEquals([1, 1], wasm.refTestAny(wasm.createStructSuper()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createStructSub()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createArray()));
  assertEquals([1, 1], wasm.refTestAny(wasm.createFuncRef()));
  assertEquals([1, 1], wasm.refTestAny(1)); // ref.i31
  assertEquals([1, 1], wasm.refTestAny({'JavaScript': 'Object'}));
  assertEquals([1, 1], wasm.refTestAny('string'));

  assertEquals([0, 1], wasm.refTestNone(null));
  assertEquals([0, 0], wasm.refTestNone(undefined));
  assertEquals([0, 0], wasm.refTestNone(wasm.createStructSuper()));
  assertEquals([0, 0], wasm.refTestNone(wasm.createStructSub()));
  assertEquals([0, 0], wasm.refTestNone(wasm.createArray()));
  assertEquals([0, 0], wasm.refTestNone(wasm.createFuncRef()));
  assertEquals([0, 0], wasm.refTestNone(1)); // ref.i31
  assertEquals([0, 0], wasm.refTestNone({'JavaScript': 'Object'}));
  assertEquals([0, 0], wasm.refTestNone('string'));

  // ref.cast
  let structSuperObj = wasm.createStructSuper();
  let structSubObj = wasm.createStructSub();
  let arrayObj = wasm.createArray();
  let jsObj = {'JavaScript': 'Object'};
  let strObj = 'string';
  let funcObj = wasm.createFuncRef();

  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(undefined));
  assertSame(structSuperObj, wasm.refCastStructSuper(structSuperObj));
  assertSame(structSubObj, wasm.refCastStructSuper(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSuper(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(structSuperObj));
  assertSame(structSubObj, wasm.refCastStructSub(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStructSub(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(structSubObj));
  assertSame(arrayObj, wasm.refCastArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastArray(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(funcObj));
  assertEquals(1, wasm.refCastI31(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastI31(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(structSubObj));
  assertSame(arrayObj, wasm.refCastAnyArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastAnyArray(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(undefined));
  assertSame(structSuperObj, wasm.refCastStruct(structSuperObj));
  assertSame(structSubObj, wasm.refCastStruct(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastStruct(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastString(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastString(jsObj));
  assertSame(strObj, wasm.refCastString(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(undefined));
  assertSame(structSuperObj, wasm.refCastEq(structSuperObj));
  assertSame(structSubObj, wasm.refCastEq(structSubObj));
  assertSame(arrayObj, wasm.refCastEq(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(funcObj));
  assertEquals(1, wasm.refCastEq(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastEq(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastAny(null));
  assertSame(undefined, wasm.refCastAny(undefined));
  assertSame(structSuperObj, wasm.refCastAny(structSuperObj));
  assertSame(structSubObj, wasm.refCastAny(structSubObj));
  assertSame(arrayObj, wasm.refCastAny(arrayObj));
  assertSame(funcObj, wasm.refCastAny(funcObj));
  assertEquals(1, wasm.refCastAny(1));
  assertSame(jsObj, wasm.refCastAny(jsObj));
  assertSame(strObj, wasm.refCastAny(strObj));

  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNone(strObj));

  // ref.cast null
  assertSame(null, wasm.refCastNullStructSuper(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(undefined));
  assertSame(structSuperObj, wasm.refCastNullStructSuper(structSuperObj));
  assertSame(structSubObj, wasm.refCastNullStructSuper(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSuper(strObj));

  assertSame(null, wasm.refCastNullStructSub(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(structSuperObj));
  assertSame(structSubObj, wasm.refCastNullStructSub(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStructSub(strObj));

  assertSame(null, wasm.refCastNullArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(structSubObj));
  assertSame(arrayObj, wasm.refCastNullArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullArray(strObj));

  assertSame(null, wasm.refCastNullI31(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(funcObj));
  assertEquals(1, wasm.refCastNullI31(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullI31(strObj));

  assertSame(null, wasm.refCastNullAnyArray(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(structSubObj));
  assertSame(arrayObj, wasm.refCastNullAnyArray(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullAnyArray(strObj));

  assertSame(null, wasm.refCastNullStruct(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(undefined));
  assertSame(structSuperObj, wasm.refCastNullStruct(structSuperObj));
  assertSame(structSubObj, wasm.refCastNullStruct(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullStruct(strObj));

  assertSame(null, wasm.refCastNullString(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullString(jsObj));
  assertSame(strObj, wasm.refCastNullString(strObj));

  assertSame(null, wasm.refCastNullEq(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullEq(undefined));
  assertSame(structSuperObj, wasm.refCastNullEq(structSuperObj));
  assertSame(structSubObj, wasm.refCastNullEq(structSubObj));
  assertSame(arrayObj, wasm.refCastNullEq(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullEq(funcObj));
  assertEquals(1, wasm.refCastNullEq(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullEq(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullEq(strObj));

  assertSame(null, wasm.refCastNullAny(null));
  assertSame(undefined, wasm.refCastNullAny(undefined));
  assertSame(structSuperObj, wasm.refCastNullAny(structSuperObj));
  assertSame(structSubObj, wasm.refCastNullAny(structSubObj));
  assertSame(arrayObj, wasm.refCastNullAny(arrayObj));
  assertSame(funcObj, wasm.refCastNullAny(funcObj));
  assertEquals(1, wasm.refCastNullAny(1));
  assertSame(jsObj, wasm.refCastNullAny(jsObj));
  assertSame(strObj, wasm.refCastNullAny(strObj));

  assertSame(null, wasm.refCastNullNone(null));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(undefined));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(structSuperObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(structSubObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(arrayObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(funcObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(1));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(jsObj));
  assertTraps(kTrapIllegalCast, () => wasm.refCastNullNone(strObj));

  // br_on_cast
  assertEquals(0, wasm.brOnCastStructSuper(null));
  assertEquals(0, wasm.brOnCastStructSuper(undefined));
  assertEquals(1, wasm.brOnCastStructSuper(structSuperObj));
  assertEquals(1, wasm.brOnCastStructSuper(structSubObj));
  assertEquals(0, wasm.brOnCastStructSuper(arrayObj));
  assertEquals(0, wasm.brOnCastStructSuper(funcObj));
  assertEquals(0, wasm.brOnCastStructSuper(1));
  assertEquals(0, wasm.brOnCastStructSuper(jsObj));
  assertEquals(0, wasm.brOnCastStructSuper(strObj));

  assertEquals(0, wasm.brOnCastStructSub(null));
  assertEquals(0, wasm.brOnCastStructSub(undefined));
  assertEquals(0, wasm.brOnCastStructSub(structSuperObj));
  assertEquals(1, wasm.brOnCastStructSub(structSubObj));
  assertEquals(0, wasm.brOnCastStructSub(arrayObj));
  assertEquals(0, wasm.brOnCastStructSub(funcObj));
  assertEquals(0, wasm.brOnCastStructSub(1));
  assertEquals(0, wasm.brOnCastStructSub(jsObj));
  assertEquals(0, wasm.brOnCastStructSub(strObj));

  assertEquals(0, wasm.brOnCastArray(null));
  assertEquals(0, wasm.brOnCastArray(undefined));
  assertEquals(0, wasm.brOnCastArray(structSuperObj));
  assertEquals(0, wasm.brOnCastArray(structSubObj));
  assertEquals(1, wasm.brOnCastArray(arrayObj));
  assertEquals(0, wasm.brOnCastArray(funcObj));
  assertEquals(0, wasm.brOnCastArray(1));
  assertEquals(0, wasm.brOnCastArray(jsObj));
  assertEquals(0, wasm.brOnCastArray(strObj));

  assertEquals(0, wasm.brOnCastI31(null));
  assertEquals(0, wasm.brOnCastI31(undefined));
  assertEquals(0, wasm.brOnCastI31(structSuperObj));
  assertEquals(0, wasm.brOnCastI31(structSubObj));
  assertEquals(0, wasm.brOnCastI31(arrayObj));
  assertEquals(0, wasm.brOnCastI31(funcObj));
  assertEquals(1, wasm.brOnCastI31(1));
  assertEquals(0, wasm.brOnCastI31(jsObj));
  assertEquals(0, wasm.brOnCastI31(strObj));

  assertEquals(0, wasm.brOnCastAnyArray(null));
  assertEquals(0, wasm.brOnCastAnyArray(undefined));
  assertEquals(0, wasm.brOnCastAnyArray(structSuperObj));
  assertEquals(0, wasm.brOnCastAnyArray(structSubObj));
  assertEquals(1, wasm.brOnCastAnyArray(arrayObj));
  assertEquals(0, wasm.brOnCastAnyArray(funcObj));
  assertEquals(0, wasm.brOnCastAnyArray(1));
  assertEquals(0, wasm.brOnCastAnyArray(jsObj));
  assertEquals(0, wasm.brOnCastAnyArray(strObj));

  assertEquals(0, wasm.brOnCastStruct(null));
  assertEquals(0, wasm.brOnCastStruct(undefined));
  assertEquals(1, wasm.brOnCastStruct(structSuperObj));
  assertEquals(1, wasm.brOnCastStruct(structSubObj));
  assertEquals(0, wasm.brOnCastStruct(arrayObj));
  assertEquals(0, wasm.brOnCastStruct(funcObj));
  assertEquals(0, wasm.brOnCastStruct(1));
  assertEquals(0, wasm.brOnCastStruct(jsObj));
  assertEquals(0, wasm.brOnCastStruct(strObj));

  assertEquals(0, wasm.brOnCastEq(null));
  assertEquals(0, wasm.brOnCastEq(undefined));
  assertEquals(1, wasm.brOnCastEq(structSuperObj));
  assertEquals(1, wasm.brOnCastEq(structSubObj));
  assertEquals(1, wasm.brOnCastEq(arrayObj));
  assertEquals(0, wasm.brOnCastEq(funcObj));
  assertEquals(1, wasm.brOnCastEq(1));
  assertEquals(0, wasm.brOnCastEq(jsObj));
  assertEquals(0, wasm.brOnCastEq(strObj));

  assertEquals(0, wasm.brOnCastString(null));
  assertEquals(0, wasm.brOnCastString(undefined));
  assertEquals(0, wasm.brOnCastString(structSuperObj));
  assertEquals(0, wasm.brOnCastString(structSubObj));
  assertEquals(0, wasm.brOnCastString(arrayObj));
  assertEquals(0, wasm.brOnCastString(funcObj));
  assertEquals(0, wasm.brOnCastString(1));
  assertEquals(0, wasm.brOnCastString(jsObj));
  assertEquals(1, wasm.brOnCastString(strObj));

  assertEquals(0, wasm.brOnCastAny(null));
  assertEquals(1, wasm.brOnCastAny(undefined));
  assertEquals(1, wasm.brOnCastAny(structSuperObj));
  assertEquals(1, wasm.brOnCastAny(structSubObj));
  assertEquals(1, wasm.brOnCastAny(arrayObj));
  assertEquals(1, wasm.brOnCastAny(funcObj));
  assertEquals(1, wasm.brOnCastAny(1));
  assertEquals(1, wasm.brOnCastAny(jsObj));
  assertEquals(1, wasm.brOnCastAny(strObj));

  assertEquals(0, wasm.brOnCastNone(null));
  assertEquals(0, wasm.brOnCastNone(undefined));
  assertEquals(0, wasm.brOnCastNone(structSuperObj));
  assertEquals(0, wasm.brOnCastNone(structSubObj));
  assertEquals(0, wasm.brOnCastNone(arrayObj));
  assertEquals(0, wasm.brOnCastNone(funcObj));
  assertEquals(0, wasm.brOnCastNone(1));
  assertEquals(0, wasm.brOnCastNone(jsObj));
  assertEquals(0, wasm.brOnCastNone(strObj));

  // br_on_cast null
  assertEquals(1, wasm.brOnCastNullStructSuper(null));
  assertEquals(0, wasm.brOnCastNullStructSuper(undefined));
  assertEquals(1, wasm.brOnCastNullStructSuper(structSuperObj));
  assertEquals(1, wasm.brOnCastNullStructSuper(structSubObj));
  assertEquals(0, wasm.brOnCastNullStructSuper(arrayObj));
  assertEquals(0, wasm.brOnCastNullStructSuper(funcObj));
  assertEquals(0, wasm.brOnCastNullStructSuper(1));
  assertEquals(0, wasm.brOnCastNullStructSuper(jsObj));
  assertEquals(0, wasm.brOnCastNullStructSuper(strObj));

  assertEquals(1, wasm.brOnCastNullStructSub(null));
  assertEquals(0, wasm.brOnCastNullStructSub(undefined));
  assertEquals(0, wasm.brOnCastNullStructSub(structSuperObj));
  assertEquals(1, wasm.brOnCastNullStructSub(structSubObj));
  assertEquals(0, wasm.brOnCastNullStructSub(arrayObj));
  assertEquals(0, wasm.brOnCastNullStructSub(funcObj));
  assertEquals(0, wasm.brOnCastNullStructSub(1));
  assertEquals(0, wasm.brOnCastNullStructSub(jsObj));
  assertEquals(0, wasm.brOnCastNullStructSub(strObj));

  assertEquals(1, wasm.brOnCastNullArray(null));
  assertEquals(0, wasm.brOnCastNullArray(undefined));
  assertEquals(0, wasm.brOnCastNullArray(structSuperObj));
  assertEquals(0, wasm.brOnCastNullArray(structSubObj));
  assertEquals(1, wasm.brOnCastNullArray(arrayObj));
  assertEquals(0, wasm.brOnCastNullArray(funcObj));
  assertEquals(0, wasm.brOnCastNullArray(1));
  assertEquals(0, wasm.brOnCastNullArray(jsObj));
  assertEquals(0, wasm.brOnCastNullArray(strObj));

  assertEquals(1, wasm.brOnCastNullI31(null));
  assertEquals(0, wasm.brOnCastNullI31(undefined));
  assertEquals(0, wasm.brOnCastNullI31(structSuperObj));
  assertEquals(0, wasm.brOnCastNullI31(structSubObj));
  assertEquals(0, wasm.brOnCastNullI31(arrayObj));
  assertEquals(0, wasm.brOnCastNullI31(funcObj));
  assertEquals(1, wasm.brOnCastNullI31(1));
  assertEquals(0, wasm.brOnCastNullI31(jsObj));
  assertEquals(0, wasm.brOnCastNullI31(strObj));

  assertEquals(1, wasm.brOnCastNullAnyArray(null));
  assertEquals(0, wasm.brOnCastNullAnyArray(undefined));
  assertEquals(0, wasm.brOnCastNullAnyArray(structSuperObj));
  assertEquals(0, wasm.brOnCastNullAnyArray(structSubObj));
  assertEquals(1, wasm.brOnCastNullAnyArray(arrayObj));
  assertEquals(0, wasm.brOnCastNullAnyArray(funcObj));
  assertEquals(0, wasm.brOnCastNullAnyArray(1));
  assertEquals(0, wasm.brOnCastNullAnyArray(jsObj));
  assertEquals(0, wasm.brOnCastNullAnyArray(strObj));

  assertEquals(1, wasm.brOnCastNullStruct(null));
  assertEquals(0, wasm.brOnCastNullStruct(undefined));
  assertEquals(1, wasm.brOnCastNullStruct(structSuperObj));
  assertEquals(1, wasm.brOnCastNullStruct(structSubObj));
  assertEquals(0, wasm.brOnCastNullStruct(arrayObj));
  assertEquals(0, wasm.brOnCastNullStruct(funcObj));
  assertEquals(0, wasm.brOnCastNullStruct(1));
  assertEquals(0, wasm.brOnCastNullStruct(jsObj));
  assertEquals(0, wasm.brOnCastNullStruct(strObj));

  assertEquals(1, wasm.brOnCastNullEq(null));
  assertEquals(0, wasm.brOnCastNullEq(undefined));
  assertEquals(1, wasm.brOnCastNullEq(structSuperObj));
  assertEquals(1, wasm.brOnCastNullEq(structSubObj));
  assertEquals(1, wasm.brOnCastNullEq(arrayObj));
  assertEquals(0, wasm.brOnCastNullEq(funcObj));
  assertEquals(1, wasm.brOnCastNullEq(1));
  assertEquals(0, wasm.brOnCastNullEq(jsObj));
  assertEquals(0, wasm.brOnCastNullEq(strObj));

  assertEquals(1, wasm.brOnCastNullString(null));
  assertEquals(0, wasm.brOnCastNullString(undefined));
  assertEquals(0, wasm.brOnCastNullString(structSuperObj));
  assertEquals(0, wasm.brOnCastNullString(structSubObj));
  assertEquals(0, wasm.brOnCastNullString(arrayObj));
  assertEquals(0, wasm.brOnCastNullString(funcObj));
  assertEquals(0, wasm.brOnCastNullString(1));
  assertEquals(0, wasm.brOnCastNullString(jsObj));
  assertEquals(1, wasm.brOnCastNullString(strObj));

  assertEquals(1, wasm.brOnCastNullAny(null));
  assertEquals(1, wasm.brOnCastNullAny(undefined));
  assertEquals(1, wasm.brOnCastNullAny(structSuperObj));
  assertEquals(1, wasm.brOnCastNullAny(structSubObj));
  assertEquals(1, wasm.brOnCastNullAny(arrayObj));
  assertEquals(1, wasm.brOnCastNullAny(funcObj));
  assertEquals(1, wasm.brOnCastNullAny(1));
  assertEquals(1, wasm.brOnCastNullAny(jsObj));
  assertEquals(1, wasm.brOnCastNullAny(strObj));

  assertEquals(1, wasm.brOnCastNullNone(null));
  assertEquals(0, wasm.brOnCastNullNone(undefined));
  assertEquals(0, wasm.brOnCastNullNone(structSuperObj));
  assertEquals(0, wasm.brOnCastNullNone(structSubObj));
  assertEquals(0, wasm.brOnCastNullNone(arrayObj));
  assertEquals(0, wasm.brOnCastNullNone(funcObj));
  assertEquals(0, wasm.brOnCastNullNone(1));
  assertEquals(0, wasm.brOnCastNullNone(jsObj));
  assertEquals(0, wasm.brOnCastNullNone(strObj));

  // br_on_cast_fail
  assertEquals(1, wasm.brOnCastFailStructSuper(null));
  assertEquals(1, wasm.brOnCastFailStructSuper(undefined));
  assertEquals(0, wasm.brOnCastFailStructSuper(structSuperObj));
  assertEquals(0, wasm.brOnCastFailStructSuper(structSubObj));
  assertEquals(1, wasm.brOnCastFailStructSuper(arrayObj));
  assertEquals(1, wasm.brOnCastFailStructSuper(funcObj));
  assertEquals(1, wasm.brOnCastFailStructSuper(1));
  assertEquals(1, wasm.brOnCastFailStructSuper(jsObj));
  assertEquals(1, wasm.brOnCastFailStructSuper(strObj));

  assertEquals(1, wasm.brOnCastFailStructSub(null));
  assertEquals(1, wasm.brOnCastFailStructSub(undefined));
  assertEquals(1, wasm.brOnCastFailStructSub(structSuperObj));
  assertEquals(0, wasm.brOnCastFailStructSub(structSubObj));
  assertEquals(1, wasm.brOnCastFailStructSub(arrayObj));
  assertEquals(1, wasm.brOnCastFailStructSub(funcObj));
  assertEquals(1, wasm.brOnCastFailStructSub(1));
  assertEquals(1, wasm.brOnCastFailStructSub(jsObj));
  assertEquals(1, wasm.brOnCastFailStructSub(strObj));

  assertEquals(1, wasm.brOnCastFailArray(null));
  assertEquals(1, wasm.brOnCastFailArray(undefined));
  assertEquals(1, wasm.brOnCastFailArray(structSuperObj));
  assertEquals(1, wasm.brOnCastFailArray(structSubObj));
  assertEquals(0, wasm.brOnCastFailArray(arrayObj));
  assertEquals(1, wasm.brOnCastFailArray(funcObj));
  assertEquals(1, wasm.brOnCastFailArray(1));
  assertEquals(1, wasm.brOnCastFailArray(jsObj));
  assertEquals(1, wasm.brOnCastFailArray(strObj));

  assertEquals(1, wasm.brOnCastFailI31(null));
  assertEquals(1, wasm.brOnCastFailI31(undefined));
  assertEquals(1, wasm.brOnCastFailI31(structSuperObj));
  assertEquals(1, wasm.brOnCastFailI31(structSubObj));
  assertEquals(1, wasm.brOnCastFailI31(arrayObj));
  assertEquals(1, wasm.brOnCastFailI31(funcObj));
  assertEquals(0, wasm.brOnCastFailI31(1));
  assertEquals(1, wasm.brOnCastFailI31(jsObj));
  assertEquals(1, wasm.brOnCastFailI31(strObj));

  assertEquals(1, wasm.brOnCastFailAnyArray(null));
  assertEquals(1, wasm.brOnCastFailAnyArray(undefined));
  assertEquals(1, wasm.brOnCastFailAnyArray(structSuperObj));
  assertEquals(1, wasm.brOnCastFailAnyArray(structSubObj));
  assertEquals(0, wasm.brOnCastFailAnyArray(arrayObj));
  assertEquals(1, wasm.brOnCastFailAnyArray(funcObj));
  assertEquals(1, wasm.brOnCastFailAnyArray(1));
  assertEquals(1, wasm.brOnCastFailAnyArray(jsObj));
  assertEquals(1, wasm.brOnCastFailAnyArray(strObj));

  assertEquals(1, wasm.brOnCastFailStruct(null));
  assertEquals(1, wasm.brOnCastFailStruct(undefined));
  assertEquals(0, wasm.brOnCastFailStruct(structSuperObj));
  assertEquals(0, wasm.brOnCastFailStruct(structSubObj));
  assertEquals(1, wasm.brOnCastFailStruct(arrayObj));
  assertEquals(1, wasm.brOnCastFailStruct(funcObj));
  assertEquals(1, wasm.brOnCastFailStruct(1));
  assertEquals(1, wasm.brOnCastFailStruct(jsObj));
  assertEquals(1, wasm.brOnCastFailStruct(strObj));

  assertEquals(1, wasm.brOnCastFailEq(null));
  assertEquals(1, wasm.brOnCastFailEq(undefined));
  assertEquals(0, wasm.brOnCastFailEq(structSuperObj));
  assertEquals(0, wasm.brOnCastFailEq(structSubObj));
  assertEquals(0, wasm.brOnCastFailEq(arrayObj));
  assertEquals(1, wasm.brOnCastFailEq(funcObj));
  assertEquals(0, wasm.brOnCastFailEq(1));
  assertEquals(1, wasm.brOnCastFailEq(jsObj));
  assertEquals(1, wasm.brOnCastFailEq(strObj));

  assertEquals(1, wasm.brOnCastFailString(null));
  assertEquals(1, wasm.brOnCastFailString(undefined));
  assertEquals(1, wasm.brOnCastFailString(structSuperObj));
  assertEquals(1, wasm.brOnCastFailString(structSubObj));
  assertEquals(1, wasm.brOnCastFailString(arrayObj));
  assertEquals(1, wasm.brOnCastFailString(funcObj));
  assertEquals(1, wasm.brOnCastFailString(1));
  assertEquals(1, wasm.brOnCastFailString(jsObj));
  assertEquals(0, wasm.brOnCastFailString(strObj));

  assertEquals(1, wasm.brOnCastFailAny(null));
  assertEquals(0, wasm.brOnCastFailAny(undefined));
  assertEquals(0, wasm.brOnCastFailAny(structSuperObj));
  assertEquals(0, wasm.brOnCastFailAny(structSubObj));
  assertEquals(0, wasm.brOnCastFailAny(arrayObj));
  assertEquals(0, wasm.brOnCastFailAny(funcObj));
  assertEquals(0, wasm.brOnCastFailAny(1));
  assertEquals(0, wasm.brOnCastFailAny(jsObj));
  assertEquals(0, wasm.brOnCastFailAny(strObj));

  assertEquals(1, wasm.brOnCastFailNone(null));
  assertEquals(1, wasm.brOnCastFailNone(undefined));
  assertEquals(1, wasm.brOnCastFailNone(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNone(structSubObj));
  assertEquals(1, wasm.brOnCastFailNone(arrayObj));
  assertEquals(1, wasm.brOnCastFailNone(funcObj));
  assertEquals(1, wasm.brOnCastFailNone(1));
  assertEquals(1, wasm.brOnCastFailNone(jsObj));
  assertEquals(1, wasm.brOnCastFailNone(strObj));

  // br_on_cast_fail null
  assertEquals(0, wasm.brOnCastFailNullStructSuper(null));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(undefined));
  assertEquals(0, wasm.brOnCastFailNullStructSuper(structSuperObj));
  assertEquals(0, wasm.brOnCastFailNullStructSuper(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(funcObj));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(1));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(jsObj));
  assertEquals(1, wasm.brOnCastFailNullStructSuper(strObj));

  assertEquals(0, wasm.brOnCastFailNullStructSub(null));
  assertEquals(1, wasm.brOnCastFailNullStructSub(undefined));
  assertEquals(1, wasm.brOnCastFailNullStructSub(structSuperObj));
  assertEquals(0, wasm.brOnCastFailNullStructSub(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullStructSub(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullStructSub(funcObj));
  assertEquals(1, wasm.brOnCastFailNullStructSub(1));
  assertEquals(1, wasm.brOnCastFailNullStructSub(jsObj));
  assertEquals(1, wasm.brOnCastFailNullStructSub(strObj));

  assertEquals(0, wasm.brOnCastFailNullArray(null));
  assertEquals(1, wasm.brOnCastFailNullArray(undefined));
  assertEquals(1, wasm.brOnCastFailNullArray(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNullArray(structSubObj));
  assertEquals(0, wasm.brOnCastFailNullArray(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullArray(funcObj));
  assertEquals(1, wasm.brOnCastFailNullArray(1));
  assertEquals(1, wasm.brOnCastFailNullArray(jsObj));
  assertEquals(1, wasm.brOnCastFailNullArray(strObj));

  assertEquals(0, wasm.brOnCastFailNullI31(null));
  assertEquals(1, wasm.brOnCastFailNullI31(undefined));
  assertEquals(1, wasm.brOnCastFailNullI31(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNullI31(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullI31(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullI31(funcObj));
  assertEquals(0, wasm.brOnCastFailNullI31(1));
  assertEquals(1, wasm.brOnCastFailNullI31(jsObj));
  assertEquals(1, wasm.brOnCastFailNullI31(strObj));

  assertEquals(0, wasm.brOnCastFailNullAnyArray(null));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(undefined));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(structSubObj));
  assertEquals(0, wasm.brOnCastFailNullAnyArray(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(funcObj));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(1));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(jsObj));
  assertEquals(1, wasm.brOnCastFailNullAnyArray(strObj));

  assertEquals(0, wasm.brOnCastFailNullStruct(null));
  assertEquals(1, wasm.brOnCastFailNullStruct(undefined));
  assertEquals(0, wasm.brOnCastFailNullStruct(structSuperObj));
  assertEquals(0, wasm.brOnCastFailNullStruct(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullStruct(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullStruct(funcObj));
  assertEquals(1, wasm.brOnCastFailNullStruct(1));
  assertEquals(1, wasm.brOnCastFailNullStruct(jsObj));
  assertEquals(1, wasm.brOnCastFailNullStruct(strObj));

  assertEquals(0, wasm.brOnCastFailNullEq(null));
  assertEquals(1, wasm.brOnCastFailNullEq(undefined));
  assertEquals(0, wasm.brOnCastFailNullEq(structSuperObj));
  assertEquals(0, wasm.brOnCastFailNullEq(structSubObj));
  assertEquals(0, wasm.brOnCastFailNullEq(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullEq(funcObj));
  assertEquals(0, wasm.brOnCastFailNullEq(1));
  assertEquals(1, wasm.brOnCastFailNullEq(jsObj));
  assertEquals(1, wasm.brOnCastFailNullEq(strObj));

  assertEquals(0, wasm.brOnCastFailNullString(null));
  assertEquals(1, wasm.brOnCastFailNullString(undefined));
  assertEquals(1, wasm.brOnCastFailNullString(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNullString(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullString(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullString(funcObj));
  assertEquals(1, wasm.brOnCastFailNullString(1));
  assertEquals(1, wasm.brOnCastFailNullString(jsObj));
  assertEquals(0, wasm.brOnCastFailNullString(strObj));

  assertEquals(0, wasm.brOnCastFailNullAny(null));
  assertEquals(0, wasm.brOnCastFailNullAny(undefined));
  assertEquals(0, wasm.brOnCastFailNullAny(structSuperObj));
  assertEquals(0, wasm.brOnCastFailNullAny(structSubObj));
  assertEquals(0, wasm.brOnCastFailNullAny(arrayObj));
  assertEquals(0, wasm.brOnCastFailNullAny(funcObj));
  assertEquals(0, wasm.brOnCastFailNullAny(1));
  assertEquals(0, wasm.brOnCastFailNullAny(jsObj));
  assertEquals(0, wasm.brOnCastFailNullAny(strObj));

  assertEquals(0, wasm.brOnCastFailNullNone(null));
  assertEquals(1, wasm.brOnCastFailNullNone(undefined));
  assertEquals(1, wasm.brOnCastFailNullNone(structSuperObj));
  assertEquals(1, wasm.brOnCastFailNullNone(structSubObj));
  assertEquals(1, wasm.brOnCastFailNullNone(arrayObj));
  assertEquals(1, wasm.brOnCastFailNullNone(funcObj));
  assertEquals(1, wasm.brOnCastFailNullNone(1));
  assertEquals(1, wasm.brOnCastFailNullNone(jsObj));
  assertEquals(1, wasm.brOnCastFailNullNone(strObj));
})();
