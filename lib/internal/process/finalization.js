'use strict';
// This file is a modified version of the on-exit-leak-free module on npm.

const {
  ArrayPrototypeFilter,
  ArrayPrototypeIndexOf,
  ArrayPrototypePush,
  ArrayPrototypeSplice,
  SafeFinalizationRegistry,
  SafeWeakRef,
} = primordials;
const { validateObject, kValidateObjectAllowFunction } = require('internal/validators');
const { emitExperimentalWarning } = require('internal/util');

function createFinalization() {
  /**
   * @type {SafeFinalizationRegistry}
   */
  let registry = null;

  const refs = {
    __proto__: null,
    exit: [],
    beforeExit: [],
  };

  const functions = {
    __proto__: null,
    exit: onExit,
    beforeExit: onBeforeExit,
  };

  function install(event) {
    if (refs[event].length > 0) {
      return;
    }

    process.on(event, functions[event]);
  }

  function uninstall(event) {
    if (refs[event].length > 0) {
      return;
    }

    process.removeListener(event, functions[event]);

    if (refs.exit.length === 0 && refs.beforeExit.length === 0) {
      registry = null;
    }
  }

  function onExit() {
    callRefsToFree('exit');
  }

  function onBeforeExit() {
    callRefsToFree('beforeExit');
  }

  function callRefsToFree(event) {
    for (const ref of refs[event]) {
      const obj = ref.deref();
      const fn = ref.fn;

      // This should always happen, however GC is
      // indeterministic so it might not happen.
      /* istanbul ignore else */
      if (obj !== undefined) {
        fn(obj, event);
      }
    }
    refs[event] = [];
  }

  function clear(ref) {
    for (const event of ['exit', 'beforeExit']) {
      const index = ArrayPrototypeIndexOf(refs[event], ref);
      ArrayPrototypeSplice(refs[event], index, index + 1);
      uninstall(event);
    }
  }

  function _register(event, obj, fn) {
    install(event);

    const ref = new SafeWeakRef(obj);
    ref.fn = fn;

    registry ||= new SafeFinalizationRegistry(clear);
    registry.register(obj, ref);

    ArrayPrototypePush(refs[event], ref);
  }

  /**
   * Execute the given function when the process exits,
   * and clean things up when the object is gc.
   * @param {any} obj
   * @param {Function} fn
   */
  function register(obj, fn) {
    emitExperimentalWarning('process.finalization.register');
    validateObject(obj, 'obj', kValidateObjectAllowFunction);

    _register('exit', obj, fn);
  }

  /**
   * Execute the given function before the process exits,
   * and clean things up when the object is gc.
   * @param {any} obj
   * @param {Function} fn
   */
  function registerBeforeExit(obj, fn) {
    emitExperimentalWarning('process.finalization.registerBeforeExit');
    validateObject(obj, 'obj', kValidateObjectAllowFunction);

    _register('beforeExit', obj, fn);
  }

  /**
   * Unregister the given object from the onExit or onBeforeExit event.
   * @param {object} obj
   */
  function unregister(obj) {
    emitExperimentalWarning('process.finalization.unregister');
    if (!registry) {
      return;
    }
    registry.unregister(obj);
    for (const event of ['exit', 'beforeExit']) {
      refs[event] = ArrayPrototypeFilter(refs[event], (ref) => {
        const _obj = ref.deref();
        return _obj && _obj !== obj;
      });
      uninstall(event);
    }
  }

  return {
    register,
    registerBeforeExit,
    unregister,
  };
}

module.exports = {
  createFinalization,
};
