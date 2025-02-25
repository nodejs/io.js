import { isMainThread, skip, isIBMi } from '../common/index.mjs';

import { strictEqual } from 'assert';
import initHooks from './init-hooks.mjs';
import tick from '../common/tick.js';
import { checkInvocations } from './hook-checks.mjs';
import { watch } from 'fs';
import process from 'process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

if (isIBMi)
  skip('IBMi does not support fs.watch()');

const hooks = initHooks();

hooks.enable();
const watcher = watch(__filename, onwatcherChanged);
function onwatcherChanged() { }

watcher.close();
tick(2);

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('FSEVENTWRAP');

  const as = hooks.activitiesOfTypes('FSEVENTWRAP');
  strictEqual(as.length, 1);

  const a = as[0];
  strictEqual(a.type, 'FSEVENTWRAP');
  strictEqual(typeof a.uid, 'number');
  strictEqual(a.triggerAsyncId, 1);
  checkInvocations(a, { init: 1, destroy: 1 }, 'when process exits');
}
