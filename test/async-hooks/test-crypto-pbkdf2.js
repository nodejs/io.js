'use strict';

const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}
const { isMainThread } = require('worker_threads');
if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different async IDs');
}

const assert = require('assert');
const tick = require('../common/tick');
const initHooks = require('./init-hooks');
const { checkInvocations } = require('./hook-checks');
const crypto = require('crypto');


const hooks = initHooks();

hooks.enable();

crypto.pbkdf2('password', 'salt', 1, 20, 'sha256', common.mustCall(onpbkdf2));

function onpbkdf2() {
  const as = hooks.activitiesOfTypes('PBKDF2REQUEST');
  const a = as[0];
  checkInvocations(a, { init: 1, before: 1 }, 'while in onpbkdf2 callback');
  tick(2);
}

process.on('exit', onexit);
function onexit() {
  hooks.disable();
  hooks.sanityCheck('PBKDF2REQUEST');

  const as = hooks.activitiesOfTypes('PBKDF2REQUEST');
  assert.strictEqual(as.length, 1);

  const a = as[0];
  assert.strictEqual(a.type, 'PBKDF2REQUEST');
  assert.strictEqual(typeof a.uid, 'number');
  assert.strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
