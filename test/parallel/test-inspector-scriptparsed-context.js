'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
const { NodeInstance } = require('../common/inspector-helper.js');
const assert = require('assert');

const script = `
  'use strict';
  const assert = require('assert');
  const vm = require('vm');
  global.outer = true;
  global.inner = false;
  const context = vm.createContext({
    outer: false,
    inner: true
  });
  const script = new vm.Script("outer");
  debugger;

  assert.strictEqual(script.runInThisContext(), true);
  assert.strictEqual(script.runInContext(context), false);
  debugger;

  vm.runInContext('inner', context);
  debugger;

  vm.runInNewContext('Array', {});
  debugger;

  vm.runInNewContext('debugger', {});
`;

async function getContext(session) {
  const created =
      await session.waitForNotification('Runtime.executionContextCreated');
  return created.params.context;
}

async function checkScriptContext(session, context) {
  const scriptParsed =
      await session.waitForNotification('Debugger.scriptParsed');
  assert.strictEqual(scriptParsed.params.executionContextId, context.id);
}

async function runTests() {
  const instance = new NodeInstance(['--inspect-brk=0', '--expose-internals'],
                                    script);
  const session = await instance.connectInspectorSession();

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send({ 'method': 'Debugger.enable' });
  await session.send({ method: 'Runtime.runIfWaitingForDebugger' });
  await session.send({ method: 'NodeRuntime.disable' });

  await session.waitForBreakOnLine(2, '[eval]');

  await session.send({ 'method': 'Runtime.enable' });
  await getContext(session);
  await session.send({ 'method': 'Debugger.resume' });
  const childContext = await getContext(session);
  await session.waitForBreakOnLine(11, '[eval]');

  console.error('[test]', 'Script is unbound');
  await session.send({ 'method': 'Debugger.resume' });
  await session.waitForBreakOnLine(15, '[eval]');

  console.error('[test]', 'vm.runInContext associates script with context');
  await session.send({ 'method': 'Debugger.resume' });
  await checkScriptContext(session, childContext);
  await session.waitForBreakOnLine(18, '[eval]');

  console.error('[test]', 'vm.runInNewContext associates script with context');
  await session.send({ 'method': 'Debugger.resume' });
  const thirdContext = await getContext(session);
  await checkScriptContext(session, thirdContext);
  await session.waitForBreakOnLine(21, '[eval]');

  console.error('[test]', 'vm.runInNewContext can contain debugger statements');
  await session.send({ 'method': 'Debugger.resume' });
  const fourthContext = await getContext(session);
  await checkScriptContext(session, fourthContext);
  await session.waitForBreakOnLine(0, 'evalmachine.<anonymous>');

  await session.runToCompletion();
  assert.strictEqual((await instance.expectShutdown()).exitCode, 0);
}

runTests().then(common.mustCall());
