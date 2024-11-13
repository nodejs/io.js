'use strict';

const {
  FunctionPrototypeBind,
  Promise,
  PromiseReject,
  PromiseWithResolvers,
  ReflectConstruct,
  SafePromisePrototypeFinally,
  Symbol,
} = primordials;

const {
  Timeout,
  Immediate,
  insert,
} = require('internal/timers');
const {
  clearImmediate,
  clearInterval,
  clearTimeout,
} = require('timers');

const {
  AbortError,
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  validateAbortSignal,
  validateBoolean,
  validateObject,
  validateNumber,
} = require('internal/validators');

const {
  kEmptyObject,
} = require('internal/util');

const kScheduler = Symbol('kScheduler');
let kResistStopPropagation;

function cancelListenerHandler(clear, reject, signal) {
  if (!this._destroyed) {
    clear(this);
    reject(new AbortError(undefined, { cause: signal?.reason }));
  }
}

function setTimeout(after, value, options = kEmptyObject) {
  try {
    if (typeof after !== 'undefined') {
      validateNumber(after, 'delay');
    }

    validateObject(options, 'options');

    if (typeof options?.signal !== 'undefined') {
      validateAbortSignal(options.signal, 'options.signal');
    }

    if (typeof options?.ref !== 'undefined') {
      validateBoolean(options.ref, 'options.ref');
    }
  } catch (err) {
    return PromiseReject(err);
  }

  const { signal, ref = true } = options;

  if (signal?.aborted) {
    return PromiseReject(new AbortError(undefined, { cause: signal.reason }));
  }

  let oncancel;
  const { promise, resolve, reject } = PromiseWithResolvers();
  const timeout = new Timeout(resolve, after, [value], false, ref);
  insert(timeout, timeout._idleTimeout);
  if (signal) {
    oncancel = FunctionPrototypeBind(cancelListenerHandler,
                                     timeout, clearTimeout, reject, signal);
    kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
    signal.addEventListener('abort', oncancel, { __proto__: null, [kResistStopPropagation]: true });
  }
  return oncancel !== undefined ?
    SafePromisePrototypeFinally(
      promise,
      () => signal.removeEventListener('abort', oncancel)) : promise;
}

function setImmediate(value, options = kEmptyObject) {
  try {
    validateObject(options, 'options');

    if (typeof options?.signal !== 'undefined') {
      validateAbortSignal(options.signal, 'options.signal');
    }

    if (typeof options?.ref !== 'undefined') {
      validateBoolean(options.ref, 'options.ref');
    }
  } catch (err) {
    return PromiseReject(err);
  }

  const { signal, ref = true } = options;

  if (signal?.aborted) {
    return PromiseReject(new AbortError(undefined, { cause: signal.reason }));
  }

  let oncancel;
  const { promise, resolve, reject } = PromiseWithResolvers();
  const immediate = new Immediate(resolve, [value]);
  if (!ref) immediate.unref();
  if (signal) {
    oncancel = FunctionPrototypeBind(cancelListenerHandler,
                                     immediate, clearImmediate, reject,
                                     signal);
    kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
    signal.addEventListener('abort', oncancel, { __proto__: null, [kResistStopPropagation]: true });
  }
  return oncancel !== undefined ?
    SafePromisePrototypeFinally(
      promise,
      () => signal.removeEventListener('abort', oncancel)) : promise;
}

async function* setInterval(after, value, options = kEmptyObject) {
  if (typeof after !== 'undefined') {
    validateNumber(after, 'delay');
  }

  validateObject(options, 'options');

  if (typeof options?.signal !== 'undefined') {
    validateAbortSignal(options.signal, 'options.signal');
  }

  if (typeof options?.ref !== 'undefined') {
    validateBoolean(options.ref, 'options.ref');
  }

  const { signal, ref = true } = options;

  if (signal?.aborted) {
    throw new AbortError(undefined, { cause: signal.reason });
  }

  let onCancel;
  let interval;
  try {
    let notYielded = 0;
    let callback;
    interval = new Timeout(() => {
      notYielded++;
      if (callback) {
        callback();
        callback = undefined;
      }
    }, after, undefined, true, ref);
    insert(interval, interval._idleTimeout);
    if (signal) {
      onCancel = () => {
        clearInterval(interval);
        if (callback) {
          callback(
            PromiseReject(
              new AbortError(undefined, { cause: signal.reason })));
          callback = undefined;
        }
      };
      kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
      signal.addEventListener('abort', onCancel, { __proto__: null, once: true, [kResistStopPropagation]: true });
    }

    while (!signal?.aborted) {
      if (notYielded === 0) {
        await new Promise((resolve) => callback = resolve);
      }
      for (; notYielded > 0; notYielded--) {
        yield value;
      }
    }
    throw new AbortError(undefined, { cause: signal?.reason });
  } finally {
    clearInterval(interval);
    signal?.removeEventListener('abort', onCancel);
  }
}

// TODO(@jasnell): Scheduler is an API currently being discussed by WICG
// for Web Platform standardization: https://github.com/WICG/scheduling-apis
// The scheduler.yield() and scheduler.wait() methods correspond roughly to
// the awaitable setTimeout and setImmediate implementations here. This api
// should be considered to be experimental until the spec for these are
// finalized. Note, also, that Scheduler is expected to be defined as a global,
// but while the API is experimental we shouldn't expose it as such.
class Scheduler {
  constructor() {
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }

  /**
   * @returns {Promise<void>}
   */
  yield() {
    if (!this[kScheduler])
      throw new ERR_INVALID_THIS('Scheduler');
    return setImmediate();
  }

  /**
   * @typedef {import('../internal/abort_controller').AbortSignal} AbortSignal
   * @param {number} delay
   * @param {{ signal?: AbortSignal }} [options]
   * @returns {Promise<void>}
   */
  wait(delay, options) {
    if (!this[kScheduler])
      throw new ERR_INVALID_THIS('Scheduler');
    return setTimeout(delay, undefined, options);
  }
}

module.exports = {
  setTimeout,
  setImmediate,
  setInterval,
  scheduler: ReflectConstruct(function() {
    this[kScheduler] = true;
  }, [], Scheduler),
};
