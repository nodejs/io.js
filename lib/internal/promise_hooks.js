'use strict';

const {
  ArrayPrototypeIndexOf,
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  ArrayPrototypeSplice,
  FunctionPrototypeBind,
} = primordials;

const { setPromiseHooks } = internalBinding('async_wrap');
const { triggerUncaughtException } = internalBinding('errors');

const { kEmptyObject } = require('internal/util');
const { validatePlainFunction } = require('internal/validators');

const hooks = {
  init: [],
  before: [],
  after: [],
  settled: [],
};

function initAll(promise, parent) {
  const hookSet = ArrayPrototypeSlice(hooks.init);
  const exceptions = [];

  for (let i = 0; i < hookSet.length; i++) {
    const init = hookSet[i];
    try {
      init(promise, parent);
    } catch (err) {
      ArrayPrototypePush(exceptions, err);
    }
  }

  // Triggering exceptions is deferred to allow other hooks to complete
  for (let i = 0; i < exceptions.length; i++) {
    const err = exceptions[i];
    triggerUncaughtException(err, false);
  }
}

function makeRunHook(list) {
  return (promise) => {
    const hookSet = ArrayPrototypeSlice(list);
    const exceptions = [];

    for (let i = 0; i < hookSet.length; i++) {
      const hook = hookSet[i];
      try {
        hook(promise);
      } catch (err) {
        ArrayPrototypePush(exceptions, err);
      }
    }

    // Triggering exceptions is deferred to allow other hooks to complete
    for (let i = 0; i < exceptions.length; i++) {
      const err = exceptions[i];
      triggerUncaughtException(err, false);
    }
  };
}

const beforeAll = makeRunHook(hooks.before);
const afterAll = makeRunHook(hooks.after);
const settledAll = makeRunHook(hooks.settled);

function maybeFastPath(list, runAll) {
  return list.length > 1 ? runAll : list[0];
}

function update() {
  const init = maybeFastPath(hooks.init, initAll);
  const before = maybeFastPath(hooks.before, beforeAll);
  const after = maybeFastPath(hooks.after, afterAll);
  const settled = maybeFastPath(hooks.settled, settledAll);
  setPromiseHooks(init, before, after, settled);
}

function stop(list, hook) {
  const index = ArrayPrototypeIndexOf(list, hook);
  if (index >= 0) {
    ArrayPrototypeSplice(list, index, 1);
    update();
  }
}

function makeUseHook(name) {
  const list = hooks[name];
  return (hook) => {
    validatePlainFunction(hook, `${name}Hook`);
    ArrayPrototypePush(list, hook);
    update();
    return FunctionPrototypeBind(stop, null, list, hook);
  };
}

const onInit = makeUseHook('init');
const onBefore = makeUseHook('before');
const onAfter = makeUseHook('after');
const onSettled = makeUseHook('settled');

function createHook({ init, before, after, settled } = kEmptyObject) {
  const hooks = [];

  if (init) ArrayPrototypePush(hooks, onInit(init));
  if (before) ArrayPrototypePush(hooks, onBefore(before));
  if (after) ArrayPrototypePush(hooks, onAfter(after));
  if (settled) ArrayPrototypePush(hooks, onSettled(settled));

  return () => {
    for (const stop of hooks) {
      stop();
    }
  };
}

module.exports = {
  createHook,
  onInit,
  onBefore,
  onAfter,
  onSettled,
};
