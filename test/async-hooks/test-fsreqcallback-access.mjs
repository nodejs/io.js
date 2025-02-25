import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { access } from 'fs';
import process from 'process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

const hooks = initHooks();

hooks.enable();
access(__filename, mustCall(onaccess));

function onaccess() {
  const as = hooks.activitiesOfTypes('FSREQCALLBACK');
  const a = as[0];
  checkInvocations(a, { init: 1, before: 1 },
                   'while in onaccess callback');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('FSREQCALLBACK');

  const as = hooks.activitiesOfTypes('FSREQCALLBACK');
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'FSREQCALLBACK');
  strictEqual(typeof a.uid, 'number');
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
