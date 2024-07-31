'use strict';
const common = require('../common');

const assert = require('node:assert');
const { execFileSync } = require('node:child_process');

if (!common.isLinux) {
  common.skip('test is Linux specific');
}

if (process.arch !== 'x64' && process.arch !== 'arm64') {
  common.skip('io_uring support on this architecture is uncertain');
}

const kv = /^(\d+)\.(\d+)\.(\d+)/.exec(execFileSync('uname', ['-r'])).slice(1).map((n) => parseInt(n, 10));
if (((kv[0] << 16) | (kv[1] << 8) | kv[2]) < 0x050ABA) {
  common.skip('io_uring is likely buggy due to old kernel');
}

const userIdentitySetters = [
  ['setuid', [1000]],
  ['seteuid', [1000]],
  ['setgid', [1000]],
  ['setegid', [1000]],
  ['setgroups', [[1000]]],
  ['initgroups', ['nodeuser', 1000]],
];

for (const [fnName, args] of userIdentitySetters) {
  const call = `process.${fnName}(${args.map((a) => JSON.stringify(a)).join(', ')})`;
  const code = `try { ${call}; } catch (err) { console.log(err); }`;

  const stdout = execFileSync(process.execPath, ['-e', code], {
    encoding: 'utf8',
    env: { ...process.env, UV_USE_IO_URING: '1' },
  });

  const msg = new RegExp(`^Error: ${fnName}\\(\\) disabled: io_uring may be enabled\\. See CVE-[X0-9]{4}-`);
  assert.match(stdout, msg);
  assert.match(stdout, /code: 'ERR_INVALID_STATE'/);

  console.log(call, stdout.slice(0, stdout.indexOf('\n')));
}
