import { skip, mustCall } from '../common/index.mjs';
import { strictEqual, fail } from 'assert';
import { exit, stdin } from 'process';

// General hook test setup
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();
hooks.enable();

if (!stdin.isTTY) {
  skip('no valid readable TTY available');
  exit();
}

// test specific setup
const checkInitOpts = { init: 1 };
const checkEndedOpts = { init: 1, before: 1, after: 1, destroy: 1 };

// test code
//
// listen to stdin except on Windows
const activities = hooks.activitiesOfTypes('TTYWRAP');
strictEqual(activities.length, 1);

const tty = activities[0];
strictEqual(tty.type, 'TTYWRAP');
strictEqual(typeof tty.uid, 'number');
strictEqual(typeof tty.triggerAsyncId, 'number');
checkInvocations(tty, checkInitOpts, 'when tty created');

const delayedOnCloseHandler = mustCall(() => {
  checkInvocations(tty, checkEndedOpts, 'when tty ended');
});
stdin.on('error', (err) => fail(err));
stdin.on('close', mustCall(() =>
  tick(2, delayedOnCloseHandler)
));
stdin.destroy();
checkInvocations(tty, checkInitOpts, 'when tty.end() was invoked');

process.on('exit', () => {
  hooks.disable();
  hooks.sanityCheck('TTYWRAP');
  checkInvocations(tty, checkEndedOpts, 'when process exits');
});
