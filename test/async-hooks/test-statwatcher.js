'use strict';

const common = require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const initHooks = require('./init-hooks');
const { checkInvocations } = require('./hook-checks');
const fs = require('fs');

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different async IDs');
}

tmpdir.refresh();

const file1 = tmpdir.resolve('file1');
const file2 = tmpdir.resolve('file2');

const onchangex = (x) => (curr, prev) => {
  console.log(`Watcher: ${x}`);
  console.log('current stat data:', curr);
  console.log('previous stat data:', prev);
};

const checkWatcherStart = (name, watcher) => {
  assert.strictEqual(watcher.type, 'STATWATCHER');
  assert.strictEqual(typeof watcher.uid, 'number');
  assert.strictEqual(watcher.triggerAsyncId, 1);
  checkInvocations(watcher, { init: 1 },
                   `${name}: when started to watch file`);
};

const hooks = initHooks();
hooks.enable();

// Install first file watcher.
const w1 = fs.watchFile(file1, { interval: 10 }, onchangex('w1'));
let as = hooks.activitiesOfTypes('STATWATCHER');
assert.strictEqual(as.length, 1);
// Count all change events to account for all of them in checkInvocations.
let w1HookCount = 0;
w1.on('change', () => w1HookCount++);

const statwatcher1 = as[0];
checkWatcherStart('watcher1', statwatcher1);

// Install second file watcher
const w2 = fs.watchFile(file2, { interval: 10 }, onchangex('w2'));
as = hooks.activitiesOfTypes('STATWATCHER');
assert.strictEqual(as.length, 2);
// Count all change events to account for all of them in checkInvocations.
let w2HookCount = 0;
w2.on('change', () => w2HookCount++);

const statwatcher2 = as[1];
checkInvocations(statwatcher1, { init: 1 },
                 'watcher1: when started to watch second file');
checkWatcherStart('watcher2', statwatcher2);

setTimeout(() => fs.writeFileSync(file1, 'foo++'),
           common.platformTimeout(100));
w1.on('change', common.mustCallAtLeast((curr, prev) => {
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

    setTimeout(() => fs.writeFileSync(file2, 'bar++'),
               common.platformTimeout(100));
    w2.on('change', common.mustCallAtLeast((curr, prev) => {
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
        fs.unwatchFile(file1);
        fs.unwatchFile(file2);
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
