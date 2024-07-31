// Flags: --no-warnings --expose-internals
'use strict';

require('../common');

const assert = require('node:assert');
const { describe, it } = require('node:test');
const {
  CompressionStream,
  DecompressionStream,
} = require('node:stream/web');

const {
  customInspectSymbol: kInspect,
} = require('internal/util');

describe('DecompressionStream kInspect method', () => {
  it('should return a predictable inspection string with DecompressionStream', () => {
    const decompressionStream = new DecompressionStream('deflate');
    const depth = 1;
    const options = {};
    const actual = decompressionStream[kInspect](depth, options);

    assert(actual.includes('DecompressionStream'));
    assert(actual.includes('ReadableStream'));
    assert(actual.includes('WritableStream'));
  });
});

describe('CompressionStream kInspect method', () => {
  it('should return a predictable inspection string with CompressionStream', () => {
    const compressionStream = new CompressionStream('deflate');
    const depth = 1;
    const options = {};
    const actual = compressionStream[kInspect](depth, options);

    assert(actual.includes('CompressionStream'));
    assert(actual.includes('ReadableStream'));
    assert(actual.includes('WritableStream'));
  });
});
