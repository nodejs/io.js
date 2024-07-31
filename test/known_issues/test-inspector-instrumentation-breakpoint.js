// This test validates inspector's Debugger.setInstrumentationBreakpoint method.
// Refs: https://github.com/nodejs/node/issues/31138

'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { resolve: UrlResolve } = require('url');
const fixtures = require('../common/fixtures');
const { NodeInstance } = require('../common/inspector-helper.js');

async function testBreakpointBeforeScriptExecution(session) {
  console.log('[test]',
              'Verifying debugger stops on start of each script ' +
    '(Debugger.setInstrumentationBreakpoint with beforeScriptExecution)');
  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setInstrumentationBreakpoint',
      'params': { 'instrumentation': 'beforeScriptExecution' } },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];

  await session.send(commands);

  // Break on start
  await session.waitForBreakOnLine(
    0, UrlResolve(session.scriptURL().toString(), 'main.js'));
  await session.send([{ 'method': 'Debugger.resume' }]);

  // Script loaded
  await session.waitForBreakOnLine(
    0, UrlResolve(session.scriptURL().toString(), 'main.js'));
  await session.send([{ 'method': 'Debugger.resume' }]);

  // Script loaded
  await session.waitForBreakOnLine(
    0, UrlResolve(session.scriptURL().toString(), 'dep.js'));
  await session.send([{ 'method': 'Debugger.resume' }]);
}

async function runTest() {
  const main = fixtures.path('inspector-instrumentation-breakpoint', 'main.js');
  const child = new NodeInstance(['--inspect-brk=0'], '', main);

  const session = await child.connectInspectorSession();
  await testBreakpointBeforeScriptExecution(session);
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTest();
