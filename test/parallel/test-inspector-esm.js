'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { resolve: UrlResolve } = require('url');
const fixtures = require('../common/fixtures');
const { NodeInstance } = require('../common/inspector-helper.js');

function assertScopeValues({ result }, expected) {
  const unmatched = new Set(Object.keys(expected));
  for (const actual of result) {
    const value = expected[actual.name];
    assert.strictEqual(actual.value.value, value);
    unmatched.delete(actual.name);
  }
  assert.deepStrictEqual(Array.from(unmatched.values()), []);
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
  await session.waitForBreakOnLine(
    0, UrlResolve(session.scriptURL().toString(), 'message.mjs'));
}

async function testBreakpoint(session) {
  console.log('[test]', 'Setting a breakpoint and verifying it is hit');
  const commands = [
    { 'method': 'Debugger.setBreakpointByUrl',
      'params': { 'lineNumber': 7,
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
  const paused = await session.waitForBreakOnLine(7, session.scriptURL());
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
  assertScopeValues(response, { t: 1001, k: 1, message: 'A message' });

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

  assert.strictEqual(result.value, 1002);

  result = (await session.send({
    'method': 'Runtime.evaluate', 'params': {
      'expression': '5 * 5'
    }
  })).result;
  assert.strictEqual(result.value, 25);
}

async function runTest() {
  const child = new NodeInstance(['--inspect-brk=0'], '',
                                 fixtures.path('es-modules/loop.mjs'));

  const session = await child.connectInspectorSession();
  await testBreakpointOnStart(session);
  await testBreakpoint(session);
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 55);
}

runTest().then(common.mustCall());
