'use strict';

const {
  ArrayFrom,
  ArrayIsArray,
  ArrayPrototypeFilter,
  ArrayPrototypeIncludes,
  ArrayPrototypePush,
  ArrayPrototypePushApply,
  ArrayPrototypeSlice,
  ArrayPrototypeSort,
  Error,
  MathMax,
  MathMin,
  ObjectDefineProperties,
  ObjectFreeze,
  ObjectKeys,
  SafeMap,
  SafeSet,
  Symbol,
  SymbolToStringTag,
} = primordials;

const {
  constants: {
    NODE_PERFORMANCE_ENTRY_TYPE_GC,
    NODE_PERFORMANCE_ENTRY_TYPE_HTTP2,
    NODE_PERFORMANCE_ENTRY_TYPE_HTTP,
    NODE_PERFORMANCE_ENTRY_TYPE_NET,
    NODE_PERFORMANCE_ENTRY_TYPE_DNS,
  },
  installGarbageCollectionTracking,
  observerCounts,
  removeGarbageCollectionTracking,
  setupObservers,
} = internalBinding('performance');

const {
  isPerformanceEntry,
  createPerformanceNodeEntry,
} = require('internal/perf/performance_entry');

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');

const {
  validateFunction,
  validateObject,
  validateInternalField,
} = require('internal/validators');

const {
  customInspectSymbol: kInspect,
  deprecate,
  lazyDOMException,
  kEmptyObject,
  kEnumerableProperty,
} = require('internal/util');

const {
  setImmediate,
} = require('timers');

const { inspect } = require('util');

const { now } = require('internal/perf/utils');

const kBuffer = Symbol('kBuffer');
const kDispatch = Symbol('kDispatch');
const kMaybeBuffer = Symbol('kMaybeBuffer');
const kDeprecatedFields = Symbol('kDeprecatedFields');

const kDeprecationMessage =
  'Custom PerformanceEntry accessors are deprecated. ' +
  'Please use the detail property.';

const kTypeSingle = 0;
const kTypeMultiple = 1;

let gcTrackingInstalled = false;

const kSupportedEntryTypes = ObjectFreeze([
  'dns',
  'function',
  'gc',
  'http',
  'http2',
  'mark',
  'measure',
  'net',
  'resource',
]);

// Performance timeline entry Buffers
let markEntryBuffer = [];
let measureEntryBuffer = [];
let resourceTimingBuffer = [];
let resourceTimingSecondaryBuffer = [];
const kPerformanceEntryBufferWarnSize = 1e6;
// https://www.w3.org/TR/timing-entrytypes-registry/#registry
// Default buffer limit for resource timing entries.
let resourceTimingBufferSizeLimit = 250;
let dispatchBufferFull;
let resourceTimingBufferFullPending = false;

const kClearPerformanceEntryBuffers = ObjectFreeze({
  'mark': 'performance.clearMarks',
  'measure': 'performance.clearMeasures',
});
const kWarnedEntryTypes = new SafeMap();

const kObservers = new SafeSet();
const kPending = new SafeSet();
let isPending = false;

function queuePending() {
  if (isPending) return;
  isPending = true;
  setImmediate(() => {
    isPending = false;
    const pendings = ArrayFrom(kPending.values());
    kPending.clear();
    for (const pending of pendings)
      pending[kDispatch]();
  });
}

function getObserverType(type) {
  switch (type) {
    case 'gc': return NODE_PERFORMANCE_ENTRY_TYPE_GC;
    case 'http2': return NODE_PERFORMANCE_ENTRY_TYPE_HTTP2;
    case 'http': return NODE_PERFORMANCE_ENTRY_TYPE_HTTP;
    case 'net': return NODE_PERFORMANCE_ENTRY_TYPE_NET;
    case 'dns': return NODE_PERFORMANCE_ENTRY_TYPE_DNS;
  }
}

function maybeDecrementObserverCounts(entryTypes) {
  for (const type of entryTypes) {
    const observerType = getObserverType(type);

    if (observerType !== undefined) {
      observerCounts[observerType]--;

      if (observerType === NODE_PERFORMANCE_ENTRY_TYPE_GC &&
          observerCounts[observerType] === 0) {
        removeGarbageCollectionTracking();
        gcTrackingInstalled = false;
      }
    }
  }
}

function maybeIncrementObserverCount(type) {
  const observerType = getObserverType(type);

  if (observerType !== undefined) {
    observerCounts[observerType]++;
    if (!gcTrackingInstalled &&
        observerType === NODE_PERFORMANCE_ENTRY_TYPE_GC) {
      installGarbageCollectionTracking();
      gcTrackingInstalled = true;
    }
  }
}

const kSkipThrow = Symbol('kSkipThrow');
const performanceObserverSorter = (first, second) => {
  return first.startTime - second.startTime;
};

class PerformanceObserverEntryList {
  constructor(skipThrowSymbol = undefined, entries = []) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }

    this[kBuffer] = ArrayPrototypeSort(entries, performanceObserverSorter);
  }

  getEntries() {
    validateInternalField(this, kBuffer, 'PerformanceObserverEntryList');
    return ArrayPrototypeSlice(this[kBuffer]);
  }

  getEntriesByType(type) {
    validateInternalField(this, kBuffer, 'PerformanceObserverEntryList');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('type');
    }
    type = `${type}`;
    return ArrayPrototypeFilter(
      this[kBuffer],
      (entry) => entry.entryType === type);
  }

  getEntriesByName(name, type = undefined) {
    validateInternalField(this, kBuffer, 'PerformanceObserverEntryList');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('name');
    }
    name = `${name}`;
    if (type != null /** not nullish */) {
      return ArrayPrototypeFilter(
        this[kBuffer],
        (entry) => entry.name === name && entry.entryType === type);
    }
    return ArrayPrototypeFilter(
      this[kBuffer],
      (entry) => entry.name === name);
  }

  [kInspect](depth, options) {
    if (depth < 0) return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `PerformanceObserverEntryList ${inspect(this[kBuffer], opts)}`;
  }
}
ObjectDefineProperties(PerformanceObserverEntryList.prototype, {
  getEntries: kEnumerableProperty,
  getEntriesByType: kEnumerableProperty,
  getEntriesByName: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    writable: false,
    enumerable: false,
    configurable: true,
    value: 'PerformanceObserverEntryList',
  },
});

class PerformanceObserver {
  #buffer = [];
  #entryTypes = new SafeSet();
  #type;
  #callback;

  constructor(callback) {
    validateFunction(callback, 'callback');
    this.#callback = callback;
  }

  observe(options = kEmptyObject) {
    validateObject(options, 'options');
    const {
      entryTypes,
      type,
      buffered,
    } = { ...options };
    if (entryTypes === undefined && type === undefined)
      throw new ERR_MISSING_ARGS('options.entryTypes', 'options.type');
    if (entryTypes != null && (type != null || buffered != null))
      throw new ERR_INVALID_ARG_VALUE('options.entryTypes',
                                      entryTypes,
                                      'options.entryTypes can not set with ' +
                                      'options.type or options.buffered together');

    switch (this.#type) {
      case undefined:
        if (entryTypes !== undefined) this.#type = kTypeMultiple;
        if (type !== undefined) this.#type = kTypeSingle;
        break;
      case kTypeSingle:
        if (entryTypes !== undefined)
          throw lazyDOMException(
            'PerformanceObserver can not change to multiple observations',
            'InvalidModificationError');
        break;
      case kTypeMultiple:
        if (type !== undefined)
          throw lazyDOMException(
            'PerformanceObserver can not change to single observation',
            'InvalidModificationError');
        break;
    }

    if (this.#type === kTypeMultiple) {
      if (!ArrayIsArray(entryTypes)) {
        throw new ERR_INVALID_ARG_TYPE(
          'options.entryTypes',
          'string[]',
          entryTypes);
      }
      maybeDecrementObserverCounts(this.#entryTypes);
      this.#entryTypes.clear();
      for (let n = 0; n < entryTypes.length; n++) {
        if (ArrayPrototypeIncludes(kSupportedEntryTypes, entryTypes[n])) {
          this.#entryTypes.add(entryTypes[n]);
          maybeIncrementObserverCount(entryTypes[n]);
        }
      }
    } else {
      if (!ArrayPrototypeIncludes(kSupportedEntryTypes, type))
        return;
      this.#entryTypes.add(type);
      maybeIncrementObserverCount(type);
      if (buffered) {
        const entries = filterBufferMapByNameAndType(undefined, type);
        ArrayPrototypePushApply(this.#buffer, entries);
        kPending.add(this);
        if (kPending.size)
          queuePending();
      }
    }

    if (this.#entryTypes.size)
      kObservers.add(this);
    else
      this.disconnect();
  }

  disconnect() {
    maybeDecrementObserverCounts(this.#entryTypes);
    kObservers.delete(this);
    kPending.delete(this);
    this.#buffer = [];
    this.#entryTypes.clear();
    this.#type = undefined;
  }

  takeRecords() {
    const list = this.#buffer;
    this.#buffer = [];
    return list;
  }

  static get supportedEntryTypes() {
    return kSupportedEntryTypes;
  }

  [kMaybeBuffer](entry) {
    if (!this.#entryTypes.has(entry.entryType))
      return;
    ArrayPrototypePush(this.#buffer, entry);
    kPending.add(this);
    if (kPending.size)
      queuePending();
  }

  [kDispatch]() {
    const entryList = new PerformanceObserverEntryList(kSkipThrow, this.takeRecords());

    this.#callback(entryList, this);
  }

  [kInspect](depth, options) {
    if (depth < 0) return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `PerformanceObserver ${inspect({
      connected: kObservers.has(this),
      pending: kPending.has(this),
      entryTypes: ArrayFrom(this.#entryTypes),
      buffer: this.#buffer,
    }, opts)}`;
  }
}
ObjectDefineProperties(PerformanceObserver.prototype, {
  observe: kEnumerableProperty,
  disconnect: kEnumerableProperty,
  takeRecords: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    writable: false,
    enumerable: false,
    configurable: true,
    value: 'PerformanceObserver',
  },
});

/**
 * https://www.w3.org/TR/performance-timeline/#dfn-queue-a-performanceentry
 *
 * Add the performance entry to the interested performance observer's queue.
 */
function enqueue(entry) {
  if (!isPerformanceEntry(entry))
    throw new ERR_INVALID_ARG_TYPE('entry', 'PerformanceEntry', entry);

  for (const obs of kObservers) {
    obs[kMaybeBuffer](entry);
  }
}

/**
 * Add the user timing entry to the global buffer.
 */
function bufferUserTiming(entry) {
  const entryType = entry.entryType;
  let buffer;
  if (entryType === 'mark') {
    buffer = markEntryBuffer;
  } else if (entryType === 'measure') {
    buffer = measureEntryBuffer;
  } else {
    return;
  }

  ArrayPrototypePush(buffer, entry);
  const count = buffer.length;

  if (count > kPerformanceEntryBufferWarnSize &&
    !kWarnedEntryTypes.has(entryType)) {
    kWarnedEntryTypes.set(entryType, true);
    // No error code for this since it is a Warning
    // eslint-disable-next-line no-restricted-syntax
    const w = new Error('Possible perf_hooks memory leak detected. ' +
                        `${count} ${entryType} entries added to the global ` +
                        'performance entry buffer. Use ' +
                        `${kClearPerformanceEntryBuffers[entryType]} to ` +
                        'clear the buffer.');
    w.name = 'MaxPerformanceEntryBufferExceededWarning';
    w.entryType = entryType;
    w.count = count;
    process.emitWarning(w);
  }
}

/**
 * Add the resource timing entry to the global buffer if the buffer size is not
 * exceeding the buffer limit, or dispatch a buffer full event on the global
 * performance object.
 *
 * See also https://www.w3.org/TR/resource-timing-2/#dfn-add-a-performanceresourcetiming-entry
 */
function bufferResourceTiming(entry) {
  if (resourceTimingBuffer.length < resourceTimingBufferSizeLimit && !resourceTimingBufferFullPending) {
    ArrayPrototypePush(resourceTimingBuffer, entry);
    return;
  }

  if (!resourceTimingBufferFullPending) {
    resourceTimingBufferFullPending = true;
    setImmediate(() => {
      while (resourceTimingSecondaryBuffer.length > 0) {
        const excessNumberBefore = resourceTimingSecondaryBuffer.length;
        dispatchBufferFull('resourcetimingbufferfull');

        // Calculate the number of items to be pushed to the global buffer.
        const numbersToPreserve = MathMax(
          MathMin(resourceTimingBufferSizeLimit - resourceTimingBuffer.length, resourceTimingSecondaryBuffer.length),
          0,
        );
        const excessNumberAfter = resourceTimingSecondaryBuffer.length - numbersToPreserve;
        for (let idx = 0; idx < numbersToPreserve; idx++) {
          ArrayPrototypePush(resourceTimingBuffer, resourceTimingSecondaryBuffer[idx]);
        }

        if (excessNumberBefore <= excessNumberAfter) {
          resourceTimingSecondaryBuffer = [];
        }
      }
      resourceTimingBufferFullPending = false;
    });
  }

  ArrayPrototypePush(resourceTimingSecondaryBuffer, entry);
}

// https://w3c.github.io/resource-timing/#dom-performance-setresourcetimingbuffersize
function setResourceTimingBufferSize(maxSize) {
  // If the maxSize parameter is less than resource timing buffer current
  // size, no PerformanceResourceTiming objects are to be removed from the
  // performance entry buffer.
  resourceTimingBufferSizeLimit = maxSize;
}

function setDispatchBufferFull(fn) {
  dispatchBufferFull = fn;
}

function clearEntriesFromBuffer(type, name) {
  if (type !== 'mark' && type !== 'measure' && type !== 'resource') {
    return;
  }

  if (type === 'mark') {
    markEntryBuffer = name === undefined ?
      [] : ArrayPrototypeFilter(markEntryBuffer, (entry) => entry.name !== name);
  } else if (type === 'measure') {
    measureEntryBuffer = name === undefined ?
      [] : ArrayPrototypeFilter(measureEntryBuffer, (entry) => entry.name !== name);
  } else {
    resourceTimingBuffer = name === undefined ?
      [] : ArrayPrototypeFilter(resourceTimingBuffer, (entry) => entry.name !== name);
  }
}

function filterBufferMapByNameAndType(name, type) {
  let bufferList;
  if (type === 'mark') {
    bufferList = markEntryBuffer;
  } else if (type === 'measure') {
    bufferList = measureEntryBuffer;
  } else if (type === 'resource') {
    bufferList = resourceTimingBuffer;
  } else if (type !== undefined) {
    // Unrecognized type;
    return [];
  } else {
    bufferList = [];
    ArrayPrototypePushApply(bufferList, markEntryBuffer);
    ArrayPrototypePushApply(bufferList, measureEntryBuffer);
    ArrayPrototypePushApply(bufferList, resourceTimingBuffer);
  }
  if (name !== undefined) {
    bufferList = ArrayPrototypeFilter(bufferList, (buffer) => buffer.name === name);
  } else if (type !== undefined) {
    bufferList = ArrayPrototypeSlice(bufferList);
  }

  return ArrayPrototypeSort(bufferList, performanceObserverSorter);
}

function observerCallback(name, type, startTime, duration, details) {
  const entry =
    createPerformanceNodeEntry(
      name,
      type,
      startTime,
      duration,
      details);

  if (details !== undefined) {
    // GC, HTTP2, and HTTP PerformanceEntry used additional
    // properties directly off the entry. Those have been
    // moved into the details property. The existing accessors
    // are still included but are deprecated.
    entry[kDeprecatedFields] = new SafeMap();

    const detailKeys = ObjectKeys(details);
    const props = {};
    for (let n = 0; n < detailKeys.length; n++) {
      const key = detailKeys[n];
      entry[kDeprecatedFields].set(key, details[key]);
      props[key] = {
        configurable: true,
        enumerable: true,
        get: deprecate(() => {
          return entry[kDeprecatedFields].get(key);
        }, kDeprecationMessage, 'DEP0152'),
        set: deprecate((value) => {
          entry[kDeprecatedFields].set(key, value);
        }, kDeprecationMessage, 'DEP0152'),
      };
    }
    ObjectDefineProperties(entry, props);
  }

  enqueue(entry);
}

setupObservers(observerCallback);

function hasObserver(type) {
  const observerType = getObserverType(type);
  return observerCounts[observerType] > 0;
}


function startPerf(target, key, context = {}) {
  target[key] = {
    ...context,
    startTime: now(),
  };
}

function stopPerf(target, key, context = {}) {
  const ctx = target[key];
  if (!ctx) {
    return;
  }
  const startTime = ctx.startTime;
  const entry = createPerformanceNodeEntry(
    ctx.name,
    ctx.type,
    startTime,
    now() - startTime,
    { ...ctx.detail, ...context.detail },
  );
  enqueue(entry);
}

module.exports = {
  PerformanceObserver,
  PerformanceObserverEntryList,
  enqueue,
  hasObserver,
  clearEntriesFromBuffer,
  filterBufferMapByNameAndType,
  startPerf,
  stopPerf,

  bufferUserTiming,
  bufferResourceTiming,
  setResourceTimingBufferSize,
  setDispatchBufferFull,
};
