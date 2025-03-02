'use strict';
// Refs: https://github.com/nodejs/node/issues/25131
const common = require('../common');
const cp = require('child_process');
const fs = require('fs');

if (process.argv[2] === 'child') {
  fs.closeSync(0);
} else {
  const child = cp.spawn(process.execPath, [__filename, 'child'], {
    stdio: ['pipe', 'inherit', 'inherit']
  });

  child.stdin.on('close', common.mustCall(() => {
    process.exit(0);
  }));

  child.on('close', common.mustNotCall(() => {}));
}
