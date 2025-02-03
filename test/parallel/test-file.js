'use strict';

const common = require('../common');
const assert = require('assert');
const { Blob, File } = require('buffer');
const { inspect } = require('util');

{
  // ensure File extends Blob
  assert.deepStrictEqual(Object.getPrototypeOf(File.prototype), Blob.prototype);
}

{
  assert.throws(() => new File(), TypeError);
  assert.throws(() => new File([]), TypeError);
}

{
  const properties = ['name', 'lastModified'];

  for (const prop of properties) {
    const desc = Object.getOwnPropertyDescriptor(File.prototype, prop);
    assert.notStrictEqual(desc, undefined);
    // Ensure these properties are getters.
    assert.strictEqual(desc.get?.name, `get ${prop}`);
    assert.strictEqual(desc.set, undefined);
    assert.strictEqual(desc.enumerable, true);
    assert.strictEqual(desc.configurable, true);
  }
}

{
  const file = new File([], '');
  assert.strictEqual(file[Symbol.toStringTag], 'File');
  assert.strictEqual(File.prototype[Symbol.toStringTag], 'File');
}

{
  assert.throws(() => File.prototype.name, TypeError);
  assert.throws(() => File.prototype.lastModified, TypeError);
}

{
  const keys = Object.keys(File.prototype).sort();
  assert.deepStrictEqual(keys, ['lastModified', 'name']);
}

{
  const file = new File([], 'dummy.txt.exe');
  assert.strictEqual(file.name, 'dummy.txt.exe');
  assert.strictEqual(file.size, 0);
  assert.strictEqual(typeof file.lastModified, 'number');
  assert(file.lastModified <= Date.now());
}

{
  const emptyFile = new File([], 'empty.txt');
  const blob = new Blob(['hello world']);

  emptyFile.text.call(blob).then(common.mustCall((text) => {
    assert.strictEqual(text, 'hello world');
  }));
}

{
  const toPrimitive = {
    [Symbol.toPrimitive]() {
      return 'NaN';
    }
  };

  const invalidLastModified = [
    null,
    'string',
    false,
    toPrimitive,
  ];

  for (const lastModified of invalidLastModified) {
    const file = new File([], '', { lastModified });
    assert.strictEqual(file.lastModified, 0);
  }
}

{
  const file = new File([], '', { lastModified: undefined });
  assert.notStrictEqual(file.lastModified, 0);
}

{
  const toPrimitive = {
    [Symbol.toPrimitive]() {
      throw new TypeError('boom');
    }
  };

  const throwValues = [
    BigInt(3n),
    toPrimitive,
  ];

  for (const lastModified of throwValues) {
    assert.throws(() => new File([], '', { lastModified }), TypeError);
  }
}

{
  const valid = [
    {
      [Symbol.toPrimitive]() {
        return 10;
      }
    },
    new Number(10),
    10,
  ];

  for (const lastModified of valid) {
    assert.strictEqual(new File([], '', { lastModified }).lastModified, 10);
  }
}

{
  const file = new File([], '');
  assert(inspect(file).startsWith('File { size: 0, type: \'\', name: \'\', lastModified:'));
}

{
  function MyClass() {}
  MyClass.prototype.lastModified = 10;

  const file = new File([], '', new MyClass());
  assert.strictEqual(file.lastModified, 10);
}

{
  let counter = 0;
  new File([], '', {
    get lastModified() {
      counter++;
      return 10;
    }
  });
  assert.strictEqual(counter, 1);
}

{
  const getter = Object.getOwnPropertyDescriptor(File.prototype, 'name').get;

  [
    undefined,
    null,
    true,
  ].forEach((invalidThis) => {
    assert.throws(
      () => getter.call(invalidThis),
      TypeError
    );
  });
}

(async () => {
  // File should be cloneable via structuredClone.
  // Refs: https://github.com/nodejs/node/issues/47612

  const body = ['hello, ', 'world'];
  const lastModified = Date.now() - 10_000;
  const name = 'hello_world.txt';

  const file = new File(body, name, { lastModified });
  const clonedFile = structuredClone(file);

  assert.deepStrictEqual(await clonedFile.text(), await file.text());
  assert.deepStrictEqual(clonedFile.lastModified, file.lastModified);
  assert.deepStrictEqual(clonedFile.name, file.name);

  const clonedFile2 = structuredClone(clonedFile);

  assert.deepStrictEqual(await clonedFile2.text(), await clonedFile.text());
  assert.deepStrictEqual(clonedFile2.lastModified, clonedFile.lastModified);
  assert.deepStrictEqual(clonedFile2.name, clonedFile.name);
})().then(common.mustCall());
