'use strict';

const {
  ArrayPrototypeAt,
  ArrayPrototypeForEach,
  ArrayPrototypeIncludes,
  DatePrototypeGetTime,
  DatePrototypeToString,
  FunctionPrototypeApply,
  FunctionPrototypeBind,
  FunctionPrototypeToString,
  NumberIsNaN,
  ObjectDefineProperties,
  ObjectDefineProperty,
  ObjectGetOwnPropertyDescriptor,
  ObjectGetOwnPropertyDescriptors,
  Promise,
  Symbol,
  SymbolAsyncIterator,
  SymbolDispose,
  globalThis,
} = primordials;

const {
  validateAbortSignal,
  validateNumber,
  validateStringArray,
} = require('internal/validators');

const {
  AbortError,
  codes: {
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
  },
} = require('internal/errors');

const { TIMEOUT_MAX } = require('internal/timers');

const PriorityQueue = require('internal/priority_queue');
const nodeTimers = require('timers');
const nodeTimersPromises = require('timers/promises');
const EventEmitter = require('events');

let kResistStopPropagation;
// Internal reference to the MockTimers class inside MockDate
let kMock;
// Initial epoch to which #now should be set to
const kInitialEpoch = 0;

function compareTimersLists(a, b) {
  return (a.runAt - b.runAt) || (a.id - b.id);
}

function setPosition(node, pos) {
  node.priorityQueuePosition = pos;
}

function abortIt(signal) {
  return new AbortError(undefined, { __proto__: null, cause: signal.reason });
}

/**
 * @enum {('setTimeout'|'setInterval'|'setImmediate'|'Date', 'scheduler.wait')[]} Supported timers
 */
const SUPPORTED_APIS = ['setTimeout', 'setInterval', 'setImmediate', 'Date', 'scheduler.wait'];
const TIMERS_DEFAULT_INTERVAL = {
  __proto__: null,
  setImmediate: -1,
};

class Timeout {
  constructor(opts) {
    this.id = opts.id;
    this.callback = opts.callback;
    this.runAt = opts.runAt;
    this.interval = opts.interval;
    this.args = opts.args;
  }

  hasRef() {
    return true;
  }

  ref() {
    return this;
  }

  unref() {
    return this;
  }

  refresh() {
    return this;
  }
}

class MockTimers {
  #realSetTimeout;
  #realClearTimeout;
  #realSetInterval;
  #realClearInterval;
  #realSetImmediate;
  #realClearImmediate;

  #realPromisifiedSetTimeout;
  #realPromisifiedSetInterval;
  #realTimersPromisifiedSchedulerWait;

  #realTimersSetTimeout;
  #realTimersClearTimeout;
  #realTimersSetInterval;
  #realTimersClearInterval;
  #realTimersSetImmediate;
  #realTimersClearImmediate;
  #realPromisifiedSetImmediate;

  #nativeDateDescriptor;

  #timersInContext = [];
  #isEnabled = false;
  #currentTimer = 1;
  #now = kInitialEpoch;

  #executionQueue = new PriorityQueue(compareTimersLists, setPosition);

  #setTimeout = FunctionPrototypeBind(this.#createTimer, this, false);
  #clearTimeout = FunctionPrototypeBind(this.#clearTimer, this);
  #setInterval = FunctionPrototypeBind(this.#createTimer, this, true);
  #clearInterval = FunctionPrototypeBind(this.#clearTimer, this);
  #clearImmediate = FunctionPrototypeBind(this.#clearTimer, this);

  #restoreSetImmediate() {
    ObjectDefineProperty(
      globalThis,
      'setImmediate',
      this.#realSetImmediate,
    );
    ObjectDefineProperty(
      globalThis,
      'clearImmediate',
      this.#realClearImmediate,
    );
    ObjectDefineProperty(
      nodeTimers,
      'setImmediate',
      this.#realTimersSetImmediate,
    );
    ObjectDefineProperty(
      nodeTimers,
      'clearImmediate',
      this.#realTimersClearImmediate,
    );
    ObjectDefineProperty(
      nodeTimersPromises,
      'setImmediate',
      this.#realPromisifiedSetImmediate,
    );
  }

  #restoreOriginalSetInterval() {
    ObjectDefineProperty(
      globalThis,
      'setInterval',
      this.#realSetInterval,
    );
    ObjectDefineProperty(
      globalThis,
      'clearInterval',
      this.#realClearInterval,
    );
    ObjectDefineProperty(
      nodeTimers,
      'setInterval',
      this.#realTimersSetInterval,
    );
    ObjectDefineProperty(
      nodeTimers,
      'clearInterval',
      this.#realTimersClearInterval,
    );
    ObjectDefineProperty(
      nodeTimersPromises,
      'setInterval',
      this.#realPromisifiedSetInterval,
    );
  }

  #restoreOriginalSchedulerWait() {
    nodeTimersPromises.scheduler.wait = FunctionPrototypeBind(
      this.#realTimersPromisifiedSchedulerWait,
      this,
    );
  }

  #restoreOriginalSetTimeout() {
    ObjectDefineProperty(
      globalThis,
      'setTimeout',
      this.#realSetTimeout,
    );
    ObjectDefineProperty(
      globalThis,
      'clearTimeout',
      this.#realClearTimeout,
    );
    ObjectDefineProperty(
      nodeTimers,
      'setTimeout',
      this.#realTimersSetTimeout,
    );
    ObjectDefineProperty(
      nodeTimers,
      'clearTimeout',
      this.#realTimersClearTimeout,
    );
    ObjectDefineProperty(
      nodeTimersPromises,
      'setTimeout',
      this.#realPromisifiedSetTimeout,
    );
  }

  #storeOriginalSetImmediate() {
    this.#realSetImmediate = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'setImmediate',
    );
    this.#realClearImmediate = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'clearImmediate',
    );
    this.#realTimersSetImmediate = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'setImmediate',
    );
    this.#realTimersClearImmediate = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'clearImmediate',
    );
    this.#realPromisifiedSetImmediate = ObjectGetOwnPropertyDescriptor(
      nodeTimersPromises,
      'setImmediate',
    );
  }

  #storeOriginalSetInterval() {
    this.#realSetInterval = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'setInterval',
    );
    this.#realClearInterval = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'clearInterval',
    );
    this.#realTimersSetInterval = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'setInterval',
    );
    this.#realTimersClearInterval = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'clearInterval',
    );
    this.#realPromisifiedSetInterval = ObjectGetOwnPropertyDescriptor(
      nodeTimersPromises,
      'setInterval',
    );
  }

  #storeOriginalSchedulerWait() {

    this.#realTimersPromisifiedSchedulerWait = FunctionPrototypeBind(
      nodeTimersPromises.scheduler.wait,
      this,
    );
  }

  #storeOriginalSetTimeout() {
    this.#realSetTimeout = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'setTimeout',
    );
    this.#realClearTimeout = ObjectGetOwnPropertyDescriptor(
      globalThis,
      'clearTimeout',
    );
    this.#realTimersSetTimeout = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'setTimeout',
    );
    this.#realTimersClearTimeout = ObjectGetOwnPropertyDescriptor(
      nodeTimers,
      'clearTimeout',
    );
    this.#realPromisifiedSetTimeout = ObjectGetOwnPropertyDescriptor(
      nodeTimersPromises,
      'setTimeout',
    );
  }

  #createTimer(isInterval, callback, delay, ...args) {
    if (delay > TIMEOUT_MAX) {
      delay = 1;
    }

    const timerId = this.#currentTimer++;
    const opts = {
      __proto__: null,
      id: timerId,
      callback,
      runAt: this.#now + delay,
      interval: isInterval ? delay : undefined,
      args,
    };

    const timer = new Timeout(opts);
    this.#executionQueue.insert(timer);
    return timer;
  }

  #clearTimer(timer) {
    if (timer?.priorityQueuePosition !== undefined) {
      this.#executionQueue.removeAt(timer.priorityQueuePosition);
      timer.priorityQueuePosition = undefined;
    }
  }

  #createDate() {
    kMock ??= Symbol('MockTimers');
    const NativeDateConstructor = this.#nativeDateDescriptor.value;
    if (NativeDateConstructor.isMock) {
      throw new ERR_INVALID_STATE('Date is already being mocked!');
    }
    /**
     * Function to mock the Date constructor, treats cases as per ECMA-262
     * and returns a Date object with a mocked implementation
     * @typedef {Date} MockDate
     * @returns {MockDate} a mocked Date object
     */
    function MockDate(year, month, date, hours, minutes, seconds, ms) {
      const mockTimersSource = MockDate[kMock];
      const nativeDate = mockTimersSource.#nativeDateDescriptor.value;

      // As of the fake-timers implementation for Sinon
      // ref https://github.com/sinonjs/fake-timers/blob/a4c757f80840829e45e0852ea1b17d87a998388e/src/fake-timers-src.js#L456
      // This covers the Date constructor called as a function ref.
      // ECMA-262 Edition 5.1 section 15.9.2.
      // and ECMA-262 Edition 14 Section 21.4.2.1
      // replaces 'this instanceof MockDate' with a more reliable check
      // from ECMA-262 Edition 14 Section 13.3.12.1 NewTarget
      if (!new.target) {
        return DatePrototypeToString(new nativeDate(mockTimersSource.#now));
      }

      // Cases where Date is called as a constructor
      // This is intended as a defensive implementation to avoid
      // having unexpected returns
      switch (arguments.length) {
        case 0:
          return new nativeDate(MockDate[kMock].#now);
        case 1:
          return new nativeDate(year);
        case 2:
          return new nativeDate(year, month);
        case 3:
          return new nativeDate(year, month, date);
        case 4:
          return new nativeDate(year, month, date, hours);
        case 5:
          return new nativeDate(year, month, date, hours, minutes);
        case 6:
          return new nativeDate(year, month, date, hours, minutes, seconds);
        default:
          return new nativeDate(year, month, date, hours, minutes, seconds, ms);
      }
    }

    // Prototype is read-only, and non assignable through Object.defineProperties
    // eslint-disable-next-line no-unused-vars -- used to get the prototype out of the object
    const { prototype, ...dateProps } = ObjectGetOwnPropertyDescriptors(NativeDateConstructor);

    // Binds all the properties of Date to the MockDate function
    ObjectDefineProperties(
      MockDate,
      dateProps,
    );

    MockDate.now = function now() {
      return MockDate[kMock].#now;
    };

    // This is just to print the function { native code } in the console
    // when the user prints the function and not the internal code
    MockDate.toString = function toString() {
      return FunctionPrototypeToString(MockDate[kMock].#nativeDateDescriptor.value);
    };

    // We need to pollute the prototype of this
    ObjectDefineProperties(MockDate, {
      __proto__: null,
      [kMock]: {
        __proto__: null,
        enumerable: false,
        configurable: false,
        writable: false,
        value: this,
      },

      isMock: {
        __proto__: null,
        enumerable: true,
        configurable: false,
        writable: false,
        value: true,
      },
    });

    MockDate.prototype = NativeDateConstructor.prototype;
    MockDate.parse = NativeDateConstructor.parse;
    MockDate.UTC = NativeDateConstructor.UTC;
    MockDate.prototype.toUTCString = NativeDateConstructor.prototype.toUTCString;
    return MockDate;
  }

  async * #setIntervalPromisified(interval, result, options) {
    const context = this;
    const emitter = new EventEmitter();
    if (options?.signal) {
      validateAbortSignal(options.signal, 'options.signal');

      if (options.signal.aborted) {
        throw abortIt(options.signal);
      }

      const onAbort = (reason) => {
        emitter.emit('data', { __proto__: null, aborted: true, reason });
      };

      kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
      options.signal.addEventListener('abort', onAbort, {
        __proto__: null,
        once: true,
        [kResistStopPropagation]: true,
      });
    }

    const eventIt = EventEmitter.on(emitter, 'data');
    const callback = () => {
      emitter.emit('data', result);
    };

    const timer = this.#createTimer(true, callback, interval, options);
    const clearListeners = () => {
      emitter.removeAllListeners();
      context.#clearTimer(timer);
    };
    const iterator = {
      __proto__: null,
      [SymbolAsyncIterator]() {
        return this;
      },
      async next() {
        const result = await eventIt.next();
        const value = ArrayPrototypeAt(result.value, 0);
        if (value?.aborted) {
          iterator.return();
          throw abortIt(options.signal);
        }

        return {
          __proto__: null,
          done: result.done,
          value,
        };
      },
      async return() {
        clearListeners();
        return eventIt.return();
      },
    };
    yield* iterator;
  }

  #setImmediate(callback, ...args) {
    return this.#createTimer(
      false,
      callback,
      TIMERS_DEFAULT_INTERVAL.setImmediate,
      ...args,
    );
  }

  #promisifyTimer({ timerFn, clearFn, ms, result, options }) {
    return new Promise((resolve, reject) => {
      if (options?.signal) {
        try {
          validateAbortSignal(options.signal, 'options.signal');
        } catch (err) {
          return reject(err);
        }

        if (options.signal.aborted) {
          return reject(abortIt(options.signal));
        }
      }

      const onabort = () => {
        clearFn(timer);
        return reject(abortIt(options.signal));
      };

      const timer = timerFn(() => {
        return resolve(result);
      }, ms);

      if (options?.signal) {
        kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
        options.signal.addEventListener('abort', onabort, {
          __proto__: null,
          once: true,
          [kResistStopPropagation]: true,
        });
      }
    });
  }

  #setImmediatePromisified(result, options) {
    return this.#promisifyTimer({
      __proto__: null,
      timerFn: FunctionPrototypeBind(this.#setImmediate, this),
      clearFn: FunctionPrototypeBind(this.#clearImmediate, this),
      ms: TIMERS_DEFAULT_INTERVAL.setImmediate,
      result,
      options,
    });
  }

  #setTimeoutPromisified(ms, result, options) {
    return this.#promisifyTimer({
      __proto__: null,
      timerFn: FunctionPrototypeBind(this.#setTimeout, this),
      clearFn: FunctionPrototypeBind(this.#clearTimeout, this),
      ms,
      result,
      options,
    });
  }

  #assertTimersAreEnabled() {
    if (!this.#isEnabled) {
      throw new ERR_INVALID_STATE(
        'You should enable MockTimers first by calling the .enable function',
      );
    }
  }

  #assertTimeArg(time) {
    if (time < 0) {
      throw new ERR_INVALID_ARG_VALUE('time', 'positive integer', time);
    }
  }

  #isValidDateWithGetTime(maybeDate) {
    // Validation inspired on https://github.com/inspect-js/is-date-object/blob/main/index.js#L3-L11
    try {
      DatePrototypeGetTime(maybeDate);
      return true;
    } catch {
      return false;
    }
  }

  #toggleEnableTimers(activate) {
    const options = {
      __proto__: null,
      toFake: {
        '__proto__': null,
        'scheduler.wait': () => {
          this.#storeOriginalSchedulerWait();

          nodeTimersPromises.scheduler.wait = (delay, options) =>
            this.#setTimeoutPromisified(delay, undefined, options);
        },
        'setTimeout': () => {
          this.#storeOriginalSetTimeout();

          globalThis.setTimeout = this.#setTimeout;
          globalThis.clearTimeout = this.#clearTimeout;

          nodeTimers.setTimeout = this.#setTimeout;
          nodeTimers.clearTimeout = this.#clearTimeout;

          nodeTimersPromises.setTimeout = FunctionPrototypeBind(
            this.#setTimeoutPromisified,
            this,
          );
        },
        'setInterval': () => {
          this.#storeOriginalSetInterval();

          globalThis.setInterval = this.#setInterval;
          globalThis.clearInterval = this.#clearInterval;

          nodeTimers.setInterval = this.#setInterval;
          nodeTimers.clearInterval = this.#clearInterval;

          nodeTimersPromises.setInterval = FunctionPrototypeBind(
            this.#setIntervalPromisified,
            this,
          );
        },
        'setImmediate': () => {
          this.#storeOriginalSetImmediate();

          // setImmediate functions needs to bind MockTimers
          // otherwise it will throw an error when called
          // "Receiver must be an instance of MockTimers"
          // because #setImmediate is the only function here
          // that calls #createTimer and it's not bound to MockTimers
          globalThis.setImmediate = FunctionPrototypeBind(
            this.#setImmediate,
            this,
          );
          globalThis.clearImmediate = this.#clearImmediate;

          nodeTimers.setImmediate = FunctionPrototypeBind(
            this.#setImmediate,
            this,
          );
          nodeTimers.clearImmediate = this.#clearImmediate;
          nodeTimersPromises.setImmediate = FunctionPrototypeBind(
            this.#setImmediatePromisified,
            this,
          );
        },
        'Date': () => {
          this.#nativeDateDescriptor = ObjectGetOwnPropertyDescriptor(globalThis, 'Date');
          globalThis.Date = this.#createDate();
        },
      },
      toReal: {
        '__proto__': null,
        'scheduler.wait': () => {
          this.#restoreOriginalSchedulerWait();
        },
        'setTimeout': () => {
          this.#restoreOriginalSetTimeout();
        },
        'setInterval': () => {
          this.#restoreOriginalSetInterval();
        },
        'setImmediate': () => {
          this.#restoreSetImmediate();
        },
        'Date': () => {
          ObjectDefineProperty(globalThis, 'Date', this.#nativeDateDescriptor);
        },
      },
    };

    const target = activate ? options.toFake : options.toReal;
    ArrayPrototypeForEach(this.#timersInContext, (timer) => target[timer]());
    this.#isEnabled = activate;
  }

  /**
   * Advances the virtual time of MockTimers by the specified duration (in milliseconds).
   * This method simulates the passage of time and triggers any scheduled timers that are due.
   * @param {number} [time=1] - The amount of time (in milliseconds) to advance the virtual time.
   * @throws {ERR_INVALID_STATE} If MockTimers are not enabled.
   * @throws {ERR_INVALID_ARG_VALUE} If a negative time value is provided.
   */
  tick(time = 1) {
    this.#assertTimersAreEnabled();
    this.#assertTimeArg(time);

    this.#now += time;
    let timer = this.#executionQueue.peek();
    while (timer) {
      if (timer.runAt > this.#now) break;
      FunctionPrototypeApply(timer.callback, undefined, timer.args);

      // Check if the timeout was cleared by calling clearTimeout inside its own callback
      const afterCallback = this.#executionQueue.peek();
      if (afterCallback?.id === timer.id) {
        this.#executionQueue.shift();
        timer.priorityQueuePosition = undefined;
      }

      if (timer.interval !== undefined) {
        timer.runAt += timer.interval;
        this.#executionQueue.insert(timer);
      }

      timer = this.#executionQueue.peek();
    }
  }

  /**
   * @typedef {{apis: SUPPORTED_APIS;now: number | Date;}} EnableOptions Options to enable the timers
   * @property {SUPPORTED_APIS} apis List of timers to enable, defaults to all
   * @property {number | Date} now The epoch to which the timers should be set to, defaults to 0
   */
  /**
   * Enables the MockTimers replacing the native timers with the fake ones.
   * @param {EnableOptions} [options]
   */
  enable(options = { __proto__: null, apis: SUPPORTED_APIS, now: 0 }) {
    const internalOptions = { __proto__: null, ...options };
    if (this.#isEnabled) {
      throw new ERR_INVALID_STATE('MockTimers is already enabled!');
    }

    if (NumberIsNaN(internalOptions.now)) {
      throw new ERR_INVALID_ARG_VALUE('now', internalOptions.now, `epoch must be a positive integer received ${internalOptions.now}`);
    }

    internalOptions.now ||= 0;

    internalOptions.apis ||= SUPPORTED_APIS;

    validateStringArray(internalOptions.apis, 'options.apis');
    // Check that the timers passed are supported
    ArrayPrototypeForEach(internalOptions.apis, (timer) => {
      if (!ArrayPrototypeIncludes(SUPPORTED_APIS, timer)) {
        throw new ERR_INVALID_ARG_VALUE(
          'options.apis',
          timer,
          `option ${timer} is not supported`,
        );
      }
    });
    this.#timersInContext = internalOptions.apis;

    // Checks if the second argument is the initial time
    if (this.#isValidDateWithGetTime(internalOptions.now)) {
      this.#now = DatePrototypeGetTime(internalOptions.now);
    } else if (validateNumber(internalOptions.now, 'initialTime') === undefined) {
      this.#assertTimeArg(internalOptions.now);
      this.#now = internalOptions.now;
    }

    this.#toggleEnableTimers(true);
  }

  /**
   * Sets the current time to the given epoch.
   * @param {number} time The epoch to set the current time to.
   */
  setTime(time = kInitialEpoch) {
    validateNumber(time, 'time');
    this.#assertTimeArg(time);
    this.#assertTimersAreEnabled();

    this.#now = time;
  }

  /**
   * An alias for `this.reset()`, allowing the disposal of the `MockTimers` instance.
   */
  [SymbolDispose]() {
    this.reset();
  }

  /**
   * Resets MockTimers, disabling any enabled timers and clearing the execution queue.
   * Does nothing if MockTimers are not enabled.
   */
  reset() {
    // Ignore if not enabled
    if (!this.#isEnabled) return;

    this.#toggleEnableTimers(false);
    this.#timersInContext = [];
    this.#now = kInitialEpoch;

    let timer = this.#executionQueue.peek();
    while (timer) {
      this.#executionQueue.shift();
      timer = this.#executionQueue.peek();
    }
  }

  /**
   * Runs all scheduled timers until there are no more pending timers.
   * @throws {ERR_INVALID_STATE} If MockTimers are not enabled.
   */
  runAll() {
    this.#assertTimersAreEnabled();
    const longestTimer = this.#executionQueue.peekBottom();
    if (!longestTimer) return;
    this.tick(longestTimer.runAt - this.#now);
  }
}

module.exports = { MockTimers };
