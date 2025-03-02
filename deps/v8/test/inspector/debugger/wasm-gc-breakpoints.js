// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

utils.load('test/inspector/wasm-inspector-test.js');

const {session, contextGroup, Protocol} =
    InspectorTest.start('Tests GC object inspection.');
session.setupScriptMap();

const module_bytes = [
  0x00, 0x61, 0x73, 0x6d, 1, 0, 0, 0,  // wasm magic

  0x01,  // type section
  0x19,  // section length
  0x01,  // number of type section entries
  0x4e,  // recursive type group
  0x04,  // number of types in the recursive group
  // type 0: struct $StrA (field ($byte i8) ($word i16) ($pointer (ref $StrB)))
  0x5f,  // struct
  0x03,  // field count
  0x78, 0x01,  // mut i8
  0x77, 0x00,  // i16
  0x64, 0x01, 0x01,  // mut ref $StrB
  // type 1: struct $StrB (field ($next (ref null $StrA)))
  0x5f,  // struct
  0x01,  // field count
  0x63, 0x00, 0x01,  // mut ref null $StrA
  // type 2: array $ArrC (mut (ref null $StrA))
  0x5e,  // array
  0x63, 0x00, 0x01,  // mut ref null $StrA
  // type 3: func
  0x60,  // signature
  0x01, 0x67,  // 1 param: stringref
  0x00,  // number of results

  0x03,  // function section
  0x02,  // section length
  0x01,  // number of functions
  0x03,  // function 0: signature 3

  // This is just so that function index 0 counts as declared.
  0x06,  // global section
  0x07,  // section length
  0x01,  // number of globals
  0x63, 0x03,  // type of global: ref null $sig3
  0x00,  // immutable
  0xd2, 0x00, 0x0b,  // initializer: ref.func $func1; end

  0x07,  // export section
  0x08,  // section length
  0x01,  // number of exports
  0x04,  // length of "main"
  0x6d, 0x61, 0x69, 0x6e,  // "main"
  0x00,  // kind: function
  0x00,  // index: 0

  /////////////////////////// CODE SECTION //////////////////////////
  0x0a,  // code section
  0x2f,  // section length
  0x01,  // number of functions

  0x2d,  // function 0: size
  0x02,  // number of locals
  0x01, 0x63, 0x00,  // (local $varA (ref null $StrA))
  0x01, 0x63, 0x02,  // (local $varC (ref null $ArrC))
  // $varA := new $StrA(127, 32767, new $StrB(null))
  0x41, 0xFF, 0x00,  // i32.const 127
  0x41, 0xFF, 0xFF, 0x01,  // i32.const 32767
  0xfb, 0x01, 0x01,  // struct.new_default $StrB
  0xfb, 0x00, 0x00,  // struct.new $StrA
  0x22, 0x01,  // local.tee $varA
  // $varA.$pointer.$next = $varA
  0xfb, 0x02, 0x00, 0x02,  // struct.get $StrA $pointer
  0x20, 0x01,  // local.get $varA
  0xfb, 0x05, 0x01, 0x00,  // struct.set $StrB $next
  // $varC := new $ArrC($varA)
  0x20, 0x01,  // local.get $varA -- value
  0x41, 0x01,  // i32.const 1 -- length
  0xfb, 0x06, 0x02,  // array.new $ArrC
  0x20, 0x00,  // local.get 0
  0x1a,  // drop
  0x21, 0x02,  // local.set $varC
  0x0b,  // end

  /////////////////////////// NAME SECTION //////////////////////////
  0x00,  // name section
  0xd4, 0x01, // section length
  0x04,  // length of "name"
  0x6e, 0x61, 0x6d, 0x65,  // "name"

  0x02,  // "local names" subsection
  0x0f,  // length of subsection
  0x01,  // number of entries
  0x00,  // for function 0
  0x02,  // number of entries for function 0
  0x01,  // local index
  0x04,  // length of "varA"
  0x76, 0x61, 0x72, 0x41,  // "varA"
  0x02,  // local index
  0x04,  // length of "varB"
  0x76, 0x61, 0x72, 0x42,  // "varB"

  0x04,  // "type names" subsection
  0x99, 0x01,  // length of subsection
  0x03,  // number of entries
  0x00,  // type index
  0x04,  // name length
  0x53, 0x74, 0x72, 0x41,  // "StrA"
  0x01,  // type index
  0x89, 0x01,  // name length
  // Called "$StrB" in other comments, actual name:
  // "veryLongNameWithMoreThanOneHundredAndTwentyEightCharactersToTestThat
  // WeAreHandlingStringBufferOverflowWithoutCrashing_ThisWontGetTruncated"
  0x76, 0x65, 0x72, 0x79, 0x4c, 0x6f, 0x6e, 0x67, 0x4e, 0x61, 0x6d, 0x65, 0x57,
  0x69, 0x74, 0x68, 0x4d, 0x6f, 0x72, 0x65, 0x54, 0x68, 0x61, 0x6e, 0x4f, 0x6e,
  0x65, 0x48, 0x75, 0x6e, 0x64, 0x72, 0x65, 0x64, 0x41, 0x6e, 0x64, 0x54, 0x77,
  0x65, 0x6e, 0x74, 0x79, 0x45, 0x69, 0x67, 0x68, 0x74, 0x43, 0x68, 0x61, 0x72,
  0x61, 0x63, 0x74, 0x65, 0x72, 0x73, 0x54, 0x6f, 0x54, 0x65, 0x73, 0x74, 0x54,
  0x68, 0x61, 0x74, 0x57, 0x65, 0x41, 0x72, 0x65, 0x48, 0x61, 0x6e, 0x64, 0x6c,
  0x69, 0x6e, 0x67, 0x53, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x42, 0x75, 0x66, 0x66,
  0x65, 0x72, 0x4f, 0x76, 0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x57, 0x69, 0x74,
  0x68, 0x6f, 0x75, 0x74, 0x43, 0x72, 0x61, 0x73, 0x68, 0x69, 0x6e, 0x67, 0x5f,
  0x54, 0x68, 0x69, 0x73, 0x57, 0x6f, 0x6e, 0x74, 0x47, 0x65, 0x74, 0x54, 0x72,
  0x75, 0x6e, 0x63, 0x61, 0x74, 0x65, 0x64,
  0x02,  // type index
  0x04,  // name length
  0x41, 0x72, 0x72, 0x43,  // "ArrC"

  0x0a,  // "field names" subsection
  0x20,  // length of subsection
  0x02,  // number of types
  0x00,  // for type $StrA
  0x03,  // number of entries for $StrA
  0x00,  // field index 0
  0x04,  // length of "byte"
  0x62, 0x79, 0x74, 0x65,  // "byte"
  0x01,  // field index 1
  0x04,  // length of "word"
  0x77, 0x6f, 0x72, 0x64,  // "word"
  0x02,  // field index 2
  0x07,  // length of "pointer"
  0x70, 0x6f, 0x69, 0x6e, 0x74, 0x65, 0x72,  // "pointer"
  0x01,  // for type $StrB
  0x01,  // number of entries for $StrB
  0x00,  // field index
  0x04,  // length of "next"
  0x6e, 0x65, 0x78, 0x74,  // "next"
];

const getResult = msg => msg.result || InspectorTest.logMessage(msg);

function setBreakpoint(offset, scriptId, scriptUrl) {
  InspectorTest.log(
      'Setting breakpoint at offset ' + offset + ' on script ' + scriptUrl);
  return Protocol.Debugger
      .setBreakpoint({
        'location':
            {'scriptId': scriptId, 'lineNumber': 0, 'columnNumber': offset}
      })
      .then(getResult);
}

Protocol.Debugger.onPaused(async msg => {
  let loc = msg.params.callFrames[0].location;
  InspectorTest.log('Paused:');
  await session.logSourceLocation(loc);
  InspectorTest.log('Scope:');
  for (var frame of msg.params.callFrames) {
    var functionName = frame.functionName || '(anonymous)';
    var lineNumber = frame.location.lineNumber;
    var columnNumber = frame.location.columnNumber;
    InspectorTest.log(`at ${functionName} (${lineNumber}:${columnNumber}):`);
    if (!/^wasm/.test(session.getCallFrameUrl(frame))) {
      InspectorTest.log('   -- skipped');
      continue;
    }
    for (var scope of frame.scopeChain) {
      InspectorTest.logObject(' - scope (' + scope.type + '):');
      var { objectId } = scope.object;
      if (scope.type == 'wasm-expression-stack') {
        objectId = (await Protocol.Runtime.callFunctionOn({
          functionDeclaration: 'function() { return this.stack }',
          objectId
        })).result.result.objectId;
      }
      var properties =
          await Protocol.Runtime.getProperties({objectId});
      await WasmInspectorTest.dumpScopeProperties(properties);
      if (scope.type === 'wasm-expression-stack' || scope.type === 'local') {
        for (var value of properties.result.result) {
          var details = await Protocol.Runtime.getProperties(
              {objectId: value.value.objectId});
          var nested_value =
              details.result.result.find(({name}) => name === 'value');
          if (!nested_value.value.objectId) continue;
          details = await Protocol.Runtime.getProperties(
              {objectId: nested_value.value.objectId});
          InspectorTest.log('     object details:');
          await WasmInspectorTest.dumpScopeProperties(details);
        }
      }
    }
  }

  Protocol.Debugger.resume();
});

InspectorTest.runAsyncTestSuite([
  async function test() {
    await Protocol.Runtime.enable();
    await Protocol.Debugger.enable();
    InspectorTest.log('Instantiating.');
    // Spawn asynchronously:
    WasmInspectorTest.instantiate(module_bytes);
    InspectorTest.log(
        'Waiting for wasm script (ignoring first non-wasm script).');
    // Ignore javascript and full module wasm script, get scripts for functions.
    const [, {params: wasm_script}] =
        await Protocol.Debugger.onceScriptParsed(2);
    let offset = 103;  // "drop" before "local.set $varC" at the end.
    await setBreakpoint(offset, wasm_script.scriptId, wasm_script.url);
    InspectorTest.log('Calling main()');
    await WasmInspectorTest.evalWithUrl('instance.exports.main("hello world")',
                                        'runWasm');
    InspectorTest.log('exports.main returned!');
  }
]);
