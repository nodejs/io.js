'use strict';

const {
  FunctionPrototypeBind,
  MathCeil,
  ObjectDefineProperties,
  ReflectApply,
  ReflectConstruct,
} = primordials;

const { createPerformanceNodeEntry } = require('internal/perf/performance_entry');
const { now } = require('internal/perf/utils');

const {
  validateFunction,
  validateObject,
} = require('internal/validators');

const {
  isHistogram,
} = require('internal/histogram');

const {
  codes: {
    ERR_INVALID_ARG_TYPE,
  },
} = require('internal/errors');

const {
  enqueue,
} = require('internal/perf/observe');

const {
  kEmptyObject,
} = require('internal/util');

function processComplete(name, start, args, histogram) {
  const duration = now() - start;
  if (histogram !== undefined)
    histogram.record(MathCeil(duration * 1e6));
  const entry =
    createPerformanceNodeEntry(
      name,
      'function',
      start,
      duration,
      args);

  for (let n = 0; n < args.length; n++)
    entry[n] = args[n];

  enqueue(entry);
}

function timerify(fn, options = kEmptyObject) {
  validateFunction(fn, 'fn');

  validateObject(options, 'options');
  const {
    histogram,
  } = options;

  if (histogram !== undefined &&
      (!isHistogram(histogram) || typeof histogram.record !== 'function')) {
    throw new ERR_INVALID_ARG_TYPE(
      'options.histogram',
      'RecordableHistogram',
      histogram);
  }

  function timerified(...args) {
    const isConstructorCall = new.target !== undefined;
    const start = now();
    const result = isConstructorCall ?
      ReflectConstruct(fn, args, fn) :
      ReflectApply(fn, this, args);
    if (!isConstructorCall && typeof result?.finally === 'function') {
      return result.finally(
        FunctionPrototypeBind(
          processComplete,
          result,
          fn.name,
          start,
          args,
          histogram));
    }
    processComplete(fn.name, start, args, histogram);
    return result;
  }

  ObjectDefineProperties(timerified, {
    length: {
      __proto__: null,
      configurable: false,
      enumerable: true,
      value: fn.length,
    },
    name: {
      __proto__: null,
      configurable: false,
      enumerable: true,
      value: `timerified ${fn.name}`,
    },
  });

  return timerified;
}

module.exports = timerify;
