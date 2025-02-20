'use strict';

const {
  ArrayPrototypeSlice,
  ErrorCaptureStackTrace,
  ObjectDefineProperty,
  ObjectPrototypeHasOwnProperty,
  Symbol,
} = primordials;

const { exitCodes: { kGenericUserError } } = internalBinding('errors');

const async_wrap = internalBinding('async_wrap');
const { setCallbackTrampoline } = async_wrap;
/* async_hook_fields is a Uint32Array wrapping the uint32_t array of
 * Environment::AsyncHooks::fields_[]. Each index tracks the number of active
 * hooks for each type.
 *
 * async_id_fields is a Float64Array wrapping the double array of
 * Environment::AsyncHooks::async_id_fields_[]. Each index contains the ids for
 * the various asynchronous states of the application. These are:
 *  kExecutionAsyncId: The async_id assigned to the resource responsible for the
 *    current execution stack.
 *  kTriggerAsyncId: The async_id of the resource that caused (or 'triggered')
 *    the resource corresponding to the current execution stack.
 *  kAsyncIdCounter: Incremental counter tracking the next assigned async_id.
 *  kDefaultTriggerAsyncId: Written immediately before a resource's constructor
 *    that sets the value of the init()'s triggerAsyncId. The precedence order
 *    of retrieving the triggerAsyncId value is:
 *    1. the value passed directly to the constructor
 *    2. value set in kDefaultTriggerAsyncId
 *    3. executionAsyncId of the current resource.
 *
 * async_ids_stack is a Float64Array that contains part of the async ID
 * stack. Each pushAsyncContext() call adds two doubles to it, and each
 * popAsyncContext() call removes two doubles from it.
 * It has a fixed size, so if that is exceeded, calls to the native
 * side are used instead in pushAsyncContext() and popAsyncContext().
 */
const {
  async_hook_fields,
  async_id_fields,
  execution_async_resources,
} = async_wrap;
// Store the pair executionAsyncId and triggerAsyncId in a AliasedFloat64Array
// in Environment::AsyncHooks::async_ids_stack_ which tracks the resource
// responsible for the current execution stack. This is unwound as each resource
// exits. In the case of a fatal exception this stack is emptied after calling
// each hook's after() callback.
const {
  pushAsyncContext: pushAsyncContext_,
  popAsyncContext: popAsyncContext_,
  executionAsyncResource: executionAsyncResource_,
  clearAsyncIdStack,
} = async_wrap;
// Properties in active_hooks are used to keep track of the set of hooks being
// executed in case another hook is enabled/disabled. The new set of hooks is
// then restored once the active set of hooks is finished executing.
const active_hooks = {
  // Array of all AsyncHooks that will be iterated whenever an async event
  // fires. Using var instead of (preferably const) in order to assign
  // active_hooks.tmp_array if a hook is enabled/disabled during hook
  // execution.
  array: [],
  // Use a counter to track nested calls of async hook callbacks and make sure
  // the active_hooks.array isn't altered mid execution.
  call_depth: 0,
  // Use to temporarily store and updated active_hooks.array if the user
  // enables or disables a hook while hooks are being processed. If a hook is
  // enabled() or disabled() during hook execution then the current set of
  // active hooks is duplicated and set equal to active_hooks.tmp_array. Any
  // subsequent changes are on the duplicated array. When all hooks have
  // completed executing active_hooks.tmp_array is assigned to
  // active_hooks.array.
  tmp_array: null,
  // Keep track of the field counts held in active_hooks.tmp_array. Because the
  // async_hook_fields can't be reassigned, store each uint32 in an array that
  // is written back to async_hook_fields when active_hooks.array is restored.
  tmp_fields: null,
};

const { registerDestroyHook } = async_wrap;
const { enqueueMicrotask } = internalBinding('task_queue');
const { resource_symbol, owner_symbol } = internalBinding('symbols');

// Each constant tracks how many callbacks there are for any given step of
// async execution. These are tracked so if the user didn't include callbacks
// for a given step, that step can bail out early.
const {
  kInit, kBefore, kAfter, kDestroy, kTotals, kPromiseResolve,
  kCheck, kExecutionAsyncId, kAsyncIdCounter, kTriggerAsyncId,
  kDefaultTriggerAsyncId, kStackLength, kUsesExecutionAsyncResource,
} = async_wrap.constants;

const { async_id_symbol,
        trigger_async_id_symbol } = internalBinding('symbols');

// Lazy load of internal/util/inspect;
let inspect;

// Used in AsyncHook and AsyncResource.
const init_symbol = Symbol('init');
const before_symbol = Symbol('before');
const after_symbol = Symbol('after');
const destroy_symbol = Symbol('destroy');
const promise_resolve_symbol = Symbol('promiseResolve');
const emitBeforeNative = emitHookFactory(before_symbol, 'emitBeforeNative');
const emitAfterNative = emitHookFactory(after_symbol, 'emitAfterNative');
const emitDestroyNative = emitHookFactory(destroy_symbol, 'emitDestroyNative');
const emitPromiseResolveNative =
    emitHookFactory(promise_resolve_symbol, 'emitPromiseResolveNative');

let domain_cb;
function useDomainTrampoline(fn) {
  domain_cb = fn;
}

function callbackTrampoline(asyncId, resource, cb, ...args) {
  const index = async_hook_fields[kStackLength] - 1;
  execution_async_resources[index] = resource;

  if (asyncId !== 0 && hasHooks(kBefore))
    emitBeforeNative(asyncId);

  let result;
  if (asyncId === 0 && typeof domain_cb === 'function') {
    args.unshift(cb);
    result = domain_cb.apply(this, args);
  } else {
    result = cb.apply(this, args);
  }

  if (asyncId !== 0 && hasHooks(kAfter))
    emitAfterNative(asyncId);

  execution_async_resources.pop();
  return result;
}

const topLevelResource = {};

function executionAsyncResource() {
  // Indicate to the native layer that this function is likely to be used,
  // in which case it will inform JS about the current async resource via
  // the trampoline above.
  async_hook_fields[kUsesExecutionAsyncResource] = 1;

  const index = async_hook_fields[kStackLength] - 1;
  if (index === -1) return topLevelResource;
  const resource = execution_async_resources[index] ||
    executionAsyncResource_(index);
  return lookupPublicResource(resource);
}

function inspectExceptionValue(e) {
  inspect ??= require('internal/util/inspect').inspect;
  return { message: inspect(e) };
}

// Used to fatally abort the process if a callback throws.
function fatalError(e) {
  if (typeof e?.stack === 'string') {
    process._rawDebug(e.stack);
  } else {
    const o = inspectExceptionValue(e);
    ErrorCaptureStackTrace(o, fatalError);
    process._rawDebug(o.stack);
  }

  const { getOptionValue } = require('internal/options');
  if (getOptionValue('--abort-on-uncaught-exception')) {
    process.abort();
  }
  process.exit(kGenericUserError);
}

function lookupPublicResource(resource) {
  if (typeof resource !== 'object' || resource === null) return resource;
  // TODO(addaleax): Merge this with owner_symbol and use it across all
  // AsyncWrap instances.
  const publicResource = resource[resource_symbol];
  if (publicResource !== undefined)
    return publicResource;
  return resource;
}

// Emit From Native //

// Used by C++ to call all init() callbacks. Because some state can be setup
// from C++ there's no need to perform all the same operations as in
// emitInitScript.
function emitInitNative(asyncId, type, triggerAsyncId, resource) {
  active_hooks.call_depth += 1;
  resource = lookupPublicResource(resource);
  // Use a single try/catch for all hooks to avoid setting up one per iteration.
  try {
    // Using var here instead of let because "for (var ...)" is faster than let.
    // Refs: https://github.com/nodejs/node/pull/30380#issuecomment-552948364
    // eslint-disable-next-line no-var
    for (var i = 0; i < active_hooks.array.length; i++) {
      if (typeof active_hooks.array[i][init_symbol] === 'function') {
        active_hooks.array[i][init_symbol](
          asyncId, type, triggerAsyncId,
          resource,
        );
      }
    }
  } catch (e) {
    fatalError(e);
  } finally {
    active_hooks.call_depth -= 1;
  }

  // Hooks can only be restored if there have been no recursive hook calls.
  // Also the active hooks do not need to be restored if enable()/disable()
  // weren't called during hook execution, in which case active_hooks.tmp_array
  // will be null.
  if (active_hooks.call_depth === 0 && active_hooks.tmp_array !== null) {
    restoreActiveHooks();
  }
}

// Called from native. The asyncId stack handling is taken care of there
// before this is called.
function emitHook(symbol, asyncId) {
  active_hooks.call_depth += 1;
  // Use a single try/catch for all hook to avoid setting up one per
  // iteration.
  try {
    // Using var here instead of let because "for (var ...)" is faster than let.
    // Refs: https://github.com/nodejs/node/pull/30380#issuecomment-552948364
    // eslint-disable-next-line no-var
    for (var i = 0; i < active_hooks.array.length; i++) {
      if (typeof active_hooks.array[i][symbol] === 'function') {
        active_hooks.array[i][symbol](asyncId);
      }
    }
  } catch (e) {
    fatalError(e);
  } finally {
    active_hooks.call_depth -= 1;
  }

  // Hooks can only be restored if there have been no recursive hook calls.
  // Also the active hooks do not need to be restored if enable()/disable()
  // weren't called during hook execution, in which case
  // active_hooks.tmp_array will be null.
  if (active_hooks.call_depth === 0 && active_hooks.tmp_array !== null) {
    restoreActiveHooks();
  }
}

function emitHookFactory(symbol, name) {
  const fn = emitHook.bind(undefined, symbol);

  // Set the name property of the function as it looks good in the stack trace.
  ObjectDefineProperty(fn, 'name', {
    __proto__: null,
    value: name,
  });
  return fn;
}

// Manage Active Hooks //

function getHookArrays() {
  if (active_hooks.call_depth === 0)
    return [active_hooks.array, async_hook_fields];
  // If this hook is being enabled while in the middle of processing the array
  // of currently active hooks then duplicate the current set of active hooks
  // and store this there. This shouldn't fire until the next time hooks are
  // processed.
  if (active_hooks.tmp_array === null)
    storeActiveHooks();
  return [active_hooks.tmp_array, active_hooks.tmp_fields];
}


function storeActiveHooks() {
  active_hooks.tmp_array = ArrayPrototypeSlice(active_hooks.array);
  // Don't want to make the assumption that kInit to kDestroy are indexes 0 to
  // 4. So do this the long way.
  active_hooks.tmp_fields = [];
  copyHooks(active_hooks.tmp_fields, async_hook_fields);
}

function copyHooks(destination, source) {
  destination[kInit] = source[kInit];
  destination[kBefore] = source[kBefore];
  destination[kAfter] = source[kAfter];
  destination[kDestroy] = source[kDestroy];
  destination[kPromiseResolve] = source[kPromiseResolve];
}


// Then restore the correct hooks array in case any hooks were added/removed
// during hook callback execution.
function restoreActiveHooks() {
  active_hooks.array = active_hooks.tmp_array;
  copyHooks(async_hook_fields, active_hooks.tmp_fields);

  active_hooks.tmp_array = null;
  active_hooks.tmp_fields = null;
}

function trackPromise(promise, parent) {
  if (promise[async_id_symbol]) {
    return;
  }

  // Get trigger id from parent async id before making the async id of the
  // child so if a new one must be made it will be lower than the child.
  const triggerAsyncId = parent ? getOrSetAsyncId(parent) :
    getDefaultTriggerAsyncId();

  promise[async_id_symbol] = newAsyncId();
  promise[trigger_async_id_symbol] = triggerAsyncId;
}

function promiseInitHook(promise, parent) {
  trackPromise(promise, parent);
  const asyncId = promise[async_id_symbol];
  const triggerAsyncId = promise[trigger_async_id_symbol];
  emitInitScript(asyncId, 'PROMISE', triggerAsyncId, promise);
}

function promiseInitHookWithDestroyTracking(promise, parent) {
  promiseInitHook(promise, parent);
  destroyTracking(promise, parent);
}

function destroyTracking(promise, parent) {
  trackPromise(promise, parent);
  const asyncId = promise[async_id_symbol];
  registerDestroyHook(promise, asyncId);
}

function promiseBeforeHook(promise) {
  trackPromise(promise);
  const asyncId = promise[async_id_symbol];
  const triggerId = promise[trigger_async_id_symbol];
  emitBeforeScript(asyncId, triggerId, promise);
}

function promiseAfterHook(promise) {
  trackPromise(promise);
  const asyncId = promise[async_id_symbol];
  if (hasHooks(kAfter)) {
    emitAfterNative(asyncId);
  }
  if (asyncId === executionAsyncId()) {
    // This condition might not be true if async_hooks was enabled during
    // the promise callback execution.
    // Popping it off the stack can be skipped in that case, because it is
    // known that it would correspond to exactly one call with
    // PromiseHookType::kBefore that was not witnessed by the PromiseHook.
    popAsyncContext(asyncId);
  }
}

function promiseResolveHook(promise) {
  trackPromise(promise);
  const asyncId = promise[async_id_symbol];
  emitPromiseResolveNative(asyncId);
}

let wantPromiseHook = false;
function enableHooks() {
  async_hook_fields[kCheck] += 1;

  setCallbackTrampoline(callbackTrampoline);
}

let stopPromiseHook;
function updatePromiseHookMode() {
  wantPromiseHook = true;
  let initHook;
  if (initHooksExist()) {
    initHook = destroyHooksExist() ? promiseInitHookWithDestroyTracking :
      promiseInitHook;
  } else if (destroyHooksExist()) {
    initHook = destroyTracking;
  }
  if (stopPromiseHook) stopPromiseHook();
  const promiseHooks = require('internal/promise_hooks');
  stopPromiseHook = promiseHooks.createHook({
    init: initHook,
    before: promiseBeforeHook,
    after: promiseAfterHook,
    settled: promiseResolveHooksExist() ? promiseResolveHook : undefined,
  });
}

function disableHooks() {
  async_hook_fields[kCheck] -= 1;

  wantPromiseHook = false;

  setCallbackTrampoline();

  // Delay the call to `disablePromiseHook()` because we might currently be
  // between the `before` and `after` calls of a Promise.
  enqueueMicrotask(disablePromiseHookIfNecessary);
}

function disablePromiseHookIfNecessary() {
  if (!wantPromiseHook && stopPromiseHook) {
    stopPromiseHook();
  }
}

// Internal Embedder API //

// Increment the internal id counter and return the value. Important that the
// counter increment first. Since it's done the same way in
// Environment::new_async_uid()
function newAsyncId() {
  return ++async_id_fields[kAsyncIdCounter];
}

function getOrSetAsyncId(object) {
  if (ObjectPrototypeHasOwnProperty(object, async_id_symbol)) {
    return object[async_id_symbol];
  }

  return object[async_id_symbol] = newAsyncId();
}


// Return the triggerAsyncId meant for the constructor calling it. It's up to
// the user to safeguard this call and make sure it's zero'd out when the
// constructor is complete.
function getDefaultTriggerAsyncId() {
  const defaultTriggerAsyncId = async_id_fields[kDefaultTriggerAsyncId];
  // If defaultTriggerAsyncId isn't set, use the executionAsyncId
  if (defaultTriggerAsyncId < 0)
    return async_id_fields[kExecutionAsyncId];
  return defaultTriggerAsyncId;
}


function clearDefaultTriggerAsyncId() {
  async_id_fields[kDefaultTriggerAsyncId] = -1;
}

/**
 * Sets a default top level trigger ID to be used
 * @template {Array<unknown>} T
 * @template {unknown} R
 * @param {number} triggerAsyncId
 * @param { (...T: args) => R } block
 * @param  {T} args
 * @returns {R}
 */
function defaultTriggerAsyncIdScope(triggerAsyncId, block, ...args) {
  if (triggerAsyncId === undefined)
    return block.apply(null, args);
  // CHECK(NumberIsSafeInteger(triggerAsyncId))
  // CHECK(triggerAsyncId > 0)
  const oldDefaultTriggerAsyncId = async_id_fields[kDefaultTriggerAsyncId];
  async_id_fields[kDefaultTriggerAsyncId] = triggerAsyncId;

  try {
    return block.apply(null, args);
  } finally {
    async_id_fields[kDefaultTriggerAsyncId] = oldDefaultTriggerAsyncId;
  }
}

function hasHooks(key) {
  return async_hook_fields[key] > 0;
}

function enabledHooksExist() {
  return hasHooks(kCheck);
}

function initHooksExist() {
  return hasHooks(kInit);
}

function afterHooksExist() {
  return hasHooks(kAfter);
}

function destroyHooksExist() {
  return hasHooks(kDestroy);
}

function promiseResolveHooksExist() {
  return hasHooks(kPromiseResolve);
}


function emitInitScript(asyncId, type, triggerAsyncId, resource) {
  // Short circuit all checks for the common case. Which is that no hooks have
  // been set. Do this to remove performance impact for embedders (and core).
  if (!hasHooks(kInit))
    return;

  if (triggerAsyncId === null) {
    triggerAsyncId = getDefaultTriggerAsyncId();
  }

  emitInitNative(asyncId, type, triggerAsyncId, resource);
}


function emitBeforeScript(asyncId, triggerAsyncId, resource) {
  pushAsyncContext(asyncId, triggerAsyncId, resource);

  if (hasHooks(kBefore))
    emitBeforeNative(asyncId);
}


function emitAfterScript(asyncId) {
  if (hasHooks(kAfter))
    emitAfterNative(asyncId);

  popAsyncContext(asyncId);
}


function emitDestroyScript(asyncId) {
  // Return early if there are no destroy callbacks, or invalid asyncId.
  if (!hasHooks(kDestroy) || asyncId <= 0)
    return;
  async_wrap.queueDestroyAsyncId(asyncId);
}


function hasAsyncIdStack() {
  return hasHooks(kStackLength);
}


// This is the equivalent of the native push_async_ids() call.
function pushAsyncContext(asyncId, triggerAsyncId, resource) {
  const offset = async_hook_fields[kStackLength];
  execution_async_resources[offset] = resource;
  if (offset * 2 >= async_wrap.async_ids_stack.length)
    return pushAsyncContext_(asyncId, triggerAsyncId);
  async_wrap.async_ids_stack[offset * 2] = async_id_fields[kExecutionAsyncId];
  async_wrap.async_ids_stack[offset * 2 + 1] = async_id_fields[kTriggerAsyncId];
  async_hook_fields[kStackLength]++;
  async_id_fields[kExecutionAsyncId] = asyncId;
  async_id_fields[kTriggerAsyncId] = triggerAsyncId;
}


// This is the equivalent of the native pop_async_ids() call.
function popAsyncContext(asyncId) {
  const stackLength = async_hook_fields[kStackLength];
  if (stackLength === 0) return false;

  if (enabledHooksExist() && async_id_fields[kExecutionAsyncId] !== asyncId) {
    // Do the same thing as the native code (i.e. crash hard).
    return popAsyncContext_(asyncId);
  }

  const offset = stackLength - 1;
  async_id_fields[kExecutionAsyncId] = async_wrap.async_ids_stack[2 * offset];
  async_id_fields[kTriggerAsyncId] = async_wrap.async_ids_stack[2 * offset + 1];
  execution_async_resources.pop();
  async_hook_fields[kStackLength] = offset;
  return offset > 0;
}


function executionAsyncId() {
  return async_id_fields[kExecutionAsyncId];
}

function triggerAsyncId() {
  return async_id_fields[kTriggerAsyncId];
}

const kNoPromiseHook = Symbol('kNoPromiseHook');

module.exports = {
  executionAsyncId,
  triggerAsyncId,
  // Private API
  getHookArrays,
  symbols: {
    async_id_symbol, trigger_async_id_symbol,
    init_symbol, before_symbol, after_symbol, destroy_symbol,
    promise_resolve_symbol, owner_symbol,
  },
  constants: {
    kInit, kBefore, kAfter, kDestroy, kTotals, kPromiseResolve,
  },
  enableHooks,
  disableHooks,
  updatePromiseHookMode,
  clearDefaultTriggerAsyncId,
  clearAsyncIdStack,
  hasAsyncIdStack,
  executionAsyncResource,
  // Internal Embedder API
  newAsyncId,
  getOrSetAsyncId,
  getDefaultTriggerAsyncId,
  defaultTriggerAsyncIdScope,
  enabledHooksExist,
  initHooksExist,
  afterHooksExist,
  destroyHooksExist,
  emitInit: emitInitScript,
  emitBefore: emitBeforeScript,
  emitAfter: emitAfterScript,
  emitDestroy: emitDestroyScript,
  pushAsyncContext,
  popAsyncContext,
  registerDestroyHook,
  useDomainTrampoline,
  nativeHooks: {
    init: emitInitNative,
    before: emitBeforeNative,
    after: emitAfterNative,
    destroy: emitDestroyNative,
    promise_resolve: emitPromiseResolveNative,
  },
  asyncWrap: {
    Providers: async_wrap.Providers,
  },
  kNoPromiseHook,
};
