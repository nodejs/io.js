// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Set unittest to false to run this test and just print results, without failing.
let unittest = true;

function run(expected, name, code) {
  let builder = new WasmModuleBuilder();
  // Index 0
  builder.addType(kSig_i_i);
  // Index 1
  builder.addType(kSig_i_ii);
  builder.addFunction("main", kSig_v_v).
    addBody(code);
  let buffer = builder.toBuffer();

  while (name.length < 35) name += " ";

  var valid = WebAssembly.validate(buffer);
  var success = expected == undefined ? "" : (valid == expected ? "(pass)" : "(fail)");
  if (valid) {
    print(name + "|   valid " + success);
  } else {
    print(name + "| invalid " + success);
  }

  if (unittest && expected != undefined) {
    assertTrue(valid === expected);
  }
}

let V = true;
let I = false;
let X = undefined;

let nop = kExprNop;
let iadd = kExprI32Add;
let ieqz = kExprI32Eqz;
let leqz = kExprI64Eqz;
let unr = kExprUnreachable;
let ret = kExprReturn;
let br0 = [kExprBr, 0];
let br1 = [kExprBr, 1];
let brt0 = [kExprBrTable, 0, 0];
let brt1 = [kExprBrTable, 0, 1];
let brt01 = [kExprBrTable, 1, 0, 1];
let f32 = [kExprF32Const, 0, 0, 0, 0];
let zero = [kExprI32Const, 0];
let zero64 = [kExprI64Const, 0];
let if_else_empty = [kExprIf, kWasmVoid, kExprElse, kExprEnd];
let if_unr = [kExprIf, kWasmVoid, kExprUnreachable, kExprEnd];
let if_else_unr = [kExprIf, kWasmVoid, kExprUnreachable, kExprElse, kExprUnreachable, kExprEnd];
let block_unr = [kExprBlock, kWasmVoid, kExprUnreachable, kExprEnd];
let loop_unr = [kExprLoop, kWasmVoid, kExprUnreachable, kExprEnd];
// An i32-typed loop returning a polymorphic stack.
let iloop_poly = [kExprLoop, kWasmI32, kExprUnreachable, kExprI32Const, 0, kExprSelect, kExprEnd];
let block_block_unr = [kExprBlock, kWasmVoid, kExprBlock, kWasmVoid, kExprUnreachable, kExprEnd, kExprEnd];
let block = [kExprBlock, kWasmVoid]
let iblock = [kExprBlock, kWasmI32]
let i_iblock = [kExprBlock, 0]
let i_iiblock = [kExprBlock, 1]
let fblock = [kExprBlock, kWasmF32]
let end = kExprEnd;
let drop = kExprDrop;

run(V, "U", [unr]);
run(V, 'U U', [unr, unr]);
run(V, "(if 0 () else ())", [...zero, ...if_else_empty]);
run(V, "(if 0 U)", [...zero, ...if_unr]);
run(V, "(if 0 U U)", [...zero, ...if_else_unr]);
run(I, "(if 0 U) iadd", [...zero, ...if_unr, iadd]);
run(I, "(if 0 U) iadd drop", [...zero, ...if_unr, iadd, drop]);
run(V, "0 0 (if 0 U) iadd drop", [...zero, ...zero, ...zero, ...if_unr, iadd, drop]);
run(V, "(if 0 U) 0 0 iadd drop", [...zero, ...if_unr, ...zero, ...zero, iadd, drop]);

run(V, "(block U)", [...block_unr]);
run(V, "(loop U)", [...loop_unr]);
run(V, "(if 0 U U)", [...zero, ...if_else_unr]);

run(V, 'U nop', [unr, nop]);
run(V, 'U iadd drop', [unr, iadd, drop]);
run(V, 'br0 iadd drop', [...br0, iadd, drop]);
run(V, '0 brt0 iadd drop', [...zero, ...brt0, iadd, drop]);
run(V, 'ret iadd drop', [ret, iadd, drop]);

run(V, 'U 0 0 iadd drop', [unr, ...zero, ...zero, iadd, drop]);
run(V, 'br0 0 0 iadd drop', [...br0, ...zero, ...zero, iadd, drop]);
run(V, '0 brt0 0 0 iadd drop', [...zero, ...brt0, ...zero, ...zero, iadd, drop]);
run(V, 'ret 0 0 iadd drop', [ret, ...zero, ...zero, iadd, drop]);

run(I, 'br0 iadd', [...br0, iadd]);
run(I, '0 brt0 iadd', [...zero, ...brt0, iadd]);
run(I, 'ret iadd', [ret, iadd]);
run(I, '0 0 br0 iadd', [...zero, ...zero, ...br0, iadd]);
run(I, '0 0 ret iadd', [...zero, ...zero, ret, iadd]);

run(I, '(block U) iadd drop', [...block_unr, iadd, drop]);
run(I, '(block (block U)) iadd drop', [...block_block_unr, iadd, drop]);
run(I, '(loop U) iadd drop', [...loop_unr, iadd]);
run(V, '(iloop (iloop U 0 select)) drop', [kExprLoop, kWasmI32, ...iloop_poly, kExprEnd, drop]);
run(I, '(if 0 U U) iadd drop', [...zero, ...if_else_unr, iadd, drop]);
run(I, 'U (i_iblock leqz)', [unr, ...i_iblock, leqz, end, drop]);
run(V, 'U (i_iblock ieqz)', [unr, ...i_iblock, ieqz, end, drop]);
run(I, 'U (iblock iadd)', [unr, ...iblock, iadd, end, drop]);
run(I, 'U zero64 (i_iiblock iadd) drop', [unr, ...zero64, ...i_iiblock, iadd, end, drop])

run(V, 'U 0 0 iadd drop', [unr, ...zero, ...zero, iadd, drop]);
run(V, "(block U) 0 0 iadd drop", [...block_unr, ...zero, ...zero, iadd, drop]);
run(V, "(loop U) 0 0 iadd drop", [...loop_unr, ...zero, ...zero, iadd, drop]);
run(V, "(block (block U)) 0 0 iadd drop", [...block_block_unr, ...zero, ...zero, iadd, drop]);
run(V, '0 0 U iadd drop', [...zero, ...zero, unr, iadd, drop]);
run(V, "0 0 (block U) iadd drop", [...zero, ...zero, ...block_unr, iadd, drop]);
run(V, "0 0 (loop U) iadd drop", [...zero, ...zero, ...loop_unr, iadd, drop]);
run(V, "0 0 (block (block U)) iadd drop", [...zero, ...zero, ...block_block_unr, iadd, drop]);

run(I, "U 0f iadd drop", [unr, ...f32, iadd, drop]);
run(I, "U 0f 0 iadd drop", [unr, ...f32, ...zero, iadd, drop]);
run(I, "U 0 0f iadd drop", [unr, ...zero, ...f32, iadd, drop]);
run(I, "(if 0 U U) 0f 0 iadd drop", [...zero, ...if_else_unr, ...f32, ...zero, iadd, drop]);
run(I, "(block U) 0f 0 iadd drop", [...block_unr, ...f32, ...zero, iadd, drop]);
run(I, "(loop U) 0f 0 iadd drop", [...loop_unr, ...f32, ...zero, iadd, drop]);
run(I, "(block (block U)) 0f 0 iadd drop", [...block_block_unr, ...f32, ...zero, iadd, drop]);

run(V, '0f U iadd drop', [...f32, unr, iadd, drop]);
run(V, '0f 0 U iadd drop', [...f32, ...zero, unr, iadd, drop]);
run(I, "0f 0 (block U) iadd drop", [...f32, ...zero, ...block_unr, iadd, drop]);
run(V, '0f U 0 iadd drop', [...f32, unr, ...zero, iadd, drop]);
run(I, "0 U 0f iadd drop", [...zero, unr, ...zero, ...f32, iadd, drop]);
run(I, "0f (block U) 0 iadd drop", [...f32, ...block_unr, ...zero, iadd, drop]);
run(I, "0 (block U) 0f iadd drop", [...zero, ...block_unr, ...f32, iadd, drop]);

run(I, "(iblock 0 (block br1)) drop", [...iblock, ...zero, ...block, ...br1, end, end, drop]);
run(I, "(iblock 0 (block 0 brt1)) drop", [...iblock, ...zero, ...block, ...zero, ...brt1, end, end, drop]);
run(I, "(block (iblock 0 0 brt01) drop)", [...block, ...iblock, ...zero, ...zero, ...brt01, end, drop, end]);
run(I, "U (iblock 0 (block br1)) drop", [unr, ...iblock, ...zero, ...block, ...br1, end, end, drop]);
run(I, "U (iblock 0 (block 0 brt1)) drop", [unr, ...iblock, ...zero, ...block, ...zero, ...brt1, end, end, drop]);
run(I, "U (block (iblock 0 0 brt01) drop)", [unr, ...block, ...iblock, ...zero, ...zero, ...brt01, end, drop, end]);
run(V, "(iblock (iblock U 0 brt01)) drop", [...iblock, ...iblock, unr, ...zero, ...brt01, end, end, drop]);
run(I, "(block (fblock U 0 brt01) drop)", [...block, ...fblock, unr, ...zero, ...brt01, end, drop, end]);
run(V, "(iblock (fblock U 0 brt01) drop 0) drop", [...iblock, ...fblock, unr, ...zero, ...brt01, end, drop, ...zero, end, drop]);

run(I, "(iblock (block (U brif 1))", [...iblock, ...block, unr, kExprBrIf, 0, end, end, kExprDrop]);
