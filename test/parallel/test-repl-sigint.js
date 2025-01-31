'use strict';
const common = require('../common');
if (common.isWindows) {
  // No way to send CTRL_C_EVENT to processes from JS right now.
  common.skip('platform not supported');
}

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('No signal handling available in Workers');
}

const assert = require('assert');
const spawn = require('child_process').spawn;

process.env.REPL_TEST_PPID = process.pid;
const child = spawn(process.execPath, [ '-i' ], {
  stdio: [null, null, 2]
});

let stdout = '';
child.stdout.setEncoding('utf8');
child.stdout.on('data', function(c) {
  stdout += c;
});

child.stdout.once('data', common.mustCall(() => {
  process.on('SIGUSR2', common.mustCall(() => {
    process.kill(child.pid, 'SIGINT');
    child.stdout.once('data', common.mustCall(() => {
      // Make sure state from before the interruption is still available.
      child.stdin.end('a*2*3*7\n');
    }));
  }));

  child.stdin.write('a = 1001;' +
                    'process.kill(+process.env.REPL_TEST_PPID, "SIGUSR2");' +
                    'while(true){}\n');
}));

child.on('close', function(code) {
  assert.strictEqual(code, 0);
  const expected = 'Script execution was interrupted by `SIGINT`';
  assert.ok(
    stdout.includes(expected),
    `Expected stdout to contain "${expected}", got ${stdout}`
  );
  assert.ok(
    stdout.includes('42042\n'),
    `Expected stdout to contain "42042", got ${stdout}`
  );
});
