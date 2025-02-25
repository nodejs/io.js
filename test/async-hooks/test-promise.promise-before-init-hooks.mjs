import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const p = new Promise(mustCall(function executor(resolve) {
  resolve(5);
}));

// Init hooks after promise was created
const hooks = initHooks({ allowNoInit: true });
hooks.enable();

p.then(function afterResolution(val) {
  strictEqual(val, 5);
  const as = hooks.activitiesOfTypes('PROMISE');
  strictEqual(as.length, 1);
  checkInvocations(as[0], { init: 1, before: 1 },
                   'after resolution child promise');
  return val;
});

process.on('exit', function onexit() {
  hooks.disable();
  hooks.sanityCheck('PROMISE');

  const as = hooks.activitiesOfTypes('PROMISE');
  strictEqual(as.length, 1);

  const a0 = as[0];
  strictEqual(a0.type, 'PROMISE');
  strictEqual(typeof a0.uid, 'number');
  // We can't get the asyncId from the parent dynamically, since init was
  // never called. However, it is known that the parent promise was created
  // immediately before the child promise, thus there should only be one
  // difference in id.
  strictEqual(a0.triggerAsyncId, a0.uid - 1);
  // We expect a destroy hook as well but we cannot guarantee predictable gc.
  checkInvocations(a0, { init: 1, before: 1, after: 1 }, 'when process exits');
});
