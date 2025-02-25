import { isMainThread, skip, mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { readFile } from 'fs';
import process from 'process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const hooks = initHooks();

hooks.enable();
readFile(__filename, mustCall(onread));

function onread() {
  const as = hooks.activitiesOfTypes('FSREQCALLBACK');
  let lastParent = 1;
  for (let i = 0; i < as.length; i++) {
    const a = as[i];
    strictEqual(a.type, 'FSREQCALLBACK');
    strictEqual(typeof a.uid, 'number');
    strictEqual(a.triggerAsyncId, lastParent);
    lastParent = a.uid;
  }
  checkInvocations(as[0], { init: 1, before: 1, after: 1, destroy: 1 },
                   'reqwrap[0]: while in onread callback');
  checkInvocations(as[1], { init: 1, before: 1, after: 1, destroy: 1 },
                   'reqwrap[1]: while in onread callback');
  checkInvocations(as[2], { init: 1, before: 1, after: 1, destroy: 1 },
                   'reqwrap[2]: while in onread callback');

  // This callback is called from within the last fs req callback therefore
  // the last req is still going and after/destroy haven't been called yet
  checkInvocations(as[3], { init: 1, before: 1 },
                   'reqwrap[3]: while in onread callback');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('FSREQCALLBACK');
  const as = hooks.activitiesOfTypes('FSREQCALLBACK');
  const a = as.pop();
  checkInvocations(a, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
