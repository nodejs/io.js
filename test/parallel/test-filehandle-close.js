'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const { test } = require('node:test');

test('FileHandle.close should fail with EBADF when closing an already closed fd', async () => {
  const fh = await fs.promises.open(__filename);
  fs.closeSync(fh.fd);

  // Test that closing an already closed fd results in EBADF
  await assert.rejects(
    () => fh.close(),
    {
      code: 'EBADF',
      syscall: 'close'
    }
  );
}).then(common.mustCall());
