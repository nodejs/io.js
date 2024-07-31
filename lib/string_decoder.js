// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const {
  ArrayBufferIsView,
  ObjectDefineProperties,
  Symbol,
  TypedArrayPrototypeSubarray,
} = primordials;

const { Buffer } = require('buffer');
const {
  kIncompleteCharactersStart,
  kIncompleteCharactersEnd,
  kMissingBytes,
  kBufferedBytes,
  kEncodingField,
  kSize,
  decode,
  flush,
} = internalBinding('string_decoder');
const {
  kIsEncodingSymbol,
  encodingsMap,
  normalizeEncoding: _normalizeEncoding,
} = require('internal/util');
const {
  ERR_INVALID_ARG_TYPE,
  ERR_INVALID_THIS,
  ERR_UNKNOWN_ENCODING,
} = require('internal/errors').codes;
const isEncoding = Buffer[kIsEncodingSymbol];

const kNativeDecoder = Symbol('kNativeDecoder');

// Do not cache `Buffer.isEncoding` when checking encoding names as some
// modules monkey-patch it to support additional encodings
/**
 * Normalize encoding notation
 * @param {string} enc
 * @returns {"utf8" | "utf16le" | "hex" | "ascii"
 *           | "base64" | "latin1" | "base64url"}
 * @throws {TypeError} Throws an error when encoding is invalid
 */
function normalizeEncoding(enc) {
  const nenc = _normalizeEncoding(enc);
  if (nenc === undefined) {
    if (Buffer.isEncoding === isEncoding || !Buffer.isEncoding(enc))
      throw new ERR_UNKNOWN_ENCODING(enc);
    return enc;
  }
  return nenc;
}

/**
 * StringDecoder provides an interface for efficiently splitting a series of
 * buffers into a series of JS strings without breaking apart multi-byte
 * characters.
 * @param {string} [encoding=utf-8]
 */
function StringDecoder(encoding) {
  this.encoding = normalizeEncoding(encoding);
  this[kNativeDecoder] = Buffer.alloc(kSize);
  this[kNativeDecoder][kEncodingField] = encodingsMap[this.encoding];
}

/**
 * Returns a decoded string, omitting any incomplete multi-bytes
 * characters at the end of the Buffer, or TypedArray, or DataView
 * @param {string | Buffer | TypedArray | DataView} buf
 * @returns {string}
 * @throws {TypeError} Throws when buf is not in one of supported types
 */
StringDecoder.prototype.write = function write(buf) {
  if (typeof buf === 'string')
    return buf;
  if (!ArrayBufferIsView(buf))
    throw new ERR_INVALID_ARG_TYPE('buf',
                                   ['Buffer', 'TypedArray', 'DataView'],
                                   buf);
  if (!this[kNativeDecoder]) {
    throw new ERR_INVALID_THIS('StringDecoder');
  }
  return decode(this[kNativeDecoder], buf);
};

/**
 * Returns any remaining input stored in the internal buffer as a string.
 * After end() is called, the stringDecoder object can be reused for new
 * input.
 * @param {string | Buffer | TypedArray | DataView} [buf]
 * @returns {string}
 */
StringDecoder.prototype.end = function end(buf) {
  let ret = '';
  if (buf !== undefined)
    ret = this.write(buf);
  if (this[kNativeDecoder][kBufferedBytes] > 0)
    ret += flush(this[kNativeDecoder]);
  return ret;
};

/* Everything below this line is undocumented legacy stuff. */
/**
 *
 * @param {string | Buffer | TypedArray | DataView} buf
 * @param {number} offset
 * @returns {string}
 */
StringDecoder.prototype.text = function text(buf, offset) {
  this[kNativeDecoder][kMissingBytes] = 0;
  this[kNativeDecoder][kBufferedBytes] = 0;
  return this.write(buf.slice(offset));
};

ObjectDefineProperties(StringDecoder.prototype, {
  lastChar: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      return TypedArrayPrototypeSubarray(this[kNativeDecoder],
                                         kIncompleteCharactersStart,
                                         kIncompleteCharactersEnd);
    },
  },
  lastNeed: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      return this[kNativeDecoder][kMissingBytes];
    },
  },
  lastTotal: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      return this[kNativeDecoder][kBufferedBytes] +
             this[kNativeDecoder][kMissingBytes];
    },
  },
});

exports.StringDecoder = StringDecoder;
