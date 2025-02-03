'use strict';

const {
  ArrayPrototypeIncludes,
  ArrayPrototypeIndexOf,
  ArrayPrototypePush,
  ArrayPrototypeSplice,
  ArrayPrototypeUnshift,
  FunctionPrototypeBind,
  NumberIsSafeInteger,
  ObjectDefineProperties,
  ObjectFreeze,
  ReflectApply,
  Symbol,
} = primordials;

const {
  ERR_ASYNC_CALLBACK,
  ERR_ASYNC_TYPE,
  ERR_INVALID_ASYNC_ID,
} = require('internal/errors').codes;
const {
  deprecate,
  kEmptyObject,
} = require('internal/util');
const {
  validateFunction,
  validateString,
} = require('internal/validators');
const internal_async_hooks = require('internal/async_hooks');

const AsyncContextFrame = require('internal/async_context_frame');

// Get functions
// For userland AsyncResources, make sure to emit a destroy event when the
// resource gets gced.
const { registerDestroyHook } = internal_async_hooks;
const {
  asyncWrap,
  executionAsyncId,
  triggerAsyncId,
  // Private API
  hasAsyncIdStack,
  getHookArrays,
  enableHooks,
  disableHooks,
  updatePromiseHookMode,
  executionAsyncResource,
  // Internal Embedder API
  newAsyncId,
  getDefaultTriggerAsyncId,
  emitInit,
  emitBefore,
  emitAfter,
  emitDestroy,
  enabledHooksExist,
  initHooksExist,
  destroyHooksExist,
} = internal_async_hooks;

// Get symbols
const {
  async_id_symbol, trigger_async_id_symbol,
  init_symbol, before_symbol, after_symbol, destroy_symbol,
  promise_resolve_symbol,
} = internal_async_hooks.symbols;

// Get constants
const {
  kInit, kBefore, kAfter, kDestroy, kTotals, kPromiseResolve,
} = internal_async_hooks.constants;

// Listener API //

class AsyncHook {
  constructor({ init, before, after, destroy, promiseResolve }) {
    if (init !== undefined && typeof init !== 'function')
      throw new ERR_ASYNC_CALLBACK('hook.init');
    if (before !== undefined && typeof before !== 'function')
      throw new ERR_ASYNC_CALLBACK('hook.before');
    if (after !== undefined && typeof after !== 'function')
      throw new ERR_ASYNC_CALLBACK('hook.after');
    if (destroy !== undefined && typeof destroy !== 'function')
      throw new ERR_ASYNC_CALLBACK('hook.destroy');
    if (promiseResolve !== undefined && typeof promiseResolve !== 'function')
      throw new ERR_ASYNC_CALLBACK('hook.promiseResolve');

    this[init_symbol] = init;
    this[before_symbol] = before;
    this[after_symbol] = after;
    this[destroy_symbol] = destroy;
    this[promise_resolve_symbol] = promiseResolve;
  }

  enable() {
    // The set of callbacks for a hook should be the same regardless of whether
    // enable()/disable() are run during their execution. The following
    // references are reassigned to the tmp arrays if a hook is currently being
    // processed.
    const { 0: hooks_array, 1: hook_fields } = getHookArrays();

    // Each hook is only allowed to be added once.
    if (ArrayPrototypeIncludes(hooks_array, this))
      return this;

    const prev_kTotals = hook_fields[kTotals];

    // createHook() has already enforced that the callbacks are all functions,
    // so here simply increment the count of whether each callbacks exists or
    // not.
    hook_fields[kTotals] = hook_fields[kInit] += +!!this[init_symbol];
    hook_fields[kTotals] += hook_fields[kBefore] += +!!this[before_symbol];
    hook_fields[kTotals] += hook_fields[kAfter] += +!!this[after_symbol];
    hook_fields[kTotals] += hook_fields[kDestroy] += +!!this[destroy_symbol];
    hook_fields[kTotals] +=
        hook_fields[kPromiseResolve] += +!!this[promise_resolve_symbol];
    ArrayPrototypePush(hooks_array, this);

    if (prev_kTotals === 0 && hook_fields[kTotals] > 0) {
      enableHooks();
    }

    updatePromiseHookMode();

    return this;
  }

  disable() {
    const { 0: hooks_array, 1: hook_fields } = getHookArrays();

    const index = ArrayPrototypeIndexOf(hooks_array, this);
    if (index === -1)
      return this;

    const prev_kTotals = hook_fields[kTotals];

    hook_fields[kTotals] = hook_fields[kInit] -= +!!this[init_symbol];
    hook_fields[kTotals] += hook_fields[kBefore] -= +!!this[before_symbol];
    hook_fields[kTotals] += hook_fields[kAfter] -= +!!this[after_symbol];
    hook_fields[kTotals] += hook_fields[kDestroy] -= +!!this[destroy_symbol];
    hook_fields[kTotals] +=
        hook_fields[kPromiseResolve] -= +!!this[promise_resolve_symbol];
    ArrayPrototypeSplice(hooks_array, index, 1);

    if (prev_kTotals > 0 && hook_fields[kTotals] === 0) {
      disableHooks();
    }

    return this;
  }
}


function createHook(fns) {
  return new AsyncHook(fns);
}


// Embedder API //

const destroyedSymbol = Symbol('destroyed');
const contextFrameSymbol = Symbol('context_frame');

class AsyncResource {
  constructor(type, opts = kEmptyObject) {
    validateString(type, 'type');

    let triggerAsyncId = opts;
    let requireManualDestroy = false;
    if (typeof opts !== 'number') {
      triggerAsyncId = opts.triggerAsyncId === undefined ?
        getDefaultTriggerAsyncId() : opts.triggerAsyncId;
      requireManualDestroy = !!opts.requireManualDestroy;
    }

    // Unlike emitInitScript, AsyncResource doesn't supports null as the
    // triggerAsyncId.
    if (!NumberIsSafeInteger(triggerAsyncId) || triggerAsyncId < -1) {
      throw new ERR_INVALID_ASYNC_ID('triggerAsyncId', triggerAsyncId);
    }

    this[contextFrameSymbol] = AsyncContextFrame.current();

    const asyncId = newAsyncId();
    this[async_id_symbol] = asyncId;
    this[trigger_async_id_symbol] = triggerAsyncId;

    if (initHooksExist()) {
      if (enabledHooksExist() && type.length === 0) {
        throw new ERR_ASYNC_TYPE(type);
      }

      emitInit(asyncId, type, triggerAsyncId, this);
    }

    if (!requireManualDestroy && destroyHooksExist()) {
      // This prop name (destroyed) has to be synchronized with C++
      const destroyed = { destroyed: false };
      this[destroyedSymbol] = destroyed;
      registerDestroyHook(this, asyncId, destroyed);
    }
  }

  runInAsyncScope(fn, thisArg, ...args) {
    const asyncId = this[async_id_symbol];
    emitBefore(asyncId, this[trigger_async_id_symbol], this);

    const contextFrame = this[contextFrameSymbol];
    const prior = AsyncContextFrame.exchange(contextFrame);
    try {
      return ReflectApply(fn, thisArg, args);
    } finally {
      AsyncContextFrame.set(prior);
      if (hasAsyncIdStack())
        emitAfter(asyncId);
    }
  }

  emitDestroy() {
    if (this[destroyedSymbol] !== undefined) {
      this[destroyedSymbol].destroyed = true;
    }
    emitDestroy(this[async_id_symbol]);
    return this;
  }

  asyncId() {
    return this[async_id_symbol];
  }

  triggerAsyncId() {
    return this[trigger_async_id_symbol];
  }

  bind(fn, thisArg) {
    validateFunction(fn, 'fn');
    let bound;
    if (thisArg === undefined) {
      const resource = this;
      bound = function(...args) {
        ArrayPrototypeUnshift(args, fn, this);
        return ReflectApply(resource.runInAsyncScope, resource, args);
      };
    } else {
      bound = FunctionPrototypeBind(this.runInAsyncScope, this, fn, thisArg);
    }
    let self = this;
    ObjectDefineProperties(bound, {
      'length': {
        __proto__: null,
        configurable: true,
        enumerable: false,
        value: fn.length,
        writable: false,
      },
      'asyncResource': {
        __proto__: null,
        configurable: true,
        enumerable: true,
        get: deprecate(function() {
          return self;
        }, 'The asyncResource property on bound functions is deprecated', 'DEP0172'),
        set: deprecate(function(val) {
          self = val;
        }, 'The asyncResource property on bound functions is deprecated', 'DEP0172'),
      },
    });
    return bound;
  }

  static bind(fn, type, thisArg) {
    type ||= fn.name;
    return (new AsyncResource(type || 'bound-anonymous-fn')).bind(fn, thisArg);
  }
}

// Placing all exports down here because the exported classes won't export
// otherwise.
module.exports = {
  // Public API
  get AsyncLocalStorage() {
    return AsyncContextFrame.enabled ?
      require('internal/async_local_storage/async_context_frame') :
      require('internal/async_local_storage/async_hooks');
  },
  createHook,
  executionAsyncId,
  triggerAsyncId,
  executionAsyncResource,
  asyncWrapProviders: ObjectFreeze({ __proto__: null, ...asyncWrap.Providers }),
  // Embedder API
  AsyncResource,
};
