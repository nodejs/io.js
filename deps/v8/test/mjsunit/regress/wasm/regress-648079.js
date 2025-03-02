// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Non-standard opcodes.
let kSig_s_v = makeSig([], [kWasmS128]);

(function() {
"use asm";
var builder = new WasmModuleBuilder();
builder.addFunction("regression_648079", kSig_s_v)
  .addBody([
    // locals:
    0x00,
    // body:
    kExprI64RemU,
    kExprI64Ctz,
    kExprI64LeU,
    kExprUnreachable,
    kExprUnreachable,
    kExprUnreachable,
    kExprUnreachable,
    kExprI64Ctz,
    kExprI64Ne,
    kExprI64ShrS,
    kExprI64GtS,
    kExprI64RemU,
    kExprUnreachable,
    kExprI64RemU,
    kExprI32Eqz,
    kExprI64LeU,
    kExprDrop,
    kExprF32Add,
    kExprI64Ior,
    kExprF32CopySign,
    kExprI64Ne,
    kExprI64GeS,
    kExprUnreachable,
    kExprF32Trunc,
    kExprF32Trunc,
    kExprUnreachable,
    kExprIf, 10,   // @32
      kExprBlock, 00,   // @34
        kExprBr,   // depth=109
        kExprI64Shl,
        kExprI64LeU,
        kExprI64GeS,
        kExprI64Clz,
        kExprF32Min,
        kExprF32Eq,
        kExprF32Trunc,
        kExprF32Trunc,
        kExprF32Trunc,
        kExprUnreachable,
        kExprI32Const,
        kExprUnreachable,
        kExprBr,   // depth=101
        kExprF32Div,
        kExprI64GtU,
        kExprI64GeS,
        kExprI64Clz,
        kExprSelect,
        kExprI64GtS,
        kExprI64RemU,
        kExprI64LeU,
        kExprI64Shl,
        kExprI64Ctz,
        kExprLoop, 01,   // @63 i32
        kExprElse,   // @65
          kExprI64LeU,
          kExprI64RemU,
          kExprI64Ne,
          kExprI64GeS,
          kExprI32Const,
          kExprI64GtS,
          kExprI64LoadMem32U,
          kExprI64Clz,
          kExprI64Shl,
          kExprI64Ne,
          kExprI64ShrS,
          kExprI64GtS,
          kExprI64DivU,
          kExprI64Ne,
          kExprI64GtS,
          kExprI64Ne,
          kExprI64Popcnt,
          kExprI64DivU,
          kExprI64DivU,
          kExprSelect,
          kExprI64Ctz,
          kExprI64Popcnt,
          kExprI64RemU,
          kExprI64Clz,
          kExprF64Sub,
          kExprF32Trunc,
          kExprF32Trunc,
          kExprI64RemU,
          kExprI64Ctz,
          kExprI64LeU,
          kExprUnreachable,
          kExprUnreachable,
          kExprUnreachable,
          kExprBrIf,   // depth=116
          kExprF32Min,
          kExprI64GtU,
          kExprBlock, 01,   // @107 i32
            kExprLocalTee,
            kExprBlock, 01,   // @111 i32
              kExprBlock, 01,   // @113 i32
                kExprBlock, 01,   // @115 i32
                  kExprBlock, 01,   // @117 i32
                    kExprBlock, 01,   // @119 i32
                      kExprBlock, 01,   // @121 i32
                        kExprBlock, 01,   // @123 i32
                          kExprBlock, 88,   // @125
                            kExprF32Trunc,
                            kExprF32Trunc,
                            kExprF32Trunc,
                            kExprUnreachable,
                            kExprLoop, 40,   // @131
                              kExprUnreachable,
                              kExprUnreachable,
                              kExprI32Add,
                              kExprBlock, 05,   // @136
                                kExprUnreachable,
                                kExprIf, 02,   // @139 i64
                                  kExprBlock, 01,   // @141 i32
                                    kExprBrIf,   // depth=16
                                    kExprLoop, 00,   // @145
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprReturn,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprI64LoadMem16U,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprNop,
                                      kExprBr,   // depth=1
                                    kExprElse,   // @164
                                      kExprF32Trunc,
                                      kExprI32Add,
                                      kExprCallIndirect,   // sig #1
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprUnreachable,
                                      kExprBlock, 00,   // @172
                                        kExprI64RemU,
                                        kExprI64Ctz,
                                        kExprI64LeU,
                                        kExprUnreachable,
                                        kExprUnreachable,
                                        kExprUnreachable,
                                        kExprUnreachable,
                                        kExprUnreachable,
                                        kExprDrop,
                                        kExprI64Popcnt,
                                        kExprF32Min,
                                        kExprUnreachable,
                                        kExprF64Sub,
                                        kExprI32Const,
                                        kExprUnreachable,
                                        kExprLocalGet,
                                        kExprI64LoadMem32U,
                                        kExprUnreachable,
                                        kExprI64RemU,
                                        kExprI32Eqz,
                                        kExprI64LeU,
                                        kExprDrop,
                                        kExprF32Add,
                                        kExprI64Ior,
                                        kExprF32CopySign,
                                        kExprI64Ne,
                                        kExprI64GeS,
                                        kExprUnreachable,
                                        kExprF32Trunc,
                                        kExprF32Trunc,
                                        kExprUnreachable,
                                        kExprIf, 10,   // @216
                                          kExprBlock, 00,   // @218
                                            kExprBr,   // depth=109
                                            kExprI64Shl,
                                            kExprI64LeU,
                                            kExprI64GeS,
                                            kExprI64Clz,
                                            kExprF32Min,
                                            kExprF32Eq,
                                            kExprF32Trunc,
                                            kExprF32Trunc,
                                            kExprF32Trunc,
                                            kExprUnreachable,
                                            kExprF64Min,
                                            kExprI32Const,
                                            kExprBr,   // depth=101
                                            kExprF32Div,
                                            kExprI64GtU,
                                            kExprI64GeS,
                                            kExprI64Clz,
                                            kExprI64Popcnt,
                                            kExprF64Lt,
                                            kExprF32Trunc,
                                            kExprF32Trunc,
                                            kExprF32Trunc,
                                            kExprUnreachable,
                                            kExprLoop, 01,   // @247 i32
                                            kExprElse,   // @249
                                              kExprI64LeU,
                                              kExprI64RemU,
                                              kExprI64Ne,
                                              kExprI64GeS,
                                              kExprI32Const,
                                              kExprBlock, 01,   // @256 i32
                                                kExprBlock, 01,   // @258 i32
                                                  kExprBlock, 01,   // @260 i32
                                                    kExprBlock, 01,   // @262 i32
                                                      kExprBlock, 01,   // @264 i32
                                                        kExprF32Ge,
                                                        kExprF32Trunc,
                                                        kExprF32Trunc,
                                                        kExprF32Trunc,
                                                        kExprUnreachable,
                                                        kExprLoop, 40,   // @271
                                                          kExprUnreachable,
                                                          kExprUnreachable,
                                                          kExprI32Add,
                                                          kExprBlock, 01,   // @276 i32
                                                            kExprUnreachable,
                                                            kExprIf, 02,   // @279 i64
                                                              kExprBlock, 00,   // @281
                                                                kExprBrIf,   // depth=16
                                                                kExprLoop, 00,   // @285
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprReturn,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprI64LoadMem16U,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprNop,
                                                                  kExprBr,   // depth=1
                                                                kExprElse,   // @304
                                                                  kExprF32Trunc,
                                                                  kExprI32Add,
                                                                  kExprCallIndirect,   // sig #1
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprUnreachable,
                                                                  kExprBlock, 00,   // @312
                                                                    kExprI64RemU,
                                                                    kExprI64Ctz,
                                                                    kExprI64LeU,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprDrop,
                                                                    kExprI64Popcnt,
                                                                    kExprF32Min,
                                                                    kExprUnreachable,
                                                                    kExprF64Sub,
                                                                    kExprI32Const,
                                                                    kExprUnreachable,
                                                                    kExprLocalGet,
                                                                    kExprI64LoadMem32U,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprNop,
                                                                    kExprBr,   // depth=1
                                                                  kExprElse,   // @348
                                                                    kExprF32Trunc,
                                                                    kExprI32Add,
                                                                    kExprCallIndirect,   // sig #1
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprBlock, 00,   // @356
                                                                    kExprI64RemU,
                                                                    kExprI64Ctz,
                                                                    kExprI64LeU,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprUnreachable,
                                                                    kExprDrop,
                                                                    kExprI64Popcnt,
                                                                    kExprF32Min,
                                                                    kExprUnreachable,
                                                                    kExprF64Sub,
                                                                    kExprI32Const,
                                                                    kExprUnreachable,
                                                                    kExprLocalGet,
                                                                    kExprI64LoadMem32U,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF32Trunc,
                                                                    kExprF32Trunc,
                                                                    kExprF32Trunc,
                                                                    kExprUnreachable,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
                                                                    kExprF64Min,
            ])
            .exportFunc();
assertThrows(function() { builder.instantiate(); });
})();
