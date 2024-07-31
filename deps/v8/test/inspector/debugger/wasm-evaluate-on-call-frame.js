// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-multi-memory

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test wasm debug evaluate');

Protocol.Debugger.enable();
Protocol.Runtime.enable();

async function compileModule(builder) {
  const moduleBytes = JSON.stringify(builder.toArray());
  const expression = `new WebAssembly.Module(new Uint8Array(${moduleBytes}).buffer);`;
  const {result: {scriptId}} = await Protocol.Runtime.compileScript({expression, sourceURL: 'v8://test/compileModule', persistScript: true});
  const [{params}, {result}] = await Promise.all([
    Protocol.Debugger.onceScriptParsed(),
    Protocol.Runtime.runScript({scriptId})
  ]);
  return [result.result, params.scriptId];
}

async function instantiateModule({objectId}, importObject) {
  const {result: {result}} = await Protocol.Runtime.callFunctionOn({
    arguments: importObject ? [importObject] : [],
    functionDeclaration: 'function(importObject) { return new WebAssembly.Instance(this, importObject); }',
    objectId
  });
  return result;
}

async function dumpOnCallFrame(callFrameId, expression) {
  const {result: {result: object}} = await Protocol.Debugger.evaluateOnCallFrame({
    callFrameId, expression
  });
  if ('customPreview' in object) {
    InspectorTest.log(
        `> ${expression} = ${JSON.stringify(object.customPreview)}`);
  } else if (object.type === 'object' && object.subtype === 'wasmvalue') {
    const {result: {result: properties}} = await Protocol.Runtime.getProperties({objectId: object.objectId, ownProperties: true})
    const valueProperty = properties.find(p => p.name === 'value');
    InspectorTest.log(`> ${expression} = ${object.description} {${valueProperty.value.description}}`);
  } else if ('description' in object) {
    InspectorTest.log(`> ${expression} = ${object.description}`);
  } else {
    InspectorTest.log(`> ${expression} = ${JSON.stringify(object.value)}`);
  }
}

async function dumpKeysOnCallFrame(callFrameId, object, keys) {
  for (const key of keys) {
    await dumpOnCallFrame(callFrameId, `${object}[${JSON.stringify(key)}]`);
    if (typeof key === 'string' && key.indexOf('.') < 0) {
      await dumpOnCallFrame(callFrameId, `${key}`);
    }
  }
}

async function waitForDebuggerPaused() {
  const {params: {callFrames: [{callFrameId, functionName}]}} = await Protocol.Debugger.oncePaused();
  InspectorTest.log(`Debugger paused in ${functionName}.`);
  return callFrameId;
}

InspectorTest.runAsyncTestSuite([
  async function testInstanceAndModule() {
    const builder = new WasmModuleBuilder();
    const main = builder.addFunction('main', kSig_v_v)
                        .addBody([]).exportFunc();

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    const callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `instance`);
    await dumpOnCallFrame(callFrameId, `module`);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testGlobals() {
    const builder = new WasmModuleBuilder();
    const global0 = builder.addGlobal(kWasmI32, true, false);  // global0
    const global1 =
      builder.addGlobal(kWasmI32, true, false).exportAs('global0');
    const global2 =
      builder.addGlobal(kWasmI64, true, false).exportAs('global3');
    const global3 = builder.addGlobal(kWasmI64, true, false);  // global3
    const main = builder.addFunction('main', kSig_v_v)
                        .addBody([
                          kExprI64Const, 42,
                          kExprGlobalSet, global3.index,
                        ]).exportFunc();
    const start = builder.addFunction('start', kSig_v_v)
                         .addBody([
                           kExprNop,
                           kExprI32Const, 0,
                           kExprGlobalSet, global0.index,
                           kExprI32Const, 1,
                           kExprGlobalSet, global1.index,
                           kExprI64Const, 2,
                           kExprGlobalSet, global2.index,
                           kExprI64Const, 3,
                           kExprGlobalSet, global3.index,
                         ]);
    builder.addStart(start.index);
    const KEYS = [0, 1, 2, 3, '$global0', '$global3'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `globals`);
    await dumpOnCallFrame(callFrameId, `typeof globals`);
    await dumpOnCallFrame(callFrameId, `Object.keys(globals)`);
    await dumpKeysOnCallFrame(callFrameId, "globals", KEYS);

    InspectorTest.log(`Stepping twice in main.`)
    await Protocol.Debugger.stepOver();  // i32.const 42
    await Protocol.Debugger.stepOver();  // global.set $global3
    callFrameId = await waitForDebuggerPaused();
    await dumpKeysOnCallFrame(callFrameId, "globals", KEYS);

    InspectorTest.log('Changing global from JavaScript.')
    await Protocol.Debugger.evaluateOnCallFrame({
      callFrameId,
      expression: `instance.exports.global0.value = 21`
    });
    await dumpKeysOnCallFrame(callFrameId, "globals", KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testFunctions() {
    const builder = new WasmModuleBuilder();
    builder.addImport('foo', 'bar', kSig_v_v);
    const main = builder.addFunction('main', kSig_i_v)
                        .addBody([
                          kExprI32Const, 0,
                        ]).exportFunc();
    builder.addFunction('func2', kSig_i_v)
           .addBody([
             kExprI32Const, 1,
           ]);
    builder.addFunction(undefined, kSig_i_v)
           .addBody([
             kExprI32Const, 2,
           ]).exportAs('func2');
    builder.addFunction(undefined, kSig_i_v)
           .addBody([
             kExprI32Const, 3,
           ]);
    const KEYS = [0, 1, 2, 3, 4, '$foo.bar', '$main', '$func2', '$func4'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const {result: { result: importObject }} = await Protocol.Runtime.evaluate({
      expression: `({foo: {bar() { }}})`
    });
    const instance = await instantiateModule(module, importObject);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `functions`);
    await dumpOnCallFrame(callFrameId, `typeof functions`);
    await dumpOnCallFrame(callFrameId, `Object.keys(functions)`);
    await dumpKeysOnCallFrame(callFrameId, "functions", KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testLocals() {
    const builder = new WasmModuleBuilder();
    const main = builder.addFunction('main', kSig_v_ii, ['x', 'x'])
                        .addLocals(kWasmI32, 1)
                        .addBody([
                          kExprI32Const, 42,
                          kExprLocalSet, 2
                        ]).exportFunc();
    const KEYS = [0, 1, 2, '$x', '$var2'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(3, 6); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `locals`);
    await dumpOnCallFrame(callFrameId, `typeof locals`);
    await dumpOnCallFrame(callFrameId, `Object.keys(locals)`);
    await dumpKeysOnCallFrame(callFrameId, "locals", KEYS);

    InspectorTest.log(`Stepping twice in main.`)
    await Protocol.Debugger.stepOver();  // i32.const 42
    await Protocol.Debugger.stepOver();  // local.set $var2
    callFrameId = await waitForDebuggerPaused();
    await dumpKeysOnCallFrame(callFrameId, "locals", KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testMemory() {
    const builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.exportMemoryAs('foo');
    const main = builder.addFunction('main', kSig_v_v)
                        .addBody([kExprNop]).exportFunc();
    const KEYS = [0, '$foo'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `memories`);
    await dumpOnCallFrame(callFrameId, `typeof memories`);
    await dumpOnCallFrame(callFrameId, `Object.keys(memories)`);
    await dumpKeysOnCallFrame(callFrameId, "memories", KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testMultipleMemories() {
    const builder = new WasmModuleBuilder();
    const mem0_idx = builder.addMemory(1, 1);
    const mem1_idx = builder.addMemory(7, 11);
    builder.exportMemoryAs('foo', mem0_idx);
    builder.exportMemoryAs('bar', mem1_idx);
    const main = builder.addFunction('main', kSig_v_v)
                        .addBody([kExprNop]).exportFunc();
    const KEYS = [0, '$foo', 1, '$bar'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, 'memories');
    await dumpOnCallFrame(callFrameId, 'memories.length');
    await dumpOnCallFrame(callFrameId, 'Object.keys(memories)');
    await dumpKeysOnCallFrame(callFrameId, 'memories', KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testTables() {
    const builder = new WasmModuleBuilder();
    builder.addTable(kWasmFuncRef, 2).exportAs('bar');
    const main = builder.addFunction('main', kSig_v_v)
                        .addBody([kExprNop]).exportFunc();
    const KEYS = [0, '$bar'];

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `tables`);
    await dumpOnCallFrame(callFrameId, `typeof tables`);
    await dumpOnCallFrame(callFrameId, `Object.keys(tables)`);
    await dumpKeysOnCallFrame(callFrameId, "tables", KEYS);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testStack() {
    const builder = new WasmModuleBuilder();
    const main = builder.addFunction('main', kSig_i_i)
                        .addBody([
                          kExprLocalGet, 0,
                          kExprI32Const, 42,
                          kExprI32Add,
                        ]).exportFunc();

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint({
      location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(5); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `stack`);
    await dumpOnCallFrame(callFrameId, `typeof stack`);
    await dumpOnCallFrame(callFrameId, `Object.keys(stack)`);

    InspectorTest.log(`Stepping twice in main.`)
    await Protocol.Debugger.stepOver();  // local.get $var0
    await Protocol.Debugger.stepOver();  // i32.const 42
    callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `stack`);
    await dumpOnCallFrame(callFrameId, `Object.keys(stack)`);
    await dumpKeysOnCallFrame(callFrameId, "stack", [0, 1]);
    await Protocol.Debugger.resume();
    await callMainPromise;
  },

  async function testCustomFormatters() {
    const builder = new WasmModuleBuilder();
    const main = builder.addFunction('main', kSig_i_i, ['x'])
                     .addBody([
                       kExprLocalGet,
                       0,
                     ])
                     .exportFunc();

    InspectorTest.log('Compile module.');
    const [module, scriptId] = await compileModule(builder);

    await Protocol.Runtime.enable();
    InspectorTest.log('Set breakpoint in main.');
    await Protocol.Debugger.setBreakpoint(
        {location: {scriptId, lineNumber: 0, columnNumber: main.body_offset}});

    InspectorTest.log('Install custom formatter.');
    await Protocol.Runtime.onConsoleAPICalled(m => InspectorTest.logMessage(m));
    await Protocol.Runtime.setCustomObjectFormatterEnabled({enabled: true});
    await Protocol.Runtime.evaluate({
      expression: `this.devtoolsFormatters = [{
        header: function(obj) { return ["div", {}, JSON.stringify(obj)]; },
        hasBody: function() { return false; }
      }]`
    });

    InspectorTest.log('Instantiate module.');
    const instance = await instantiateModule(module);

    InspectorTest.log('Call main.');
    const callMainPromise = Protocol.Runtime.callFunctionOn({
      functionDeclaration: `function() { return this.exports.main(5); }`,
      objectId: instance.objectId
    });
    let callFrameId = await waitForDebuggerPaused();
    await dumpOnCallFrame(callFrameId, `locals`);
    await dumpOnCallFrame(callFrameId, `Object.keys(locals)`);
    await dumpKeysOnCallFrame(callFrameId, 'locals', [0, '$x']);

    await Protocol.Debugger.resume();
    await callMainPromise;
  }
]);
