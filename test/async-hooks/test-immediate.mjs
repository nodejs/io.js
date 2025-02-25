import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();
hooks.enable();

// Install first immediate
setImmediate(mustCall(onimmediate));

const as = hooks.activitiesOfTypes('Immediate');
strictEqual(as.length, 1);
const imd1 = as[0];
strictEqual(imd1.type, 'Immediate');
strictEqual(typeof imd1.uid, 'number');
strictEqual(typeof imd1.triggerAsyncId, 'number');
checkInvocations(imd1, { init: 1 },
                 'imd1: when first set immediate installed');

let imd2;

function onimmediate() {
  let as = hooks.activitiesOfTypes('Immediate');
  strictEqual(as.length, 1);
  checkInvocations(imd1, { init: 1, before: 1 },
                   'imd1: when first set immediate triggered');

  // Install second immediate
  setImmediate(mustCall(onimmediateTwo));
  as = hooks.activitiesOfTypes('Immediate');
  strictEqual(as.length, 2);
  imd2 = as[1];
  strictEqual(imd2.type, 'Immediate');
  strictEqual(typeof imd2.uid, 'number');
  strictEqual(typeof imd2.triggerAsyncId, 'number');
  checkInvocations(imd1, { init: 1, before: 1 },
                   'imd1: when second set immediate installed');
  checkInvocations(imd2, { init: 1 },
                   'imd2: when second set immediate installed');
}

function onimmediateTwo() {
  checkInvocations(imd1, { init: 1, before: 1, after: 1, destroy: 1 },
                   'imd1: when second set immediate triggered');
  checkInvocations(imd2, { init: 1, before: 1 },
                   'imd2: when second set immediate triggered');
  tick(1);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('Immediate');
  checkInvocations(imd1, { init: 1, before: 1, after: 1, destroy: 1 },
                   'imd1: when process exits');
  checkInvocations(imd2, { init: 1, before: 1, after: 1, destroy: 1 },
                   'imd2: when process exits');
}
