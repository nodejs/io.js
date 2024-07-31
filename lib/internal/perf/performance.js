'use strict';

const {
  ObjectDefineProperties,
  ReflectConstruct,
  Symbol,
  SymbolToStringTag,
} = primordials;

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');

const {
  EventTarget,
  Event,
  kTrustEvent,
  initEventTarget,
  defineEventHandler,
} = require('internal/event_target');

const { now, getTimeOriginTimestamp } = require('internal/perf/utils');

const { markResourceTiming } = require('internal/perf/resource_timing');

const {
  mark,
  measure,
  clearMarkTimings,
} = require('internal/perf/usertiming');
const {
  clearEntriesFromBuffer,
  filterBufferMapByNameAndType,
  setResourceTimingBufferSize,
  setDispatchBufferFull,
} = require('internal/perf/observe');

const { eventLoopUtilization } = require('internal/perf/event_loop_utilization');
const nodeTiming = require('internal/perf/nodetiming');
const timerify = require('internal/perf/timerify');
const { customInspectSymbol: kInspect, kEnumerableProperty, kEmptyObject } = require('internal/util');
const { inspect } = require('util');
const { validateInternalField } = require('internal/validators');
const { convertToInt } = require('internal/webidl');

const kPerformanceBrand = Symbol('performance');

class Performance extends EventTarget {
  constructor() {
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }

  [kInspect](depth, options) {
    if (depth < 0) return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `Performance ${inspect({
      nodeTiming: this.nodeTiming,
      timeOrigin: this.timeOrigin,
    }, opts)}`;
  }

  clearMarks(name = undefined) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (name !== undefined) {
      name = `${name}`;
    }
    clearMarkTimings(name);
    clearEntriesFromBuffer('mark', name);
  }

  clearMeasures(name = undefined) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (name !== undefined) {
      name = `${name}`;
    }
    clearEntriesFromBuffer('measure', name);
  }

  clearResourceTimings(name = undefined) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (name !== undefined) {
      name = `${name}`;
    }
    clearEntriesFromBuffer('resource', name);
  }

  getEntries() {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    return filterBufferMapByNameAndType();
  }

  getEntriesByName(name) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('name');
    }
    name = `${name}`;
    return filterBufferMapByNameAndType(name, undefined);
  }

  getEntriesByType(type) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('type');
    }
    type = `${type}`;
    return filterBufferMapByNameAndType(undefined, type);
  }

  mark(name, options = kEmptyObject) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('name');
    }
    return mark(name, options);
  }

  measure(name, startOrMeasureOptions = kEmptyObject, endMark = undefined) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('name');
    }
    return measure(name, startOrMeasureOptions, endMark);
  }

  now() {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    return now();
  }

  setResourceTimingBufferSize(maxSize) {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('maxSize');
    }
    // unsigned long
    maxSize = convertToInt('maxSize', maxSize, 32);
    return setResourceTimingBufferSize(maxSize);
  }

  get timeOrigin() {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    return getTimeOriginTimestamp();
  }

  toJSON() {
    validateInternalField(this, kPerformanceBrand, 'Performance');
    return {
      nodeTiming: this.nodeTiming,
      timeOrigin: this.timeOrigin,
      eventLoopUtilization: this.eventLoopUtilization(),
    };
  }
}

ObjectDefineProperties(Performance.prototype, {
  clearMarks: kEnumerableProperty,
  clearMeasures: kEnumerableProperty,
  clearResourceTimings: kEnumerableProperty,
  getEntries: kEnumerableProperty,
  getEntriesByName: kEnumerableProperty,
  getEntriesByType: kEnumerableProperty,
  mark: kEnumerableProperty,
  measure: kEnumerableProperty,
  now: kEnumerableProperty,
  timeOrigin: kEnumerableProperty,
  toJSON: kEnumerableProperty,
  setResourceTimingBufferSize: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    writable: false,
    enumerable: false,
    configurable: true,
    value: 'Performance',
  },

  // Node.js specific extensions.
  eventLoopUtilization: {
    __proto__: null,
    configurable: true,
    // Node.js specific extensions.
    enumerable: false,
    writable: true,
    value: eventLoopUtilization,
  },
  nodeTiming: {
    __proto__: null,
    configurable: true,
    // Node.js specific extensions.
    enumerable: false,
    writable: true,
    value: nodeTiming,
  },
  // In the browser, this function is not public.  However, it must be used inside fetch
  // which is a Node.js dependency, not a internal module
  markResourceTiming: {
    __proto__: null,
    configurable: true,
    // Node.js specific extensions.
    enumerable: false,
    writable: true,
    value: markResourceTiming,
  },
  timerify: {
    __proto__: null,
    configurable: true,
    // Node.js specific extensions.
    enumerable: false,
    writable: true,
    value: timerify,
  },
});
defineEventHandler(Performance.prototype, 'resourcetimingbufferfull');

function createPerformance() {
  return ReflectConstruct(function Performance() {
    initEventTarget(this);
    this[kPerformanceBrand] = true;
  }, [], Performance);
}

const performance = createPerformance();

function dispatchBufferFull(type) {
  const event = new Event(type, {
    [kTrustEvent]: true,
  });
  performance.dispatchEvent(event);
}
setDispatchBufferFull(dispatchBufferFull);

module.exports = {
  Performance,
  performance,
};
