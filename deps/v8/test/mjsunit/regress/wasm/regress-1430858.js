// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-lazy-compilation --wasm-test-streaming

(function test() {
  let bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d,  // wasm magic
    0x01, 0x00, 0x00, 0x00,  // wasm version

    0x01,                    // section kind: Type
    0x09,                    // section length 9
    0x01, 0x60,              // types count 1:  kind: func
    0x00,                    // param count 0
    0x05,                    // return count 5
    0x6f, 0x7e, 0x7d, 0x7e,  // externref i64 f32 i64
    0x70,                    // funcref

    0x03,                    // section kind: Function
    0x02,                    // section length 2
    0x01, 0x00,              // functions count 1: 0 $func0 (result externref) (result i64) (result f32) (result i64) (result funcref)

    0x0a,                    // section kind: Code
    0x17,                    // section length 23
    0x01,                    // functions count 1
                             // function #0 $func0
    0x15,                    // body size 21
    0x00,                    // 0 entries in locals list
    0x00,                    // unreachable
    0x00,                    // unreachable
    0x00,                    // unreachable
    0x00,                    // unreachable
    0x00,                    // unreachable
    0x00,                    // unreachable
    0x00,                    // unreachable
    0xfb, 0x19, 0b11, 0x00, 0x00, 0x00,  // br_on_cast_fail null 0 $type0 $type0
    0x14, 0x10,              // call_ref $type16
    0xfb, 0x00,              // invalid opcode
    0x68,                    // i32.ctz
    0x2b, 0x26,              // f64.load align=64
  ]);
  assertPromiseResult(WebAssembly.compileStreaming(Promise.resolve(bytes)).then(
    assertUnreachable,
    error => assertInstanceof(error, WebAssembly.CompileError)));
})();
