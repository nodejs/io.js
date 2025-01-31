'use strict';

// Modeled very closely on the AbortController implementation
// in https://github.com/mysticatea/abort-controller (MIT license)

const {
  ArrayPrototypePush,
  ObjectAssign,
  ObjectDefineProperties,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  PromiseResolve,
  PromiseWithResolvers,
  SafeFinalizationRegistry,
  SafeSet,
  SafeWeakRef,
  Symbol,
  SymbolToStringTag,
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
const { kMaxEventTargetListeners } = require('events');
const {
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
  converters,
  createInterfaceConverter,
  createSequenceConverter,
} = require('internal/webidl');

const {
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
  markTransferMode,
} = require('internal/worker/js_transferable');

let _MessageChannel;

const kDontThrowSymbol = Symbol('kDontThrowSymbol');

// Loading the MessageChannel and markTransferable have to be done lazily
// because otherwise we'll end up with a require cycle that ends up with
// an incomplete initialization of abort_controller.

function lazyMessageChannel() {
  _MessageChannel ??= require('internal/worker/io').MessageChannel;
  return new _MessageChannel();
}

const clearTimeoutRegistry = new SafeFinalizationRegistry(clearTimeout);
const dependantSignalsCleanupRegistry = new SafeFinalizationRegistry((signalWeakRef) => {
  const signal = signalWeakRef.deref();
  if (signal === undefined) {
    return;
  }
  signal[kDependantSignals].forEach((ref) => {
    if (ref.deref() === undefined) {
      signal[kDependantSignals].delete(ref);
    }
  });
});

const gcPersistentSignals = new SafeSet();

const sourceSignalsCleanupRegistry = new SafeFinalizationRegistry(({ sourceSignalRef, composedSignalRef }) => {
  const composedSignal = composedSignalRef.deref();
  if (composedSignal !== undefined) {
    composedSignal[kSourceSignals].delete(sourceSignalRef);

    if (composedSignal[kSourceSignals].size === 0) {
      // This signal will no longer abort. There's no need to keep it in the gcPersistentSignals set.
      gcPersistentSignals.delete(composedSignal);
    }
  }
});

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

    this[kMaxEventTargetListeners] = 0;
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
      markTransferMode(this, false, true);
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
      setWeakAbortSignalTimeout(new SafeWeakRef(signal), delay));
    return signal;
  }

  /**
   * @param {AbortSignal[]} signals
   * @returns {AbortSignal}
   */
  static any(signals) {
    const signalsArray = converters['sequence<AbortSignal>'](
      signals,
      { __proto__: null, context: 'signals' },
    );

    const resultSignal = new AbortSignal(kDontThrowSymbol, { composite: true });
    if (!signalsArray.length) {
      return resultSignal;
    }
    const resultSignalWeakRef = new SafeWeakRef(resultSignal);
    resultSignal[kSourceSignals] = new SafeSet();
    for (let i = 0; i < signalsArray.length; i++) {
      const signal = signalsArray[i];
      if (signal.aborted) {
        abortSignal(resultSignal, signal.reason);
        return resultSignal;
      }
      signal[kDependantSignals] ??= new SafeSet();
      if (!signal[kComposite]) {
        const signalWeakRef = new SafeWeakRef(signal);
        resultSignal[kSourceSignals].add(signalWeakRef);
        signal[kDependantSignals].add(resultSignalWeakRef);
        dependantSignalsCleanupRegistry.register(resultSignal, signalWeakRef);
        sourceSignalsCleanupRegistry.register(signal, {
          sourceSignalRef: signalWeakRef,
          composedSignalRef: resultSignalWeakRef,
        });
      } else if (!signal[kSourceSignals]) {
        continue;
      } else {
        for (const sourceSignalWeakRef of signal[kSourceSignals]) {
          const sourceSignal = sourceSignalWeakRef.deref();
          if (!sourceSignal) {
            continue;
          }
          assert(!sourceSignal.aborted);
          assert(!sourceSignal[kComposite]);

          if (resultSignal[kSourceSignals].has(sourceSignalWeakRef)) {
            continue;
          }
          resultSignal[kSourceSignals].add(sourceSignalWeakRef);
          sourceSignal[kDependantSignals].add(resultSignalWeakRef);
          dependantSignalsCleanupRegistry.register(resultSignal, sourceSignalWeakRef);
          sourceSignalsCleanupRegistry.register(signal, {
            sourceSignalRef: sourceSignalWeakRef,
            composedSignalRef: resultSignalWeakRef,
          });
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

converters.AbortSignal = createInterfaceConverter('AbortSignal', AbortSignal.prototype);
converters['sequence<AbortSignal>'] = createSequenceConverter(converters.AbortSignal);

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

// https://dom.spec.whatwg.org/#dom-abortsignal-abort
function abortSignal(signal, reason) {
  // 1. If signal is aborted, then return.
  if (signal[kAborted]) return;

  // 2. Set signal's abort reason to reason if it is given;
  //    otherwise to a new "AbortError" DOMException.
  signal[kAborted] = true;
  signal[kReason] = reason;
  // 3. Let dependentSignalsToAbort be a new list.
  const dependentSignalsToAbort = ObjectSetPrototypeOf([], null);
  // 4. For each dependentSignal of signal's dependent signals:
  signal[kDependantSignals]?.forEach((s) => {
    const dependentSignal = s.deref();
    // 1. If dependentSignal is not aborted, then:
    if (dependentSignal && !dependentSignal[kAborted]) {
      // 1. Set dependentSignal's abort reason to signal's abort reason.
      dependentSignal[kReason] = reason;
      dependentSignal[kAborted] = true;
      // 2. Append dependentSignal to dependentSignalsToAbort.
      ArrayPrototypePush(dependentSignalsToAbort, dependentSignal);
    }
  });

  // 5. Run the abort steps for signal
  runAbort(signal);
  // 6. For each dependentSignal of dependentSignalsToAbort,
  //    run the abort steps for dependentSignal.
  for (let i = 0; i < dependentSignalsToAbort.length; i++) {
    const dependentSignal = dependentSignalsToAbort[i];
    runAbort(dependentSignal);
  }
}

// To run the abort steps for an AbortSignal signal
function runAbort(signal) {
  const event = new Event('abort', {
    [kTrustEvent]: true,
  });
  signal.dispatchEvent(event);
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
  markTransferMode(signal, false, true);
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
  converters.AbortSignal(signal, { __proto__: null, context: 'signal' });
  validateObject(resource, 'resource', kValidateObjectAllowObjects);
  if (signal.aborted)
    return PromiseResolve();
  const abortPromise = PromiseWithResolvers();
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
