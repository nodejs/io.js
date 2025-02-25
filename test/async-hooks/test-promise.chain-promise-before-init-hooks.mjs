import { isMainThread, skip, mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const p = new Promise(mustCall(function executor(resolve) {
  resolve(5);
}));

p.then(function afterResolution(val) {
  strictEqual(val, 5);
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
  strictEqual(as.length, 0);
  strictEqual(unknown.length, 1);

  const a0 = unknown[0];
  strictEqual(a0.type, 'Unknown');
  strictEqual(typeof a0.uid, 'number');
  checkInvocations(a0, { before: 1, after: 1 }, 'when process exits');
});
