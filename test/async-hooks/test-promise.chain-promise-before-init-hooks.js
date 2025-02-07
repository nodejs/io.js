'use strict';

const common = require('../common');
const assert = require('assert');
const initHooks = require('./init-hooks');
const { checkInvocations } = require('./hook-checks');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different async IDs');
}

const p = new Promise(common.mustCall(function executor(resolve) {
  resolve(5);
}));

p.then(function afterResolution(val) {
  assert.strictEqual(val, 5);
  return val;
});

// Init hooks after chained promise is created
const hooks = initHooks();
hooks._allowNoInit = true;
hooks.enable();


process.on('exit', function onexit() {
  hooks.disable();
  hooks.sanityCheck('PROMISE');

  // Because the init event was never emitted the hooks listener doesn't
  // know what the type was. Thus check for Unknown rather than PROMISE.
  const as = hooks.activitiesOfTypes('PROMISE');
  const unknown = hooks.activitiesOfTypes('Unknown');
  assert.strictEqual(as.length, 0);
  assert.strictEqual(unknown.length, 1);

  const a0 = unknown[0];
  assert.strictEqual(a0.type, 'Unknown');
  assert.strictEqual(typeof a0.uid, 'number');
  checkInvocations(a0, { before: 1, after: 1 }, 'when process exits');
});
