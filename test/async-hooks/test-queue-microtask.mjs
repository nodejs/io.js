import { mustCall } from '../common/index.mjs';

import { strictEqual } from 'assert';
import { executionAsyncId, triggerAsyncId } from 'async_hooks';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();
hooks.enable();

const rootAsyncId = executionAsyncId();

queueMicrotask(mustCall(() => {
  strictEqual(triggerAsyncId(), rootAsyncId);
}));

process.on('exit', () => {
  hooks.sanityCheck();

  const as = hooks.activitiesOfTypes('Microtask');
  checkInvocations(as[0], {
    init: 1, before: 1, after: 1, destroy: 1
  }, 'when process exits');
});
