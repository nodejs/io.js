// Flags: --expose-internals

// This tests Node.js-specific behaviors of TextDecoder

'use strict';

const common = require('../common');

const assert = require('assert');
const { customInspectSymbol: inspect } = require('internal/util');
const util = require('util');

const buf = Buffer.from([0xef, 0xbb, 0xbf, 0x74, 0x65,
                         0x73, 0x74, 0xe2, 0x82, 0xac]);

const encoding_sets = ['unicode-1-1-utf-8', 'unicode11utf8', 'unicode20utf8',
                       'utf8', 'utf-8', 'x-unicode20utf8'];
// Make Sure TextDecoder exist
assert(TextDecoder);

// Test TextDecoder, UTF-8, fatal: false, ignoreBOM: false
{
  encoding_sets.forEach((i) => {
    const dec = new TextDecoder(i);
    assert.strictEqual(dec.encoding, 'utf-8');
    const res = dec.decode(buf);
    assert.strictEqual(res, 'test€');
  });

  encoding_sets.forEach((i) => {
    const dec = new TextDecoder(i);
    let res = '';
    res += dec.decode(buf.slice(0, 8), { stream: true });
    res += dec.decode(buf.slice(8));
    assert.strictEqual(res, 'test€');
  });
}

// Test TextDecoder, UTF-8, fatal: false, ignoreBOM: true
{
  encoding_sets.forEach((i) => {
    const dec = new TextDecoder(i, { ignoreBOM: true });
    const res = dec.decode(buf);
    assert.strictEqual(res, '\ufefftest€');
  });

  encoding_sets.forEach((i) => {
    const dec = new TextDecoder(i, { ignoreBOM: true });
    let res = '';
    res += dec.decode(buf.slice(0, 8), { stream: true });
    res += dec.decode(buf.slice(8));
    assert.strictEqual(res, '\ufefftest€');
  });
}

// Invalid encoders
{
  ['meow', 'nonunicode', 'foo', 'bar'].forEach((fakeEncoding) => {
    assert.throws(
      () => { new TextDecoder(fakeEncoding); },
      {
        code: 'ERR_ENCODING_NOT_SUPPORTED',
        name: 'RangeError'
      }
    );
  });
}

// Test TextDecoder, UTF-8, fatal: true, ignoreBOM: false
if (common.hasIntl) {
  ['unicode-1-1-utf-8', 'utf8', 'utf-8'].forEach((i) => {
    const dec = new TextDecoder(i, { fatal: true });
    assert.throws(() => dec.decode(buf.slice(0, 8)),
                  {
                    code: 'ERR_ENCODING_INVALID_ENCODED_DATA',
                    name: 'TypeError',
                    message: 'The encoded data was not valid ' +
                          'for encoding utf-8'
                  });
  });

  ['unicode-1-1-utf-8', 'utf8', 'utf-8'].forEach((i) => {
    const dec = new TextDecoder(i, { fatal: true });
    dec.decode(buf.slice(0, 8), { stream: true });
    dec.decode(buf.slice(8));
  });
} else {
  assert.throws(
    () => new TextDecoder('utf-8', { fatal: true }),
    {
      code: 'ERR_NO_ICU',
      name: 'TypeError',
      message: '"fatal" option is not supported on Node.js compiled without ICU'
    });
}

// Test TextDecoder, label undefined, options null
{
  const dec = new TextDecoder(undefined, null);
  assert.strictEqual(dec.encoding, 'utf-8');
  assert.strictEqual(dec.fatal, false);
  assert.strictEqual(dec.ignoreBOM, false);
  assert.strictEqual(dec[Symbol.toStringTag], 'TextDecoder');
}

// Test TextDecoder, UTF-16le
{
  const dec = new TextDecoder('utf-16le');
  const res = dec.decode(Buffer.from('test€', 'utf-16le'));
  assert.strictEqual(res, 'test€');
}

// Test TextDecoder, UTF-16be
if (common.hasIntl) {
  const dec = new TextDecoder('utf-16be');
  const res = dec.decode(Buffer.from('test€', 'utf-16le').swap16());
  assert.strictEqual(res, 'test€');
}

// Test TextDecoder inspect with hidden fields
{
  const dec = new TextDecoder('utf-8', { ignoreBOM: true });
  if (common.hasIntl) {
    assert.strictEqual(
      util.inspect(dec, { showHidden: true }),
      'TextDecoder {\n' +
      '  encoding: \'utf-8\',\n' +
      '  fatal: false,\n' +
      '  ignoreBOM: true,\n' +
      '  Symbol(flags): 4,\n' +
      '  Symbol(handle): undefined\n' +
      '}'
    );
  } else {
    assert.strictEqual(
      util.inspect(dec, { showHidden: true }),
      'TextDecoder {\n' +
      "  encoding: 'utf-8',\n" +
      '  fatal: false,\n' +
      '  ignoreBOM: true,\n' +
      '  Symbol(flags): 4,\n' +
      '  Symbol(handle): StringDecoder {\n' +
      "    encoding: 'utf8',\n" +
      '    Symbol(kNativeDecoder): <Buffer 00 00 00 00 00 00 01>\n' +
      '  }\n' +
      '}'
    );
  }
}


// Test TextDecoder inspect without hidden fields
{
  const dec = new TextDecoder('utf-8', { ignoreBOM: true });
  assert.strictEqual(
    util.inspect(dec, { showHidden: false }),
    'TextDecoder { encoding: \'utf-8\', fatal: false, ignoreBOM: true }'
  );
}

// Test TextDecoder inspect with negative depth
{
  const dec = new TextDecoder();
  assert.strictEqual(util.inspect(dec, { depth: -1 }), '[TextDecoder]');
}

{
  const inspectFn = TextDecoder.prototype[inspect];
  const decodeFn = TextDecoder.prototype.decode;
  const {
    encoding: { get: encodingGetter },
    fatal: { get: fatalGetter },
    ignoreBOM: { get: ignoreBOMGetter },
  } = Object.getOwnPropertyDescriptors(TextDecoder.prototype);

  const instance = new TextDecoder();

  const expectedError = {
    code: 'ERR_INVALID_THIS',
    name: 'TypeError',
    message: 'Value of "this" must be of type TextDecoder'
  };

  inspectFn.call(instance, Infinity, {});
  decodeFn.call(instance);
  encodingGetter.call(instance);
  fatalGetter.call(instance);
  ignoreBOMGetter.call(instance);

  const invalidThisArgs = [{}, [], true, 1, '', new TextEncoder()];
  invalidThisArgs.forEach((i) => {
    assert.throws(() => inspectFn.call(i, Infinity, {}), expectedError);
    assert.throws(() => decodeFn.call(i), expectedError);
    assert.throws(() => encodingGetter.call(i), expectedError);
    assert.throws(() => fatalGetter.call(i), expectedError);
    assert.throws(() => ignoreBOMGetter.call(i), expectedError);
  });
}

{
  assert.throws(
    () => new TextDecoder('utf-8', 1),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
}

// Test TextDecoder for incomplete UTF-8 byte sequence.
{
  const decoder = new TextDecoder();
  const chunk = new Uint8Array([0x66, 0x6f, 0x6f, 0xed]);
  const str = decoder.decode(chunk);
  assert.strictEqual(str, 'foo\ufffd');
}

if (common.hasIntl) {
  try {
    const decoder = new TextDecoder('Shift_JIS');
    const chunk = new Uint8Array([-1]);
    const str = decoder.decode(chunk);
    assert.strictEqual(str, '\ufffd');
  } catch (e) {
    // Encoding may not be available, e.g. small-icu builds
    assert.strictEqual(e.code, 'ERR_ENCODING_NOT_SUPPORTED');
  }
}

{
  const buffer = new ArrayBuffer(1);
  new MessageChannel().port1.postMessage(buffer, [buffer]); // buffer is detached
  const decoder = new TextDecoder();
  assert.strictEqual(decoder.decode(buffer), '');
}
