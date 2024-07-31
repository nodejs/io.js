// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --log-function-events

let sampleCollected = false;

function OnProfilerSampleCallback(profile) {
  profile = profile.replaceAll('\\', '/');
  profile = JSON.parse(profile);
  let functionNames = profile.nodes.map(n => n.callFrame.functionName);
  for (let i = 0; i < functionNames.length; ++i) {
    if (functionNames[i].startsWith('js-to-wasm')) {
      assertTrue(functionNames[i + 1].startsWith('f'));
      assertTrue(functionNames[i + 2].startsWith('wasm-to-js'));
      assertTrue(functionNames[i + 3].startsWith('g'));
      // {sampleCollected} is set at the end because the asserts above don't
      // show up in the test runner, probably because this function is called as
      // a callback from d8.
      sampleCollected = true;
      return;
    }
  }
  assertUnreachable();
}
d8.profiler.setOnProfileEndListener(OnProfilerSampleCallback);

function Asm(stdlib, imports, buffer) {
  "use asm";
  var g = imports.g;

  function f(i) {
    i = i|0;
    return g() | 0;
  }

  return { f: f };
}

var heap = new ArrayBuffer(64*1024);

function g() {
  d8.profiler.triggerSample();
  console.profileEnd();
  return 42;
}
var asm = Asm(this, {g: g}, heap);
assertTrue(%IsAsmWasmCode(Asm));

console.profile();
asm.f(3);
assertTrue(sampleCollected);
