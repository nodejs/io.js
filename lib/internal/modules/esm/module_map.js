'use strict';

const {
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypeSort,
  JSONStringify,
  ObjectKeys,
  SafeMap,
} = primordials;
const { kImplicitTypeAttribute } = require('internal/modules/esm/assert');
let debug = require('internal/util/debuglog').debuglog('esm', (fn) => {
  debug = fn;
});
const { ERR_INVALID_ARG_TYPE } = require('internal/errors').codes;
const { validateString } = require('internal/validators');

/**
 * Cache the results of the `resolve` step of the module resolution and loading process.
 * Future resolutions of the same input (specifier, parent URL and import attributes)
 * must return the same result if the first attempt was successful, per
 * https://tc39.es/ecma262/#sec-HostLoadImportedModule.
 * This cache is *not* used when custom loaders are registered.
 */
class ResolveCache extends SafeMap {
  constructor(i) { super(i); } // eslint-disable-line no-useless-constructor

  /**
   * Generates the internal serialized cache key and returns it along the actual cache object.
   *
   * It is exposed to allow more efficient read and overwrite a cache entry.
   * @param {string} specifier
   * @param {Record<string,string>} importAttributes
   * @returns {string}
   */
  serializeKey(specifier, importAttributes) {
    // To serialize the ModuleRequest (specifier + list of import attributes),
    // we need to sort the attributes by key, then stringifying,
    // so that different import statements with the same attributes are always treated
    // as identical.
    const keys = ObjectKeys(importAttributes);

    if (keys.length === 0) {
      return specifier + '::';
    }

    return specifier + '::' + ArrayPrototypeJoin(
      ArrayPrototypeMap(
        ArrayPrototypeSort(keys),
        (key) => JSONStringify(key) + JSONStringify(importAttributes[key])),
      ',');
  }

  #getModuleCachedImports(parentURL) {
    let internalCache = super.get(parentURL);
    if (internalCache == null) {
      super.set(parentURL, internalCache = { __proto__: null });
    }
    return internalCache;
  }

  /**
   * @param {string} serializedKey
   * @param {string} parentURL
   * @returns {import('./loader').ModuleExports | Promise<import('./loader').ModuleExports>}
   */
  get(serializedKey, parentURL) {
    return this.#getModuleCachedImports(parentURL)[serializedKey];
  }

  /**
   * @param {string} serializedKey
   * @param {string} parentURL
   * @param {{ format: string, url: URL['href'] }} result
   */
  set(serializedKey, parentURL, result) {
    this.#getModuleCachedImports(parentURL)[serializedKey] = result;
    return this;
  }

  has(serializedKey, parentURL) {
    return serializedKey in this.#getModuleCachedImports(parentURL);
  }
}

/**
 * Cache the results of the `load` step of the module resolution and loading process.
 */
class LoadCache extends SafeMap {
  constructor(i) { super(i); } // eslint-disable-line no-useless-constructor
  get(url, type = kImplicitTypeAttribute) {
    validateString(url, 'url');
    validateString(type, 'type');
    return super.get(url)?.[type];
  }
  set(url, type = kImplicitTypeAttribute, job) {
    validateString(url, 'url');
    validateString(type, 'type');

    const { ModuleJobBase } = require('internal/modules/esm/module_job');
    if (job instanceof ModuleJobBase !== true &&
        typeof job !== 'function') {
      throw new ERR_INVALID_ARG_TYPE('job', 'ModuleJob', job);
    }
    debug(`Storing ${url} (${
      type === kImplicitTypeAttribute ? 'implicit type' : type
    }) in ModuleLoadMap`);
    const cachedJobsForUrl = super.get(url) ?? { __proto__: null };
    cachedJobsForUrl[type] = job;
    return super.set(url, cachedJobsForUrl);
  }
  has(url, type = kImplicitTypeAttribute) {
    validateString(url, 'url');
    validateString(type, 'type');
    return super.get(url)?.[type] !== undefined;
  }
  delete(url, type = kImplicitTypeAttribute) {
    const cached = super.get(url);
    if (cached) {
      cached[type] = undefined;
    }
  }
}

module.exports = {
  LoadCache,
  ResolveCache,
};
