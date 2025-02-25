import { hasCrypto, skip, isMainThread, mustCall } from '../common/index.mjs';
if (!hasCrypto)
  skip('missing crypto');
if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { randomBytes } from 'crypto';
import process from 'process';


const hooks = initHooks();

hooks.enable();
randomBytes(1, mustCall(onrandomBytes));

function onrandomBytes() {
  const as = hooks.activitiesOfTypes('RANDOMBYTESREQUEST');
  const a = as[0];
  checkInvocations(a, { init: 1, before: 1 },
                   'while in onrandomBytes callback');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('RANDOMBYTESREQUEST');

  const as = hooks.activitiesOfTypes('RANDOMBYTESREQUEST');
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'RANDOMBYTESREQUEST');
  strictEqual(typeof a.uid, 'number');
  strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
