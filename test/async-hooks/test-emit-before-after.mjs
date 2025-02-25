// Flags: --expose-internals

import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import internalAsyncHooks from 'internal/async_hooks';
import initHooks from './init-hooks.mjs';

const { newAsyncId, emitBefore, emitAfter, emitInit } = internalAsyncHooks;
const expectedId = newAsyncId();
const expectedTriggerId = newAsyncId();
const expectedType = 'test_emit_before_after_type';

// Verify that if there is no registered hook, then nothing will happen.
emitBefore(expectedId, expectedTriggerId);
emitAfter(expectedId);

const chkBefore = mustCall((id) => strictEqual(id, expectedId));
const chkAfter = mustCall((id) => strictEqual(id, expectedId));

const checkOnce = (fn) => {
  let called = false;
  return (...args) => {
    if (called) return;

    called = true;
    fn(...args);
  };
};

initHooks({
  onbefore: checkOnce(chkBefore),
  onafter: checkOnce(chkAfter),
  allowNoInit: true
}).enable();

emitInit(expectedId, expectedType, expectedTriggerId);
emitBefore(expectedId, expectedTriggerId);
emitAfter(expectedId);
