// NOTE: this also covers process wrap as one is created along with the pipes
// when we launch the sleep process
'use strict';
// Flags: --expose-gc

const common = require('../common');
const assert = require('assert');
const tick = require('../common/tick');
const initHooks = require('./init-hooks');
const { checkInvocations } = require('./hook-checks');
const { spawn } = require('child_process');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different async IDs');
}

const hooks = initHooks();

hooks.enable();
const nodeVersionSpawn = spawn(process.execPath, [ '--version' ]);

nodeVersionSpawn
  .on('exit', common.mustCall(onsleepExit))
  .on('close', common.mustCall(onsleepClose));

// A process wrap and 3 pipe wraps for std{in,out,err} are initialized
// synchronously
const processes = hooks.activitiesOfTypes('PROCESSWRAP');
const pipes = hooks.activitiesOfTypes('PIPEWRAP');
assert.strictEqual(processes.length, 1);
assert.strictEqual(pipes.length, 3);

const processwrap = processes[0];
const pipe1 = pipes[0];
const pipe2 = pipes[1];
const pipe3 = pipes[2];

assert.strictEqual(processwrap.type, 'PROCESSWRAP');
assert.strictEqual(processwrap.triggerAsyncId, 1);
checkInvocations(processwrap, { init: 1 },
                 'processwrap when sleep.spawn was called');

[ pipe1, pipe2, pipe3 ].forEach((x) => {
  assert.strictEqual(x.type, 'PIPEWRAP');
  assert.strictEqual(x.triggerAsyncId, 1);
  checkInvocations(x, { init: 1 }, 'pipe wrap when sleep.spawn was called');
});

function onsleepExit() {
  checkInvocations(processwrap, { init: 1, before: 1 },
                   'processwrap while in onsleepExit callback');
}

function onsleepClose() {
  tick(1, () =>
    checkInvocations(
      processwrap,
      { init: 1, before: 1, after: 1 },
      'processwrap while in onsleepClose callback'),
  );
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('PROCESSWRAP');
  hooks.sanityCheck('PIPEWRAP');

  checkInvocations(
    processwrap,
    { init: 1, before: 1, after: 1 },
    'processwrap while in onsleepClose callback');

  [ pipe1, pipe2, pipe3 ].forEach((x) => {
    assert.strictEqual(x.type, 'PIPEWRAP');
    assert.strictEqual(x.triggerAsyncId, 1);
  });

  const ioEvents = Math.min(pipe2.before.length, pipe2.after.length);
  // 2 events without any IO and at least one more for the node version data.
  // Usually it is just one event, but it can be more.
  assert.ok(ioEvents >= 3, `at least 3 stdout io events, got ${ioEvents}`);

  checkInvocations(pipe1, { init: 1, before: 1, after: 1 },
                   'pipe wrap when sleep.spawn was called');
  checkInvocations(pipe2, { init: 1, before: ioEvents, after: ioEvents },
                   'pipe wrap when sleep.spawn was called');
  checkInvocations(pipe3, { init: 1, before: 2, after: 2 },
                   'pipe wrap when sleep.spawn was called');
}
