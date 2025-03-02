'use strict';

require('../common');
const { test } = require('node:test');
const assert = require('node:assert');

// https://github.com/nodejs/node/issues/57272

test('should throw error when writing after close', async (t) => {
  const writable = new WritableStream({
    write(chunk) {
      console.log(chunk);
    },
  });

  const writer = writable.getWriter();

  await writer.write('Hello');
  await writer.close();

  await assert.rejects(
    async () => {
      await writer.write('World');
    },
    {
      name: 'TypeError',
    }
  );
});
