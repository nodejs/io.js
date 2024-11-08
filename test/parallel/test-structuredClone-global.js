'use strict';

require('../common');
const assert = require('assert');

const prefix = "Failed to execute 'structuredClone'";
const key = 'transfer';
const context = 'Options';
const memberConverterError = `${prefix}: ${key} in ${context} can not be converted to sequence.`;
const dictionaryConverterError = `${prefix}: ${context} cannot be converted to a dictionary`;

assert.throws(() => structuredClone(), { code: 'ERR_MISSING_ARGS' });
assert.throws(() => structuredClone(undefined, ''),
              { code: 'ERR_INVALID_ARG_TYPE', message: dictionaryConverterError });
assert.throws(() => structuredClone(undefined, 1),
              { code: 'ERR_INVALID_ARG_TYPE', message: dictionaryConverterError });
assert.throws(() => structuredClone(undefined, { transfer: 1 }),
              { code: 'ERR_INVALID_ARG_TYPE', message: memberConverterError });
assert.throws(() => structuredClone(undefined, { transfer: '' }),
              { code: 'ERR_INVALID_ARG_TYPE', message: memberConverterError });
assert.throws(() => structuredClone(undefined, { transfer: null }),
              { code: 'ERR_INVALID_ARG_TYPE', message: memberConverterError });

// Options can be null or undefined.
assert.strictEqual(structuredClone(undefined), undefined);
assert.strictEqual(structuredClone(undefined, null), undefined);
// Transfer can be null or undefined.
assert.strictEqual(structuredClone(undefined, { }), undefined);

// Transferables or its subclasses should be received with its closest transferable superclass
for (const StreamClass of [ReadableStream, WritableStream, TransformStream]) {
  const original = new StreamClass();
  const transfer = structuredClone(original, { transfer: [original] });
  assert.strictEqual(Object.getPrototypeOf(transfer), StreamClass.prototype);
  assert.ok(transfer instanceof StreamClass);

  const extended = class extends StreamClass {};
  const extendedOriginal = new extended();
  const extendedTransfer = structuredClone(extendedOriginal, { transfer: [extendedOriginal] });
  assert.strictEqual(Object.getPrototypeOf(extendedTransfer), StreamClass.prototype);
  assert.ok(extendedTransfer instanceof StreamClass);
}

for (const Transferrable of [File, Blob]) {
  const a2 = Transferrable === File ? '' : {};
  const original = new Transferrable([], a2);
  const transfer = structuredClone(original);
  assert.strictEqual(Object.getPrototypeOf(transfer), Transferrable.prototype);
  assert.ok(transfer instanceof Transferrable);

  const extendedOriginal = new (class extends Transferrable {})([], a2);
  const extendedTransfer = structuredClone(extendedOriginal);
  assert.strictEqual(Object.getPrototypeOf(extendedTransfer), Transferrable.prototype);
  assert.ok(extendedTransfer instanceof Transferrable);
}

// Transfer can be iterable
{
  const value = {
    a: new ReadableStream(),
    b: new WritableStream(),
  };
  const cloned = structuredClone(value, {
    transfer: {
      *[Symbol.iterator]() {
        for (const key in value) {
          yield value[key];
        }
      }
    }
  });
  for (const key in value) {
    assert.ok(value[key].locked);
    assert.ok(!cloned[key].locked);
  }
}

{
  // See: https://github.com/nodejs/node/issues/49940
  const cloned = structuredClone({}, {
    transfer: {
      *[Symbol.iterator]() {}
    }
  });

  assert.deepStrictEqual(cloned, {});
}

const blob = new Blob();
assert.throws(() => structuredClone(blob, { transfer: [blob] }), { name: 'DataCloneError' });
