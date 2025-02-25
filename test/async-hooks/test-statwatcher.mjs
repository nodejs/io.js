import { isMainThread, skip, platformTimeout, mustCallAtLeast } from '../common/index.mjs';
import { refresh, path as _path } from '../common/tmpdir.js';
import { strictEqual } from 'assert';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { watchFile, writeFileSync, unwatchFile } from 'fs';
import { join } from 'path';
import process from 'process';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

refresh();

const file1 = join(_path, 'file1');
const file2 = join(_path, 'file2');

const onchangex = (x) => (curr, prev) => {
  console.log(`Watcher: ${x}`);
  console.log('current stat data:', curr);
  console.log('previous stat data:', prev);
};

const checkWatcherStart = (name, watcher) => {
  strictEqual(watcher.type, 'STATWATCHER');
  strictEqual(typeof watcher.uid, 'number');
  strictEqual(watcher.triggerAsyncId, 1);
  checkInvocations(watcher, { init: 1 },
                   `${name}: when started to watch file`);
};

const hooks = initHooks();
hooks.enable();

// Install first file watcher.
const w1 = watchFile(file1, { interval: 10 }, onchangex('w1'));
let as = hooks.activitiesOfTypes('STATWATCHER');
strictEqual(as.length, 1);
// Count all change events to account for all of them in checkInvocations.
let w1HookCount = 0;
w1.on('change', () => w1HookCount++);

const statwatcher1 = as[0];
checkWatcherStart('watcher1', statwatcher1);

// Install second file watcher
const w2 = watchFile(file2, { interval: 10 }, onchangex('w2'));
as = hooks.activitiesOfTypes('STATWATCHER');
strictEqual(as.length, 2);
// Count all change events to account for all of them in checkInvocations.
let w2HookCount = 0;
w2.on('change', () => w2HookCount++);

const statwatcher2 = as[1];
checkInvocations(statwatcher1, { init: 1 },
                 'watcher1: when started to watch second file');
checkWatcherStart('watcher2', statwatcher2);

setTimeout(() => writeFileSync(file1, 'foo++'),
           platformTimeout(100));
w1.on('change', mustCallAtLeast((curr, prev) => {
  console.log('w1 change to', curr, 'from', prev);
  // Wait until we get the write above.
  if (prev.size !== 0 || curr.size !== 5)
    return;

  setImmediate(() => {
    checkInvocations(statwatcher1,
                     { init: 1, before: w1HookCount, after: w1HookCount },
                     'watcher1: when unwatched first file');
    checkInvocations(statwatcher2, { init: 1 },
                     'watcher2: when unwatched first file');

    setTimeout(() => writeFileSync(file2, 'bar++'),
               platformTimeout(100));
    w2.on('change', mustCallAtLeast((curr, prev) => {
      console.log('w2 change to', curr, 'from', prev);
      // Wait until we get the write above.
      if (prev.size !== 0 || curr.size !== 5)
        return;

      setImmediate(() => {
        checkInvocations(statwatcher1,
                         { init: 1, before: w1HookCount, after: w1HookCount },
                         'watcher1: when unwatched second file');
        checkInvocations(statwatcher2,
                         { init: 1, before: w2HookCount, after: w2HookCount },
                         'watcher2: when unwatched second file');
        unwatchFile(file1);
        unwatchFile(file2);
      });
    }));
  });
}));

process.once('exit', () => {
  hooks.disable();
  hooks.sanityCheck('STATWATCHER');
  checkInvocations(statwatcher1,
                   { init: 1, before: w1HookCount, after: w1HookCount },
                   'watcher1: when process exits');
  checkInvocations(statwatcher2,
                   { init: 1, before: w2HookCount, after: w2HookCount },
                   'watcher2: when process exits');
});
