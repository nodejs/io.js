'use strict';

// Modeled very closely on the AbortController implementation
// in https://github.com/mysticatea/abort-controller (MIT license)

const {
  ObjectAssign,
  ObjectDefineProperties,
  ObjectDefineProperty,
  PromiseResolve,
  SafeFinalizationRegistry,
  SafeSet,
  Symbol,
  SymbolToStringTag,
  WeakRef,
} = primordials;

const {
  defineEventHandler,
  EventTarget,
  Event,
  kTrustEvent,
  kNewListener,
  kRemoveListener,
  kResistStopPropagation,
  kWeakHandler,
} = require('internal/event_target');
const {
  createDeferredPromise,
  customInspectSymbol,
  kEmptyObject,
  kEnumerableProperty,
} = require('internal/util');
const { inspect } = require('internal/util/inspect');
const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  validateAbortSignal,
  validateAbortSignalArray,
  validateObject,
  validateUint32,
  kValidateObjectAllowObjects,
} = require('internal/validators');

const {
  DOMException,
} = internalBinding('messaging');

const {
  clearTimeout,
  setTimeout,
} = require('timers');
const assert = require('internal/assert');

const {
  kDeserialize,
  kTransfer,
  kTransferList,
} = require('internal/worker/js_transferable');

let _MessageChannel;
let markTransferMode;

const kDontThrowSymbol = Symbol('kDontThrowSymbol');

// Loading the MessageChannel and markTransferable have to be done lazily
// because otherwise we'll end up with a require cycle that ends up with
// an incomplete initialization of abort_controller.

function lazyMessageChannel() {
  _MessageChannel ??= require('internal/worker/io').MessageChannel;
  return new _MessageChannel();
}

function lazyMarkTransferMode(obj, cloneable, transferable) {
  markTransferMode ??=
    require('internal/worker/js_transferable').markTransferMode;
  markTransferMode(obj, cloneable, transferable);
}

const clearTimeoutRegistry = new SafeFinalizationRegistry(clearTimeout);
const gcPersistentSignals = new SafeSet();

const kAborted = Symbol('kAborted');
const kReason = Symbol('kReason');
const kCloneData = Symbol('kCloneData');
const kTimeout = Symbol('kTimeout');
const kMakeTransferable = Symbol('kMakeTransferable');
const kComposite = Symbol('kComposite');
const kSourceSignals = Symbol('kSourceSignals');
const kDependantSignals = Symbol('kDependantSignals');

function customInspect(self, obj, depth, options) {
  if (depth < 0)
    return self;

  const opts = ObjectAssign({}, options, {
    depth: options.depth === null ? null : options.depth - 1,
  });

  return `${self.constructor.name} ${inspect(obj, opts)}`;
}

function validateThisAbortSignal(obj) {
  if (obj?.[kAborted] === undefined)
    throw new ERR_INVALID_THIS('AbortSignal');
}

// Because the AbortSignal timeout cannot be canceled, we don't want the
// presence of the timer alone to keep the AbortSignal from being garbage
// collected if it otherwise no longer accessible. We also don't want the
// timer to keep the Node.js process open on it's own. Therefore, we wrap
// the AbortSignal in a WeakRef and have the setTimeout callback close
// over the WeakRef rather than directly over the AbortSignal, and we unref
// the created timer object. Separately, we add the signal to a
// FinalizerRegistry that will clear the timeout when the signal is gc'd.
function setWeakAbortSignalTimeout(weakRef, delay) {
  const timeout = setTimeout(() => {
    const signal = weakRef.deref();
    if (signal !== undefined) {
      gcPersistentSignals.delete(signal);
      abortSignal(
        signal,
        new DOMException(
          'The operation was aborted due to timeout',
          'TimeoutError'));
    }
  }, delay);
  timeout.unref();
  return timeout;
}

class AbortSignal extends EventTarget {

  /**
   * @param {symbol | undefined} dontThrowSymbol
   * @param {{
   *   aborted? : boolean,
   *   reason? : any,
   *   transferable? : boolean,
   *   composite? : boolean,
   * }} [init]
   * @private
   */
  constructor(dontThrowSymbol = undefined, init = kEmptyObject) {
    if (dontThrowSymbol !== kDontThrowSymbol) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    super();

    const {
      aborted = false,
      reason = undefined,
      transferable = false,
      composite = false,
    } = init;
    this[kAborted] = aborted;
    this[kReason] = reason;
    this[kComposite] = composite;
    if (transferable) {
      lazyMarkTransferMode(this, false, true);
    }
  }

  /**
   * @type {boolean}
   */
  get aborted() {
    validateThisAbortSignal(this);
    return !!this[kAborted];
  }

  /**
   * @type {any}
   */
  get reason() {
    validateThisAbortSignal(this);
    return this[kReason];
  }

  throwIfAborted() {
    validateThisAbortSignal(this);
    if (this[kAborted]) {
      throw this[kReason];
    }
  }

  [customInspectSymbol](depth, options) {
    return customInspect(this, {
      aborted: this.aborted,
    }, depth, options);
  }

  /**
   * @param {any} [reason]
   * @returns {AbortSignal}
   */
  static abort(
    reason = new DOMException('This operation was aborted', 'AbortError')) {
    return new AbortSignal(kDontThrowSymbol, { aborted: true, reason });
  }

  /**
   * @param {number} delay
   * @returns {AbortSignal}
   */
  static timeout(delay) {
    validateUint32(delay, 'delay', false);
    const signal = new AbortSignal(kDontThrowSymbol);
    signal[kTimeout] = true;
    clearTimeoutRegistry.register(
      signal,
      setWeakAbortSignalTimeout(new WeakRef(signal), delay));
    return signal;
  }

  /**
   * @param {AbortSignal[]} signals
   * @returns {AbortSignal}
   */
  static any(signals) {
    validateAbortSignalArray(signals, 'signals');
    const resultSignal = new AbortSignal(kDontThrowSymbol, { composite: true });
    if (!signals.length) {
      return resultSignal;
    }
    const resultSignalWeakRef = new WeakRef(resultSignal);
    resultSignal[kSourceSignals] = new SafeSet();
    for (let i = 0; i < signals.length; i++) {
      const signal = signals[i];
      if (signal.aborted) {
        abortSignal(resultSignal, signal.reason);
        return resultSignal;
      }
      signal[kDependantSignals] ??= new SafeSet();
      if (!signal[kComposite]) {
        resultSignal[kSourceSignals].add(new WeakRef(signal));
        signal[kDependantSignals].add(resultSignalWeakRef);
      } else if (!signal[kSourceSignals]) {
        continue;
      } else {
        for (const sourceSignal of signal[kSourceSignals]) {
          const sourceSignalRef = sourceSignal.deref();
          if (!sourceSignalRef) {
            continue;
          }
          assert(!sourceSignalRef.aborted);
          assert(!sourceSignalRef[kComposite]);

          if (resultSignal[kSourceSignals].has(sourceSignal)) {
            continue;
          }
          resultSignal[kSourceSignals].add(sourceSignal);
          sourceSignalRef[kDependantSignals].add(resultSignalWeakRef);
        }
      }
    }
    return resultSignal;
  }

  [kNewListener](size, type, listener, once, capture, passive, weak) {
    super[kNewListener](size, type, listener, once, capture, passive, weak);
    const isTimeoutOrNonEmptyCompositeSignal = this[kTimeout] || (this[kComposite] && this[kSourceSignals]?.size);
    if (isTimeoutOrNonEmptyCompositeSignal &&
        type === 'abort' &&
        !this.aborted &&
        !weak &&
        size === 1) {
      // If this is a timeout signal, or a non-empty composite signal, and we're adding a non-weak abort
      // listener, then we don't want it to be gc'd while the listener
      // is attached and the timer still hasn't fired. So, we retain a
      // strong ref that is held for as long as the listener is registered.
      gcPersistentSignals.add(this);
    }
  }

  [kRemoveListener](size, type, listener, capture) {
    super[kRemoveListener](size, type, listener, capture);
    const isTimeoutOrNonEmptyCompositeSignal = this[kTimeout] || (this[kComposite] && this[kSourceSignals]?.size);
    if (isTimeoutOrNonEmptyCompositeSignal && type === 'abort' && size === 0) {
      gcPersistentSignals.delete(this);
    }
  }

  [kTransfer]() {
    validateThisAbortSignal(this);
    const aborted = this.aborted;
    if (aborted) {
      const reason = this.reason;
      return {
        data: { aborted, reason },
        deserializeInfo: 'internal/abort_controller:ClonedAbortSignal',
      };
    }

    const { port1, port2 } = this[kCloneData];
    this[kCloneData] = undefined;

    this.addEventListener('abort', () => {
      port1.postMessage(this.reason);
      port1.close();
    }, { once: true });

    return {
      data: { port: port2 },
      deserializeInfo: 'internal/abort_controller:ClonedAbortSignal',
    };
  }

  [kTransferList]() {
    if (!this.aborted) {
      const { port1, port2 } = lazyMessageChannel();
      port1.unref();
      port2.unref();
      this[kCloneData] = {
        port1,
        port2,
      };
      return [port2];
    }
    return [];
  }

  [kDeserialize]({ aborted, reason, port }) {
    if (aborted) {
      this[kAborted] = aborted;
      this[kReason] = reason;
      return;
    }

    port.onmessage = ({ data }) => {
      abortSignal(this, data);
      port.close();
      port.onmessage = undefined;
    };
    // The receiving port, by itself, should never keep the event loop open.
    // The unref() has to be called *after* setting the onmessage handler.
    port.unref();
  }
}

function ClonedAbortSignal() {
  return new AbortSignal(kDontThrowSymbol, { transferable: true });
}
ClonedAbortSignal.prototype[kDeserialize] = () => {};

ObjectDefineProperties(AbortSignal.prototype, {
  aborted: kEnumerableProperty,
});

ObjectDefineProperty(AbortSignal.prototype, SymbolToStringTag, {
  __proto__: null,
  writable: false,
  enumerable: false,
  configurable: true,
  value: 'AbortSignal',
});

defineEventHandler(AbortSignal.prototype, 'abort');

function abortSignal(signal, reason) {
  if (signal[kAborted]) return;
  signal[kAborted] = true;
  signal[kReason] = reason;
  const event = new Event('abort', {
    [kTrustEvent]: true,
  });
  signal.dispatchEvent(event);
  signal[kDependantSignals]?.forEach((s) => {
    const signalRef = s.deref();
    if (signalRef) abortSignal(signalRef, reason);
  });
}

class AbortController {
  #signal;

  /**
   * @type {AbortSignal}
   */
  get signal() {
    this.#signal ??= new AbortSignal(kDontThrowSymbol);
    return this.#signal;
  }

  /**
   * @param {any} [reason]
   */
  abort(reason = new DOMException('This operation was aborted', 'AbortError')) {
    abortSignal(this.#signal ??= new AbortSignal(kDontThrowSymbol), reason);
  }

  [customInspectSymbol](depth, options) {
    return customInspect(this, {
      signal: this.signal,
    }, depth, options);
  }

  static [kMakeTransferable]() {
    const controller = new AbortController();
    controller.#signal = new AbortSignal(kDontThrowSymbol, { transferable: true });
    return controller;
  }
}

/**
 * Enables the AbortSignal to be transferable using structuredClone/postMessage.
 * @param {AbortSignal} signal
 * @returns {AbortSignal}
 */
function transferableAbortSignal(signal) {
  if (signal?.[kAborted] === undefined)
    throw new ERR_INVALID_ARG_TYPE('signal', 'AbortSignal', signal);
  lazyMarkTransferMode(signal, false, true);
  return signal;
}

/**
 * Creates an AbortController with a transferable AbortSignal
 */
function transferableAbortController() {
  return AbortController[kMakeTransferable]();
}

/**
 * @param {AbortSignal} signal
 * @param {any} resource
 * @returns {Promise<void>}
 */
async function aborted(signal, resource) {
  if (signal === undefined) {
    throw new ERR_INVALID_ARG_TYPE('signal', 'AbortSignal', signal);
  }
  validateAbortSignal(signal, 'signal');
  validateObject(resource, 'resource', kValidateObjectAllowObjects);
  if (signal.aborted)
    return PromiseResolve();
  const abortPromise = createDeferredPromise();
  const opts = { __proto__: null, [kWeakHandler]: resource, once: true, [kResistStopPropagation]: true };
  signal.addEventListener('abort', abortPromise.resolve, opts);
  return abortPromise.promise;
}

ObjectDefineProperties(AbortController.prototype, {
  signal: kEnumerableProperty,
  abort: kEnumerableProperty,
});

ObjectDefineProperty(AbortController.prototype, SymbolToStringTag, {
  __proto__: null,
  writable: false,
  enumerable: false,
  configurable: true,
  value: 'AbortController',
});

module.exports = {
  AbortController,
  AbortSignal,
  ClonedAbortSignal,
  aborted,
  transferableAbortSignal,
  transferableAbortController,
};
