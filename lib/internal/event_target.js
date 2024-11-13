'use strict';

const {
  ArrayFrom,
  ArrayPrototypeReduce,
  Boolean,
  Error,
  FunctionPrototypeCall,
  NumberIsInteger,
  ObjectAssign,
  ObjectDefineProperties,
  ObjectDefineProperty,
  ObjectGetOwnPropertyDescriptor,
  ReflectApply,
  SafeFinalizationRegistry,
  SafeMap,
  SafeWeakMap,
  SafeWeakRef,
  SafeWeakSet,
  String,
  Symbol,
  SymbolFor,
  SymbolToStringTag,
} = primordials;

const {
  codes: {
    ERR_EVENT_RECURSION,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_THIS,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');
const {
  validateAbortSignal,
  validateObject,
  validateString,
  validateInternalField,
  kValidateObjectAllowObjects,
} = require('internal/validators');

const {
  customInspectSymbol,
  kEmptyObject,
  kEnumerableProperty,
} = require('internal/util');
const { inspect } = require('util');
const webidl = require('internal/webidl');

const kIsEventTarget = SymbolFor('nodejs.event_target');
const kIsNodeEventTarget = Symbol('kIsNodeEventTarget');

const EventEmitter = require('events');
const {
  kMaxEventTargetListeners,
  kMaxEventTargetListenersWarned,
} = EventEmitter;

const kEvents = Symbol('kEvents');
const kIsBeingDispatched = Symbol('kIsBeingDispatched');
const kStop = Symbol('kStop');
const kTarget = Symbol('kTarget');
const kHandlers = Symbol('kHandlers');
const kWeakHandler = Symbol('kWeak');
const kResistStopPropagation = Symbol('kResistStopPropagation');

const kHybridDispatch = SymbolFor('nodejs.internal.kHybridDispatch');
const kRemoveWeakListenerHelper = Symbol('kRemoveWeakListenerHelper');
const kCreateEvent = Symbol('kCreateEvent');
const kNewListener = Symbol('kNewListener');
const kRemoveListener = Symbol('kRemoveListener');
const kIsNodeStyleListener = Symbol('kIsNodeStyleListener');
const kTrustEvent = Symbol('kTrustEvent');

const { now } = require('internal/perf/utils');

const kType = Symbol('type');
const kDetail = Symbol('detail');

const isTrustedSet = new SafeWeakSet();
const isTrusted = ObjectGetOwnPropertyDescriptor({
  get isTrusted() {
    return isTrustedSet.has(this);
  },
}, 'isTrusted').get;

const isTrustedDescriptor = {
  __proto__: null,
  configurable: false,
  enumerable: true,
  get: isTrusted,
};

function isEvent(value) {
  return typeof value?.[kType] === 'string';
}

class Event {
  #cancelable = false;
  #bubbles = false;
  #composed = false;
  #defaultPrevented = false;
  #timestamp = now();
  #propagationStopped = false;

  /**
   * @param {string} type
   * @param {{
   *   bubbles?: boolean,
   *   cancelable?: boolean,
   *   composed?: boolean,
   * }} [options]
   */
  constructor(type, options = undefined) {
    if (arguments.length === 0)
      throw new ERR_MISSING_ARGS('type');
    if (options != null)
      validateObject(options, 'options');
    this.#bubbles = !!options?.bubbles;
    this.#cancelable = !!options?.cancelable;
    this.#composed = !!options?.composed;

    this[kType] = `${type}`;
    if (options?.[kTrustEvent]) {
      isTrustedSet.add(this);
    }

    this[kTarget] = null;
    this[kIsBeingDispatched] = false;
  }

  /**
   * @param {string} type
   * @param {boolean} [bubbles]
   * @param {boolean} [cancelable]
   */
  initEvent(type, bubbles = false, cancelable = false) {
    if (arguments.length === 0)
      throw new ERR_MISSING_ARGS('type');

    if (this[kIsBeingDispatched]) {
      return;
    }
    this[kType] = `${type}`;
    this.#bubbles = !!bubbles;
    this.#cancelable = !!cancelable;
  }

  [customInspectSymbol](depth, options) {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    const name = this.constructor.name;
    if (depth < 0)
      return name;

    const opts = ObjectAssign({}, options, {
      depth: NumberIsInteger(options.depth) ? options.depth - 1 : options.depth,
    });

    return `${name} ${inspect({
      type: this[kType],
      defaultPrevented: this.#defaultPrevented,
      cancelable: this.#cancelable,
      timeStamp: this.#timestamp,
    }, opts)}`;
  }

  stopImmediatePropagation() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    // Spec mention "stopImmediatePropagation should set both "stop propagation"
    // and "stop immediate propagation" flags"
    // cf: from https://dom.spec.whatwg.org/#dom-event-stopimmediatepropagation
    this.stopPropagation();
    this[kStop] = true;
  }

  preventDefault() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    this.#defaultPrevented = true;
  }

  /**
   * @type {EventTarget}
   */
  get target() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kTarget];
  }

  /**
   * @type {EventTarget}
   */
  get currentTarget() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kIsBeingDispatched] ? this[kTarget] : null;
  }

  /**
   * @type {EventTarget}
   */
  get srcElement() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kTarget];
  }

  /**
   * @type {string}
   */
  get type() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kType];
  }

  /**
   * @type {boolean}
   */
  get cancelable() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#cancelable;
  }

  /**
   * @type {boolean}
   */
  get defaultPrevented() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#cancelable && this.#defaultPrevented;
  }

  /**
   * @type {number}
   */
  get timeStamp() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#timestamp;
  }


  // The following are non-op and unused properties/methods from Web API Event.
  // These are not supported in Node.js and are provided purely for
  // API completeness.
  /**
   * @returns {EventTarget[]}
   */
  composedPath() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kIsBeingDispatched] ? [this[kTarget]] : [];
  }

  /**
   * @type {boolean}
   */
  get returnValue() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return !this.#cancelable || !this.#defaultPrevented;
  }

  /**
   * @type {boolean}
   */
  get bubbles() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#bubbles;
  }

  /**
   * @type {boolean}
   */
  get composed() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#composed;
  }

  /**
   * @type {number}
   */
  get eventPhase() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this[kIsBeingDispatched] ? Event.AT_TARGET : Event.NONE;
  }

  /**
   * @type {boolean}
   */
  get cancelBubble() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    return this.#propagationStopped;
  }

  /**
   * @type {boolean}
   */
  set cancelBubble(value) {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    if (value) {
      this.#propagationStopped = true;
    }
  }

  stopPropagation() {
    if (!isEvent(this))
      throw new ERR_INVALID_THIS('Event');
    this.#propagationStopped = true;
  }
}

ObjectDefineProperties(
  Event.prototype, {
    [SymbolToStringTag]: {
      __proto__: null,
      writable: false,
      enumerable: false,
      configurable: true,
      value: 'Event',
    },
    initEvent: kEnumerableProperty,
    stopImmediatePropagation: kEnumerableProperty,
    preventDefault: kEnumerableProperty,
    target: kEnumerableProperty,
    currentTarget: kEnumerableProperty,
    srcElement: kEnumerableProperty,
    type: kEnumerableProperty,
    cancelable: kEnumerableProperty,
    defaultPrevented: kEnumerableProperty,
    timeStamp: kEnumerableProperty,
    composedPath: kEnumerableProperty,
    returnValue: kEnumerableProperty,
    bubbles: kEnumerableProperty,
    composed: kEnumerableProperty,
    eventPhase: kEnumerableProperty,
    cancelBubble: kEnumerableProperty,
    stopPropagation: kEnumerableProperty,
    // Don't conform to the spec with isTrusted. The spec defines it as
    // LegacyUnforgeable but defining it in the constructor has a big
    // performance impact and the property doesn't seem to be useful outside of
    // browsers.
    isTrusted: isTrustedDescriptor,
  });

const staticProps = ['NONE', 'CAPTURING_PHASE', 'AT_TARGET', 'BUBBLING_PHASE'];

ObjectDefineProperties(
  Event,
  ArrayPrototypeReduce(staticProps, (result, staticProp, index = 0) => {
    result[staticProp] = {
      __proto__: null,
      writable: false,
      configurable: false,
      enumerable: true,
      value: index,
    };
    return result;
  }, {}),
);

function isCustomEvent(value) {
  return isEvent(value) && (value?.[kDetail] !== undefined);
}

class CustomEvent extends Event {
  /**
   * @constructor
   * @param {string} type
   * @param {{
   *   bubbles?: boolean,
   *   cancelable?: boolean,
   *   composed?: boolean,
   *   detail?: any,
   * }} [options]
   */
  constructor(type, options = kEmptyObject) {
    if (arguments.length === 0)
      throw new ERR_MISSING_ARGS('type');
    super(type, options);
    this[kDetail] = options?.detail ?? null;
  }

  /**
   * @type {any}
   */
  get detail() {
    if (!isCustomEvent(this))
      throw new ERR_INVALID_THIS('CustomEvent');
    return this[kDetail];
  }
}

ObjectDefineProperties(CustomEvent.prototype, {
  [SymbolToStringTag]: {
    __proto__: null,
    writable: false,
    enumerable: false,
    configurable: true,
    value: 'CustomEvent',
  },
  detail: kEnumerableProperty,
});

// Weak listener cleanup
// This has to be lazy for snapshots to work
let weakListenersState = null;
// The resource needs to retain the callback so that it doesn't
// get garbage collected now that it's weak.
let objectToWeakListenerMap = null;
function weakListeners() {
  weakListenersState ??= new SafeFinalizationRegistry(
    ({ eventTarget, listener, eventType }) => eventTarget.deref()?.[kRemoveWeakListenerHelper](eventType, listener),
  );
  objectToWeakListenerMap ??= new SafeWeakMap();
  return { registry: weakListenersState, map: objectToWeakListenerMap };
}

const kFlagOnce = 1 << 0;
const kFlagCapture = 1 << 1;
const kFlagPassive = 1 << 2;
const kFlagNodeStyle = 1 << 3;
const kFlagWeak = 1 << 4;
const kFlagRemoved = 1 << 5;
const kFlagResistStopPropagation = 1 << 6;

// The listeners for an EventTarget are maintained as a linked list.
// Unfortunately, the way EventTarget is defined, listeners are accounted
// using the tuple [handler,capture], and even if we don't actually make
// use of capture or bubbling, in order to be spec compliant we have to
// take on the additional complexity of supporting it. Fortunately, using
// the linked list makes dispatching faster, even if adding/removing is
// slower.
class Listener {
  constructor(eventTarget, eventType, previous, listener, once, capture, passive,
              isNodeStyleListener, weak, resistStopPropagation) {
    this.next = undefined;
    if (previous !== undefined)
      previous.next = this;
    this.previous = previous;
    this.listener = listener;

    let flags = 0b0;
    if (once)
      flags |= kFlagOnce;
    if (capture)
      flags |= kFlagCapture;
    if (passive)
      flags |= kFlagPassive;
    if (isNodeStyleListener)
      flags |= kFlagNodeStyle;
    if (weak)
      flags |= kFlagWeak;
    if (resistStopPropagation)
      flags |= kFlagResistStopPropagation;
    this.flags = flags;

    this.removed = false;

    if (this.weak) {
      this.callback = new SafeWeakRef(listener);
      weakListeners().registry.register(listener, {
        __proto__: null,
        // Weak ref so the listener won't hold the eventTarget alive
        eventTarget: new SafeWeakRef(eventTarget),
        listener: this,
        eventType,
      }, this);
      // Make the retainer retain the listener in a WeakMap
      weakListeners().map.set(weak, listener);
      this.listener = this.callback;
    } else if (typeof listener === 'function') {
      this.callback = listener;
      this.listener = listener;
    } else {
      this.callback = async (...args) => {
        if (listener.handleEvent)
          await ReflectApply(listener.handleEvent, listener, args);
      };
      this.listener = listener;
    }
  }

  get once() {
    return Boolean(this.flags & kFlagOnce);
  }
  get capture() {
    return Boolean(this.flags & kFlagCapture);
  }
  get passive() {
    return Boolean(this.flags & kFlagPassive);
  }
  get isNodeStyleListener() {
    return Boolean(this.flags & kFlagNodeStyle);
  }
  get weak() {
    return Boolean(this.flags & kFlagWeak);
  }
  get resistStopPropagation() {
    return Boolean(this.flags & kFlagResistStopPropagation);
  }
  get removed() {
    return Boolean(this.flags & kFlagRemoved);
  }
  set removed(value) {
    if (value)
      this.flags |= kFlagRemoved;
    else
      this.flags &= ~kFlagRemoved;
  }

  same(listener, capture) {
    const myListener = this.weak ? this.listener.deref() : this.listener;
    return myListener === listener && this.capture === capture;
  }

  remove() {
    if (this.previous !== undefined)
      this.previous.next = this.next;
    if (this.next !== undefined)
      this.next.previous = this.previous;
    this.removed = true;
    if (this.weak)
      weakListeners().registry.unregister(this);
  }
}

function initEventTarget(self) {
  self[kEvents] = new SafeMap();
  self[kMaxEventTargetListeners] = EventEmitter.defaultMaxListeners;
  self[kMaxEventTargetListenersWarned] = false;
  self[kHandlers] = new SafeMap();
}

class EventTarget {
  // Used in checking whether an object is an EventTarget. This is a well-known
  // symbol as EventTarget may be used cross-realm.
  // Ref: https://github.com/nodejs/node/pull/33661
  static [kIsEventTarget] = true;

  constructor() {
    initEventTarget(this);
  }

  [kNewListener](size, type, listener, once, capture, passive, weak) {
    if (this[kMaxEventTargetListeners] > 0 &&
        size > this[kMaxEventTargetListeners] &&
        !this[kMaxEventTargetListenersWarned]) {
      this[kMaxEventTargetListenersWarned] = true;
      // No error code for this since it is a Warning
      // eslint-disable-next-line no-restricted-syntax
      const w = new Error('Possible EventTarget memory leak detected. ' +
                          `${size} ${type} listeners ` +
                          `added to ${inspect(this, { depth: -1 })}. MaxListeners is ${this[kMaxEventTargetListeners]}. Use ` +
                          'events.setMaxListeners() to increase limit');
      w.name = 'MaxListenersExceededWarning';
      w.target = this;
      w.type = type;
      w.count = size;
      process.emitWarning(w);
    }
  }
  [kRemoveListener](size, type, listener, capture) {}

  /**
   * @callback EventTargetCallback
   * @param {Event} event
   */

  /**
   * @typedef {{ handleEvent: EventTargetCallback }} EventListener
   */

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @param {{
   *   capture?: boolean,
   *   once?: boolean,
   *   passive?: boolean,
   *   signal?: AbortSignal
   * }} [options]
   */
  addEventListener(type, listener, options = kEmptyObject) {
    if (!isEventTarget(this))
      throw new ERR_INVALID_THIS('EventTarget');
    if (arguments.length < 2)
      throw new ERR_MISSING_ARGS('type', 'listener');

    let once = false;
    let capture = false;
    let passive = false;
    let isNodeStyleListener = false;
    let weak = false;
    let resistStopPropagation = false;

    if (options !== kEmptyObject) {
      // We validateOptions before the validateListener check because the spec
      // requires us to hit getters.
      options = validateEventListenerOptions(options);

      once = options.once;
      capture = options.capture;
      passive = options.passive;
      isNodeStyleListener = options.isNodeStyleListener;
      weak = options.weak;
      resistStopPropagation = options.resistStopPropagation;

      const signal = options.signal;

      validateAbortSignal(signal, 'options.signal');

      if (signal) {
        if (signal.aborted) {
          return;
        }
        // TODO(benjamingr) make this weak somehow? ideally the signal would
        // not prevent the event target from GC.
        signal.addEventListener('abort', () => {
          this.removeEventListener(type, listener, options);
        }, { __proto__: null, once: true, [kWeakHandler]: this, [kResistStopPropagation]: true });
      }
    }

    if (!validateEventListener(listener)) {
      // The DOM silently allows passing undefined as a second argument
      // No error code for this since it is a Warning
      // eslint-disable-next-line no-restricted-syntax
      const w = new Error(`addEventListener called with ${listener}` +
                          ' which has no effect.');
      w.name = 'AddEventListenerArgumentTypeWarning';
      w.target = this;
      w.type = type;
      process.emitWarning(w);
      return;
    }

    type = webidl.converters.DOMString(type);

    let root = this[kEvents].get(type);

    if (root === undefined) {
      root = { size: 1, next: undefined, resistStopPropagation: Boolean(resistStopPropagation) };
      // This is the first handler in our linked list.
      new Listener(this, type, root, listener, once, capture, passive,
                   isNodeStyleListener, weak, resistStopPropagation);
      this[kNewListener](
        root.size,
        type,
        listener,
        once,
        capture,
        passive,
        weak);
      this[kEvents].set(type, root);
      return;
    }

    let handler = root.next;
    let previous = root;

    // We have to walk the linked list to see if we have a match
    while (handler !== undefined && !handler.same(listener, capture)) {
      previous = handler;
      handler = handler.next;
    }

    if (handler !== undefined) { // Duplicate! Ignore
      return;
    }

    new Listener(this, type, previous, listener, once, capture, passive,
                 isNodeStyleListener, weak, resistStopPropagation);
    root.size++;
    root.resistStopPropagation ||= Boolean(resistStopPropagation);
    this[kNewListener](root.size, type, listener, once, capture, passive, weak);
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @param {{
   *   capture?: boolean,
   * }} [options]
   */
  removeEventListener(type, listener, options = kEmptyObject) {
    if (!isEventTarget(this))
      throw new ERR_INVALID_THIS('EventTarget');
    if (arguments.length < 2)
      throw new ERR_MISSING_ARGS('type', 'listener');
    if (!validateEventListener(listener))
      return;

    type = webidl.converters.DOMString(type);
    const capture = options?.capture === true;

    const root = this[kEvents].get(type);
    if (root === undefined || root.next === undefined)
      return;

    let handler = root.next;
    while (handler !== undefined) {
      if (handler.same(listener, capture)) {
        handler.remove();
        root.size--;
        if (root.size === 0)
          this[kEvents].delete(type);
        this[kRemoveListener](root.size, type, listener, capture);
        break;
      }
      handler = handler.next;
    }
  }

  [kRemoveWeakListenerHelper](type, listener) {
    const root = this[kEvents].get(type);
    if (root === undefined || root.next === undefined)
      return;

    const capture = listener.capture === true;

    let handler = root.next;
    while (handler !== undefined) {
      if (handler === listener) {
        handler.remove();
        root.size--;
        if (root.size === 0)
          this[kEvents].delete(type);
        // Undefined is passed as the listener as the listener was GCed
        this[kRemoveListener](root.size, type, undefined, capture);
        break;
      }
      handler = handler.next;
    }
  }

  /**
   * @param {Event} event
   */
  dispatchEvent(event) {
    if (!isEventTarget(this))
      throw new ERR_INVALID_THIS('EventTarget');
    if (arguments.length < 1)
      throw new ERR_MISSING_ARGS('event');

    if (!(event instanceof Event))
      throw new ERR_INVALID_ARG_TYPE('event', 'Event', event);

    if (event[kIsBeingDispatched])
      throw new ERR_EVENT_RECURSION(event.type);

    this[kHybridDispatch](event, event.type, event);

    return event.defaultPrevented !== true;
  }

  [kHybridDispatch](nodeValue, type, event) {
    const createEvent = () => {
      if (event === undefined) {
        event = this[kCreateEvent](nodeValue, type);
        event[kTarget] = this;
        event[kIsBeingDispatched] = true;
      }
      return event;
    };
    if (event !== undefined) {
      event[kTarget] = this;
      event[kIsBeingDispatched] = true;
    }

    const root = this[kEvents].get(type);
    if (root === undefined || root.next === undefined) {
      if (event !== undefined)
        event[kIsBeingDispatched] = false;
      return true;
    }

    let handler = root.next;
    let next;

    const iterationCondition = () => {
      if (handler === undefined) {
        return false;
      }
      return root.resistStopPropagation || handler.passive || event?.[kStop] !== true;
    };
    while (iterationCondition()) {
      // Cache the next item in case this iteration removes the current one
      next = handler.next;

      if (handler.removed || (event?.[kStop] === true && !handler.resistStopPropagation)) {
        // Deal with the case an event is removed while event handlers are
        // Being processed (removeEventListener called from a listener)
        // And the case of event.stopImmediatePropagation() being called
        // For events not flagged as resistStopPropagation
        handler = next;
        continue;
      }
      if (handler.once) {
        handler.remove();
        root.size--;
        const { listener, capture } = handler;
        this[kRemoveListener](root.size, type, listener, capture);
      }

      try {
        let arg;
        if (handler.isNodeStyleListener) {
          arg = nodeValue;
        } else {
          arg = createEvent();
        }
        const callback = handler.weak ?
          handler.callback.deref() : handler.callback;
        let result;
        if (callback) {
          result = FunctionPrototypeCall(callback, this, arg);
          if (!handler.isNodeStyleListener) {
            arg[kIsBeingDispatched] = false;
          }
        }
        if (result !== undefined && result !== null)
          addCatch(result);
      } catch (err) {
        emitUncaughtException(err);
      }

      handler = next;
    }

    if (event !== undefined)
      event[kIsBeingDispatched] = false;
  }

  [kCreateEvent](nodeValue, type) {
    return new CustomEvent(type, { detail: nodeValue });
  }
  [customInspectSymbol](depth, options) {
    if (!isEventTarget(this))
      throw new ERR_INVALID_THIS('EventTarget');
    const name = this.constructor.name;
    if (depth < 0)
      return name;

    const opts = ObjectAssign({}, options, {
      depth: NumberIsInteger(options.depth) ? options.depth - 1 : options.depth,
    });

    return `${name} ${inspect({}, opts)}`;
  }
}

ObjectDefineProperties(EventTarget.prototype, {
  addEventListener: kEnumerableProperty,
  removeEventListener: kEnumerableProperty,
  dispatchEvent: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    writable: false,
    enumerable: false,
    configurable: true,
    value: 'EventTarget',
  },
});

function initNodeEventTarget(self) {
  initEventTarget(self);
}

class NodeEventTarget extends EventTarget {
  static [kIsNodeEventTarget] = true;
  static defaultMaxListeners = 10;

  constructor() {
    super();
    initNodeEventTarget(this);
  }

  /**
   * @param {number} n
   */
  setMaxListeners(n) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    EventEmitter.setMaxListeners(n, this);
  }

  /**
   * @returns {number}
   */
  getMaxListeners() {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    return this[kMaxEventTargetListeners];
  }

  /**
   * @returns {string[]}
   */
  eventNames() {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    return ArrayFrom(this[kEvents].keys());
  }

  /**
   * @param {string} type
   * @returns {number}
   */
  listenerCount(type) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    const root = this[kEvents].get(String(type));
    return root !== undefined ? root.size : 0;
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @param {{
   *   capture?: boolean,
   * }} [options]
   * @returns {NodeEventTarget}
   */
  off(type, listener, options) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    this.removeEventListener(type, listener, options);
    return this;
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @param {{
   *   capture?: boolean,
   * }} [options]
   * @returns {NodeEventTarget}
   */
  removeListener(type, listener, options) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    this.removeEventListener(type, listener, options);
    return this;
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @returns {NodeEventTarget}
   */
  on(type, listener) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    this.addEventListener(type, listener, { [kIsNodeStyleListener]: true });
    return this;
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @returns {NodeEventTarget}
   */
  addListener(type, listener) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    this.addEventListener(type, listener, { [kIsNodeStyleListener]: true });
    return this;
  }

  /**
   * @param {string} type
   * @param {any} arg
   * @returns {boolean}
   */
  emit(type, arg) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    validateString(type, 'type');
    const hadListeners = this.listenerCount(type) > 0;
    this[kHybridDispatch](arg, type);
    return hadListeners;
  }

  /**
   * @param {string} type
   * @param {EventTargetCallback|EventListener} listener
   * @returns {NodeEventTarget}
   */
  once(type, listener) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    this.addEventListener(type, listener,
                          { once: true, [kIsNodeStyleListener]: true });
    return this;
  }

  /**
   * @param {string} [type]
   * @returns {NodeEventTarget}
   */
  removeAllListeners(type) {
    if (!isNodeEventTarget(this))
      throw new ERR_INVALID_THIS('NodeEventTarget');
    if (type !== undefined) {
      this[kEvents].delete(String(type));
    } else {
      this[kEvents].clear();
    }

    return this;
  }
}

ObjectDefineProperties(NodeEventTarget.prototype, {
  setMaxListeners: kEnumerableProperty,
  getMaxListeners: kEnumerableProperty,
  eventNames: kEnumerableProperty,
  listenerCount: kEnumerableProperty,
  off: kEnumerableProperty,
  removeListener: kEnumerableProperty,
  on: kEnumerableProperty,
  addListener: kEnumerableProperty,
  once: kEnumerableProperty,
  emit: kEnumerableProperty,
  removeAllListeners: kEnumerableProperty,
});

// EventTarget API

function validateEventListener(listener) {
  if (typeof listener === 'function' ||
      typeof listener?.handleEvent === 'function') {
    return true;
  }

  if (listener == null)
    return false;

  if (typeof listener === 'object') {
    // Require `handleEvent` lazily.
    return true;
  }

  throw new ERR_INVALID_ARG_TYPE('listener', 'EventListener', listener);
}

function validateEventListenerOptions(options) {
  if (typeof options === 'boolean')
    return { capture: options };

  if (options === null)
    return kEmptyObject;
  validateObject(options, 'options', kValidateObjectAllowObjects);
  return {
    once: Boolean(options.once),
    capture: Boolean(options.capture),
    passive: Boolean(options.passive),
    signal: options.signal,
    weak: options[kWeakHandler],
    resistStopPropagation: options[kResistStopPropagation] ?? false,
    isNodeStyleListener: Boolean(options[kIsNodeStyleListener]),
  };
}

// Test whether the argument is an event object. This is far from a fool-proof
// test, for example this input will result in a false positive:
// > isEventTarget({ constructor: EventTarget })
// It stands in its current implementation as a compromise.
// Ref: https://github.com/nodejs/node/pull/33661
function isEventTarget(obj) {
  return obj?.constructor?.[kIsEventTarget];
}

function isNodeEventTarget(obj) {
  return obj?.constructor?.[kIsNodeEventTarget];
}

function addCatch(promise) {
  const then = promise.then;
  if (typeof then === 'function') {
    FunctionPrototypeCall(then, promise, undefined, function(err) {
      // The callback is called with nextTick to avoid a follow-up
      // rejection from this promise.
      emitUncaughtException(err);
    });
  }
}

function emitUncaughtException(err) {
  process.nextTick(() => { throw err; });
}

function makeEventHandler(handler) {
  // Event handlers are dispatched in the order they were first set
  // See https://github.com/nodejs/node/pull/35949#issuecomment-722496598
  function eventHandler(...args) {
    if (typeof eventHandler.handler !== 'function') {
      return;
    }
    return ReflectApply(eventHandler.handler, this, args);
  }
  eventHandler.handler = handler;
  return eventHandler;
}

function defineEventHandler(emitter, name, event = name) {
  // 8.1.5.1 Event handlers - basically `on[eventName]` attributes
  const propName = `on${name}`;
  function get() {
    validateInternalField(this, kHandlers, 'EventTarget');
    return this[kHandlers]?.get(event)?.handler ?? null;
  }
  ObjectDefineProperty(get, 'name', {
    __proto__: null,
    value: `get ${propName}`,
  });

  function set(value) {
    validateInternalField(this, kHandlers, 'EventTarget');
    let wrappedHandler = this[kHandlers]?.get(event);
    if (wrappedHandler) {
      if (typeof wrappedHandler.handler === 'function') {
        this[kEvents].get(event).size--;
        const size = this[kEvents].get(event).size;
        this[kRemoveListener](size, event, wrappedHandler.handler, false);
      }
      wrappedHandler.handler = value;
      if (typeof wrappedHandler.handler === 'function') {
        this[kEvents].get(event).size++;
        const size = this[kEvents].get(event).size;
        this[kNewListener](size, event, value, false, false, false, false);
      }
    } else {
      wrappedHandler = makeEventHandler(value);
      this.addEventListener(event, wrappedHandler);
    }
    this[kHandlers].set(event, wrappedHandler);
  }
  ObjectDefineProperty(set, 'name', {
    __proto__: null,
    value: `set ${propName}`,
  });

  ObjectDefineProperty(emitter, propName, {
    __proto__: null,
    get,
    set,
    configurable: true,
    enumerable: true,
  });
}

module.exports = {
  Event,
  CustomEvent,
  EventTarget,
  NodeEventTarget,
  defineEventHandler,
  initEventTarget,
  initNodeEventTarget,
  kCreateEvent,
  kNewListener,
  kTrustEvent,
  kRemoveListener,
  kEvents,
  kWeakHandler,
  kResistStopPropagation,
  isEventTarget,
};
