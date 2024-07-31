// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertInvalid(fn, message) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError, message);
}

assertInvalid(
  builder => builder.addLiteralStringRef("foo"),
  /unexpected section <StringRef> \(enable with --experimental-wasm-stringref\)/);

let enableMessage = 'enable with --experimental-wasm-stringref'

for (let [name, code] of [['string', kStringRefCode],
                          ['stringview_wtf8', kStringViewWtf8Code],
                          ['stringview_wtf16', kStringViewWtf16Code],
                          ['stringview_iter', kStringViewIterCode]]) {
  let message = new RegExp(`invalid value type '${name}ref', ${enableMessage}`);
  let default_init = [kExprRefNull, code];

  assertInvalid(b => b.addType(makeSig([code], [])), message);
  assertInvalid(b => b.addStruct([makeField(code, true)]), message);
  assertInvalid(b => b.addArray(code, true), message);
  assertInvalid(b => b.addType(makeSig([], [code])), message);
  assertInvalid(b => b.addGlobal(code, true, false, default_init), message);
  assertInvalid(b => b.addTable(code, 0), message);
  assertInvalid(b => b.addPassiveElementSegment([default_init], code), message);
  assertInvalid(b => b.addTag(makeSig([code], [])), message);
  assertInvalid(
    b => b.addFunction(undefined, kSig_v_v).addLocals(code, 1).addBody([]),
    message);
}
