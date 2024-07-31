// Flags: --expose-internals
'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
common.skipIf32Bits();
const { NodeInstance } = require('../common/inspector-helper.js');
const assert = require('assert');

common.skipIfWorker(); // Signal starts a server for a main thread inspector

const script = `
process._rawDebug('Waiting until a signal enables the inspector...');
let waiting = setInterval(waitUntilDebugged, 50);

function waitUntilDebugged() {
  const { internalBinding } = require('internal/test/binding');
  if (!internalBinding('inspector').isEnabled()) return;
  clearInterval(waiting);
  // At this point, even though the Inspector is enabled, the default async
  // call stack depth is 0. We need a chance to call
  // Debugger.setAsyncCallStackDepth *before* activating the actual timer for
  // async stack traces to work. Directly using a debugger statement would be
  // too brittle, and using a longer timeout would unnecessarily slow down the
  // test on most machines. Triggering a debugger break through an interval is
  // a faster and more reliable way.
  process._rawDebug('Signal received, waiting for debugger setup');
  waiting = setInterval(() => { debugger; }, 50);
}

// This function is called by the inspector client (session)
function setupTimeoutWithBreak() {
  clearInterval(waiting);
  process._rawDebug('Debugger ready, setting up timeout with a break');
  setTimeout(() => { debugger; }, 50);
}
`;

async function waitForInitialSetup(session) {
  console.error('[test]', 'Waiting for initial setup');
  await session.waitForBreakOnLine(16, '[eval]');
}

async function setupTimeoutForStackTrace(session) {
  console.error('[test]', 'Setting up timeout for async stack trace');
  await session.send([
    { 'method': 'Runtime.evaluate',
      'params': { expression: 'setupTimeoutWithBreak()' } },
    { 'method': 'Debugger.resume' },
  ]);
}

async function checkAsyncStackTrace(session) {
  console.error('[test]', 'Verify basic properties of asyncStackTrace');
  const paused = await session.waitForBreakOnLine(23, '[eval]');
  assert(paused.params.asyncStackTrace,
         `${Object.keys(paused.params)} contains "asyncStackTrace" property`);
  assert(paused.params.asyncStackTrace.description, 'Timeout');
  assert(paused.params.asyncStackTrace.callFrames
           .some((frame) => frame.functionName === 'setupTimeoutWithBreak'));
}

async function runTests() {
  const instance = await NodeInstance.startViaSignal(script);
  const session = await instance.connectInspectorSession();
  await session.send([
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setAsyncCallStackDepth',
      'params': { 'maxDepth': 10 } },
    { 'method': 'Debugger.setBlackboxPatterns',
      'params': { 'patterns': [] } },
  ]);

  await waitForInitialSetup(session);
  await setupTimeoutForStackTrace(session);
  await checkAsyncStackTrace(session);

  console.error('[test]', 'Stopping child instance');
  session.disconnect();
  instance.kill();
}

runTests().then(common.mustCall());
