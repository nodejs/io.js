'use strict';
require('../common');

const {
  Readable,
  pipeline,
  PassThrough
} = require('stream');
const { it } = require('node:test');
const { strictEqual, deepStrictEqual } = require('assert');

const { from, unref } = Readable;

const nextTick = () => new Promise((resolve) => process.nextTick(resolve));

it('unref stream error should propagate to original one', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);

  let emittedError;
  originalStream.on('error', (e) => {
    emittedError = e;
  });
  const unrefStream = unref(originalStream);

  const thrownError = new Error('test');

  unrefStream.destroy(thrownError);

  await nextTick();
  strictEqual(unrefStream.destroyed, true);
  strictEqual(originalStream.destroyed, true);

  // Need another tick to propagate the error
  await nextTick();
  strictEqual(emittedError, thrownError);
});

it('Original stream error should propagate to unref one', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);
  const unrefStream = unref(originalStream);

  let emittedError;
  unrefStream.on('error', (e) => {
    emittedError = e;
  });

  const thrownError = new Error('test');

  originalStream.destroy(thrownError);

  await nextTick();
  strictEqual(unrefStream.destroyed, true);
  strictEqual(originalStream.destroyed, true);

  await nextTick();
  strictEqual(emittedError, thrownError);
});

it('Should not close original stream when unref one finished but not consumed all data', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);

  const unrefStream = unref(originalStream);

  // eslint-disable-next-line no-unused-vars
  for await (const _ of unrefStream) {
    break;
  }

  await nextTick();
  strictEqual(unrefStream.destroyed, true);
  strictEqual(originalStream.destroyed, false);
});

it('Should continue consuming the original stream data from where the unref stopped', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);

  const firstItem = await unref(originalStream).take(1).toArray();
  deepStrictEqual(firstItem, [1]);

  const restOfData = await originalStream.toArray();
  deepStrictEqual(restOfData, [2, 3, 4, 5]);
});

it('Should close original stream when unref one consume all data', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);

  const unrefStream = unref(originalStream);

  const data = await unrefStream.toArray();
  deepStrictEqual(data, [1, 2, 3, 4, 5]);

  await nextTick();

  strictEqual(unrefStream.destroyed, true);
  strictEqual(originalStream.destroyed, true);
});

it('original stream close should close unref one', async () => {
  const originalStream = from([1, 2, 3, 4, 5]);
  const unrefStream = unref(originalStream);

  await originalStream.toArray();

  strictEqual(originalStream.destroyed, true);
  strictEqual(unrefStream.destroyed, true);
});

it('make sure not leaking memory', async () => {
  function getMemoryAllocatedInMB() {
    return Math.round(process.memoryUsage().rss / 1024 / 1024 * 100) / 100;
  }

  const bigData = () => from(async function* () {
    const obj = Array.from({ length: 100000 }, () => (Array.from({ length: 15 }, (_, i) => i)));
    while (true) {
      yield obj.map((item) => item.slice(0));
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
  }());

  const originalStream = pipeline(bigData(), new PassThrough({ objectMode: true }), () => {
  });
  unref(originalStream);
  originalStream.iterator({ destroyOnReturn: true });

  // Making sure some data passed so we won't catch something that is related to the infra
  const iterator = originalStream.iterator({ destroyOnReturn: true });
  for (let j = 0; j < 10; j++) {
    await iterator.next();
  }

  const currentMemory = getMemoryAllocatedInMB();

  for (let j = 0; j < 10; j++) {
    await iterator.next();
  }

  const newMemory = getMemoryAllocatedInMB();

  originalStream.destroy(null);
  strictEqual(newMemory - currentMemory < 100, true, `After consuming 10 items the memory increased by ${Math.floor(newMemory - currentMemory)}MB`);
});
