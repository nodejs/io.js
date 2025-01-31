'use strict';

require('../common');
const { spawnSync } = require('child_process');
const assert = require('assert');

const warnFlags = [
  '--allow-addons',
  '--allow-child-process',
  '--allow-wasi',
  '--allow-worker',
];

for (const flag of warnFlags) {
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission', flag, '-e',
      'setTimeout(() => {}, 1)',
    ]
  );

  assert.match(stderr.toString(), new RegExp(`SecurityWarning: The flag ${flag} must be used with extreme caution`));
  assert.strictEqual(status, 0);
}
