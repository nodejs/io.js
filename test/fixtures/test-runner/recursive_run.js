'use strict';

const { test, run } = require('node:test');

test('recursive run() calls', async () => {
  for await (const event of run({ files: [__filename] }));
});
