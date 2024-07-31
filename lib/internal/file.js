'use strict';

const {
  DateNow,
  NumberIsNaN,
  ObjectDefineProperties,
  StringPrototypeToWellFormed,
  SymbolToStringTag,
} = primordials;

const {
  Blob,
} = require('internal/blob');

const {
  customInspectSymbol: kInspect,
  kEnumerableProperty,
  kEmptyObject,
} = require('internal/util');

const {
  codes: {
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');

const {
  inspect,
} = require('internal/util/inspect');

class File extends Blob {
  /** @type {string} */
  #name;

  /** @type {number} */
  #lastModified;

  constructor(fileBits, fileName, options = kEmptyObject) {
    if (arguments.length < 2) {
      throw new ERR_MISSING_ARGS('fileBits', 'fileName');
    }

    super(fileBits, options);

    let { lastModified } = options ?? kEmptyObject;

    if (lastModified !== undefined) {
      // Using Number(...) will not throw an error for bigints.
      lastModified = +lastModified;

      if (NumberIsNaN(lastModified)) {
        lastModified = 0;
      }
    } else {
      lastModified = DateNow();
    }

    this.#name = StringPrototypeToWellFormed(`${fileName}`);
    this.#lastModified = lastModified;
  }

  get name() {
    return this.#name;
  }

  get lastModified() {
    return this.#lastModified;
  }

  [kInspect](depth, options) {
    if (depth < 0) {
      return this;
    }

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `File ${inspect({
      size: this.size,
      type: this.type,
      name: this.#name,
      lastModified: this.#lastModified,
    }, opts)}`;
  }
}

ObjectDefineProperties(File.prototype, {
  name: kEnumerableProperty,
  lastModified: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    configurable: true,
    value: 'File',
  },
});

module.exports = {
  File,
};
