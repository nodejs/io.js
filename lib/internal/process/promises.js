'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeShift,
  Error,
  ObjectPrototypeHasOwnProperty,
  SafeMap,
  SafeWeakMap,
} = primordials;

const {
  tickInfo,
  promiseRejectEvents: {
    kPromiseRejectWithNoHandler,
    kPromiseHandlerAddedAfterReject,
    kPromiseRejectAfterResolved,
    kPromiseResolveAfterResolved,
  },
  setPromiseRejectCallback,
} = internalBinding('task_queue');

const { deprecate } = require('internal/util');

const {
  noSideEffectsToString,
  triggerUncaughtException,
  exitCodes: { kGenericUserError },
} = internalBinding('errors');

const {
  pushAsyncContext,
  popAsyncContext,
  symbols: {
    async_id_symbol: kAsyncIdSymbol,
    trigger_async_id_symbol: kTriggerAsyncIdSymbol,
  },
} = require('internal/async_hooks');
const { isErrorStackTraceLimitWritable } = require('internal/errors');

const AsyncContextFrame = require('internal/async_context_frame');

// *Must* match Environment::TickInfo::Fields in src/env.h.
const kHasRejectionToWarn = 1;

// By default true because in cases where process is not a global
// it is not possible to determine if the user has added a listener
// to the process object.
let hasMultipleResolvesListener = true;

if (process.on) {
  hasMultipleResolvesListener = process.listenerCount('multipleResolves') !== 0;

  process.on('newListener', (eventName) => {
    if (eventName === 'multipleResolves') {
      hasMultipleResolvesListener = true;
    }
  });

  process.on('removeListener', (eventName) => {
    if (eventName === 'multipleResolves') {
      hasMultipleResolvesListener = process.listenerCount('multipleResolves') !== 0;
    }
  });
}

/**
 * Errors & Warnings
 */

class UnhandledPromiseRejection extends Error {
  code = 'ERR_UNHANDLED_REJECTION';
  name = 'UnhandledPromiseRejection';
  /**
   * @param {Error} reason
   */
  constructor(reason) {
    super('This error originated either by throwing inside of an ' +
    'async function without a catch block, or by rejecting a promise which ' +
    'was not handled with .catch(). The promise rejected with the reason "' +
    noSideEffectsToString(reason) + '".');
  }
}

class UnhandledPromiseRejectionWarning extends Error {
  name = 'UnhandledPromiseRejectionWarning';
  /**
   * @param {number} uid
   */
  constructor(uid) {
    const message = 'Unhandled promise rejection. This error originated either by ' +
    'throwing inside of an async function without a catch block, ' +
    'or by rejecting a promise which was not handled with .catch(). ' +
    'To terminate the node process on unhandled promise ' +
    'rejection, use the CLI flag `--unhandled-rejections=strict` (see ' +
    'https://nodejs.org/api/cli.html#cli_unhandled_rejections_mode). ' +
    `(rejection id: ${uid})`;

    // UnhandledPromiseRejectionWarning will get the stack trace from the
    // reason, so we can disable the stack trace limit temporarily for better
    // performance.
    if (isErrorStackTraceLimitWritable()) {
      const stackTraceLimit = Error.stackTraceLimit;
      Error.stackTraceLimit = 0;
      super(message);
      Error.stackTraceLimit = stackTraceLimit;
    } else {
      super(message);
    }
  }
}

class PromiseRejectionHandledWarning extends Error {
  name = 'PromiseRejectionHandledWarning';

  /**
   * @param {number} uid
   */
  constructor(uid) {
    super(`Promise rejection was handled asynchronously (rejection id: ${uid})`);
    this.id = uid;
  }
}

/**
 * @typedef PromiseInfo
 * @property {*} reason the reason for the rejection
 * @property {number} uid the unique id of the promise
 * @property {boolean} warned whether the rejection has been warned
 * @property {object} [domain] the domain the promise was created in
 */

/**
 * @type {WeakMap<Promise, PromiseInfo>}
 */
const maybeUnhandledPromises = new SafeWeakMap();

/**
 * Using a Mp causes the promise to be referenced at least for one tick.
 * @type {Map<Promise, PromiseInfo>}
 */
let pendingUnhandledRejections = new SafeMap();

/**
 * @type {Array<{promise: Promise, warning: Error}>}
 */
const asyncHandledRejections = [];

/**
 * @type {number}
 */
let lastPromiseId = 0;

/**
 * @param {boolean} value
 */
function setHasRejectionToWarn(value) {
  tickInfo[kHasRejectionToWarn] = value ? 1 : 0;
}

/**
 * @returns {boolean}
 */
function hasRejectionToWarn() {
  return tickInfo[kHasRejectionToWarn] === 1;
}

/**
 * @param {string|Error} obj
 * @returns {obj is Error}
 */
function isErrorLike(obj) {
  return typeof obj === 'object' &&
         obj !== null &&
         ObjectPrototypeHasOwnProperty(obj, 'stack');
}

/**
 * @param {0|1|2|3} type
 * @param {Promise} promise
 * @param {Error} reason
 */
function promiseRejectHandler(type, promise, reason) {
  if (unhandledRejectionsMode === undefined) {
    unhandledRejectionsMode = getUnhandledRejectionsMode();
  }
  switch (type) {
    case kPromiseRejectWithNoHandler: // 0
      unhandledRejection(promise, reason);
      break;
    case kPromiseHandlerAddedAfterReject: // 1
      handledRejection(promise);
      break;
    case kPromiseRejectAfterResolved: // 2
      if (hasMultipleResolvesListener) {
        resolveErrorReject(promise, reason);
      }
      break;
    case kPromiseResolveAfterResolved: // 3
      if (hasMultipleResolvesListener) {
        resolveErrorResolve(promise, reason);
      }
      break;
  }
}

const multipleResolvesDeprecate = deprecate(
  () => {},
  'The multipleResolves event has been deprecated.',
  'DEP0160',
);

/**
 * @param {Promise} promise
 * @param {Error} reason
 */
function resolveErrorResolve(promise, reason) {
  // We have to wrap this in a next tick. Otherwise the error could be caught by
  // the executed promise.
  process.nextTick(() => {
    // Emit the multipleResolves event.
    // This is a deprecated event, so we have to check if it's being listened to.
    if (process.emit('multipleResolves', 'resolve', promise, reason)) {
      // If the event is being listened to, emit a deprecation warning.
      multipleResolvesDeprecate();
    }
  });
}

/**
 * @param {Promise} promise
 * @param {Error} reason
 */
function resolveErrorReject(promise, reason) {
  // We have to wrap this in a next tick. Otherwise the error could be caught by
  // the executed promise.
  process.nextTick(() => {
    if (process.emit('multipleResolves', 'reject', promise, reason)) {
      multipleResolvesDeprecate();
    }
  });
}

/**
 * @param {Promise} promise
 * @param {PromiseInfo} promiseInfo
 * @returns {boolean}
 */
const emitUnhandledRejection = (promise, promiseInfo) => {
  return promiseInfo.domain ?
    promiseInfo.domain.emit('error', promiseInfo.reason) :
    process.emit('unhandledRejection', promiseInfo.reason, promise);
};

/**
 * @param {Promise} promise
 * @param {Error} reason
 */
function unhandledRejection(promise, reason) {
  pendingUnhandledRejections.set(promise, {
    reason,
    uid: ++lastPromiseId,
    warned: false,
    domain: process.domain,
    contextFrame: AsyncContextFrame.current(),
  });
  setHasRejectionToWarn(true);
}

/**
 * @param {Promise} promise
 */
function handledRejection(promise) {
  if (pendingUnhandledRejections.has(promise)) {
    pendingUnhandledRejections.delete(promise);
    return;
  }
  const promiseInfo = maybeUnhandledPromises.get(promise);
  if (promiseInfo !== undefined) {
    maybeUnhandledPromises.delete(promise);
    if (promiseInfo.warned) {
      // Generate the warning object early to get a good stack trace.
      const warning = new PromiseRejectionHandledWarning(promiseInfo.uid);
      ArrayPrototypePush(asyncHandledRejections, { promise, warning });
      setHasRejectionToWarn(true);
    }
  }
}

const unhandledRejectionErrName = UnhandledPromiseRejectionWarning.name;

/**
 * @param {PromiseInfo} promiseInfo
 */
function emitUnhandledRejectionWarning(promiseInfo) {
  const warning = new UnhandledPromiseRejectionWarning(promiseInfo.uid);
  const reason = promiseInfo.reason;
  try {
    if (isErrorLike(reason)) {
      warning.stack = reason.stack;
      process.emitWarning(reason.stack, unhandledRejectionErrName);
    } else {
      process.emitWarning(
        noSideEffectsToString(reason), unhandledRejectionErrName);
    }
  } catch {
    try {
      process.emitWarning(
        noSideEffectsToString(reason), unhandledRejectionErrName);
    } catch {
      // Ignore.
    }
  }

  process.emitWarning(warning);
}

/**
 * @callback UnhandledRejectionsModeHandler
 * @param {Promise} promise
 * @param {PromiseInfo} promiseInfo
 * @param {number} [promiseAsyncId]
 * @returns {boolean}
 */

/**
 * The mode of unhandled rejections.
 * @type {UnhandledRejectionsModeHandler}
 */
let unhandledRejectionsMode;

/**
 * --unhandled-rejections=strict:
 * Emit 'uncaughtException'. If it's not handled, print the error to stderr
 * and exit the process.
 * Otherwise, emit 'unhandledRejection'. If 'unhandledRejection' is not
 * handled, emit 'UnhandledPromiseRejectionWarning'.
 * @type {UnhandledRejectionsModeHandler}
 */
function strictUnhandledRejectionsMode(promise, promiseInfo, promiseAsyncId) {
  const reason = promiseInfo.reason;
  const err = isErrorLike(reason) ?
    reason : new UnhandledPromiseRejection(reason);
  // This destroys the async stack, don't clear it after
  triggerUncaughtException(err, true /* fromPromise */);
  if (promiseAsyncId === undefined) {
    pushAsyncContext(
      promise[kAsyncIdSymbol],
      promise[kTriggerAsyncIdSymbol],
      promise,
    );
  }
  const handled = emitUnhandledRejection(promise, promiseInfo);
  if (!handled) emitUnhandledRejectionWarning(promiseInfo);
  return true;
}

/**
 * --unhandled-rejections=none:
 * Emit 'unhandledRejection', but do not emit any warning.
 * @type {UnhandledRejectionsModeHandler}
 */
function ignoreUnhandledRejectionsMode(promise, promiseInfo) {
  emitUnhandledRejection(promise, promiseInfo);
  return true;
}

/**
 * --unhandled-rejections=warn:
 * Emit 'unhandledRejection', then emit 'UnhandledPromiseRejectionWarning'.
 * @type {UnhandledRejectionsModeHandler}
 */
function alwaysWarnUnhandledRejectionsMode(promise, promiseInfo) {
  emitUnhandledRejection(promise, promiseInfo);
  emitUnhandledRejectionWarning(promiseInfo);
  return true;
}

/**
 * --unhandled-rejections=throw:
 * Emit 'unhandledRejection', if it's unhandled, emit
 * 'uncaughtException'. If it's not handled, print the error to stderr
 * and exit the process.
 * @type {UnhandledRejectionsModeHandler}
 */
function throwUnhandledRejectionsMode(promise, promiseInfo) {
  const reason = promiseInfo.reason;
  const handled = emitUnhandledRejection(promise, promiseInfo);
  if (!handled) {
    const err = isErrorLike(reason) ?
      reason :
      new UnhandledPromiseRejection(reason);
    // This destroys the async stack, don't clear it after
    triggerUncaughtException(err, true /* fromPromise */);
    return false;
  }
  return true;
}

/**
 * --unhandled-rejections=warn-with-error-code:
 * Emit 'unhandledRejection', if it's unhandled, emit
 * 'UnhandledPromiseRejectionWarning', then set process exit code to 1.
 * @type {UnhandledRejectionsModeHandler}
 */
function warnWithErrorCodeUnhandledRejectionsMode(promise, promiseInfo) {
  const handled = emitUnhandledRejection(promise, promiseInfo);
  if (!handled) {
    emitUnhandledRejectionWarning(promiseInfo);
    process.exitCode = kGenericUserError;
  }
  return true;
}

/**
 * @returns {UnhandledRejectionsModeHandler}
 */
function getUnhandledRejectionsMode() {
  const { getOptionValue } = require('internal/options');
  switch (getOptionValue('--unhandled-rejections')) {
    case 'none':
      return ignoreUnhandledRejectionsMode;
    case 'warn':
      return alwaysWarnUnhandledRejectionsMode;
    case 'strict':
      return strictUnhandledRejectionsMode;
    case 'throw':
      return throwUnhandledRejectionsMode;
    case 'warn-with-error-code':
      return warnWithErrorCodeUnhandledRejectionsMode;
    default:
      return throwUnhandledRejectionsMode;
  }
}

// If this method returns true, we've executed user code or triggered
// a warning to be emitted which requires the microtask and next tick
// queues to be drained again.
function processPromiseRejections() {
  let maybeScheduledTicksOrMicrotasks = asyncHandledRejections.length > 0;

  while (asyncHandledRejections.length !== 0) {
    const { promise, warning } = ArrayPrototypeShift(asyncHandledRejections);
    if (!process.emit('rejectionHandled', promise)) {
      process.emitWarning(warning);
    }
  }

  let needPop = true;
  let promiseAsyncId;

  const pending = pendingUnhandledRejections;
  pendingUnhandledRejections = new SafeMap();

  for (const { 0: promise, 1: promiseInfo } of pending.entries()) {
    maybeUnhandledPromises.set(promise, promiseInfo);

    promiseInfo.warned = true;

    // We need to check if async_hooks are enabled
    // don't use enabledHooksExist as a Promise could
    // come from a vm.* context and not have an async id
    promiseAsyncId = promise[kAsyncIdSymbol];
    if (promiseAsyncId !== undefined) {
      pushAsyncContext(
        promiseAsyncId,
        promise[kTriggerAsyncIdSymbol],
        promise,
      );
    }

    const { contextFrame } = promiseInfo;
    const priorContextFrame = AsyncContextFrame.exchange(contextFrame);
    try {
      needPop = unhandledRejectionsMode(promise, promiseInfo, promiseAsyncId);
    } finally {
      AsyncContextFrame.set(priorContextFrame);
      needPop &&
      promiseAsyncId !== undefined &&
      popAsyncContext(promiseAsyncId);
    }
    maybeScheduledTicksOrMicrotasks = true;
  }
  return maybeScheduledTicksOrMicrotasks ||
         pendingUnhandledRejections.size !== 0;
}

function listenForRejections() {
  setPromiseRejectCallback(promiseRejectHandler);
}

module.exports = {
  hasRejectionToWarn,
  setHasRejectionToWarn,
  listenForRejections,
  processPromiseRejections,
};
