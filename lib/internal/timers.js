'use strict';

// HOW and WHY the timers implementation works the way it does.
//
// Timers are crucial to Node.js. Internally, any TCP I/O connection creates a
// timer so that we can time out of connections. Additionally, many user
// libraries and applications also use timers. As such there may be a
// significantly large amount of timeouts scheduled at any given time.
// Therefore, it is very important that the timers implementation is performant
// and efficient.
//
// Note: It is suggested you first read through the lib/internal/linkedlist.js
// linked list implementation, since timers depend on it extensively. It can be
// somewhat counter-intuitive at first, as it is not actually a class. Instead,
// it is a set of helpers that operate on an existing object.
//
// In order to be as performant as possible, the architecture and data
// structures are designed so that they are optimized to handle the following
// use cases as efficiently as possible:

// - Adding a new timer. (insert)
// - Removing an existing timer. (remove)
// - Handling a timer timing out. (timeout)
//
// Whenever possible, the implementation tries to make the complexity of these
// operations as close to constant-time as possible.
// (So that performance is not impacted by the number of scheduled timers.)
//
// Object maps are kept which contain linked lists keyed by their duration in
// milliseconds.
//
/* eslint-disable node-core/non-ascii-character */
//
// ╔════ > Object Map
// ║
// ╠══
// ║ lists: { '40': { }, '320': { etc } } (keys of millisecond duration)
// ╚══          ┌────┘
//              │
// ╔══          │
// ║ TimersList { _idleNext: { }, _idlePrev: (self) }
// ║         ┌────────────────┘
// ║    ╔══  │                              ^
// ║    ║    { _idleNext: { },  _idlePrev: { }, _onTimeout: (callback) }
// ║    ║      ┌───────────┘
// ║    ║      │                                  ^
// ║    ║      { _idleNext: { etc },  _idlePrev: { }, _onTimeout: (callback) }
// ╠══  ╠══
// ║    ║
// ║    ╚════ >  Actual JavaScript timeouts
// ║
// ╚════ > Linked List
//
/* eslint-enable node-core/non-ascii-character */
//
// With this, virtually constant-time insertion (append), removal, and timeout
// is possible in the JavaScript layer. Any one list of timers is able to be
// sorted by just appending to it because all timers within share the same
// duration. Therefore, any timer added later will always have been scheduled to
// timeout later, thus only needing to be appended.
// Removal from an object-property linked list is also virtually constant-time
// as can be seen in the lib/internal/linkedlist.js implementation.
// Timeouts only need to process any timers currently due to expire, which will
// always be at the beginning of the list for reasons stated above. Any timers
// after the first one encountered that does not yet need to timeout will also
// always be due to timeout at a later time.
//
// Less-than constant time operations are thus contained in two places:
// The PriorityQueue — an efficient binary heap implementation that does all
// operations in worst-case O(log n) time — which manages the order of expiring
// Timeout lists and the object map lookup of a specific list by the duration of
// timers within (or creation of a new list). However, these operations combined
// have shown to be trivial in comparison to other timers architectures.

const {
  MathMax,
  MathTrunc,
  NumberIsFinite,
  NumberIsNaN,
  NumberMIN_SAFE_INTEGER,
  ReflectApply,
  Symbol,
} = primordials;

const binding = internalBinding('timers');
const {
  immediateInfo,
  timeoutInfo,
} = binding;

const {
  getDefaultTriggerAsyncId,
  newAsyncId,
  initHooksExist,
  destroyHooksExist,
  // The needed emit*() functions.
  emitInit,
  emitBefore,
  emitAfter,
  emitDestroy,
} = require('internal/async_hooks');

// Symbols for storing async id state.
const async_id_symbol = Symbol('asyncId');
const trigger_async_id_symbol = Symbol('triggerId');

const kHasPrimitive = Symbol('kHasPrimitive');

const {
  ERR_OUT_OF_RANGE,
} = require('internal/errors').codes;
const {
  validateFunction,
  validateNumber,
} = require('internal/validators');

const L = require('internal/linkedlist');
const PriorityQueue = require('internal/priority_queue');

const { inspect } = require('internal/util/inspect');
let debug = require('internal/util/debuglog').debuglog('timer', (fn) => {
  debug = fn;
});

const AsyncContextFrame = require('internal/async_context_frame');

const async_context_frame = Symbol('kAsyncContextFrame');

// *Must* match Environment::ImmediateInfo::Fields in src/env.h.
const kCount = 0;
const kRefCount = 1;
const kHasOutstanding = 2;

// Timeout values > TIMEOUT_MAX are set to 1.
const TIMEOUT_MAX = 2 ** 31 - 1;

let timerListId = NumberMIN_SAFE_INTEGER;

const kRefed = Symbol('refed');

let nextExpiry = Infinity;
// timeoutInfo is an Int32Array that contains the reference count of Timeout
// objects at index 0. This is a TypedArray so that GetActiveResourcesInfo() in
// `src/node_process_methods.cc` is able to access this value without crossing
// the JS-C++ boundary, which is slow at the time of writing.
timeoutInfo[0] = 0;

// This is a priority queue with a custom sorting function that first compares
// the expiry times of two lists and if they're the same then compares their
// individual IDs to determine which list was created first.
const timerListQueue = new PriorityQueue(compareTimersLists, setPosition);

// Object map containing linked lists of timers, keyed and sorted by their
// duration in milliseconds.
//
// - key = time in milliseconds
// - value = linked list
const timerListMap = { __proto__: null };

// This stores all the known timer async ids to allow users to clearTimeout and
// clearInterval using those ids, to match the spec and the rest of the web
// platform.
const knownTimersById = { __proto__: null };

function initAsyncResource(resource, type) {
  const asyncId = resource[async_id_symbol] = newAsyncId();
  const triggerAsyncId =
    resource[trigger_async_id_symbol] = getDefaultTriggerAsyncId();
  resource[async_context_frame] = AsyncContextFrame.current();
  if (initHooksExist())
    emitInit(asyncId, type, triggerAsyncId, resource);
}

let warnedNegativeNumber = false;
let warnedNotNumber = false;

class Timeout {
  // Timer constructor function.
  // The entire prototype is defined in lib/timers.js
  constructor(callback, after, args, isRepeat, isRefed) {
    if (after === undefined) {
      after = 1;
    } else {
      after *= 1; // Coalesce to number or NaN
    }

    if (!(after >= 1 && after <= TIMEOUT_MAX)) {
      if (after > TIMEOUT_MAX) {
        process.emitWarning(`${after} does not fit into` +
                            ' a 32-bit signed integer.' +
                            '\nTimeout duration was set to 1.',
                            'TimeoutOverflowWarning');
      } else if (after < 0 && !warnedNegativeNumber) {
        warnedNegativeNumber = true;
        process.emitWarning(`${after} is a negative number.` +
                            '\nTimeout duration was set to 1.',
                            'TimeoutNegativeWarning');
      } else if (NumberIsNaN(after) && !warnedNotNumber) {
        warnedNotNumber = true;
        process.emitWarning(`${after} is not a number.` +
                            '\nTimeout duration was set to 1.',
                            'TimeoutNaNWarning');
      }
      after = 1; // Schedule on next tick, follows browser behavior
    }

    this._idleTimeout = after;
    this._idlePrev = this;
    this._idleNext = this;
    this._idleStart = null;
    // This must be set to null first to avoid function tracking
    // on the hidden class, revisit in V8 versions after 6.2
    this._onTimeout = null;
    this._onTimeout = callback;
    this._timerArgs = args;
    this._repeat = isRepeat ? after : null;
    this._destroyed = false;

    if (isRefed)
      incRefCount();
    this[kRefed] = isRefed;
    this[kHasPrimitive] = false;

    initAsyncResource(this, 'Timeout');
  }

  // Make sure the linked list only shows the minimal necessary information.
  [inspect.custom](_, options) {
    return inspect(this, {
      ...options,
      // Only inspect one level.
      depth: 0,
      // It should not recurse.
      customInspect: false,
    });
  }

  refresh() {
    if (this[kRefed])
      active(this);
    else
      unrefActive(this);

    return this;
  }

  unref() {
    if (this[kRefed]) {
      this[kRefed] = false;
      if (!this._destroyed)
        decRefCount();
    }
    return this;
  }

  ref() {
    if (!this[kRefed]) {
      this[kRefed] = true;
      if (!this._destroyed)
        incRefCount();
    }
    return this;
  }

  hasRef() {
    return this[kRefed];
  }
}

class TimersList {
  constructor(expiry, msecs) {
    this._idleNext = this; // Create the list with the linkedlist properties to
    this._idlePrev = this; // Prevent any unnecessary hidden class changes.
    this.expiry = expiry;
    this.id = timerListId++;
    this.msecs = msecs;
    this.priorityQueuePosition = null;
  }

  // Make sure the linked list only shows the minimal necessary information.
  [inspect.custom](_, options) {
    return inspect(this, {
      ...options,
      // Only inspect one level.
      depth: 0,
      // It should not recurse.
      customInspect: false,
    });
  }
}

// A linked list for storing `setImmediate()` requests
class ImmediateList {
  constructor() {
    this.head = null;
    this.tail = null;
  }

  // Appends an item to the end of the linked list, adjusting the current tail's
  // next pointer and the item's previous pointer where applicable
  append(item) {
    if (this.tail !== null) {
      this.tail._idleNext = item;
      item._idlePrev = this.tail;
    } else {
      this.head = item;
    }
    this.tail = item;
  }

  // Removes an item from the linked list, adjusting the pointers of adjacent
  // items and the linked list's head or tail pointers as necessary
  remove(item) {
    if (item._idleNext) {
      item._idleNext._idlePrev = item._idlePrev;
    }

    if (item._idlePrev) {
      item._idlePrev._idleNext = item._idleNext;
    }

    if (item === this.head)
      this.head = item._idleNext;
    if (item === this.tail)
      this.tail = item._idlePrev;

    item._idleNext = null;
    item._idlePrev = null;
  }
}

// Create a single linked list instance only once at startup
const immediateQueue = new ImmediateList();

function incRefCount() {
  if (timeoutInfo[0]++ === 0) {
    // We need to use the binding as the receiver for fast API calls.
    binding.toggleTimerRef(true);
  }
}

function decRefCount() {
  if (--timeoutInfo[0] === 0) {
    // We need to use the binding as the receiver for fast API calls.
    binding.toggleTimerRef(false);
  }
}

// Schedule or re-schedule a timer.
// The item must have been enroll()'d first.
function active(item) {
  insertGuarded(item, true);
}

// Internal APIs that need timeouts should use `unrefActive()` instead of
// `active()` so that they do not unnecessarily keep the process open.
function unrefActive(item) {
  insertGuarded(item, false);
}

// The underlying logic for scheduling or re-scheduling a timer.
//
// Appends a timer onto the end of an existing timers list, or creates a new
// list if one does not already exist for the specified timeout duration.
function insertGuarded(item, refed, start) {
  const msecs = item._idleTimeout;
  if (msecs < 0 || msecs === undefined)
    return;

  insert(item, msecs, start);

  const isDestroyed = item._destroyed;
  if (isDestroyed || !item[async_id_symbol]) {
    item._destroyed = false;
    initAsyncResource(item, 'Timeout');
  }

  if (isDestroyed) {
    if (refed)
      incRefCount();
  } else if (refed === !item[kRefed]) {
    if (refed)
      incRefCount();
    else
      decRefCount();
  }
  item[kRefed] = refed;
}

// We need to use the binding as the receiver for fast API calls.
function insert(item, msecs, start = binding.getLibuvNow()) {
  // Truncate so that accuracy of sub-millisecond timers is not assumed.
  msecs = MathTrunc(msecs);
  item._idleStart = start;

  // Use an existing list if there is one, otherwise we need to make a new one.
  let list = timerListMap[msecs];
  if (list === undefined) {
    debug('no %d list was found in insert, creating a new one', msecs);
    const expiry = start + msecs;
    timerListMap[msecs] = list = new TimersList(expiry, msecs);
    timerListQueue.insert(list);

    if (nextExpiry > expiry) {
      // We need to use the binding as the receiver for fast API calls.
      binding.scheduleTimer(msecs);
      nextExpiry = expiry;
    }
  }

  L.append(list, item);
}

function setUnrefTimeout(callback, after) {
  // Type checking identical to setTimeout()
  validateFunction(callback, 'callback');

  const timer = new Timeout(callback, after, undefined, false, false);
  insert(timer, timer._idleTimeout);

  return timer;
}

// Type checking used by timers.enroll() and Socket#setTimeout()
function getTimerDuration(msecs, name) {
  validateNumber(msecs, name);
  if (msecs < 0 || !NumberIsFinite(msecs)) {
    throw new ERR_OUT_OF_RANGE(name, 'a non-negative finite number', msecs);
  }

  // Ensure that msecs fits into signed int32
  if (msecs > TIMEOUT_MAX) {
    process.emitWarning(`${msecs} does not fit into a 32-bit signed integer.` +
                        `\nTimer duration was truncated to ${TIMEOUT_MAX}.`,
                        'TimeoutOverflowWarning');
    return TIMEOUT_MAX;
  }

  return msecs;
}

function compareTimersLists(a, b) {
  const expiryDiff = a.expiry - b.expiry;
  if (expiryDiff === 0) {
    if (a.id < b.id)
      return -1;
    if (a.id > b.id)
      return 1;
  }
  return expiryDiff;
}

function setPosition(node, pos) {
  node.priorityQueuePosition = pos;
}

function getTimerCallbacks(runNextTicks) {
  // If an uncaught exception was thrown during execution of immediateQueue,
  // this queue will store all remaining Immediates that need to run upon
  // resolution of all error handling (if process is still alive).
  const outstandingQueue = new ImmediateList();

  function processImmediate() {
    const queue = outstandingQueue.head !== null ?
      outstandingQueue : immediateQueue;
    let immediate = queue.head;

    // Clear the linked list early in case new `setImmediate()`
    // calls occur while immediate callbacks are executed
    if (queue !== outstandingQueue) {
      queue.head = queue.tail = null;
      immediateInfo[kHasOutstanding] = 1;
    }

    let prevImmediate;
    let ranAtLeastOneImmediate = false;
    while (immediate !== null) {
      if (ranAtLeastOneImmediate)
        runNextTicks();
      else
        ranAtLeastOneImmediate = true;

      // It's possible for this current Immediate to be cleared while executing
      // the next tick queue above, which means we need to use the previous
      // Immediate's _idleNext which is guaranteed to not have been cleared.
      if (immediate._destroyed) {
        outstandingQueue.head = immediate = prevImmediate._idleNext;
        continue;
      }

      // TODO(RaisinTen): Destroy and unref the Immediate after _onImmediate()
      // gets executed, just like how Timeouts work.
      immediate._destroyed = true;

      immediateInfo[kCount]--;
      if (immediate[kRefed])
        immediateInfo[kRefCount]--;
      immediate[kRefed] = null;

      prevImmediate = immediate;

      const priorContextFrame =
        AsyncContextFrame.exchange(immediate[async_context_frame]);

      const asyncId = immediate[async_id_symbol];
      emitBefore(asyncId, immediate[trigger_async_id_symbol], immediate);

      try {
        const argv = immediate._argv;
        if (!argv)
          immediate._onImmediate();
        else
          immediate._onImmediate(...argv);
      } finally {
        immediate._onImmediate = null;

        if (destroyHooksExist())
          emitDestroy(asyncId);

        outstandingQueue.head = immediate = immediate._idleNext;
      }

      emitAfter(asyncId);

      AsyncContextFrame.set(priorContextFrame);
    }

    if (queue === outstandingQueue)
      outstandingQueue.head = null;
    immediateInfo[kHasOutstanding] = 0;
  }


  function processTimers(now) {
    debug('process timer lists %d', now);
    nextExpiry = Infinity;

    let list;
    let ranAtLeastOneList = false;
    while ((list = timerListQueue.peek()) != null) {
      if (list.expiry > now) {
        nextExpiry = list.expiry;
        return timeoutInfo[0] > 0 ? nextExpiry : -nextExpiry;
      }
      if (ranAtLeastOneList)
        runNextTicks();
      else
        ranAtLeastOneList = true;
      listOnTimeout(list, now);
    }
    return 0;
  }

  function listOnTimeout(list, now) {
    const msecs = list.msecs;

    debug('timeout callback %d', msecs);

    let ranAtLeastOneTimer = false;
    let timer;
    while ((timer = L.peek(list)) != null) {
      const diff = now - timer._idleStart;

      // Check if this loop iteration is too early for the next timer.
      // This happens if there are more timers scheduled for later in the list.
      if (diff < msecs) {
        list.expiry = MathMax(timer._idleStart + msecs, now + 1);
        list.id = timerListId++;
        timerListQueue.percolateDown(1);
        debug('%d list wait because diff is %d', msecs, diff);
        return;
      }

      if (ranAtLeastOneTimer)
        runNextTicks();
      else
        ranAtLeastOneTimer = true;

      // The actual logic for when a timeout happens.
      L.remove(timer);

      const asyncId = timer[async_id_symbol];

      if (!timer._onTimeout) {
        if (!timer._destroyed) {
          timer._destroyed = true;

          if (timer[kHasPrimitive])
            delete knownTimersById[asyncId];

          if (timer[kRefed])
            timeoutInfo[0]--;

          if (destroyHooksExist())
            emitDestroy(asyncId);
        }
        continue;
      }

      const priorContextFrame =
        AsyncContextFrame.exchange(timer[async_context_frame]);

      emitBefore(asyncId, timer[trigger_async_id_symbol], timer);

      let start;
      if (timer._repeat) {
        // We need to use the binding as the receiver for fast API calls.
        start = binding.getLibuvNow();
      }

      try {
        const args = timer._timerArgs;
        if (args === undefined)
          timer._onTimeout();
        else
          ReflectApply(timer._onTimeout, timer, args);
      } finally {
        if (timer._repeat && timer._idleTimeout !== -1) {
          timer._idleTimeout = timer._repeat;
          insert(timer, timer._idleTimeout, start);
        } else if (!timer._idleNext && !timer._idlePrev && !timer._destroyed) {
          timer._destroyed = true;

          if (timer[kHasPrimitive])
            delete knownTimersById[asyncId];

          if (timer[kRefed])
            timeoutInfo[0]--;

          if (destroyHooksExist())
            emitDestroy(asyncId);
        }
      }

      emitAfter(asyncId);

      AsyncContextFrame.set(priorContextFrame);
    }

    // If `L.peek(list)` returned nothing, the list was either empty or we have
    // called all of the timer timeouts.
    // As such, we can remove the list from the object map and
    // the PriorityQueue.
    debug('%d list empty', msecs);

    // The current list may have been removed and recreated since the reference
    // to `list` was created. Make sure they're the same instance of the list
    // before destroying.
    if (list === timerListMap[msecs]) {
      delete timerListMap[msecs];
      timerListQueue.shift();
    }
  }

  return {
    processImmediate,
    processTimers,
  };
}

class Immediate {
  constructor(callback, args) {
    this._idleNext = null;
    this._idlePrev = null;
    this._onImmediate = callback;
    this._argv = args;
    this._destroyed = false;
    this[kRefed] = false;

    initAsyncResource(this, 'Immediate');

    this.ref();
    immediateInfo[kCount]++;

    immediateQueue.append(this);
  }

  ref() {
    if (this[kRefed] === false) {
      this[kRefed] = true;

      if (immediateInfo[kRefCount]++ === 0) {
        // We need to use the binding as the receiver for fast API calls.
        binding.toggleImmediateRef(true);
      }
    }
    return this;
  }

  unref() {
    if (this[kRefed] === true) {
      this[kRefed] = false;
      if (--immediateInfo[kRefCount] === 0) {
        // We need to use the binding as the receiver for fast API calls.
        binding.toggleImmediateRef(false);
      }
    }
    return this;
  }

  hasRef() {
    return !!this[kRefed];
  }
}

module.exports = {
  TIMEOUT_MAX,
  kTimeout: Symbol('timeout'), // For hiding Timeouts on other internals.
  async_id_symbol,
  trigger_async_id_symbol,
  Timeout,
  Immediate,
  kRefed,
  kHasPrimitive,
  initAsyncResource,
  setUnrefTimeout,
  getTimerDuration,
  immediateQueue,
  getTimerCallbacks,
  immediateInfoFields: {
    kCount,
    kRefCount,
    kHasOutstanding,
  },
  active,
  unrefActive,
  insert,
  timerListMap,
  timerListQueue,
  decRefCount,
  incRefCount,
  knownTimersById,
};
