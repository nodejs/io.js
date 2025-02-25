import { isMainThread, skip, mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { lookupService } from 'dns';
import process from 'process';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const hooks = initHooks();

hooks.enable();
lookupService('127.0.0.1', 80, mustCall(onlookupService));
function onlookupService() {
  // We don't care about the error here in order to allow
  // tests to run offline (lookup will fail in that case and the err be set)

  const as = hooks.activitiesOfTypes('GETNAMEINFOREQWRAP');
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'GETNAMEINFOREQWRAP');
  strictEqual(typeof a.uid, 'number');
  strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, before: 1 },
                   'while in onlookupService callback');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('GETNAMEINFOREQWRAP');

  const as = hooks.activitiesOfTypes('GETNAMEINFOREQWRAP');
  const a = as[0];
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
