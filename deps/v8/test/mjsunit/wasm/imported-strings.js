// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings
// For {isOneByteString}:
// Flags: --expose-externalize-string

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kRefExtern = wasmRefType(kWasmExternRef);

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".

let kSig_e_ii = makeSig([kWasmI32, kWasmI32], [kRefExtern]);
let kSig_e_v = makeSig([], [kRefExtern]);
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_i_rr = makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]);
let kSig_i_rri = makeSig([kWasmExternRef, kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32],
                          [kWasmI32]);
let kSig_ii_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32],
                           [kWasmI32, kWasmI32]);
let kSig_e_i = makeSig([kWasmI32], [kRefExtern]);
let kSig_e_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32],
                         [kRefExtern]);
let kSig_e_rr = makeSig([kWasmExternRef, kWasmExternRef], [kRefExtern]);
let kSig_e_r = makeSig([kWasmExternRef], [kRefExtern]);

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

let kArrayI16;
let kArrayI8;
let kStringCast;
let kStringTest;
let kStringFromWtf16Array;
let kStringFromUtf8Array;
let kStringIntoUtf8Array;
let kStringToUtf8Array;
let kStringToWtf16Array;
let kStringMeasureUtf8;
let kStringFromCharCode;
let kStringFromCodePoint;
let kStringCharCodeAt;
let kStringCodePointAt;
let kStringLength;
let kStringConcat;
let kStringSubstring;
let kStringEquals;
let kStringCompare;
let kStringIndexOfImported;
let kStringToLowerCaseImported;

function MakeBuilder() {
  let builder = new WasmModuleBuilder();
  builder.startRecGroup();
  kArrayI16 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  builder.endRecGroup();
  builder.startRecGroup();
  kArrayI8 = builder.addArray(kWasmI8, true, kNoSuperType, true);
  builder.endRecGroup();
  let arrayref = wasmRefNullType(kArrayI16);
  let array8ref = wasmRefNullType(kArrayI8);

  kStringCast = builder.addImport('wasm:js-string', 'cast', kSig_e_r);
  kStringTest = builder.addImport('wasm:js-string', 'test', kSig_i_r);
  kStringFromWtf16Array = builder.addImport(
      'wasm:js-string', 'fromCharCodeArray',
      makeSig([arrayref, kWasmI32, kWasmI32], [kRefExtern]));
  kStringFromUtf8Array = builder.addImport(
      'wasm:text-decoder', 'decodeStringFromUTF8Array',
      makeSig([array8ref, kWasmI32, kWasmI32], [kRefExtern]));
  kStringToWtf16Array = builder.addImport(
      'wasm:js-string', 'intoCharCodeArray',
      makeSig([kWasmExternRef, arrayref, kWasmI32], [kWasmI32]));
  kStringMeasureUtf8 =
      builder.addImport('wasm:text-encoder', 'measureStringAsUTF8', kSig_i_r);
  kStringIntoUtf8Array = builder.addImport(
      'wasm:text-encoder', 'encodeStringIntoUTF8Array',
      makeSig([kWasmExternRef, array8ref, kWasmI32], [kWasmI32]));
  kStringToUtf8Array = builder.addImport(
      'wasm:text-encoder', 'encodeStringToUTF8Array',
      makeSig([kWasmExternRef], [wasmRefType(kArrayI8)]));
  kStringFromCharCode =
      builder.addImport('wasm:js-string', 'fromCharCode', kSig_e_i);
  kStringFromCodePoint =
      builder.addImport('wasm:js-string', 'fromCodePoint', kSig_e_i);
  kStringCharCodeAt =
      builder.addImport('wasm:js-string', 'charCodeAt', kSig_i_ri);
  kStringCodePointAt =
      builder.addImport('wasm:js-string', 'codePointAt', kSig_i_ri);
  kStringLength = builder.addImport('wasm:js-string', 'length', kSig_i_r);
  kStringConcat = builder.addImport('wasm:js-string', 'concat', kSig_e_rr);
  kStringSubstring =
      builder.addImport('wasm:js-string', 'substring', kSig_e_rii);
  kStringEquals = builder.addImport('wasm:js-string', 'equals', kSig_i_rr);
  kStringCompare = builder.addImport('wasm:js-string', 'compare', kSig_i_rr);
  kStringIndexOfImported = builder.addImport('m', 'indexOf', kSig_i_rri);
  kStringToLowerCaseImported = builder.addImport('m', 'toLowerCase', kSig_r_r);

  return builder;
}

let kImports = {
  strings: interestingStrings,
  m: {
    indexOf: Function.prototype.call.bind(String.prototype.indexOf),
    toLowerCase: Function.prototype.call.bind(String.prototype.toLowerCase),
  },
};
let kBuiltins = { builtins: ["js-string", "text-decoder", "text-encoder"] };

(function TestStringCast() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("cast", kSig_e_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringCast,
    ]);

  builder.addFunction("cast_null", kSig_e_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringCast,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  assertEquals('foo', instance.exports.cast('foo'));
  assertThrows(
      () => instance.exports.cast(123), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.cast(undefined), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.cast(true), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.cast(null), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.cast_null(), WebAssembly.RuntimeError,
      'illegal cast');
})();

(function TestStringTest() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("test", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringTest,
    ]);

  builder.addFunction("test_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringTest,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  assertEquals(1, instance.exports.test("foo"));
  assertEquals(0, instance.exports.test(123));
  assertEquals(0, instance.exports.test(undefined));
  assertEquals(0, instance.exports.test(true));
  assertEquals(0, instance.exports.test(null));
  assertEquals(0, instance.exports.test_null());
})();

(function TestIndexOfImportedStrings() {
  print(arguments.callee.name);
  let builder = new MakeBuilder();

  builder.addFunction('indexOf', kSig_i_rri).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, kStringCast,
    kExprLocalGet, 1,
    kExprCallFunction, kStringCast,
    kExprLocalGet, 2,
    kExprCallFunction, kStringIndexOfImported,
  ]);
  let instance = builder.instantiate(kImports, kBuiltins);

  assertEquals(2, instance.exports.indexOf('xxfooxx', 'foo', 0));
  assertEquals(2, instance.exports.indexOf('xxfooxx', 'foo', -2));
  assertEquals(-1, instance.exports.indexOf('xxfooxx', 'foo', 100));
  // Make sure we don't lose bits when Smi-tagging of the start position.
  assertEquals(-1, instance.exports.indexOf('xxfooxx', 'foo', 0x4000_0000));
  assertEquals(-1, instance.exports.indexOf('xxfooxx', 'foo', 0x2000_0000));
  assertEquals(
      2,
      instance.exports.indexOf(
          'xxfooxx', 'foo', 0x8000_0000));  // Negative i32.

  // Both first and second args should be non-null strings.
  assertThrows(
      () => instance.exports.indexOf('xxnullxx', null, 0),
      WebAssembly.RuntimeError, 'illegal cast');
  assertThrows(
      () => instance.exports.indexOf(12345, 234, 0), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.indexOf(null, 'foo', 0), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.indexOf(null, 'null', 0), WebAssembly.RuntimeError,
      'illegal cast');
})();

(function TestStringToLowerCaseImported() {
  print(arguments.callee.name);
  let builder = new MakeBuilder();

  builder.addFunction('toLowerCase', kSig_r_r).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, kStringCast,
    kExprCallFunction, kStringToLowerCaseImported,
  ]);
  let instance = builder.instantiate(kImports, kBuiltins);

  assertEquals(
      'make this lowercase!',
      instance.exports.toLowerCase('MAKE THIS LOWERCASE!'));

  // The argument should be a non-null string.
  assertThrows(
      () => instance.exports.toLowerCase(null), WebAssembly.RuntimeError,
      'illegal cast');
  assertThrows(
      () => instance.exports.toLowerCase(123), WebAssembly.RuntimeError,
      'illegal cast');
})();

(function TestStringConst() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  for (let i = 0; i < interestingStrings.length; i++) {
    builder.addImportedGlobal('strings', i.toString(), kRefExtern, false);
    builder.addFunction("string_const" + i, kSig_e_v).exportFunc()
      .addBody([kExprGlobalGet, i]);
  }
  for (let i = 0; i < interestingStrings.length; i++) {
    builder.addGlobal(kRefExtern, false, false, [kExprGlobalGet, i])
      .exportAs("global" + i);
  }
  let instance = builder.instantiate(kImports, kBuiltins);
  for (let [i, str] of interestingStrings.entries()) {
    assertEquals(str, instance.exports["string_const" + i]());
    assertEquals(str, instance.exports["global" + i].value);
  }
})();

(function TestStringLength() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("string_length", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringLength,
    ]);

  builder.addFunction("string_length_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringLength,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let str of interestingStrings) {
    assertEquals(str.length, instance.exports.string_length(str));
  }

  assertThrows(() => instance.exports.string_length(null),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.string_length_null(),
               WebAssembly.RuntimeError, "illegal cast");
})();

(function TestStringConcat() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("concat", kSig_e_rr)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, kStringConcat,
    ]);

  builder.addFunction("concat_null_head", kSig_e_r)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, kStringConcat,
    ]);

  builder.addFunction("concat_null_tail", kSig_e_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringConcat,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  for (let head of interestingStrings) {
    for (let tail of interestingStrings) {
      assertEquals(head + tail, instance.exports.concat(head, tail));
    }
  }

  assertThrows(() => instance.exports.concat(null, "hey"),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.concat('hey', null),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.concat_null_head("hey"),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.concat_null_tail("hey"),
               WebAssembly.RuntimeError, "illegal cast");
})();

(function TestStringEq() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("eq", kSig_i_rr)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, kStringEquals,
    ]);

  builder.addFunction("eq_null_a", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, kStringEquals,
    ]);

  builder.addFunction("eq_null_b", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringEquals,
    ]);

  builder.addFunction("eq_both_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringEquals,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  for (let head of interestingStrings) {
    for (let tail of interestingStrings) {
      let result = (head == tail) | 0;
      assertEquals(result, instance.exports.eq(head, tail));
      assertEquals(result, instance.exports.eq(head + head, tail + tail));
    }
    assertEquals(0, instance.exports.eq_null_a(head))
    assertEquals(0, instance.exports.eq(null, head))
    assertEquals(0, instance.exports.eq(head, null))
  }

  assertEquals(1, instance.exports.eq_both_null());
  assertEquals(1, instance.exports.eq(null, null));
})();

(function TestStringViewWtf16() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("get_codeunit", kSig_i_ri)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, kStringCharCodeAt,
    ]);

  builder.addFunction("get_codeunit_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprI32Const, 0,
      kExprCallFunction, kStringCharCodeAt,
    ]);

  builder.addFunction("get_codepoint", kSig_i_ri)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, kStringCodePointAt,
    ]);

  builder.addFunction("get_codepoint_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprI32Const, 0,
      kExprCallFunction, kStringCodePointAt,
    ]);

  builder.addFunction("slice", kSig_e_rii)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallFunction, kStringSubstring,
    ]);

  builder.addFunction("slice_null", kSig_e_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprI32Const, 0,
      kExprI32Const, 0,
      kExprCallFunction, kStringSubstring,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let str of interestingStrings) {
    for (let i = 0; i < str.length; i++) {
      assertEquals(str.charCodeAt(i),
                   instance.exports.get_codeunit(str, i));
      assertEquals(str.codePointAt(i),
                   instance.exports.get_codepoint(str, i));
    }
    assertEquals(str, instance.exports.slice(str, 0, -1));
  }

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

  assertThrows(() => instance.exports.get_codeunit(null, 0),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.get_codeunit_null(),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.get_codeunit("", 0),
               WebAssembly.RuntimeError, "string offset out of bounds");
  assertThrows(() => instance.exports.get_codepoint(null, 0),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.get_codepoint_null(),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.get_codepoint("", 0),
               WebAssembly.RuntimeError, "string offset out of bounds");
  assertThrows(() => instance.exports.slice(null, 0, 0),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.slice_null(),
               WebAssembly.RuntimeError, "illegal cast");

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
  onebyte = instance.exports.slice("\u1234abcdefghij\u1234", 1, 11);
  assertEquals("abcdefghij", onebyte);  // Longer than 8.
  assertTrue(isOneByteString(onebyte));

  // Check that the runtime code path also creates one-byte substrings.
  assertTrue(isOneByteString(
      instance.exports.slice(input + "\u1234", start, end)));
})();

(function TestStringCompare() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("compare", kSig_i_rr)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, kStringCompare,
    ]);

  builder.addFunction("compare_null_a", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprLocalGet, 0,
      kExprCallFunction, kStringCompare,
    ]);

  builder.addFunction("compare_null_b", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringCompare,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let lhs of interestingStrings) {
    for (let rhs of interestingStrings) {
      const expected = lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
      assertEquals(expected, instance.exports.compare(lhs, rhs));
    }
  }

  assertThrows(() => instance.exports.compare(null, "abc"),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.compare("abc", null),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.compare_null_a("abc"),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.compare_null_b("abc"),
               WebAssembly.RuntimeError, "illegal cast");
})();

(function TestStringCompareNullCheckStaticType() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  // Use a mix of nullable and non-nullable input types to the compare.
  builder.addFunction("compareLhsNullable",
                      makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprRefAsNonNull,
      kExprLocalGet, 1,
      kExprCallFunction, kStringCompare,
    ]);

  builder.addFunction("compareRhsNullable",
                      makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]))
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprRefAsNonNull,
      kExprCallFunction, kStringCompare,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  assertThrows(() => instance.exports.compareLhsNullable(null, "abc"),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.compareLhsNullable("abc", null),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.compareRhsNullable(null, "abc"),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.compareRhsNullable("abc", null),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
})();

(function TestStringFromCodePoint() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("asString", kSig_e_i)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringFromCodePoint,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let char of "Az1#\n\ucccc\ud800\udc00") {
    assertEquals(char, instance.exports.asString(char.codePointAt(0)));
  }
  for (let codePoint of [0x110000, 0xFFFFFFFF, -1]) {
    assertThrows(() => instance.exports.asString(codePoint),
                 WebAssembly.RuntimeError, /Invalid code point -?[0-9]+/);
  }
})();

(function TestStringFromCharCode() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("asString", kSig_e_i)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringFromCharCode,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  let inputs = "Az1#\n\ucccc\ud800\udc00";
  for (let i = 0; i < inputs.length; i++) {
    assertEquals(inputs.charAt(i),
                 instance.exports.asString(inputs.charCodeAt(i)));
  }
})();

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
  let invalid_inputs = [
    'trailing high byte \xa9',
    'interstitial high \xa9 byte',
    'invalid \xc0 byte',
    'invalid three-byte \xed\xd0\x80',
    'surrogate \xed\xa0\x80\xed\xb0\x80 pair'
  ];
  let invalid_replaced = [
    'trailing high byte \uFFFD',
    'interstitial high \uFFFD byte',
    'invalid \uFFFD byte',
    'invalid three-byte \uFFFD\u0400',
    'surrogate \uFFFD\uFFFD\uFFFD\uFFFD\uFFFD\uFFFD pair'
  ];
  for (let i = 0; i < invalid_inputs.length; i++) {
    let bytes = invalid_inputs[i];
    invalid[bytes] = {
      offset: data.length,
      length: bytes.length,
      replaced: invalid_replaced[i]
    };
    for (let i = 0; i < bytes.length; i++) {
      data.push(bytes.charCodeAt(i));
    }
  }
  return { valid, invalid, data: Uint8Array.from(data) };
};

(function TestStringNewUtf8Array() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  let data = makeWtf8TestDataSegment();
  let data_index = builder.addPassiveDataSegment(data.data);

  let ascii_data_index =
      builder.addPassiveDataSegment(Uint8Array.from(encodeWtf8("ascii")));

  let make_i8_array = builder.addFunction(
      "make_i8_array", makeSig([], [wasmRefType(kArrayI8)]))
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const(data.data.length),
      kGCPrefix, kExprArrayNewData, kArrayI8, data_index
    ]).index;

  builder.addFunction("new_utf8", kSig_e_ii)
    .exportFunc()
    .addBody([
      kExprCallFunction, make_i8_array,
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprCallFunction, kStringFromUtf8Array,
    ]);

  builder.addFunction("bounds_check", kSig_e_ii)
    .exportFunc()
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const("ascii".length),
      kGCPrefix, kExprArrayNewData, kArrayI8, ascii_data_index,
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprCallFunction, kStringFromUtf8Array,
    ]);

  builder.addFunction("null_array", kSig_e_v).exportFunc()
    .addBody([
      kExprRefNull, kArrayI8,
      kExprI32Const, 0, kExprI32Const, 0,
      kExprCallFunction, kStringFromUtf8Array,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let [str, {offset, length}] of Object.entries(data.valid)) {
    let start = offset;
    let end = offset + length;
    if (HasIsolatedSurrogate(str)) {
      // Isolated surrogates have the three-byte pattern ED [A0,BF] [80,BF].
      // When the sloppy decoder gets to the second byte, it will reject
      // the sequence, and then retry parsing at the second byte.
      // Seeing the second byte can't start a sequence, it replaces the
      // second byte and continues with the next, which also can't start
      // a sequence.  The result is that one isolated surrogate is replaced
      // by three U+FFFD codepoints.
      assertEquals(ReplaceIsolatedSurrogates(str, '\ufffd\ufffd\ufffd'),
                   instance.exports.new_utf8(start, end));
    } else {
      assertEquals(str, instance.exports.new_utf8(start, end));
    }
  }
  for (let [str, {offset, length, replaced}] of Object.entries(data.invalid)) {
    let start = offset;
    let end = offset + length;
    assertEquals(replaced, instance.exports.new_utf8(start, end));
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
  let builder = MakeBuilder();

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

  let make_i16_array = builder.addFunction(
      "make_i16_array", makeSig([], [wasmRefType(kArrayI16)]))
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const(data.data.length / 2),
      kGCPrefix, kExprArrayNewData, kArrayI16, data_index
    ]).index;

  builder.addFunction("new_wtf16", kSig_e_ii)
    .exportFunc()
    .addBody([
      kExprCallFunction, make_i16_array,
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprCallFunction, kStringFromWtf16Array,
    ]);

  builder.addFunction("bounds_check", kSig_e_ii)
    .exportFunc()
    .addBody([
      ...wasmI32Const(0),
      ...wasmI32Const("ascii".length),
      kGCPrefix, kExprArrayNewData, kArrayI16, ascii_data_index,
      kExprLocalGet, 0, kExprLocalGet, 1,
      kExprCallFunction, kStringFromWtf16Array,
    ]);

  builder.addFunction("null_array", kSig_e_v).exportFunc()
    .addBody([
      kExprRefNull, kArrayI16,
      kExprI32Const, 0, kExprI32Const, 0,
      kExprCallFunction, kStringFromWtf16Array,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
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

(function TestStringEncodeWtf16Array() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  // Allocate an array and encode into it.  Then decode it.
  // (str, length, offset) -> str
  builder.addFunction("encode", kSig_e_rii)
    .exportFunc()
    .addLocals(wasmRefNullType(kArrayI16), 1)
    .addLocals(kWasmI32, 1)
    .addBody([
      // Allocate buffer.
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayNewDefault, kArrayI16,
      kExprLocalSet, 3,

      // Write buffer, store number of code units written.
      kExprLocalGet, 0,
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      kExprCallFunction, kStringToWtf16Array,
      kExprLocalSet, 4,

      // Read buffer.
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      kExprLocalGet, 2, kExprLocalGet, 4, kExprI32Add,
      kExprCallFunction, kStringFromWtf16Array,
    ]);

  builder.addFunction("encode_null_string", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, kArrayI16,
      kExprI32Const, 0,
      kExprCallFunction, kStringToWtf16Array,
    ]);

  builder.addFunction("encode_null_array", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, kArrayI16,
      kExprI32Const, 0, kExprI32Const, 0,
      kExprCallFunction, kStringFromWtf16Array,
      kExprRefNull, kArrayI16,
      kExprI32Const, 0,
      kExprCallFunction, kStringToWtf16Array,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let str of interestingStrings) {
    assertEquals(str, instance.exports.encode(str, str.length, 0));
    assertEquals(str, instance.exports.encode(str, str.length + 20, 10));
  }

  assertThrows(() => instance.exports.encode(null, 0, 0),
               WebAssembly.RuntimeError, "illegal cast");
  assertThrows(() => instance.exports.encode_null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.encode_null_string(),
               WebAssembly.RuntimeError, "illegal cast");

  for (let str of interestingStrings) {
    let message = "array element access out of bounds";
    assertThrows(() => instance.exports.encode(str, str.length, 1),
                 WebAssembly.RuntimeError, message);
  }
})();

(function TestStringMeasureUtf8() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  builder.addFunction("string_measure_utf8", kSig_i_r)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringMeasureUtf8,
    ]);

  builder.addFunction("string_measure_utf8_null", kSig_i_v)
    .exportFunc()
    .addBody([
      kExprRefNull, kExternRefCode,
      kExprCallFunction, kStringMeasureUtf8,
    ]);

  let instance = builder.instantiate(kImports, kBuiltins);
  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    assertEquals(wtf8.length, instance.exports.string_measure_utf8(str));
  }

  assertThrows(() => instance.exports.string_measure_utf8_null(),
               WebAssembly.RuntimeError, "illegal cast");
})();

(function TestStringEncodeUtf8Array() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  // Allocate an array that's exactly the expected size, and encode
  // into it.  Then decode it.
  // (str, length, offset=0) -> str
  builder.addFunction("encode_utf8", kSig_e_rii)
    .exportFunc()
    .addLocals(wasmRefNullType(kArrayI8), 1)
    .addLocals(kWasmI32, 1)
    .addBody([
      // Allocate buffer.
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayNewDefault, kArrayI8,
      kExprLocalSet, 3,

      // Write buffer, store number of bytes written.
      kExprLocalGet, 0,
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      kExprCallFunction, kStringIntoUtf8Array,
      kExprLocalSet, 4,

      // Read buffer.
      kExprLocalGet, 3,
      kExprLocalGet, 2,
      kExprLocalGet, 2, kExprLocalGet, 4, kExprI32Add,
      kExprCallFunction, kStringFromUtf8Array,
    ]);


  builder.addFunction("encode_null_string", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kExternRefCode,
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, kArrayI8,
        kExprI32Const, 0,
        kExprCallFunction, kStringIntoUtf8Array,
      ]);
  builder.addFunction("encode_null_array", kSig_i_v)
    .exportFunc()
    .addBody([
        kExprI32Const, 0, kGCPrefix, kExprArrayNewDefault, kArrayI8,
        kExprI32Const, 0, kExprI32Const, 0,
        kExprCallFunction, kStringFromUtf8Array,
        kExprRefNull, kArrayI8,
        kExprI32Const, 0,
        kExprCallFunction, kStringIntoUtf8Array,
      ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  for (let str of interestingStrings) {
    let replaced = ReplaceIsolatedSurrogates(str);
    if (!HasIsolatedSurrogate(str)) assertEquals(str, replaced);
    let wtf8 = encodeWtf8(replaced);
    assertEquals(replaced,
                 instance.exports.encode_utf8(str, wtf8.length, 0));
    assertEquals(replaced,
                 instance.exports.encode_utf8(str, wtf8.length + 20, 10));
  }

  assertThrows(() => instance.exports.encode_null_array(),
               WebAssembly.RuntimeError, "dereferencing a null pointer");
  assertThrows(() => instance.exports.encode_null_string(),
               WebAssembly.RuntimeError, "illegal cast");

  for (let str of interestingStrings) {
    let wtf8 = encodeWtf8(str);
    let message = "array element access out of bounds";
    assertThrows(() => instance.exports.encode_utf8(str, wtf8.length, 1),
                 WebAssembly.RuntimeError, message);
  }
})();

(function TestStringToUtf8Array() {
  print(arguments.callee.name);
  let builder = MakeBuilder();

  // Convert the string to an array, then decode it back.
  builder.addFunction("encode_utf8", kSig_e_r)
    .exportFunc()
    .addLocals(wasmRefNullType(kArrayI8), 1)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, kStringToUtf8Array,
      kExprLocalTee, 1,

      kExprI32Const, 0,  // start
      kExprLocalGet, 1, kGCPrefix, kExprArrayLen,  // end
      kExprCallFunction, kStringFromUtf8Array,
    ]);

  let sig_a8_v = makeSig([], [wasmRefType(kArrayI8)]);
  builder.addFunction("encode_null_string", sig_a8_v)
    .exportFunc()
    .addBody([
        kExprRefNull, kExternRefCode,
        kExprCallFunction, kStringToUtf8Array,
      ]);

  let instance = builder.instantiate(kImports, kBuiltins);

  for (let str of interestingStrings) {
    let replaced = ReplaceIsolatedSurrogates(str);
    if (!HasIsolatedSurrogate(str)) assertEquals(str, replaced);
    let wtf8 = encodeWtf8(replaced);
    assertEquals(replaced,
                 instance.exports.encode_utf8(str, wtf8.length, 0));
    assertEquals(replaced,
                 instance.exports.encode_utf8(str, wtf8.length + 20, 10));
  }

  assertThrows(() => instance.exports.encode_null_string(),
               WebAssembly.RuntimeError, "illegal cast");
})();
