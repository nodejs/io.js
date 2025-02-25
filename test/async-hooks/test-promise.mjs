import { isMainThread, skip, mustCall } from '../common/index.mjs';

import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const hooks = initHooks();

hooks.enable();

const p = new Promise(mustCall(executor));
p.then(function afterResolution(val) {
  strictEqual(val, 5);
  const as = hooks.activitiesOfTypes('PROMISE');
  strictEqual(as.length, 2);
  checkInvocations(as[0], { init: 1 }, 'after resolution parent promise');
  checkInvocations(as[1], { init: 1, before: 1 },
                   'after resolution child promise');
});

function executor(resolve) {
  const as = hooks.activitiesOfTypes('PROMISE');
  strictEqual(as.length, 1);
  const a = as[0];
  checkInvocations(a, { init: 1 }, 'while in promise executor');
  resolve(5);
}

process.on('exit', onexit);
function onexit() {
  hooks.disable();
  hooks.sanityCheck('PROMISE');

  const as = hooks.activitiesOfTypes('PROMISE');
  strictEqual(as.length, 2);

  const a0 = as[0];
  strictEqual(a0.type, 'PROMISE');
  strictEqual(typeof a0.uid, 'number');
  strictEqual(a0.triggerAsyncId, 1);
  checkInvocations(a0, { init: 1 }, 'when process exits');

  const a1 = as[1];
  strictEqual(a1.type, 'PROMISE');
  strictEqual(typeof a1.uid, 'number');
  strictEqual(a1.triggerAsyncId, a0.uid);
  // We expect a destroy hook as well but we cannot guarantee predictable gc.
  checkInvocations(a1, { init: 1, before: 1, after: 1 }, 'when process exits');
}
