'use strict';

const {
  ObjectDefineProperties,
  SymbolToStringTag,
} = primordials;

const {
  newReadableWritablePairFromDuplex,
} = require('internal/webstreams/adapters');

const { customInspect } = require('internal/webstreams/util');

const {
  customInspectSymbol: kInspect,
  kEnumerableProperty,
} = require('internal/util');

const { createEnumConverter } = require('internal/webidl');

let zlib;
function lazyZlib() {
  zlib ??= require('zlib');
  return zlib;
}

const formatConverter = createEnumConverter('CompressionFormat', [
  'deflate',
  'deflate-raw',
  'gzip',
]);

/**
 * @typedef {import('./readablestream').ReadableStream} ReadableStream
 * @typedef {import('./writablestream').WritableStream} WritableStream
 */

class CompressionStream {
  #handle;
  #transform;

  /**
   * @param {'deflate'|'deflate-raw'|'gzip'} format
   */
  constructor(format) {
    format = formatConverter(format, {
      prefix: "Failed to construct 'CompressionStream'",
      context: '1st argument',
    });
    switch (format) {
      case 'deflate':
        this.#handle = lazyZlib().createDeflate();
        break;
      case 'deflate-raw':
        this.#handle = lazyZlib().createDeflateRaw();
        break;
      case 'gzip':
        this.#handle = lazyZlib().createGzip();
        break;
    }
    this.#transform = newReadableWritablePairFromDuplex(this.#handle);
  }

  /**
   * @readonly
   * @type {ReadableStream}
   */
  get readable() {
    return this.#transform.readable;
  }

  /**
   * @readonly
   * @type {WritableStream}
   */
  get writable() {
    return this.#transform.writable;
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, 'CompressionStream', {
      readable: this.#transform.readable,
      writable: this.#transform.writable,
    });
  }
}

class DecompressionStream {
  #handle;
  #transform;

  /**
   * @param {'deflate'|'deflate-raw'|'gzip'} format
   */
  constructor(format) {
    format = formatConverter(format, {
      prefix: "Failed to construct 'DecompressionStream'",
      context: '1st argument',
    });
    switch (format) {
      case 'deflate':
        this.#handle = lazyZlib().createInflate();
        break;
      case 'deflate-raw':
        this.#handle = lazyZlib().createInflateRaw();
        break;
      case 'gzip':
        this.#handle = lazyZlib().createGunzip();
        break;
    }
    this.#transform = newReadableWritablePairFromDuplex(this.#handle);
  }

  /**
   * @readonly
   * @type {ReadableStream}
   */
  get readable() {
    return this.#transform.readable;
  }

  /**
   * @readonly
   * @type {WritableStream}
   */
  get writable() {
    return this.#transform.writable;
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, 'DecompressionStream', {
      readable: this.#transform.readable,
      writable: this.#transform.writable,
    });
  }
}

ObjectDefineProperties(CompressionStream.prototype, {
  readable: kEnumerableProperty,
  writable: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    configurable: true,
    value: 'CompressionStream',
  },
});

ObjectDefineProperties(DecompressionStream.prototype, {
  readable: kEnumerableProperty,
  writable: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    configurable: true,
    value: 'DecompressionStream',
  },
});

module.exports = {
  CompressionStream,
  DecompressionStream,
};
