import { isMainThread, skip, mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { lookup } from 'dns';
import process from 'process';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const hooks = initHooks();

hooks.enable();
lookup('www.google.com', 4, mustCall(onlookup));
function onlookup() {
  // We don't care about the error here in order to allow
  // tests to run offline (lookup will fail in that case and the err be set);

  const as = hooks.activitiesOfTypes('GETADDRINFOREQWRAP');
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'GETADDRINFOREQWRAP');
  strictEqual(typeof a.uid, 'number');
  strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, before: 1 }, 'while in onlookup callback');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('GETADDRINFOREQWRAP');

  const as = hooks.activitiesOfTypes('GETADDRINFOREQWRAP');
  const a = as[0];
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
