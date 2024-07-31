// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref --expose-externalize-string

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kSig_w_v = makeSig([], [kWasmStringRef]);
let kSig_w_ii = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);

function encodeWtf8(str) {
  // String iterator coalesces surrogate pairs.
  let out = [];
  for (let codepoint of str) {
    codepoint = codepoint.codePointAt(0);
    if (codepoint <= 0x7f) {
      out.push(codepoint);
    } else if (codepoint <= 0x7ff) {
      out.push(0xc0 | (codepoint >> 6));
      out.push(0x80 | (codepoint & 0x3f));
    } else if (codepoint <= 0xffff) {
      out.push(0xe0 | (codepoint >> 12));
      out.push(0x80 | ((codepoint >> 6) & 0x3f));
      out.push(0x80 | (codepoint & 0x3f));
    } else if (codepoint <= 0x10ffff) {
      out.push(0xf0 | (codepoint >> 18));
      out.push(0x80 | ((codepoint >> 12) & 0x3f));
      out.push(0x80 | ((codepoint >> 6) & 0x3f));
      out.push(0x80 | (codepoint & 0x3f));
    } else {
      throw new Error("bad codepoint " + codepoint);
    }
  }
  return out;
}

let externalString = "I'm an external string";
externalizeString(externalString);
let interestingStrings = [
  '',
  'ascii',
  'latin\xa91',        // Latin-1.
  '2 \ucccc b',        // Two-byte.
  'a \ud800\udc00 b',  // Proper surrogate pair.
  'a \ud800 b',        // Lone lead surrogate.
  'a \udc00 b',        // Lone trail surrogate.
  '\ud800 bc',         // Lone lead surrogate at the start.
  '\udc00 bc',         // Lone trail surrogate at the start.
  'ab \ud800',         // Lone lead surrogate at the end.
  'ab \udc00',         // Lone trail surrogate at the end.
  'a \udc00\ud800 b',  // Swapped surrogate pair.
  externalString,      // External string.
];

function IsSurrogate(codepoint) {
  return 0xD800 <= codepoint && codepoint <= 0xDFFF
}
function HasIsolatedSurrogate(str) {
  for (let codepoint of str) {
    let value = codepoint.codePointAt(0);
    if (IsSurrogate(value)) return true;
  }
  return false;
}
function ReplaceIsolatedSurrogates(str, replacement='\ufffd') {
  let replaced = '';
  for (let codepoint of str) {
    replaced +=
      IsSurrogate(codepoint.codePointAt(0)) ? replacement : codepoint;
  }
  return replaced;
}

function makeWtf8TestDataSegment() {
  let data = []
  let valid = {};
  let invalid = {};

  for (let str of interestingStrings) {
    let bytes = encodeWtf8(str);
    valid[str] = { offset: data.length, length: bytes.length };
    for (let byte of bytes) {
      data.push(byte);
    }
  }
  for (let bytes of ['trailing high byte \xa9',
                     'interstitial high \xa9 byte',
                     'invalid \xc0 byte',
                     'invalid three-byte \xed\xd0\x80',
                     'surrogate \xed\xa0\x80\xed\xb0\x80 pair']) {
    invalid[bytes] = { offset: data.length, length: bytes.length };
    for (let i = 0; i < bytes.length; i++) {
      data.push(bytes.charCodeAt(i));
    }
  }

  return { valid, invalid, data: Uint8Array.from(data) };
};

(function TestStringNewWtf8Array() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let data = makeWtf8TestDataSegment();
  let data_index = builder.addPassiveDataSegment(data.data);

  let ascii_data_index =
      builder.addPassiveDataSegment(Uint8Array.from(encodeWtf8("ascii")));

  let i8_array = builder.addArray(kWasmI8, true);

  let make_i8_array = builder.addFunction(
      "make_i8_array", makeSig([], [wasmRefType(i8_array)]))
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const(data.data.length),
      kGCPrefix, kExprArrayNewData, i8_array, data_index
    ]).index;

  for (let [instr, name] of
       [[kExprStringNewWtf8Array, "new_wtf8"],
        [kExprStringNewUtf8Array, "new_utf8"],
        [kExprStringNewUtf8ArrayTry, "new_utf8_try"],
        [kExprStringNewLossyUtf8Array, "new_utf8_sloppy"]]) {
    builder.addFunction(name, kSig_w_ii)
      .exportFunc()
      .addBody([
        kExprCallFunction, make_i8_array,
        kExprLocalGet, 0, kExprLocalGet, 1,
        ...GCInstr(instr)
      ]);
  }

  builder.addFunction("bounds_check", kSig_w_ii)
    .exportFunc()
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const("ascii".length),
      kGCPrefix, kExprArrayNewData, i8_array, ascii_data_index,
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewWtf8Array)
    ]);

  builder.addFunction("null_array", kSig_w_v).exportFunc()
    .addBody([
      kExprRefNull, i8_array,
      kExprI32Const, 0, kExprI32Const, 0,
      ...GCInstr(kExprStringNewWtf8Array)
    ])

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    let start = offset;
    let end = offset + length;
    assertEquals(str, instance.exports.new_wtf8(start, end));
    if (HasIsolatedSurrogate(str)) {
      assertThrows(() => instance.exports.new_utf8(start, end),
                   WebAssembly.RuntimeError, "invalid UTF-8 string");
      assertNull(instance.exports.new_utf8_try(start, end));

      // Isolated surrogates have the three-byte pattern ED [A0,BF]
      // [80,BF].  When the sloppy decoder gets to the second byte, it
      // will reject the sequence, and then retry parsing at the second
      // byte.  Seeing the second byte can't start a sequence, it
      // replaces the second byte and continues with the next, which
      // also can't start a sequence.  The result is that one isolated
      // surrogate is replaced by three U+FFFD codepoints.
      assertEquals(ReplaceIsolatedSurrogates(str, '\ufffd\ufffd\ufffd'),
                   instance.exports.new_utf8_sloppy(start, end));
    } else {
      assertEquals(str, instance.exports.new_utf8(start, end));
      assertEquals(str, instance.exports.new_utf8_sloppy(start, end));
      assertEquals(str, instance.exports.new_utf8_try(start, end));
    }
  }
  for (let [str, {offset, length}] of Object.entries(data.invalid)) {
    let start = offset;
    let end = offset + length;
    assertThrows(() => instance.exports.new_wtf8(start, end),
                 WebAssembly.RuntimeError, "invalid WTF-8 string");
    assertThrows(() => instance.exports.new_utf8(start, end),
                 WebAssembly.RuntimeError, "invalid UTF-8 string");
    assertNull(instance.exports.new_utf8_try(start, end));
  }

  assertEquals("ascii", instance.exports.bounds_check(0, "ascii".length));
  assertEquals("", instance.exports.bounds_check("ascii".length,
                                                 "ascii".length));
  assertEquals("i", instance.exports.bounds_check("ascii".length - 1,
                                                  "ascii".length));
  assertThrows(() => instance.exports.bounds_check(0, 100),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.bounds_check(0, -1),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.bounds_check(-1, 0),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.bounds_check("ascii".length,
                                                   "ascii".length + 1),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringNewUtf8ArrayTryNullCheck() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let data = makeWtf8TestDataSegment();
  let data_index = builder.addPassiveDataSegment(data.data);
  let i8_array = builder.addArray(kWasmI8, true);

  let make_i8_array = builder.addFunction(
      "make_i8_array", makeSig([], [wasmRefType(i8_array)]))
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const(data.data.length),
      kGCPrefix, kExprArrayNewData, i8_array, data_index
    ]).index;

  builder.addFunction("is_null_new_utf8_try", kSig_i_ii)
    .exportFunc()
    .addBody([
      kExprCallFunction, make_i8_array,
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewUtf8ArrayTry),
      kExprRefIsNull,
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    assertEquals(
        +HasIsolatedSurrogate(str),
        instance.exports.is_null_new_utf8_try(offset, offset+length));
  }
  for (let [str, {offset, length}] of Object.entries(data.invalid)) {
    assertEquals(1, instance.exports.is_null_new_utf8_try(offset, offset+length));
  }
})();

function encodeWtf16LE(str) {
  // String iterator coalesces surrogate pairs.
  let out = [];
  for (let i = 0; i < str.length; i++) {
    codeunit = str.charCodeAt(i);
    out.push(codeunit & 0xff)
    out.push(codeunit >> 8);
  }
  return out;
}

function makeWtf16TestDataSegment(strings) {
  let data = []
  let valid = {};

  for (let str of strings) {
    valid[str] = { offset: data.length, length: str.length };
    for (let byte of encodeWtf16LE(str)) {
      data.push(byte);
    }
  }

  return { valid, data: Uint8Array.from(data) };
};

(function TestStringNewWtf16Array() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // string.new_wtf16_array switches to a different implementation (runtime
  // instead of Torque) for more than 32 characters, so provide some coverage
  // for that case.
  let strings = interestingStrings.concat([
    "String with more than 32 characters, all of which are ASCII",
    "Two-byte string with more than 32 characters \ucccc \ud800\udc00 \xa9?"
  ]);

  let data = makeWtf16TestDataSegment(strings);
  let data_index = builder.addPassiveDataSegment(data.data);
  let ascii_data_index =
      builder.addPassiveDataSegment(Uint8Array.from(encodeWtf16LE("ascii")));

  let i16_array = builder.addArray(kWasmI16, true);

  let make_i16_array = builder.addFunction(
      "make_i16_array", makeSig([], [wasmRefType(i16_array)]))
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const(data.data.length / 2),
      kGCPrefix, kExprArrayNewData, i16_array, data_index
    ]).index;

  builder.addFunction("new_wtf16", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprCallFunction, make_i16_array,
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewWtf16Array)
    ]);

  builder.addFunction("bounds_check", kSig_w_ii)
    .exportFunc()
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const("ascii".length),
      kGCPrefix, kExprArrayNewData, i16_array, ascii_data_index,
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewWtf16Array)
    ]);

  builder.addFunction("null_array", kSig_w_v).exportFunc()
    .addBody([
      kExprRefNull, i16_array,
      kExprI32Const, 0,
      kExprI32Const, 0,
      ...GCInstr(kExprStringNewWtf16Array)
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    let start = offset / 2;
    let end = start + length;
    assertEquals(str, instance.exports.new_wtf16(start, end));
  }

  assertEquals("ascii", instance.exports.bounds_check(0, "ascii".length));
  assertEquals("", instance.exports.bounds_check("ascii".length,
                                                 "ascii".length));
  assertEquals("i", instance.exports.bounds_check("ascii".length - 1,
                                                  "ascii".length));
  assertThrows(() => instance.exports.bounds_check(0, 100),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.bounds_check("ascii".length,
                                                   "ascii".length + 1),
               WebAssembly.RuntimeError, "array element access out of bounds");
  assertThrows(() => instance.exports.null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringEncodeWtf8Array() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let i8_array = builder.addArray(kWasmI8, true);

  let kSig_w_wii =
      makeSig([kWasmStringRef, kWasmI32, kWasmI32],
              [kWasmStringRef]);
  for (let [instr, name] of [[kExprStringEncodeUtf8Array, "utf8"],
                             [kExprStringEncodeWtf8Array, "wtf8"],
                             [kExprStringEncodeLossyUtf8Array, "replace"]]) {
    // Allocate an array that's exactly the expected size, and encode
    // into it.  Then decode it.
    // (str, length, offset=0) -> str
    builder.addFunction("encode_" + name, kSig_w_wii)
      .exportFunc()
      .addLocals(wasmRefNullType(i8_array), 1)
      .addLocals(kWasmI32, 1)
      .addBody([
        // Allocate buffer.
        kExprLocalGet, 1,
        kGCPrefix, kExprArrayNewDefault, i8_array,
        kExprLocalSet, 3,

        // Write buffer, store number of bytes written.
        kExprLocalGet, 0,
        kExprLocalGet, 3,
        kExprLocalGet, 2,
        ...GCInstr(instr),
        kExprLocalSet, 4,

        // Read buffer.
        kExprLocalGet, 3,
        kExprLocalGet, 2,
        kExprLocalGet, 2, kExprLocalGet, 4, kExprI32Add,
        ...GCInstr(kExprStringNewWtf8Array)
      ]);
  }

  builder.addFunction("encode_null_string", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kStringRefCode,
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, i8_array,
        kExprI32Const, 0,
        ...GCInstr(kExprStringEncodeWtf8Array)
      ]);
  builder.addFunction("encode_null_array", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, i8_array,
        kExprI32Const, 0, kExprI32Const, 0,
        ...GCInstr(kExprStringNewWtf8Array),
        kExprRefNull, i8_array,
        kExprI32Const, 0,
        ...GCInstr(kExprStringEncodeWtf8Array)
      ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    assertEquals(str, instance.exports.encode_wtf8(str, wtf8.length, 0));
    assertEquals(str, instance.exports.encode_wtf8(str, wtf8.length + 20,
                                                   10));
  }

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    if (HasIsolatedSurrogate(str)) {
      assertThrows(() => instance.exports.encode_utf8(str, wtf8.length, 0),
          WebAssembly.RuntimeError,
          "Failed to encode string as UTF-8: contains unpaired surrogate");
    } else {
      assertEquals(str, instance.exports.encode_utf8(str, wtf8.length, 0));
      assertEquals(str,
                   instance.exports.encode_wtf8(str, wtf8.length + 20, 10));
    }
  }

  for (let str of interestingStrings) {
    let offset = 42;
    let replaced = ReplaceIsolatedSurrogates(str);
    if (!HasIsolatedSurrogate(str)) assertEquals(str, replaced);
    let wtf8 = encodeWtf8(replaced);
    assertEquals(replaced,
                 instance.exports.encode_replace(str, wtf8.length, 0));
    assertEquals(replaced,
                 instance.exports.encode_replace(str, wtf8.length + 20, 10));
  }

  assertThrows(() => instance.exports.encode_null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.encode_null_string(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    let message = "array element access out of bounds";
    assertThrows(() => instance.exports.encode_wtf8(str, wtf8.length, 1),
                 WebAssembly.RuntimeError, message);
    assertThrows(() => instance.exports.encode_utf8(str, wtf8.length, 1),
                 WebAssembly.RuntimeError, message);
    assertThrows(() => instance.exports.encode_replace(str, wtf8.length, 1),
                 WebAssembly.RuntimeError, message);
  }
})();

(function TestStringEncodeWtf16Array() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let i16_array = builder.addArray(kWasmI16, true);

  let kSig_w_wii =
      makeSig([kWasmStringRef, kWasmI32, kWasmI32],
              [kWasmStringRef]);
  // Allocate an array and encode into it.  Then decode it.
  // (str, length, offset) -> str
  builder.addFunction("encode", kSig_w_wii)
    .exportFunc()
    .addLocals(wasmRefNullType(i16_array), 1)
    .addLocals(kWasmI32, 1)
    .addBody([
      // Allocate buffer.
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayNewDefault, i16_array,
      kExprLocalSet, 3,

      // Write buffer, store number of code units written.
      kExprLocalGet, 0,
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      ...GCInstr(kExprStringEncodeWtf16Array),
      kExprLocalSet, 4,

      // Read buffer.
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      kExprLocalGet, 2, kExprLocalGet, 4, kExprI32Add,
      ...GCInstr(kExprStringNewWtf16Array),
    ]);

  builder.addFunction("encode_null_string", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kStringRefCode,
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, i16_array,
        kExprI32Const, 0,
        ...GCInstr(kExprStringEncodeWtf16Array)
      ]);
  builder.addFunction("encode_null_array", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, i16_array,
        kExprI32Const, 0, kExprI32Const, 0,
        ...GCInstr(kExprStringNewWtf16Array),
        kExprRefNull, i16_array,
        kExprI32Const, 0,
        ...GCInstr(kExprStringEncodeWtf16Array)
      ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    assertEquals(str, instance.exports.encode(str, str.length, 0));
    assertEquals(str, instance.exports.encode(str, str.length + 20, 10));
  }

  assertThrows(() => instance.exports.encode_null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.encode_null_string(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");

  for (let str of interestingStrings) {
    let message = "array element access out of bounds";
    assertThrows(() => instance.exports.encode(str, str.length, 1),
                 WebAssembly.RuntimeError, message);
  }
})();
