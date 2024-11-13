'use strict';

const {
  ArrayPrototypeIndexOf,
  ArrayPrototypePush,
  ArrayPrototypeSplice,
  ObjectIs,
  ReflectApply,
  Symbol,
} = primordials;

const {
  AsyncResource,
  createHook,
  executionAsyncResource,
} = require('async_hooks');

const storageList = [];
const storageHook = createHook({
  init(asyncId, type, triggerAsyncId, resource) {
    const currentResource = executionAsyncResource();
    // Value of currentResource is always a non null object
    for (let i = 0; i < storageList.length; ++i) {
      storageList[i]._propagate(resource, currentResource, type);
    }
  },
});

class AsyncLocalStorage {
  constructor() {
    this.kResourceStore = Symbol('kResourceStore');
    this.enabled = false;
  }

  static bind(fn) {
    return AsyncResource.bind(fn);
  }

  static snapshot() {
    return AsyncLocalStorage.bind((cb, ...args) => cb(...args));
  }

  disable() {
    if (this.enabled) {
      this.enabled = false;
      // If this.enabled, the instance must be in storageList
      const index = ArrayPrototypeIndexOf(storageList, this);
      ArrayPrototypeSplice(storageList, index, 1);
      if (storageList.length === 0) {
        storageHook.disable();
      }
    }
  }

  _enable() {
    if (!this.enabled) {
      this.enabled = true;
      ArrayPrototypePush(storageList, this);
      storageHook.enable();
    }
  }

  // Propagate the context from a parent resource to a child one
  _propagate(resource, triggerResource, type) {
    const store = triggerResource[this.kResourceStore];
    if (this.enabled) {
      resource[this.kResourceStore] = store;
    }
  }

  enterWith(store) {
    this._enable();
    const resource = executionAsyncResource();
    resource[this.kResourceStore] = store;
  }

  run(store, callback, ...args) {
    // Avoid creation of an AsyncResource if store is already active
    if (ObjectIs(store, this.getStore())) {
      return ReflectApply(callback, null, args);
    }

    this._enable();

    const resource = executionAsyncResource();
    const oldStore = resource[this.kResourceStore];

    resource[this.kResourceStore] = store;

    try {
      return ReflectApply(callback, null, args);
    } finally {
      resource[this.kResourceStore] = oldStore;
    }
  }

  exit(callback, ...args) {
    if (!this.enabled) {
      return ReflectApply(callback, null, args);
    }
    this.disable();
    try {
      return ReflectApply(callback, null, args);
    } finally {
      this._enable();
    }
  }

  getStore() {
    if (this.enabled) {
      const resource = executionAsyncResource();
      return resource[this.kResourceStore];
    }
  }
}

module.exports = AsyncLocalStorage;
