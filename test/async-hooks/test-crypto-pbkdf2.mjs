import { hasCrypto, skip, isMainThread, mustCall } from '../common/index.mjs';
if (!hasCrypto)
  skip('missing crypto');
if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { pbkdf2 } from 'crypto';
import process from 'process';


const hooks = initHooks();

hooks.enable();

pbkdf2('password', 'salt', 1, 20, 'sha256', mustCall(onpbkdf2));

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
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'PBKDF2REQUEST');
  strictEqual(typeof a.uid, 'number');
  strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
