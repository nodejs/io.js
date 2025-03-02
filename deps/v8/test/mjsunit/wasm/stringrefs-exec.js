// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging
// For {isOneByteString}:
// Flags: --expose-externalize-string

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kSig_w_ii = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);
let kSig_w_v = makeSig([], [kWasmStringRef]);
let kSig_i_w = makeSig([kWasmStringRef], [kWasmI32]);
let kSig_i_wi = makeSig([kWasmStringRef, kWasmI32], [kWasmI32]);
let kSig_i_wii = makeSig([kWasmStringRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_i_ww = makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]);
let kSig_i_wiii = makeSig([kWasmStringRef, kWasmI32, kWasmI32, kWasmI32],
                          [kWasmI32]);
let kSig_ii_wiii = makeSig([kWasmStringRef, kWasmI32, kWasmI32, kWasmI32],
                           [kWasmI32, kWasmI32]);
let kSig_v_w = makeSig([kWasmStringRef], []);
let kSig_w_i = makeSig([kWasmI32], [kWasmStringRef]);
let kSig_w_wii = makeSig([kWasmStringRef, kWasmI32, kWasmI32],
                         [kWasmStringRef]);
let kSig_w_ww = makeSig([kWasmStringRef, kWasmStringRef], [kWasmStringRef]);
let kSig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);

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

// Compute the string that corresponds to the valid WTF-8 bytes from
// start (inclusive) to end (exclusive).
function decodeWtf8(wtf8, start, end) {
  let result = ''
  while (start < end) {
    let cp;
    let b0 = wtf8[start];
    if ((b0 & 0xC0) == 0x80) {
      // The precondition is that we have valid WTF-8 bytes and that
      // start and end are codepoint boundaries.  Here we make a weak
      // assertion about that invariant, that we don't start decoding
      // with a continuation byte.
      throw new Error('invalid wtf8');
    }
    if (b0 <= 0x7F) {
      cp = b0;
      start += 1;
    } else if (b0 <= 0xDF) {
      cp = (b0 & 0x1f) << 6;
      cp |= (wtf8[start + 1] & 0x3f);
      start += 2;
    } else if (b0 <= 0xEF) {
      cp = (b0 & 0x0f) << 12;
      cp |= (wtf8[start + 1] & 0x3f) << 6;
      cp |= (wtf8[start + 2] & 0x3f);
      start += 3;
    } else {
      cp = (b0 & 0x07) << 18;
      cp |= (wtf8[start + 1] & 0x3f) << 12;
      cp |= (wtf8[start + 2] & 0x3f) << 6;
      cp |= (wtf8[start + 3] & 0x3f);
      start += 4;
    }
    result += String.fromCodePoint(cp);
  }
  assertEquals(start, end);
  return result;
}

// We iterate over every one of these strings and every substring of it,
// so to keep test execution times fast on slow platforms, keep both this
// list and the individual strings reasonably short.
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

(function TestStringNewWtf8() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  let data = makeWtf8TestDataSegment();
  builder.addActiveDataSegment(0, [kExprI32Const, 0], data.data);

  builder.addFunction("string_new_utf8", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewUtf8), 0
    ]);

  builder.addFunction("string_new_utf8_try", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewUtf8Try), 0
    ]);

  builder.addFunction("string_new_wtf8", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewWtf8), 0
    ]);

  builder.addFunction("string_new_utf8_sloppy", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewLossyUtf8), 0
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    assertEquals(str, instance.exports.string_new_wtf8(offset, length));
    if (HasIsolatedSurrogate(str)) {
      assertThrows(() => instance.exports.string_new_utf8(offset, length),
                   WebAssembly.RuntimeError, "invalid UTF-8 string");
      assertEquals(null, instance.exports.string_new_utf8_try(offset, length));

      // Isolated surrogates have the three-byte pattern ED [A0,BF]
      // [80,BF].  When the sloppy decoder gets to the second byte, it
      // will reject the sequence, and then retry parsing at the second
      // byte.  Seeing the second byte can't start a sequence, it
      // replaces the second byte and continues with the next, which
      // also can't start a sequence.  The result is that one isolated
      // surrogate is replaced by three U+FFFD codepoints.
      assertEquals(ReplaceIsolatedSurrogates(str, '\ufffd\ufffd\ufffd'),
                   instance.exports.string_new_utf8_sloppy(offset, length));
    } else {
      assertEquals(str, instance.exports.string_new_utf8(offset, length));
      assertEquals(str, instance.exports.string_new_utf8_try(offset, length));
      assertEquals(str,
                   instance.exports.string_new_utf8_sloppy(offset, length));
    }
  }
  for (let [str, {offset, length}] of Object.entries(data.invalid)) {
    assertThrows(() => instance.exports.string_new_wtf8(offset, length),
                 WebAssembly.RuntimeError, "invalid WTF-8 string");
    assertThrows(() => instance.exports.string_new_utf8(offset, length),
                 WebAssembly.RuntimeError, "invalid UTF-8 string");
    assertEquals(null, instance.exports.string_new_utf8_try(offset, length));
  }
})();

(function TestStringNewUtf8TryNullCheck() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  let data = makeWtf8TestDataSegment();
  builder.addActiveDataSegment(0, [kExprI32Const, 0], data.data);

  builder.addFunction("is_null_new_utf8_try", kSig_i_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewUtf8Try), 0,
      kExprRefIsNull,
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    print(offset, length);
    assertEquals(
        +HasIsolatedSurrogate(str),
        instance.exports.is_null_new_utf8_try(offset, length));
  }
  for (let [str, {offset, length}] of Object.entries(data.invalid)) {
    assertEquals(1, instance.exports.is_null_new_utf8_try(offset, length));
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

function makeWtf16TestDataSegment() {
  let data = []
  let valid = {};

  for (let str of interestingStrings) {
    valid[str] = { offset: data.length, length: str.length };
    for (let byte of encodeWtf16LE(str)) {
      data.push(byte);
    }
  }

  return { valid, data: Uint8Array.from(data) };
};

(function TestStringNewWtf16() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  let data = makeWtf16TestDataSegment();
  builder.addActiveDataSegment(0, [kExprI32Const, 0], data.data);

  builder.addFunction("string_new_wtf16", kSig_w_ii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      ...GCInstr(kExprStringNewWtf16), 0
    ]);

  let instance = builder.instantiate();
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    assertEquals(str, instance.exports.string_new_wtf16(offset, length));
  }
})();

(function TestStringConst() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  for (let [index, str] of interestingStrings.entries()) {
    builder.addLiteralStringRef(encodeWtf8(str));

    builder.addFunction("string_const" + index, kSig_w_v)
      .exportFunc()
      .addBody([...GCInstr(kExprStringConst), index]);

    builder.addGlobal(kWasmStringRef, false, false,
                      [...GCInstr(kExprStringConst), index])
      .exportAs("global" + index);
  }

  let instance = builder.instantiate();
  for (let [index, str] of interestingStrings.entries()) {
    assertEquals(str, instance.exports["string_const" + index]());
    assertEquals(str, instance.exports["global" + index].value);
  }
})();

(function TestStringMeasureUtf8AndWtf8() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("string_measure_utf8", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringMeasureUtf8)
    ]);

  builder.addFunction("string_measure_wtf8", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringMeasureWtf8)
    ]);

  builder.addFunction("string_measure_utf8_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringMeasureUtf8)
    ]);

  builder.addFunction("string_measure_wtf8_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringMeasureWtf8)
    ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    assertEquals(wtf8.length, instance.exports.string_measure_wtf8(str));
    if (HasIsolatedSurrogate(str)) {
      assertEquals(-1, instance.exports.string_measure_utf8(str));
    } else {
      assertEquals(wtf8.length, instance.exports.string_measure_utf8(str));
    }
  }

  assertThrows(() => instance.exports.string_measure_utf8_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.string_measure_wtf8_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringMeasureWtf16() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("string_measure_wtf16", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringMeasureWtf16)
    ]);

  builder.addFunction("string_measure_wtf16_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringMeasureWtf16)
    ]);

  let instance = builder.instantiate();
  for (let str of interestingStrings) {
    assertEquals(str.length, instance.exports.string_measure_wtf16(str));
  }

  assertThrows(() => instance.exports.string_measure_wtf16_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringEncodeWtf8() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  builder.exportMemoryAs("memory");

  for (let [instr, name] of [[kExprStringEncodeUtf8, "utf8"],
                             [kExprStringEncodeWtf8, "wtf8"],
                             [kExprStringEncodeLossyUtf8, "replace"]]) {
    builder.addFunction("encode_" + name, kSig_i_wi)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        ...GCInstr(instr), 0,
      ]);
  }

  builder.addFunction("encode_null", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kStringRefCode,
        kExprI32Const, 42,
        ...GCInstr(kExprStringEncodeWtf8), 0, 0,
      ]);

  let instance = builder.instantiate();
  let memory = new Uint8Array(instance.exports.memory.buffer);
  function clearMemory(low, high) {
    for (let i = low; i < high; i++) {
      memory[i] = 0;
    }
  }
  function assertMemoryBytesZero(low, high) {
    for (let i = low; i < high; i++) {
      assertEquals(0, memory[i]);
    }
  }
  function checkMemory(offset, bytes) {
    let slop = 64;
    assertMemoryBytesZero(Math.max(0, offset - slop), offset);
    for (let i = 0; i < bytes.length; i++) {
      assertEquals(bytes[i], memory[offset + i]);
    }
    assertMemoryBytesZero(offset + bytes.length,
                          Math.min(memory.length,
                                   offset + bytes.length + slop));
  }

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    let offset = memory.length - wtf8.length;
    assertEquals(wtf8.length, instance.exports.encode_wtf8(str, offset));
    checkMemory(offset, wtf8);
    clearMemory(offset, offset + wtf8.length);
  }

  for (let str of interestingStrings) {
    let offset = 0;
    if (HasIsolatedSurrogate(str)) {
      assertThrows(() => instance.exports.encode_utf8(str, offset),
          WebAssembly.RuntimeError,
          "Failed to encode string as UTF-8: contains unpaired surrogate");
    } else {
      let wtf8 = encodeWtf8(str);
      assertEquals(wtf8.length, instance.exports.encode_utf8(str, offset));
      checkMemory(offset, wtf8);
      clearMemory(offset, offset + wtf8.length);
    }
  }

  for (let str of interestingStrings) {
    let offset = 42;
    let replaced = ReplaceIsolatedSurrogates(str);
    if (!HasIsolatedSurrogate(str)) assertEquals(str, replaced);
    let wtf8 = encodeWtf8(replaced);
    assertEquals(wtf8.length, instance.exports.encode_replace(str, offset));
    checkMemory(offset, wtf8);
    clearMemory(offset, offset + wtf8.length);
  }

  assertThrows(() => instance.exports.encode_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");

  checkMemory(memory.length - 10, []);

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    let offset = memory.length - wtf8.length + 1;
    assertThrows(() => instance.exports.encode_wtf8(str, offset),
                 WebAssembly.RuntimeError, "memory access out of bounds");
    assertThrows(() => instance.exports.encode_utf8(str, offset),
                 WebAssembly.RuntimeError, "memory access out of bounds");
    assertThrows(() => instance.exports.encode_replace(str, offset),
                 WebAssembly.RuntimeError, "memory access out of bounds");
    checkMemory(offset - 1, []);
  }
})();

(function TestStringEncodeWtf16() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  builder.exportMemoryAs("memory");

  builder.addFunction("encode_wtf16", kSig_i_wi)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      ...GCInstr(kExprStringEncodeWtf16), 0,
    ]);

  builder.addFunction("encode_null", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kStringRefCode,
        kExprI32Const, 42,
        ...GCInstr(kExprStringEncodeWtf16), 0,
      ]);

  let instance = builder.instantiate();
  let memory = new Uint8Array(instance.exports.memory.buffer);
  function clearMemory(low, high) {
    for (let i = low; i < high; i++) {
      memory[i] = 0;
    }
  }
  function assertMemoryBytesZero(low, high) {
    for (let i = low; i < high; i++) {
      assertEquals(0, memory[i]);
    }
  }
  function checkMemory(offset, bytes) {
    let slop = 64;
    assertMemoryBytesZero(Math.max(0, offset - slop), offset);
    for (let i = 0; i < bytes.length; i++) {
      assertEquals(bytes[i], memory[offset + i]);
    }
    assertMemoryBytesZero(offset + bytes.length,
                          Math.min(memory.length,
                                   offset + bytes.length + slop));
  }

  for (let str of interestingStrings) {
    let wtf16 = encodeWtf16LE(str);
    let offset = memory.length - wtf16.length;
    assertEquals(str.length, instance.exports.encode_wtf16(str, offset));
    checkMemory(offset, wtf16);
    clearMemory(offset, offset + wtf16.length);
  }

  for (let str of interestingStrings) {
    let wtf16 = encodeWtf16LE(str);
    let offset = 0;
    assertEquals(str.length, instance.exports.encode_wtf16(str, offset));
    checkMemory(offset, wtf16);
    clearMemory(offset, offset + wtf16.length);
  }

  assertThrows(() => instance.exports.encode_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");

  checkMemory(memory.length - 10, []);

  for (let str of interestingStrings) {
    let offset = 1;
    assertThrows(() => instance.exports.encode_wtf16(str, offset),
                 WebAssembly.RuntimeError,
                 "operation does not support unaligned accesses");
  }

  for (let str of interestingStrings) {
    let wtf16 = encodeWtf16LE(str);
    let offset = memory.length - wtf16.length + 2;
    assertThrows(() => instance.exports.encode_wtf16(str, offset),
                 WebAssembly.RuntimeError, "memory access out of bounds");
    checkMemory(offset - 2, []);
  }
})();

(function TestStringConcat() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("concat", kSig_w_ww)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      ...GCInstr(kExprStringConcat)
    ]);

  builder.addFunction("concat_null_head", kSig_w_w)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringConcat)
    ]);
  builder.addFunction("concat_null_tail", kSig_w_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringConcat)
    ]);

  let instance = builder.instantiate();

  for (let head of interestingStrings) {
    for (let tail of interestingStrings) {
      assertEquals(head + tail, instance.exports.concat(head, tail));
    }
  }

  assertThrows(() => instance.exports.concat_null_head("hey"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.concat_null_tail("hey"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringEq() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("eq", kSig_i_ww)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      ...GCInstr(kExprStringEq)
    ]);

  builder.addFunction("eq_null_a", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringEq)
    ]);
  builder.addFunction("eq_null_b", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringEq)
    ]);
  builder.addFunction("eq_both_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringEq)
    ]);

  let instance = builder.instantiate();

  for (let head of interestingStrings) {
    for (let tail of interestingStrings) {
      let result = (head == tail)|0;
      assertEquals(result, instance.exports.eq(head, tail));
      assertEquals(result, instance.exports.eq(head + head, tail + tail));
    }
    assertEquals(0, instance.exports.eq_null_a(head))
    assertEquals(0, instance.exports.eq_null_b(head))
  }

  assertEquals(1, instance.exports.eq_both_null());
})();

(function TestStringIsUSVSequence() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("is_usv_sequence", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringIsUsvSequence)
    ]);

  builder.addFunction("is_usv_sequence_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kStringRefCode,
      ...GCInstr(kExprStringIsUsvSequence)
    ]);

  let instance = builder.instantiate();

  for (let str of interestingStrings) {
    assertEquals(HasIsolatedSurrogate(str) ? 0 : 1,
                 instance.exports.is_usv_sequence(str));
  }

  assertThrows(() => instance.exports.is_usv_sequence_null(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringViewWtf16() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  builder.exportMemoryAs("memory");

  builder.addFunction("view_from_null", kSig_v_v).exportFunc().addBody([
    kExprRefNull, kStringRefCode,
    ...GCInstr(kExprStringAsWtf16),
    kExprDrop,
  ]);

  builder.addFunction("length", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf16),
      ...GCInstr(kExprStringViewWtf16Length)
    ]);

  builder.addFunction("get_codeunit", kSig_i_wi)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf16),
      kExprLocalGet, 1,
      ...GCInstr(kExprStringViewWtf16GetCodeunit)
    ]);

  builder.addFunction("encode", kSig_i_wiii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf16),
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      ...GCInstr(kExprStringViewWtf16Encode), 0
    ]);

  builder.addFunction("slice", kSig_w_wii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf16),
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      ...GCInstr(kExprStringViewWtf16Slice)
    ]);

  // Non-nullable stringview references are still encoded as regular
  // non-nullable references, so they Just Work with br_on_* instructions.
  builder.addFunction("br_on_null", kSig_i_w).exportFunc().addBody([
    kExprBlock, kWasmI32,
      kExprI32Const, 11,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf16),
      kExprBrOnNull, 0,  // Never taken.
      kExprDrop,  // Drop the string view.
      kExprDrop,  // Drop the "11".
      kExprI32Const, 42,
    kExprEnd,
  ]);

  builder.addFunction("br_on_non_null", kSig_i_w).exportFunc().addBody([
    kExprBlock, kWasmI32,
      kExprBlock, kStringViewWtf16Code,
        kExprLocalGet, 0,
        ...GCInstr(kExprStringAsWtf16),
        kExprBrOnNonNull, 0,  // Always taken.
        kExprI32Const, 11,
        kExprBr, 1,
      kExprEnd,
      kExprDrop,  // Drop the string view.
      kExprI32Const, 42,
    kExprEnd,
  ]);

  let instance = builder.instantiate();
  let memory = new Uint8Array(instance.exports.memory.buffer);
  for (let str of interestingStrings) {
    assertEquals(str.length, instance.exports.length(str));
    for (let i = 0; i < str.length; i++) {
      assertEquals(str.charCodeAt(i),
                   instance.exports.get_codeunit(str, i));
    }
    assertEquals(str, instance.exports.slice(str, 0, -1));
  }

  function checkEncoding(str, slice, start, length) {
    let bytes = encodeWtf16LE(slice);
    function clearMemory(low, high) {
      for (let i = low; i < high; i++) {
        memory[i] = 0;
      }
    }
    function assertMemoryBytesZero(low, high) {
      for (let i = low; i < high; i++) {
        assertEquals(0, memory[i]);
      }
    }
    function checkMemory(offset, bytes) {
      let slop = 64;
      assertMemoryBytesZero(Math.max(0, offset - slop), offset);
      for (let i = 0; i < bytes.length; i++) {
        assertEquals(bytes[i], memory[offset + i]);
      }
      assertMemoryBytesZero(offset + bytes.length,
                            Math.min(memory.length,
                                     offset + bytes.length + slop));
    }

    for (let offset of [0, 42, memory.length - bytes.length]) {
      assertEquals(slice.length,
                   instance.exports.encode(str, offset, start, length));
      checkMemory(offset, bytes);
      clearMemory(offset, offset + bytes.length);
    }

    assertThrows(() => instance.exports.encode(str, 1, start, length),
                 WebAssembly.RuntimeError,
                 "operation does not support unaligned accesses");
    assertThrows(
        () => instance.exports.encode(str, memory.length - bytes.length + 2,
                                      start, length),
        WebAssembly.RuntimeError, "memory access out of bounds");
    checkMemory(memory.length - bytes.length - 2, []);
  }
  checkEncoding("fox", "f", 0, 1);
  checkEncoding("fox", "fo", 0, 2);
  checkEncoding("fox", "fox", 0, 3);
  checkEncoding("fox", "fox", 0, 300);
  checkEncoding("fox", "", 1, 0);
  checkEncoding("fox", "o", 1, 1);
  checkEncoding("fox", "ox", 1, 2);
  checkEncoding("fox", "ox", 1, 200);
  checkEncoding("fox", "", 2, 0);
  checkEncoding("fox", "x", 2, 1);
  checkEncoding("fox", "x", 2, 2);
  checkEncoding("fox", "", 3, 0);
  checkEncoding("fox", "", 3, 1_000_000_000);
  checkEncoding("fox", "", 1_000_000_000, 1_000_000_000);
  checkEncoding("fox", "", 100, 100);
  // Bounds checks before alignment checks.
  assertThrows(() => instance.exports.encode("foo", memory.length - 1, 0, 3),
               WebAssembly.RuntimeError, "memory access out of bounds");

  assertEquals("", instance.exports.slice("foo", 0, 0));
  assertEquals("f", instance.exports.slice("foo", 0, 1));
  assertEquals("fo", instance.exports.slice("foo", 0, 2));
  assertEquals("foo", instance.exports.slice("foo", 0, 3));
  assertEquals("foo", instance.exports.slice("foo", 0, 4));
  assertEquals("o", instance.exports.slice("foo", 1, 2));
  assertEquals("oo", instance.exports.slice("foo", 1, 3));
  assertEquals("oo", instance.exports.slice("foo", 1, 100));
  assertEquals("", instance.exports.slice("foo", 1, 0));
  assertEquals("", instance.exports.slice("foo", 3, 4));
  assertEquals("foo", instance.exports.slice("foo", 0, -1));
  assertEquals("", instance.exports.slice("foo", -1, 1));

  assertEquals(42, instance.exports.br_on_null("foo"));
  assertEquals(42, instance.exports.br_on_non_null("foo"));

  assertThrows(() => instance.exports.view_from_null(),
               WebAssembly.RuntimeError, 'dereferencing a null pointer');
  assertThrows(() => instance.exports.get_codeunit("", 0),
               WebAssembly.RuntimeError, "string offset out of bounds");

  // Cover runtime code path for long slices.
  const prefix = "a".repeat(10);
  const slice = "x".repeat(40);
  const suffix = "b".repeat(40);
  const input = prefix + slice + suffix;
  const start = prefix.length;
  const end = start + slice.length;
  assertEquals(slice, instance.exports.slice(input, start, end));

  // Check that we create one-byte substrings when possible.
  let onebyte = instance.exports.slice("\u1234abcABCDE", 1, 4);
  assertEquals("abc", onebyte);
  assertTrue(isOneByteString(onebyte));

  // Check that the CodeStubAssembler implementation also creates one-byte
  // substrings.
  onebyte = instance.exports.slice("\u1234abcA", 1, 4);
  assertEquals("abc", onebyte);
  assertTrue(isOneByteString(onebyte));
  // Cover the code path that checks 8 characters at a time.
  onebyte = instance.exports.slice("\u1234abcdefgh\u1234", 1, 9);
  assertEquals("abcdefgh", onebyte);  // Exactly 8 characters.
  assertTrue(isOneByteString(onebyte));
  onebyte = instance.exports.slice("\u1234abcdefghijXYZ", 1, 11);
  assertEquals("abcdefghij", onebyte);  // Longer than 8.
  assertTrue(isOneByteString(onebyte));

  // Check that the runtime code path also creates one-byte substrings.
  assertTrue(isOneByteString(
      instance.exports.slice(input + "\u1234", start, end)));
})();

(function TestStringViewWtf8() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, undefined);
  builder.exportMemoryAs("memory");

  builder.addFunction("advance", kSig_i_wii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf8),
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      ...GCInstr(kExprStringViewWtf8Advance)
    ]);

  for (let [instr, name] of
       [[kExprStringViewWtf8EncodeUtf8, "utf8"],
        [kExprStringViewWtf8EncodeWtf8, "wtf8"],
        [kExprStringViewWtf8EncodeLossyUtf8, "replace"]]) {
    builder.addFunction(`encode_${name}`, kSig_ii_wiii)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        ...GCInstr(kExprStringAsWtf8),
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kExprLocalGet, 3,
        ...GCInstr(instr), 0
      ]);
  }

  builder.addFunction(`slice`, kSig_w_wii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsWtf8),
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      ...GCInstr(kExprStringViewWtf8Slice)
    ]);

  function Wtf8StartsCodepoint(wtf8, offset) {
    return (wtf8[offset] & 0xc0) != 0x80;
  }
  function Wtf8PositionTreatment(wtf8, offset) {
    while (offset < wtf8.length) {
      if (Wtf8StartsCodepoint(wtf8, offset)) return offset;
      offset++;
    }
    return wtf8.length;
  }
  function CodepointStart(wtf8, offset) {
    if (offset >= wtf8.length) return wtf8.length;
    while (!Wtf8StartsCodepoint(wtf8, offset)) {
      offset--;
    }
    return offset;
  }

  let instance = builder.instantiate();
  let memory = new Uint8Array(instance.exports.memory.buffer);

  for (let pos = 0; pos < "ascii".length; pos++) {
    assertEquals(pos + 1, instance.exports.advance("ascii", pos, 1));
  }

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    assertEquals(wtf8.length, instance.exports.advance(str, 0, -1));
    assertEquals(wtf8.length, instance.exports.advance(str, -1, 0));
    assertEquals(wtf8.length, instance.exports.advance(str, 0, wtf8.length));
    assertEquals(wtf8.length, instance.exports.advance(str, wtf8.length, 0));
    assertEquals(wtf8.length,
                 instance.exports.advance(str, 0, wtf8.length + 1));
    assertEquals(wtf8.length,
                 instance.exports.advance(str, wtf8.length + 1, 0));
    for (let pos = 0; pos <= wtf8.length; pos++) {
      for (let bytes = 0; bytes <= wtf8.length - pos; bytes++) {
        assertEquals(
            CodepointStart(wtf8, Wtf8PositionTreatment(wtf8, pos) + bytes),
            instance.exports.advance(str, pos, bytes));
      }
    }
  }

  function clearMemory(low, high) {
    for (let i = low; i < high; i++) {
      memory[i] = 0;
    }
  }
  function assertMemoryBytesZero(low, high) {
    for (let i = low; i < high; i++) {
      assertEquals(0, memory[i]);
    }
  }
  function checkMemory(offset, bytes) {
    let slop = 16;
    assertMemoryBytesZero(Math.max(0, offset - slop), offset);
    for (let i = 0; i < bytes.length; i++) {
      assertEquals(bytes[i], memory[offset + i]);
    }
    assertMemoryBytesZero(offset + bytes.length,
                          Math.min(memory.length,
                                   offset + bytes.length + slop));
  }
  function checkEncoding(variant, str, slice, start, length) {
    let all_bytes = encodeWtf8(str);
    let bytes = encodeWtf8(slice);

    let encode = instance.exports[`encode_${variant}`];
    let expected_start = Wtf8PositionTreatment(all_bytes, start);
    let expected_end = CodepointStart(all_bytes, expected_start + bytes.length);
    for (let offset of [0, 42, memory.length - bytes.length]) {
      assertArrayEquals([expected_end, expected_end - expected_start],
                        encode(str, offset, start, length));
      checkMemory(offset, bytes);
      clearMemory(offset, offset + bytes.length);
    }

    assertThrows(() => encode(str, memory.length - bytes.length + 2,
                              start, length),
                 WebAssembly.RuntimeError, "memory access out of bounds");
    checkMemory(memory.length - bytes.length - 2, []);
  }

  checkEncoding('utf8', "fox", "f", 0, 1);
  checkEncoding('utf8', "fox", "fo", 0, 2);
  checkEncoding('utf8', "fox", "fox", 0, 3);
  checkEncoding('utf8', "fox", "fox", 0, 300);
  checkEncoding('utf8', "fox", "", 1, 0);
  checkEncoding('utf8', "fox", "o", 1, 1);
  checkEncoding('utf8', "fox", "ox", 1, 2);
  checkEncoding('utf8', "fox", "ox", 1, 200);
  checkEncoding('utf8', "fox", "", 2, 0);
  checkEncoding('utf8', "fox", "x", 2, 1);
  checkEncoding('utf8', "fox", "x", 2, 2);
  checkEncoding('utf8', "fox", "", 3, 0);
  checkEncoding('utf8', "fox", "", 3, 1_000_000_000);
  checkEncoding('utf8', "fox", "", 1_000_000_000, 1_000_000_000);
  checkEncoding('utf8', "fox", "", 100, 100);

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    for (let pos = 0; pos <= wtf8.length; pos++) {
      for (let bytes = 0; bytes <= wtf8.length - pos; bytes++) {
        let start = Wtf8PositionTreatment(wtf8, pos);
        let end = CodepointStart(wtf8, start + bytes);
        let expected = decodeWtf8(wtf8, start, end);
        checkEncoding('wtf8', str, expected, pos, bytes);
        if (HasIsolatedSurrogate(expected)) {
          assertThrows(() => instance.exports.encode_utf8(str, 0, pos, bytes),
                       WebAssembly.RuntimeError,
                       "Failed to encode string as UTF-8: " +
                       "contains unpaired surrogate");
          checkEncoding('replace', str,
                        ReplaceIsolatedSurrogates(expected), pos, bytes);
        } else {
          checkEncoding('utf8', str, expected, pos, bytes);
          checkEncoding('replace', str, expected, pos, bytes);
        }
      }
    }
  }

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    for (let start = 0; start <= wtf8.length; start++) {
      for (let end = start; end <= wtf8.length; end++) {
        let expected_slice = decodeWtf8(wtf8,
                                        Wtf8PositionTreatment(wtf8, start),
                                        Wtf8PositionTreatment(wtf8, end));
        assertEquals(expected_slice, instance.exports.slice(str, start, end));
      }
    }
  }
})();

(function TestStringViewIter() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let wrapper = builder.addStruct([makeField(kWasmStringViewIter, true)]);

  let global = builder.addGlobal(wasmRefNullType(wrapper), true, false);

  builder.addFunction("iterate", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsIter),
      kGCPrefix, kExprStructNew, wrapper,
      kExprGlobalSet, global.index
    ]);

  builder.addFunction("next", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      ...GCInstr(kExprStringViewIterNext)
    ]);

  builder.addFunction("advance", kSig_i_i)
    .exportFunc()
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterAdvance)
    ]);

  builder.addFunction("rewind", kSig_i_i)
    .exportFunc()
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterRewind)
    ]);

  builder.addFunction("slice", kSig_w_i)
    .exportFunc()
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterSlice)
    ]);

  let instance = builder.instantiate();

  for (let str of interestingStrings) {
    let codepoints = [];
    for (let codepoint of str) {
      codepoints.push(codepoint.codePointAt(0));
    }

    instance.exports.iterate(str);
    for (let codepoint of codepoints) {
      assertEquals(codepoint, instance.exports.next());
    }
    assertEquals(-1, instance.exports.next());
    assertEquals(-1, instance.exports.next());

    for (let i = 1; i <= codepoints.length; i++) {
      assertEquals(i, instance.exports.rewind(i));
      assertEquals(codepoints[codepoints.length - i], instance.exports.next());
      assertEquals(i - 1, instance.exports.advance(-1));
    }
    for (let i = 0; i < codepoints.length; i++) {
      instance.exports.rewind(-1);
      assertEquals(i, instance.exports.advance(i));
      assertEquals(codepoints[i], instance.exports.next());
    }

    assertEquals(codepoints.length, instance.exports.rewind(-1));
    assertEquals(0, instance.exports.rewind(-1));
    assertEquals(codepoints.length, instance.exports.advance(-1));
    assertEquals(0, instance.exports.advance(-1));

    for (let start = 0; start <= codepoints.length; start++) {
      for (let end = start; end <= codepoints.length; end++) {
        let expected_slice =
            String.fromCodePoint(...codepoints.slice(start, end));
        instance.exports.iterate(str);
        assertEquals(start, instance.exports.advance(start));
        assertEquals(expected_slice, instance.exports.slice(end - start));
      }
    }
    instance.exports.iterate(str);
    assertEquals(str, instance.exports.slice(codepoints.length));
    assertEquals(str, instance.exports.slice(-1));
    assertEquals("", instance.exports.slice(0));
    assertEquals(codepoints.length, instance.exports.advance(-1));
    assertEquals("", instance.exports.slice(-1));
  }
})();

(function TestStringCompare() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction("compare",
                      makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      ...GCInstr(kExprStringCompare)
    ]);

  let instance = builder.instantiate();
  for (let lhs of interestingStrings) {
    for (let rhs of interestingStrings) {
      print(`"${lhs}" <=> "${rhs}"`);
      const expected = lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
      assertEquals(expected, instance.exports.compare(lhs, rhs));
    }
  }

  assertThrows(() => instance.exports.compare(null, "abc"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.compare("abc", null),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringCompareNullCheckStaticType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Use a mix of nullable and non-nullable input types to the compare.
  builder.addFunction("compareLhsNullable",
                      makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefAsNonNull,
      kExprLocalGet, 1,
      ...GCInstr(kExprStringCompare)
    ]);

  builder.addFunction("compareRhsNullable",
                      makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprRefAsNonNull,
      ...GCInstr(kExprStringCompare)
    ]);

  let instance = builder.instantiate();
  assertThrows(() => instance.exports.compareLhsNullable(null, "abc"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.compareLhsNullable("abc", null),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.compareRhsNullable(null, "abc"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.compareRhsNullable("abc", null),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringFromCodePoint() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("asString",
                      makeSig([kWasmI32], [wasmRefType(kWasmStringRef)]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringFromCodePoint),
    ]);

  let instance = builder.instantiate();
  for (let char of "Az1#\n\ucccc\ud800\udc00") {
    assertEquals(char, instance.exports.asString(char.codePointAt(0)));
  }
  for (let codePoint of [0x110000, 0xFFFFFFFF, -1]) {
    assertThrows(() => instance.exports.asString(codePoint),
                 WebAssembly.RuntimeError, /Invalid code point [0-9]+/);
  }
})();

(function TestStringHash() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("hash", kSig_i_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringHash),
    ]);

  let hash = builder.instantiate().exports.hash;
  assertEquals(hash(""), hash(""));
  assertEquals(hash("foo"), hash("foo"));
  assertEquals(hash("bar"), hash("bar"));
  assertEquals(hash("123"), hash("123"));
  // Assuming that hash collisions are very rare.
  assertNotEquals(hash("foo"), hash("bar"));
  // Test with cons strings.
  assertEquals(hash("f" + "o" + "o"), hash("foo"));
  assertEquals(hash("f" + 1), hash("f1"));

  assertEquals(hash(new String(" foo ").trim()), hash("foo"));
  assertEquals(hash(new String("xfoox").substring(1, 4)), hash("foo"));

  // Test integer index hash.
  let dummy_obj = {123: 456};
  let index_string = "123";
  assertEquals(456, dummy_obj[index_string]);
  assertEquals(hash("1" + "23"), hash(index_string));
})();
