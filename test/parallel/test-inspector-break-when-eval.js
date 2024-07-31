'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
const assert = require('assert');
const { NodeInstance } = require('../common/inspector-helper.js');
const fixtures = require('../common/fixtures');
const { pathToFileURL } = require('url');

// This needs to be an ES module file to ensure that internal modules are
// loaded before pausing. See
// https://bugs.chromium.org/p/chromium/issues/detail?id=1246905
const script = fixtures.path('inspector-global-function.mjs');

async function setupDebugger(session) {
  console.log('[test]', 'Setting up a debugger');
  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setAsyncCallStackDepth',
      'params': { 'maxDepth': 0 } },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send(commands);
  await session.send({ method: 'NodeRuntime.disable' });

  await session.waitForNotification('Debugger.paused', 'Initial pause');

  // NOTE(mmarchini): We wait for the second console.log to ensure we loaded
  // every internal module before pausing. See
  // https://bugs.chromium.org/p/chromium/issues/detail?id=1246905
  const waitForReady = session.waitForConsoleOutput('log', 'Ready!');
  session.send({ 'method': 'Debugger.resume' });
  await waitForReady;
}

async function breakOnLine(session) {
  console.log('[test]', 'Breaking in the code');
  const commands = [
    { 'method': 'Debugger.setBreakpointByUrl',
      'params': { 'lineNumber': 9,
                  'url': pathToFileURL(script).toString(),
                  'columnNumber': 0,
                  'condition': '' } },
    { 'method': 'Runtime.evaluate',
      'params': { 'expression': 'sum()',
                  'objectGroup': 'console',
                  'includeCommandLineAPI': true,
                  'silent': false,
                  'contextId': 1,
                  'returnByValue': false,
                  'generatePreview': true,
                  'userGesture': true,
                  'awaitPromise': false } },
  ];
  session.send(commands);
  await session.waitForBreakOnLine(9, pathToFileURL(script).toString());
}

async function stepOverConsoleStatement(session) {
  console.log('[test]', 'Step over console statement and test output');
  session.send({ 'method': 'Debugger.stepOver' });
  await session.waitForConsoleOutput('log', [0, 3]);
  await session.waitForNotification('Debugger.paused');
}

async function runTests() {
  // NOTE(mmarchini): Use --inspect-brk to improve avoid undeterministic
  // behavior.
  const child = new NodeInstance(['--inspect-brk=0'], undefined, script);
  const session = await child.connectInspectorSession();
  await setupDebugger(session);
  await breakOnLine(session);
  await stepOverConsoleStatement(session);
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTests().then(common.mustCall());
