import { mustCall } from '../common/index.mjs';
import process from 'process';

// This tests ensures that the triggerId of the nextTick function sets the
// triggerAsyncId correctly.

import { strictEqual } from 'assert';
import { executionAsyncId, triggerAsyncId } from 'async_hooks';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();
hooks.enable();

const rootAsyncId = executionAsyncId();

process.nextTick(mustCall(() => {
  strictEqual(triggerAsyncId(), rootAsyncId);
}));

process.on('exit', () => {
  hooks.sanityCheck();

  const as = hooks.activitiesOfTypes('TickObject');
  checkInvocations(as[0], {
    init: 1, before: 1, after: 1, destroy: 1
  }, 'when process exits');
});
