// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const {
  MathTrunc,
  ObjectDefineProperties,
  ObjectDefineProperty,
  SymbolDispose,
  SymbolToPrimitive,
} = primordials;

const binding = internalBinding('timers');
const {
  immediateInfo,
} = binding;
const L = require('internal/linkedlist');
const {
  async_id_symbol,
  Timeout,
  Immediate,
  decRefCount,
  immediateInfoFields: {
    kCount,
    kRefCount,
  },
  kRefed,
  kHasPrimitive,
  timerListMap,
  timerListQueue,
  immediateQueue,
  insert,
  knownTimersById,
} = require('internal/timers');
const {
  promisify: { custom: customPromisify },
} = require('internal/util');
let debug = require('internal/util/debuglog').debuglog('timer', (fn) => {
  debug = fn;
});
const { validateFunction } = require('internal/validators');

let timersPromises;
let timers;

const {
  destroyHooksExist,
  // The needed emit*() functions.
  emitDestroy,
} = require('internal/async_hooks');

// Remove a timer. Cancels the timeout and resets the relevant timer properties.
function unenroll(item) {
  if (item._destroyed)
    return;

  item._destroyed = true;

  if (item[kHasPrimitive])
    delete knownTimersById[item[async_id_symbol]];

  // Fewer checks may be possible, but these cover everything.
  if (destroyHooksExist() && item[async_id_symbol] !== undefined)
    emitDestroy(item[async_id_symbol]);

  L.remove(item);

  // We only delete refed lists because unrefed ones are incredibly likely
  // to come from http and be recreated shortly after.
  // TODO: Long-term this could instead be handled by creating an internal
  // clearTimeout that makes it clear that the list should not be deleted.
  // That function could then be used by http and other similar modules.
  if (item[kRefed]) {
    // Compliment truncation during insert().
    const msecs = MathTrunc(item._idleTimeout);
    const list = timerListMap[msecs];
    if (list !== undefined && L.isEmpty(list)) {
      debug('unenroll: list empty');
      timerListQueue.removeAt(list.priorityQueuePosition);
      delete timerListMap[list.msecs];
    }

    decRefCount();
  }

  // If active is called later, then we want to make sure not to insert again
  item._idleTimeout = -1;
}


/**
 * Schedules the execution of a one-time `callback`
 * after `after` milliseconds.
 * @param {Function} callback
 * @param {number} [after]
 * @param {...any} [args]
 * @returns {Timeout}
 */
function setTimeout(callback, after, ...args) {
  validateFunction(callback, 'callback');
  const timeout = new Timeout(callback, after, args.length ? args : undefined, false, true);
  insert(timeout, timeout._idleTimeout);
  return timeout;
}

ObjectDefineProperty(setTimeout, customPromisify, {
  __proto__: null,
  enumerable: true,
  get() {
    timersPromises ??= require('timers/promises');
    return timersPromises.setTimeout;
  },
});

/**
 * Cancels a timeout.
 * @param {Timeout | string | number} timer
 * @returns {void}
 */
function clearTimeout(timer) {
  if (timer?._onTimeout) {
    timer._onTimeout = null;
    unenroll(timer);
    return;
  }
  if (typeof timer === 'number' || typeof timer === 'string') {
    const timerInstance = knownTimersById[timer];
    if (timerInstance !== undefined) {
      timerInstance._onTimeout = null;
      unenroll(timerInstance);
    }
  }
}

/**
 * Schedules repeated execution of `callback`
 * every `repeat` milliseconds.
 * @param {Function} callback
 * @param {number} [repeat]
 * @param {...any} [args]
 * @returns {Timeout}
 */
function setInterval(callback, repeat, ...args) {
  validateFunction(callback, 'callback');
  const timeout = new Timeout(callback, repeat, args.length ? args : undefined, true, true);
  insert(timeout, timeout._idleTimeout);
  return timeout;
}

/**
 * Cancels an interval.
 * @param {Timeout | string | number} timer
 * @returns {void}
 */
function clearInterval(timer) {
  // clearTimeout and clearInterval can be used to clear timers created from
  // both setTimeout and setInterval, as specified by HTML Living Standard:
  // https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#dom-setinterval
  clearTimeout(timer);
}

Timeout.prototype.close = function() {
  clearTimeout(this);
  return this;
};

Timeout.prototype[SymbolDispose] = function() {
  clearTimeout(this);
};

/**
 * Coerces a `Timeout` to a primitive.
 * @returns {number}
 */
Timeout.prototype[SymbolToPrimitive] = function() {
  const id = this[async_id_symbol];
  if (!this[kHasPrimitive]) {
    this[kHasPrimitive] = true;
    knownTimersById[id] = this;
  }
  return id;
};

/**
 * Schedules the immediate execution of `callback`
 * after I/O events' callbacks.
 * @param {Function} callback
 * @param {...any} [args]
 * @returns {Immediate}
 */
function setImmediate(callback, ...args) {
  validateFunction(callback, 'callback');
  return new Immediate(callback, args.length ? args : undefined);
}

ObjectDefineProperty(setImmediate, customPromisify, {
  __proto__: null,
  enumerable: true,
  get() {
    timersPromises ??= require('timers/promises');
    return timersPromises.setImmediate;
  },
});

/**
 * Cancels an immediate.
 * @param {Immediate} immediate
 * @returns {void}
 */
function clearImmediate(immediate) {
  if (!immediate?._onImmediate || immediate._destroyed)
    return;

  immediateInfo[kCount]--;
  immediate._destroyed = true;

  if (immediate[kRefed] && --immediateInfo[kRefCount] === 0) {
    // We need to use the binding as the receiver for fast API calls.
    binding.toggleImmediateRef(false);
  }
  immediate[kRefed] = null;

  if (destroyHooksExist() && immediate[async_id_symbol] !== undefined) {
    emitDestroy(immediate[async_id_symbol]);
  }

  immediate._onImmediate = null;

  immediateQueue.remove(immediate);
}

Immediate.prototype[SymbolDispose] = function() {
  clearImmediate(this);
};

module.exports = timers = {
  setTimeout,
  clearTimeout,
  setImmediate,
  clearImmediate,
  setInterval,
  clearInterval,
};

ObjectDefineProperties(timers, {
  promises: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      timersPromises ??= require('timers/promises');
      return timersPromises;
    },
  },
});
