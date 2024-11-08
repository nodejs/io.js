'use strict';

const {
  ObjectDefineProperties,
  ObjectSetPrototypeOf,
} = primordials;

const { PerformanceEntry } = require('internal/perf/performance_entry');

const {
  now,
  getMilestoneTimestamp,
} = require('internal/perf/utils');

const {
  customInspectSymbol: kInspect,
} = require('internal/util');

const { inspect } = require('util');

const {
  constants: {
    NODE_PERFORMANCE_MILESTONE_NODE_START,
    NODE_PERFORMANCE_MILESTONE_V8_START,
    NODE_PERFORMANCE_MILESTONE_LOOP_START,
    NODE_PERFORMANCE_MILESTONE_LOOP_EXIT,
    NODE_PERFORMANCE_MILESTONE_BOOTSTRAP_COMPLETE,
    NODE_PERFORMANCE_MILESTONE_ENVIRONMENT,
  },
  loopIdleTime,
  uvMetricsInfo,
} = internalBinding('performance');

class PerformanceNodeTiming {
  constructor() {
    ObjectDefineProperties(this, {
      name: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        value: 'node',
      },

      entryType: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        value: 'node',
      },

      startTime: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        value: 0,
      },

      duration: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get: now,
      },

      nodeStart: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(NODE_PERFORMANCE_MILESTONE_NODE_START);
        },
      },

      v8Start: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(NODE_PERFORMANCE_MILESTONE_V8_START);
        },
      },

      environment: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(NODE_PERFORMANCE_MILESTONE_ENVIRONMENT);
        },
      },

      loopStart: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(NODE_PERFORMANCE_MILESTONE_LOOP_START);
        },
      },

      loopExit: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(NODE_PERFORMANCE_MILESTONE_LOOP_EXIT);
        },
      },

      bootstrapComplete: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get() {
          return getMilestoneTimestamp(
            NODE_PERFORMANCE_MILESTONE_BOOTSTRAP_COMPLETE);
        },
      },

      idleTime: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get: loopIdleTime,
      },

      uvMetricsInfo: {
        __proto__: null,
        enumerable: true,
        configurable: true,
        get: () => {
          const metrics = uvMetricsInfo();
          return {
            loopCount: metrics[0],
            events: metrics[1],
            eventsWaiting: metrics[2],
          };
        },
      },
    });
  }

  [kInspect](depth, options) {
    if (depth < 0) return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `PerformanceNodeTiming ${inspect(this.toJSON(), opts)}`;
  }

  toJSON() {
    return {
      name: 'node',
      entryType: 'node',
      startTime: this.startTime,
      duration: this.duration,
      nodeStart: this.nodeStart,
      v8Start: this.v8Start,
      bootstrapComplete: this.bootstrapComplete,
      environment: this.environment,
      loopStart: this.loopStart,
      loopExit: this.loopExit,
      idleTime: this.idleTime,
    };
  }
}

ObjectSetPrototypeOf(
  PerformanceNodeTiming.prototype,
  PerformanceEntry.prototype);

module.exports = new PerformanceNodeTiming();
