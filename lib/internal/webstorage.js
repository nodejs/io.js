'use strict';
const {
  ObjectDefineProperties,
} = primordials;
const { ERR_INVALID_ARG_VALUE } = require('internal/errors').codes;
const { getOptionValue } = require('internal/options');
const { emitExperimentalWarning } = require('internal/util');
const { kConstructorKey, Storage } = internalBinding('webstorage');
const { getValidatedPath } = require('internal/fs/utils');
const kInMemoryPath = ':memory:';

emitExperimentalWarning('Web Storage');

module.exports = { Storage };

let lazyLocalStorage;
let lazySessionStorage;

ObjectDefineProperties(module.exports, {
  __proto__: null,
  localStorage: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      if (lazyLocalStorage === undefined) {
        const location = getOptionValue('--localstorage-file');

        if (location === '') {
          throw new ERR_INVALID_ARG_VALUE('--localstorage-file',
                                          location,
                                          'is an invalid localStorage location');
        }

        lazyLocalStorage = new Storage(kConstructorKey, getValidatedPath(location));
      }

      return lazyLocalStorage;
    },
  },
  sessionStorage: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      if (lazySessionStorage === undefined) {
        lazySessionStorage = new Storage(kConstructorKey, kInMemoryPath);
      }

      return lazySessionStorage;
    },
  },
});
