import { isMainThread, skip, mustCall } from '../common/index.mjs';

import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { executionAsyncId } from 'async_hooks';

if (!isMainThread)
  skip('Worker bootstrapping works differently -> different async IDs');

const promiseAsyncIds = [];
const hooks = initHooks({
  oninit(asyncId, type) {
    if (type === 'PROMISE') {
      promiseAsyncIds.push(asyncId);
    }
  }
});

hooks.enable();
Promise.reject();

process.on('unhandledRejection', mustCall(() => {
  strictEqual(promiseAsyncIds.length, 1);
  strictEqual(executionAsyncId(), promiseAsyncIds[0]);
}));
