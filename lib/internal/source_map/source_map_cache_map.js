'use strict';

const {
  ArrayPrototypeForEach,
  ObjectFreeze,
  SafeFinalizationRegistry,
  SafeMap,
  SafeWeakRef,
  SymbolIterator,
} = primordials;
const {
  privateSymbols: {
    source_map_data_private_symbol,
  },
} = internalBinding('util');

/**
 * Specialized map of WeakRefs to module instances that caches source map
 * entries by `filename` and `sourceURL`. Cached entries can be iterated with
 * `for..of` syntax.
 *
 * The cache map maintains the cache entries by:
 * - `weakModuleMap`(Map): a strong sourceURL -> WeakRef(Module),
 * - WeakRef(Module[source_map_data_private_symbol]): source map data.
 *
 * Obsolete `weakModuleMap` entries are removed by the `finalizationRegistry`
 * callback. This pattern decouples the strong url reference to the source map
 * data and allow the cache to be reclaimed eagerly, without depending on an
 * indeterministic callback of a finalization registry.
 */
class SourceMapCacheMap {
  /**
   * @type {Map<string, WeakRef<*>>}
   * The cached module instance can be removed from the global module registry
   * with approaches like mutating `require.cache`.
   * The `weakModuleMap` exposes entries by `filename` and `sourceURL`.
   * In the case of mutated module registry, obsolete entries are removed from
   * the cache by the `finalizationRegistry`.
   */
  #weakModuleMap = new SafeMap();

  #cleanup = ({ keys }) => {
    // Delete the entry if the weak target has been reclaimed.
    // If the weak target is not reclaimed, the entry was overridden by a new
    // weak target.
    ArrayPrototypeForEach(keys, (key) => {
      const ref = this.#weakModuleMap.get(key);
      if (ref && ref.deref() === undefined) {
        this.#weakModuleMap.delete(key);
      }
    });
  };
  #finalizationRegistry = new SafeFinalizationRegistry(this.#cleanup);

  /**
   * Sets the value for the given key, associated with the given module
   * instance.
   * @param {string[]} keys array of urls to index the value entry.
   * @param {*} sourceMapData the value entry.
   * @param {object} moduleInstance an object that can be weakly referenced and
   * invalidate the [key, value] entry after this object is reclaimed.
   */
  set(keys, sourceMapData, moduleInstance) {
    const weakRef = new SafeWeakRef(moduleInstance);
    ArrayPrototypeForEach(keys, (key) => this.#weakModuleMap.set(key, weakRef));
    moduleInstance[source_map_data_private_symbol] = sourceMapData;
    this.#finalizationRegistry.register(moduleInstance, { keys });
  }

  /**
   * Get an entry by the given key.
   * @param {string} key a file url or source url
   */
  get(key) {
    const weakRef = this.#weakModuleMap.get(key);
    const moduleInstance = weakRef?.deref();
    if (moduleInstance === undefined) {
      return;
    }
    return moduleInstance[source_map_data_private_symbol];
  }

  /**
   * Estimate the size of the cache. The actual size may be smaller because
   * some entries may be reclaimed with the module instance.
   */
  get size() {
    return this.#weakModuleMap.size;
  }

  [SymbolIterator]() {
    const iterator = this.#weakModuleMap.entries();

    const next = () => {
      const result = iterator.next();
      if (result.done) return result;
      const { 0: key, 1: weakRef } = result.value;
      const moduleInstance = weakRef.deref();
      if (moduleInstance == null) return next();
      const value = moduleInstance[source_map_data_private_symbol];
      return { done: false, value: [key, value] };
    };

    return {
      [SymbolIterator]() { return this; },
      next,
    };
  }
}

ObjectFreeze(SourceMapCacheMap.prototype);

module.exports = {
  SourceMapCacheMap,
};
