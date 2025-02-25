// Flags: --expose-gc

import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { resolve } from 'dns';
import process from 'process';

const hooks = initHooks();

hooks.enable();
// Uses cares for queryA which in turn uses QUERYWRAP
resolve('localhost', mustCall(onresolved));

function onresolved() {
  const as = hooks.activitiesOfTypes('QUERYWRAP');
  const a = as[0];
  strictEqual(as.length, 1);
  checkInvocations(a, { init: 1, before: 1 }, 'while in onresolved callback');
  tick(1E4);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('QUERYWRAP');

  const as = hooks.activitiesOfTypes('QUERYWRAP');
  strictEqual(as.length, 1);
  const a = as[0];

  strictEqual(a.type, 'QUERYWRAP');
  strictEqual(typeof a.uid, 'number');
  strictEqual(typeof a.triggerAsyncId, 'number');
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
