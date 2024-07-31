'use strict';

const {
  ArrayPrototypeJoin,
  SafeSet,
} = primordials;

const { hasTracing } = internalBinding('config');

const kMaxTracingCount = 10;

const {
  ERR_TRACE_EVENTS_CATEGORY_REQUIRED,
  ERR_TRACE_EVENTS_UNAVAILABLE,
} = require('internal/errors').codes;

const { ownsProcessState } = require('internal/worker');
if (!hasTracing || !ownsProcessState)
  throw new ERR_TRACE_EVENTS_UNAVAILABLE();

const { CategorySet, getEnabledCategories } = internalBinding('trace_events');
const { customInspectSymbol } = require('internal/util');
const { format } = require('internal/util/inspect');
const {
  validateObject,
  validateStringArray,
} = require('internal/validators');

const enabledTracingObjects = new SafeSet();

class Tracing {
  #handle;
  #categories;
  #enabled = false;

  constructor(categories) {
    this.#handle = new CategorySet(categories);
    this.#categories = categories;
  }

  enable() {
    if (!this.#enabled) {
      this.#enabled = true;
      this.#handle.enable();
      enabledTracingObjects.add(this);
      if (enabledTracingObjects.size > kMaxTracingCount) {
        process.emitWarning(
          'Possible trace_events memory leak detected. There are more than ' +
          `${kMaxTracingCount} enabled Tracing objects.`,
        );
      }
    }
  }

  disable() {
    if (this.#enabled) {
      this.#enabled = false;
      this.#handle.disable();
      enabledTracingObjects.delete(this);
    }
  }

  get enabled() {
    return this.#enabled;
  }

  get categories() {
    return ArrayPrototypeJoin(this.#categories, ',');
  }

  [customInspectSymbol](depth, opts) {
    if (typeof depth === 'number' && depth < 0)
      return this;

    const obj = {
      enabled: this.enabled,
      categories: this.categories,
    };
    return `Tracing ${format(obj)}`;
  }
}

function createTracing(options) {
  validateObject(options, 'options');
  validateStringArray(options.categories, 'options.categories');

  if (options.categories.length <= 0)
    throw new ERR_TRACE_EVENTS_CATEGORY_REQUIRED();

  return new Tracing(options.categories);
}

module.exports = {
  createTracing,
  getEnabledCategories,
};
