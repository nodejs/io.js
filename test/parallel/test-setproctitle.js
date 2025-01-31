'use strict';
// Original test written by Jakub Lekstan <kuebzky@gmail.com>
const common = require('../common');
const { isMainThread } = require('worker_threads');

// FIXME add sunos support
if (common.isSunOS || common.isIBMi) {
  common.skip(`Unsupported platform [${process.platform}]`);
}

if (!isMainThread) {
  common.skip('Setting the process title from Workers is not supported');
}

const assert = require('assert');
const { exec, execSync } = require('child_process');
const path = require('path');

// The title shouldn't be too long; libuv's uv_set_process_title() out of
// security considerations no longer overwrites envp, only argv, so the
// maximum title length is possibly quite short.
let title = String(process.pid);

assert.notStrictEqual(process.title, title);
process.title = title;
assert.strictEqual(process.title, title);

// Test setting the title but do not try to run `ps` on Windows.
if (common.isWindows) {
  common.skip('Windows does not have "ps" utility');
}

try {
  execSync('command -v ps');
} catch (err) {
  if (err.status === 1) {
    common.skip('The "ps" utility is not available');
  }
  throw err;
}

// To pass this test on alpine, since Busybox `ps` does not
// support `-p` switch, use `ps -o` and `grep` instead.
const cmd = common.isLinux ?
  `ps -o pid,args | grep '${process.pid} ${title}' | grep -v grep` :
  `ps -p ${process.pid} -o args=`;

exec(cmd, common.mustSucceed((stdout, stderr) => {
  assert.strictEqual(stderr, '');

  // Freebsd always add ' (procname)' to the process title
  if (common.isFreeBSD || common.isOpenBSD)
    title += ` (${path.basename(process.execPath)})`;

  // Omitting trailing whitespace and \n
  assert.strictEqual(stdout.replace(/\s+$/, '').endsWith(title), true);
}));
