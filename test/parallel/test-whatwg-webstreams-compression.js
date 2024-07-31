// Flags: --no-warnings
'use strict';

const common = require('../common');

const {
  CompressionStream,
  DecompressionStream,
} = require('stream/web');

const assert = require('assert');
const dec = new TextDecoder();

async function test(format) {
  const gzip = new CompressionStream(format);
  const gunzip = new DecompressionStream(format);

  assert.strictEqual(gzip[Symbol.toStringTag], 'CompressionStream');
  assert.strictEqual(gunzip[Symbol.toStringTag], 'DecompressionStream');

  gzip.readable.pipeTo(gunzip.writable).then(common.mustCall());

  const reader = gunzip.readable.getReader();
  const writer = gzip.writable.getWriter();

  const compressed_data = [];
  const reader_function = ({ value, done }) => {
    if (value)
      compressed_data.push(value);
    if (!done)
      return reader.read().then(reader_function);
    assert.strictEqual(dec.decode(Buffer.concat(compressed_data)), 'hello');
  };
  const reader_promise = reader.read().then(reader_function);

  await Promise.all([
    reader_promise,
    reader_promise.then(() => reader.read().then(({ done }) => assert(done))),
    writer.write('hello'),
    writer.close(),
  ]);
}

Promise.all(['gzip', 'deflate', 'deflate-raw'].map((i) => test(i))).then(common.mustCall());

[1, 'hello', false, {}].forEach((i) => {
  assert.throws(() => new CompressionStream(i), {
    code: 'ERR_INVALID_ARG_VALUE',
  });
  assert.throws(() => new DecompressionStream(i), {
    code: 'ERR_INVALID_ARG_VALUE',
  });
});

assert.throws(
  () => Reflect.get(CompressionStream.prototype, 'readable', {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
assert.throws(
  () => Reflect.get(CompressionStream.prototype, 'writable', {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
assert.throws(
  () => Reflect.get(DecompressionStream.prototype, 'readable', {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
assert.throws(
  () => Reflect.get(DecompressionStream.prototype, 'writable', {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
