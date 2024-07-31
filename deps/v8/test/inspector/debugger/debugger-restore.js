// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-stress-incremental-marking

const {session, contextGroup, Protocol} = InspectorTest.start('Checks that Debugger agent correctly restore its state.');

const line = '// ' + '-'.repeat(512);
const script = `
  ${line}
  ${line}
  console.log('Hello from script');
`;

function reconnect() {
  InspectorTest.log('will reconnect...');
  session.reconnect();
}

InspectorTest.runAsyncTestSuite([
  async function testMaxScriptsCacheSize() {
    InspectorTest.log('Enable debugger agent with script cache of 4k');
    await Protocol.Debugger.enable({ maxScriptsCacheSize: 4096 }); // 'script' fits into cache.

    reconnect();

    InspectorTest.log('Evaluate large script');
    contextGroup.addScript(script);
    const { params: { scriptId } } = await Protocol.Debugger.onceScriptParsed();

    InspectorTest.log('Trigger GC');
    await Protocol.HeapProfiler.collectGarbage();

    InspectorTest.log('Request script content...');
    const result = await Protocol.Debugger.getScriptSource({ scriptId });
    if (result.error) {
      InspectorTest.log('Failed to retrieve script content');
      return;
    }
    InspectorTest.log(`Received script content of length: ${result.result.scriptSource.length}`);
  },
]);
