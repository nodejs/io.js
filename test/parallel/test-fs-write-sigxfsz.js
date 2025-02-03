// Check that exceeding RLIMIT_FSIZE fails with EFBIG
// rather than terminating the process with SIGXFSZ.
'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');

const assert = require('assert');
const child_process = require('child_process');
const fs = require('fs');

if (common.isWindows)
  common.skip('no RLIMIT_FSIZE on Windows');

if (process.config.variables.node_shared)
  common.skip('SIGXFSZ signal handler not installed in shared library mode');

if (process.argv[2] === 'child') {
  const filename = tmpdir.resolve('efbig.txt');
  tmpdir.refresh();
  fs.writeFileSync(filename, '.'.repeat(1 << 16));  // Exceeds RLIMIT_FSIZE.
} else {
  const [cmd, opts] = common.escapePOSIXShell`ulimit -f 1 && "${process.execPath}" "${__filename}" child`;
  const result = child_process.spawnSync('/bin/sh', ['-c', cmd], opts);
  const haystack = result.stderr.toString();
  const needle = 'Error: EFBIG: file too large, write';
  const ok = haystack.includes(needle);
  if (!ok) console.error(haystack);
  assert(ok);
  assert.strictEqual(result.status, 1);
  assert.strictEqual(result.stdout.toString(), '');
}
