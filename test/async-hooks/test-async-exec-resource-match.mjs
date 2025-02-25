import { mustCallAtLeast, mustCall } from '../common/index.mjs';
import { ok } from 'assert';
import { readFile } from 'fs';
import { createHook, executionAsyncResource, AsyncResource } from 'async_hooks';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

// Ignore any asyncIds created before our hook is active.
let firstSeenAsyncId = -1;
const idResMap = new Map();
const numExpectedCalls = 5;

createHook({
  init: mustCallAtLeast(
    (asyncId, type, triggerId, resource) => {
      if (firstSeenAsyncId === -1) {
        firstSeenAsyncId = asyncId;
      }
      ok(idResMap.get(asyncId) === undefined);
      idResMap.set(asyncId, resource);
    }, numExpectedCalls),
  before(asyncId) {
    if (asyncId >= firstSeenAsyncId) {
      beforeHook(asyncId);
    }
  },
  after(asyncId) {
    if (asyncId >= firstSeenAsyncId) {
      afterHook(asyncId);
    }
  }
}).enable();

const beforeHook = mustCallAtLeast(
  (asyncId) => {
    const res = idResMap.get(asyncId);
    ok(res !== undefined);
    const execRes = executionAsyncResource();
    ok(execRes === res, 'resource mismatch in before');
  }, numExpectedCalls);

const afterHook = mustCallAtLeast(
  (asyncId) => {
    const res = idResMap.get(asyncId);
    ok(res !== undefined);
    const execRes = executionAsyncResource();
    ok(execRes === res, 'resource mismatch in after');
  }, numExpectedCalls);

const res = new AsyncResource('TheResource');
const initRes = idResMap.get(res.asyncId());
ok(initRes === res, 'resource mismatch in init');
res.runInAsyncScope(mustCall(() => {
  const execRes = executionAsyncResource();
  ok(execRes === res, 'resource mismatch in cb');
}));

readFile(__filename, mustCall());
