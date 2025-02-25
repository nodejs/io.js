import { platformTimeout, mustCall, mustNotCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const TIMEOUT = platformTimeout(100);

const hooks = initHooks();
hooks.enable();

// Install first timeout
setTimeout(mustCall(ontimeout), TIMEOUT);
const as = hooks.activitiesOfTypes('Timeout');
strictEqual(as.length, 1);
const t1 = as[0];
strictEqual(t1.type, 'Timeout');
strictEqual(typeof t1.uid, 'number');
strictEqual(typeof t1.triggerAsyncId, 'number');
checkInvocations(t1, { init: 1 }, 't1: when first timer installed');

let timer;
let t2;
function ontimeout() {
  checkInvocations(t1, { init: 1, before: 1 }, 't1: when first timer fired');

  setTimeout(onSecondTimeout, TIMEOUT).unref();
  const as = hooks.activitiesOfTypes('Timeout');
  t2 = as[1];
  strictEqual(as.length, 2);
  checkInvocations(t1, { init: 1, before: 1 },
                   't1: when second timer installed');
  checkInvocations(t2, { init: 1 },
                   't2: when second timer installed');

  timer = setTimeout(mustNotCall(), 2 ** 31 - 1);
}

function onSecondTimeout() {
  const as = hooks.activitiesOfTypes('Timeout');
  strictEqual(as.length, 3);
  checkInvocations(t1, { init: 1, before: 1, after: 1 },
                   't1: when second timer fired');
  checkInvocations(t2, { init: 1, before: 1 },
                   't2: when second timer fired');
  clearTimeout(timer);
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('Timeout');

  checkInvocations(t1, { init: 1, before: 1, after: 1, destroy: 1 },
                   't1: when process exits');
  checkInvocations(t2, { init: 1, before: 1, after: 1, destroy: 1 },
                   't2: when process exits');
}
