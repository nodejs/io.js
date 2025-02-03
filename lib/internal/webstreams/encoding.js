'use strict';

const {
  ObjectDefineProperties,
  String,
  StringPrototypeCharCodeAt,
  Uint8Array,
} = primordials;

const {
  TextDecoder,
  TextEncoder,
} = require('internal/encoding');

const {
  TransformStream,
} = require('internal/webstreams/transformstream');

const { customInspect } = require('internal/webstreams/util');

const {
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  customInspectSymbol: kInspect,
  kEmptyObject,
  kEnumerableProperty,
} = require('internal/util');

/**
 * @typedef {import('./readablestream').ReadableStream} ReadableStream
 * @typedef {import('./writablestream').WritableStream} WritableStream
 */

class TextEncoderStream {
  #pendingHighSurrogate = null;
  #handle;
  #transform;

  constructor() {
    this.#handle = new TextEncoder();
    this.#transform = new TransformStream({
      transform: (chunk, controller) => {
        // https://encoding.spec.whatwg.org/#encode-and-enqueue-a-chunk
        chunk = String(chunk);
        let finalChunk = '';
        for (let i = 0; i < chunk.length; i++) {
          const item = chunk[i];
          const codeUnit = StringPrototypeCharCodeAt(item, 0);
          if (this.#pendingHighSurrogate !== null) {
            const highSurrogate = this.#pendingHighSurrogate;
            this.#pendingHighSurrogate = null;
            if (0xDC00 <= codeUnit && codeUnit <= 0xDFFF) {
              finalChunk += highSurrogate + item;
              continue;
            }
            finalChunk += '\uFFFD';
          }
          if (0xD800 <= codeUnit && codeUnit <= 0xDBFF) {
            this.#pendingHighSurrogate = item;
            continue;
          }
          if (0xDC00 <= codeUnit && codeUnit <= 0xDFFF) {
            finalChunk += '\uFFFD';
            continue;
          }
          finalChunk += item;
        }
        if (finalChunk) {
          const value = this.#handle.encode(finalChunk);
          controller.enqueue(value);
        }
      },
      flush: (controller) => {
        // https://encoding.spec.whatwg.org/#encode-and-flush
        if (this.#pendingHighSurrogate !== null) {
          controller.enqueue(new Uint8Array([0xEF, 0xBF, 0xBD]));
        }
      },
    });
  }

  /**
   * @readonly
   * @type {string}
   */
  get encoding() {
    return this.#handle.encoding;
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
    if (this == null)
      throw new ERR_INVALID_THIS('TextEncoderStream');
    return customInspect(depth, options, 'TextEncoderStream', {
      encoding: this.#handle.encoding,
      readable: this.#transform.readable,
      writable: this.#transform.writable,
    });
  }
}

class TextDecoderStream {
  #handle;
  #transform;

  /**
   * @param {string} [encoding]
   * @param {{
   *   fatal? : boolean,
   *   ignoreBOM? : boolean,
   * }} [options]
   */
  constructor(encoding = 'utf-8', options = kEmptyObject) {
    this.#handle = new TextDecoder(encoding, options);
    this.#transform = new TransformStream({
      transform: (chunk, controller) => {
        if (chunk === undefined) {
          throw new ERR_INVALID_ARG_TYPE('chunk', 'string', chunk);
        }
        const value = this.#handle.decode(chunk, { stream: true });
        if (value)
          controller.enqueue(value);
      },
      flush: (controller) => {
        const value = this.#handle.decode();
        if (value)
          controller.enqueue(value);
        controller.terminate();
      },
    });
  }

  /**
   * @readonly
   * @type {string}
   */
  get encoding() {
    return this.#handle.encoding;
  }

  /**
   * @readonly
   * @type {boolean}
   */
  get fatal() {
    return this.#handle.fatal;
  }

  /**
   * @readonly
   * @type {boolean}
   */
  get ignoreBOM() {
    return this.#handle.ignoreBOM;
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
    if (this == null)
      throw new ERR_INVALID_THIS('TextDecoderStream');
    return customInspect(depth, options, 'TextDecoderStream', {
      encoding: this.#handle.encoding,
      fatal: this.#handle.fatal,
      ignoreBOM: this.#handle.ignoreBOM,
      readable: this.#transform.readable,
      writable: this.#transform.writable,
    });
  }
}

ObjectDefineProperties(TextEncoderStream.prototype, {
  encoding: kEnumerableProperty,
  readable: kEnumerableProperty,
  writable: kEnumerableProperty,
});

ObjectDefineProperties(TextDecoderStream.prototype, {
  encoding: kEnumerableProperty,
  fatal: kEnumerableProperty,
  ignoreBOM: kEnumerableProperty,
  readable: kEnumerableProperty,
  writable: kEnumerableProperty,
});

module.exports = {
  TextEncoderStream,
  TextDecoderStream,
};
