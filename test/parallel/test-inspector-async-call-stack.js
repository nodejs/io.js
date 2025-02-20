// Flags: --expose-internals
'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
common.skipIf32Bits();

const assert = require('assert');
const { inspect } = require('util');
const { internalBinding } = require('internal/test/binding');
const { async_hook_fields, constants, getPromiseHooks } = internalBinding('async_wrap');
const { kTotals } = constants;
const inspector = require('inspector/promises');

const setDepth = 'Debugger.setAsyncCallStackDepth';
const emptyPromiseHooks = [ undefined, undefined, undefined, undefined ];
function verifyAsyncHookDisabled(message) {
  assert.strictEqual(async_hook_fields[kTotals], 0,
                     `${async_hook_fields[kTotals]} !== 0: ${message}`);
  const promiseHooks = getPromiseHooks();
  assert.deepStrictEqual(
    promiseHooks, emptyPromiseHooks,
    `${message}: promise hooks ${inspect(promiseHooks)}`
  );
}

function verifyAsyncHookEnabled(message) {
  assert.strictEqual(async_hook_fields[kTotals], 4,
                     `${async_hook_fields[kTotals]} !== 4: ${message}`);
  const promiseHooks = getPromiseHooks();
  assert.deepStrictEqual(  // Inspector async hooks should not enable promise hooks
    promiseHooks, emptyPromiseHooks,
    `${message}: promise hooks ${inspect(promiseHooks)}`
  );
}

// By default inspector async hooks should not have been installed.
verifyAsyncHookDisabled('inspector async hook should be disabled at startup');

const session = new inspector.Session();
verifyAsyncHookDisabled('creating a session should not enable async hooks');

session.connect();
verifyAsyncHookDisabled('connecting a session should not enable async hooks');

(async () => {
  await session.post('Debugger.enable');
  verifyAsyncHookDisabled('enabling debugger should not enable async hooks');
  await assert.rejects(session.post(setDepth, { invalid: 'message' }), { code: 'ERR_INSPECTOR_COMMAND' });
  verifyAsyncHookDisabled('invalid message should not enable async hooks');
  await assert.rejects(session.post(setDepth, { maxDepth: 'five' }), { code: 'ERR_INSPECTOR_COMMAND' });
  verifyAsyncHookDisabled('invalid maxDepth (string) should not enable ' +
                          'async hooks');
  await assert.rejects(session.post(setDepth, { maxDepth: NaN }), { code: 'ERR_INSPECTOR_COMMAND' });
  verifyAsyncHookDisabled('invalid maxDepth (NaN) should not enable ' +
                          'async hooks');
  await session.post(setDepth, { maxDepth: 10 });
  verifyAsyncHookEnabled('valid message should enable async hooks');

  await session.post(setDepth, { maxDepth: 0 });
  verifyAsyncHookDisabled('Setting maxDepth to 0 should disable ' +
                          'async hooks');

  await session.post(setDepth, { maxDepth: 32 });
  verifyAsyncHookEnabled('valid message should enable async hooks');

  await session.post('Debugger.disable');
  verifyAsyncHookDisabled('Debugger.disable should disable async hooks');

  await session.post('Debugger.enable');
  verifyAsyncHookDisabled('Enabling debugger should not enable hooks');

  await session.post(setDepth, { maxDepth: 64 });
  verifyAsyncHookEnabled('valid message should enable async hooks');

  await session.disconnect();

  verifyAsyncHookDisabled('Disconnecting session should disable ' +
                          'async hooks');
})().then(common.mustCall());
