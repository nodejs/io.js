'use strict';

const {
  SafeSet,
} = primordials;

let hook;
let config;

function lazyHookCreation() {
  const inspector = internalBinding('inspector');
  const { createHook } = require('async_hooks');
  config = internalBinding('config');

  hook = createHook({
    init(asyncId, type, triggerAsyncId, resource) {
    // It's difficult to tell which tasks will be recurring and which won't,
    // therefore we mark all tasks as recurring. Based on the discussion
    // in https://github.com/nodejs/node/pull/13870#discussion_r124515293,
    // this should be fine as long as we call asyncTaskCanceled() too.
      const recurring = true;
      if (type === 'PROMISE')
        this.promiseIds.add(asyncId);
      else
        inspector.asyncTaskScheduled(type, asyncId, recurring);
    },

    before(asyncId) {
      if (this.promiseIds.has(asyncId))
        return;
      inspector.asyncTaskStarted(asyncId);
    },

    after(asyncId) {
      if (this.promiseIds.has(asyncId))
        return;
      inspector.asyncTaskFinished(asyncId);
    },

    destroy(asyncId) {
      if (this.promiseIds.has(asyncId))
        return this.promiseIds.delete(asyncId);
      inspector.asyncTaskCanceled(asyncId);
    },
  });

  hook.promiseIds = new SafeSet();
}

function enable() {
  if (hook === undefined) lazyHookCreation();
  if (config.bits < 64) {
    // V8 Inspector stores task ids as (void*) pointers.
    // async_hooks store ids as 64bit numbers.
    // As a result, we cannot reliably translate async_hook ids to V8 async_task
    // ids on 32bit platforms.
    process.emitWarning(
      'Warning: Async stack traces in debugger are not available ' +
      `on ${config.bits}bit platforms. The feature is disabled.`,
      {
        code: 'INSPECTOR_ASYNC_STACK_TRACES_NOT_AVAILABLE',
      });
  } else {
    hook.enable();
  }
}

function disable() {
  if (hook === undefined) lazyHookCreation();
  hook.disable();
}

module.exports = {
  enable,
  disable,
};
