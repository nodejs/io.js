// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming

// Because of https://crbug.com/v8/10748 we skip pumping the message loop
// with --stress-incremental-marking, which makes this test fail. Thus disable
// that stress mode here.
// Flags: --no-stress-incremental-marking

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Tests reported code offsets on wasm scripts');

// Include a sentinel in every module to avoid reusing a cached module.
let sentinel = 0;

function addHeader(binary, num_functions) {
  binary.emit_header();

  // Add a custom section with the sentinel.
  var custom_section = new Binary();
  custom_section.emit_u8(0);               // section code
  custom_section.emit_u8(10);              // section length
  custom_section.emit_string('sentinel');  // name
  custom_section.emit_bytes([sentinel]);   // payload
  ++sentinel;
  binary.emit_bytes(custom_section.trunc_buffer());

  // Type section with a single function type with no params and no returns.
  binary.emit_section(kTypeSectionCode, section => {
    section.emit_u32v(1);  // count
    section.emit_u8(kWasmFunctionTypeForm);
    section.emit_u32v(0);  // params
    section.emit_u32v(0);  // results
  });

  // Function section with {num_functions} many functions.
  binary.emit_section(kFunctionSectionCode, section => {
    section.emit_u32v(num_functions);
    for (let i = 0; i < num_functions; ++i) {
      section.emit_u32v(0);  // type index
    }
  });
}

function createModuleWithNoCodeSection() {
  let binary = new Binary;
  addHeader(binary, 0);
  return binary.trunc_buffer();
}

function createModuleWithEmptyCodeSection() {
  let binary = new Binary;
  addHeader(binary, 0);

  // Section code.
  binary.emit_u8(kCodeSectionCode);
  // Section length (1).
  binary.emit_u32v(1);
  // Payload (functions count: 0).
  binary.emit_u32v(0);

  return binary.trunc_buffer();
}

function createModuleWithFiveByteSectionLength() {
  let binary = new Binary;
  addHeader(binary, 1);

  // Section code.
  binary.emit_u8(kCodeSectionCode);
  // Section length (4 as 5-byte LEB).
  binary.emit_bytes([0x84, 0x80, 0x80, 0x80, 0x00]);
  binary.emit_u32v(1);                // functions count
  binary.emit_u32v(2);                // body size
  binary.emit_u32v(0);                // num locals
  binary.emit_bytes([kExprEnd]);      // body

  return binary.trunc_buffer();
}

function createModuleWithFiveBytePayload() {
  let binary = new Binary;
  addHeader(binary, 1);

  // Section code.
  binary.emit_u8(kCodeSectionCode);
  // Section length (8).
  binary.emit_bytes([0x88, 0x80, 0x80, 0x80, 0x00]);
  // Functions count (1 as 5-byte LEB).
  binary.emit_bytes([0x81, 0x80, 0x80, 0x80, 0x00]);
  binary.emit_u32v(2);                // body size
  binary.emit_u32v(0);                // num locals
  binary.emit_bytes([kExprEnd]);      // body

  return binary.trunc_buffer();
}

function compileSync(bytes) {
  new WebAssembly.Module(new Uint8Array(bytes));
}
function compileAsync(bytes) {
  WebAssembly.compile(new Uint8Array(bytes));
}

contextGroup.addScript(
    `${compileSync}${compileAsync}`, 0, 0, 'v8://test/compileFunctions');

InspectorTest.runAsyncTestSuite([
  async function test() {
    Protocol.Debugger.enable();
    let script_ids = new Map();
    let generators = [
      createModuleWithNoCodeSection, createModuleWithEmptyCodeSection,
      createModuleWithFiveByteSectionLength, createModuleWithFiveBytePayload
    ];
    for (let generator of generators) {
      session.Protocol.Runtime.evaluate({
        'expression': `
          compileSync([${generator()}]);
          compileAsync([${generator()}]);
        `
      });

      // Wait for both wasm scripts to be there and print their information.
      for (let wasm_scripts = 0; wasm_scripts < 2;) {
        ({params} = await Protocol.Debugger.onceScriptParsed());
        if (!params.url.startsWith('wasm://')) continue;
        if (!script_ids.has(params.scriptId)) {
          script_ids.set(params.scriptId, script_ids.size);
        }
        // Print script IDs to ensure that script are not deduplicated (via
        // cache).
        let stable_id = script_ids.get(params.scriptId);
        InspectorTest.log(`Wasm script parsed: ID ${stable_id}, startColumn: ${
            params.startColumn}, endColumn: ${params.endColumn}, codeOffset: ${
            params.codeOffset}`);
        ++wasm_scripts;
      }
    }
  }
]);
