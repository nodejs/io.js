'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { NodeInstance } = require('../common/inspector-helper.js');

function checkListResponse(response) {
  const expectedLength = 1;
  assert.strictEqual(
    response.length,
    expectedLength,
    `Expected response length ${response.length} to be ${expectedLength}.`
  );
  assert.ok(response[0].devtoolsFrontendUrl);
  assert.ok(
    /ws:\/\/localhost:\d+\/[0-9A-Fa-f]{8}-/
      .test(response[0].webSocketDebuggerUrl),
    response[0].webSocketDebuggerUrl);
}

function checkVersion(response) {
  assert.ok(response);
  const expected = {
    'Browser': `node.js/${process.version}`,
    'Protocol-Version': '1.1',
  };
  assert.strictEqual(JSON.stringify(response),
                     JSON.stringify(expected));
}

function checkBadPath(err) {
  assert(err instanceof SyntaxError);
  assert.match(err.message, /Unexpected token/);
  assert.match(err.body, /WebSockets request was expected/);
}

function checkException(message) {
  assert.strictEqual(message.exceptionDetails, undefined);
}

function assertScopeValues({ result }, expected) {
  const unmatched = new Set(Object.keys(expected));
  for (const actual of result) {
    const value = expected[actual.name];
    if (value) {
      assert.strictEqual(
        actual.value.value,
        value,
        `Expected scope values to be ${actual.value.value} instead of ${value}.`
      );
      unmatched.delete(actual.name);
    }
  }
  if (unmatched.size)
    assert.fail(Array.from(unmatched.values()));
}

async function testBreakpointOnStart(session) {
  console.log('[test]',
              'Verifying debugger stops on start (--inspect-brk option)');
  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setPauseOnExceptions',
      'params': { 'state': 'none' } },
    { 'method': 'Debugger.setAsyncCallStackDepth',
      'params': { 'maxDepth': 0 } },
    { 'method': 'Profiler.enable' },
    { 'method': 'Profiler.setSamplingInterval',
      'params': { 'interval': 100 } },
    { 'method': 'Debugger.setBlackboxPatterns',
      'params': { 'patterns': [] } },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send(commands);
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForBreakOnLine(0, session.scriptURL());
}

async function testBreakpoint(session) {
  console.log('[test]', 'Setting a breakpoint and verifying it is hit');
  const commands = [
    { 'method': 'Debugger.setBreakpointByUrl',
      'params': { 'lineNumber': 5,
                  'url': session.scriptURL(),
                  'columnNumber': 0,
                  'condition': '' } },
    { 'method': 'Debugger.resume' },
  ];
  await session.send(commands);
  const { scriptSource } = await session.send({
    'method': 'Debugger.getScriptSource',
    'params': { 'scriptId': session.mainScriptId },
  });
  assert(scriptSource && (scriptSource.includes(session.script())),
         `Script source is wrong: ${scriptSource}`);

  await session.waitForConsoleOutput('log', ['A message', 5]);
  const paused = await session.waitForBreakOnLine(5, session.scriptURL());
  const scopeId = paused.params.callFrames[0].scopeChain[0].object.objectId;

  console.log('[test]', 'Verify we can read current application state');
  const response = await session.send({
    'method': 'Runtime.getProperties',
    'params': {
      'objectId': scopeId,
      'ownProperties': false,
      'accessorPropertiesOnly': false,
      'generatePreview': true
    }
  });
  assertScopeValues(response, { t: 1001, k: 1 });

  let { result } = await session.send({
    'method': 'Debugger.evaluateOnCallFrame', 'params': {
      'callFrameId': session.pausedDetails().callFrames[0].callFrameId,
      'expression': 'k + t',
      'objectGroup': 'console',
      'includeCommandLineAPI': true,
      'silent': false,
      'returnByValue': false,
      'generatePreview': true
    }
  });
  const expectedEvaluation = 1002;
  assert.strictEqual(
    result.value,
    expectedEvaluation,
    `Expected evaluation to be ${expectedEvaluation}, got ${result.value}.`
  );

  result = (await session.send({
    'method': 'Runtime.evaluate', 'params': {
      'expression': '5 * 5'
    }
  })).result;
  const expectedResult = 25;
  assert.strictEqual(
    result.value,
    expectedResult,
    `Expected Runtime.evaluate to be ${expectedResult}, got ${result.value}.`
  );
}

async function testI18NCharacters(session) {
  console.log('[test]', 'Verify sending and receiving UTF8 characters');
  const chars = 'טֶ字и';
  session.send({
    'method': 'Debugger.evaluateOnCallFrame', 'params': {
      'callFrameId': session.pausedDetails().callFrames[0].callFrameId,
      'expression': `console.log("${chars}")`,
      'objectGroup': 'console',
      'includeCommandLineAPI': true,
      'silent': false,
      'returnByValue': false,
      'generatePreview': true
    }
  });
  await session.waitForConsoleOutput('log', [chars]);
}

async function testCommandLineAPI(session) {
  const testModulePath = require.resolve('../fixtures/empty.js');
  const testModuleStr = JSON.stringify(testModulePath);
  const printAModulePath = require.resolve('../fixtures/printA.js');
  const printAModuleStr = JSON.stringify(printAModulePath);
  const printBModulePath = require.resolve('../fixtures/printB.js');
  const printBModuleStr = JSON.stringify(printBModulePath);

  // We can use `require` outside of a callframe with require in scope
  let result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': 'typeof require("fs").readFile === "function"',
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.strictEqual(result.result.value, true);

  // The global require has the same properties as a normal `require`
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': [
          'typeof require.resolve === "function"',
          'typeof require.extensions === "object"',
          'typeof require.cache === "object"',
        ].join(' && '),
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.strictEqual(result.result.value, true);
  // `require` twice returns the same value
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        // 1. We require the same module twice
        // 2. We mutate the exports so we can compare it later on
        'expression': `
          Object.assign(
            require(${testModuleStr}),
            { old: 'yes' }
          ) === require(${testModuleStr})`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.strictEqual(result.result.value, true);
  // After require the module appears in require.cache
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': `JSON.stringify(
          require.cache[${testModuleStr}].exports
        )`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.deepStrictEqual(JSON.parse(result.result.value),
                         { old: 'yes' });
  // Remove module from require.cache
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': `delete require.cache[${testModuleStr}]`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.strictEqual(result.result.value, true);
  // Require again, should get fresh (empty) exports
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': `JSON.stringify(require(${testModuleStr}))`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.deepStrictEqual(JSON.parse(result.result.value), {});
  // require 2nd module, exports an empty object
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': `JSON.stringify(require(${printAModuleStr}))`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.deepStrictEqual(JSON.parse(result.result.value), {});
  // Both modules end up with the same module.parent
  result = await session.send(
    {
      'method': 'Runtime.evaluate', 'params': {
        'expression': `JSON.stringify({
          parentsEqual:
            require.cache[${testModuleStr}].parent ===
            require.cache[${printAModuleStr}].parent,
          parentId: require.cache[${testModuleStr}].parent.id,
        })`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.deepStrictEqual(JSON.parse(result.result.value), {
    parentsEqual: true,
    parentId: '<inspector console>'
  });
  // The `require` in the module shadows the command line API's `require`
  result = await session.send(
    {
      'method': 'Debugger.evaluateOnCallFrame', 'params': {
        'callFrameId': session.pausedDetails().callFrames[0].callFrameId,
        'expression': `(
          require(${printBModuleStr}),
          require.cache[${printBModuleStr}].parent.id
        )`,
        'includeCommandLineAPI': true
      }
    });
  checkException(result);
  assert.notStrictEqual(result.result.value,
                        '<inspector console>');
}

async function runTest() {
  const child = new NodeInstance();
  checkListResponse(await child.httpGet(null, '/json'));
  checkListResponse(await child.httpGet(null, '/json/list'));
  checkVersion(await child.httpGet(null, '/json/version'));

  await child.httpGet(null, '/json/activate').catch(checkBadPath);
  await child.httpGet(null, '/json/activate/boom').catch(checkBadPath);
  await child.httpGet(null, '/json/badpath').catch(checkBadPath);

  const session = await child.connectInspectorSession();
  await testBreakpointOnStart(session);
  await testBreakpoint(session);
  await testI18NCharacters(session);
  await testCommandLineAPI(session);
  await session.runToCompletion();
  const expectedExitCode = 55;
  const { exitCode } = await child.expectShutdown();
  assert.strictEqual(
    exitCode,
    expectedExitCode,
    `Expected exit code to be ${expectedExitCode} but got ${expectedExitCode}.`
  );
}

runTest().then(common.mustCall());
