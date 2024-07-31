// Flags: --expose-internals

// This tests interoperability between TextEncoder and TextDecoder with
// Node.js util.inspect and Buffer APIs

'use strict';

require('../common');

const assert = require('assert');
const { customInspectSymbol: inspect } = require('internal/util');

const encoded = Buffer.from([0xef, 0xbb, 0xbf, 0x74, 0x65,
                             0x73, 0x74, 0xe2, 0x82, 0xac]);

// Make Sure TextEncoder exists
assert(TextEncoder);

// Test TextEncoder
{
  const enc = new TextEncoder();
  assert.strictEqual(enc.encoding, 'utf-8');
  assert(enc);
  const buf = enc.encode('\ufefftest€');
  assert.strictEqual(Buffer.compare(buf, encoded), 0);
}

{
  const enc = new TextEncoder();
  const buf = enc.encode();
  assert.strictEqual(buf.length, 0);
}

{
  const enc = new TextEncoder();
  const buf = enc.encode(undefined);
  assert.strictEqual(buf.length, 0);
}

{
  const inspectFn = TextEncoder.prototype[inspect];
  const encodeFn = TextEncoder.prototype.encode;
  const encodingGetter =
    Object.getOwnPropertyDescriptor(TextEncoder.prototype, 'encoding').get;

  const instance = new TextEncoder();

  const expectedError = {
    code: 'ERR_INVALID_THIS',
    name: 'TypeError',
    message: 'Value of "this" must be of type TextEncoder'
  };

  inspectFn.call(instance, Infinity, {});
  encodeFn.call(instance);
  encodingGetter.call(instance);

  const invalidThisArgs = [{}, [], true, 1, '', new TextDecoder()];
  for (const i of invalidThisArgs) {
    assert.throws(() => inspectFn.call(i, Infinity, {}), expectedError);
    assert.throws(() => encodeFn.call(i), expectedError);
    assert.throws(() => encodingGetter.call(i), expectedError);
  }
}
