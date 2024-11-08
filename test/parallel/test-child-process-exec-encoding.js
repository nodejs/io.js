'use strict';
const common = require('../common');
const stdoutData = 'foo';
const stderrData = 'bar';

if (process.argv[2] === 'child') {
  // The following console calls are part of the test.
  console.log(stdoutData);
  console.error(stderrData);
} else {
  const assert = require('assert');
  const cp = require('child_process');
  const expectedStdout = `${stdoutData}\n`;
  const expectedStderr = `${stderrData}\n`;
  function run(options, callback) {
    const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child`;
    options = { ...options, env: { ...opts?.env, ...options.env } };

    cp.exec(cmd, options, common.mustSucceed((stdout, stderr) => {
      callback(stdout, stderr);
    }));
  }

  // Test default encoding, which should be utf8.
  run({}, (stdout, stderr) => {
    assert.strictEqual(typeof stdout, 'string');
    assert.strictEqual(typeof stderr, 'string');
    assert.strictEqual(stdout, expectedStdout);
    assert.strictEqual(stderr, expectedStderr);
  });

  // Test explicit utf8 encoding.
  run({ encoding: 'utf8' }, (stdout, stderr) => {
    assert.strictEqual(typeof stdout, 'string');
    assert.strictEqual(typeof stderr, 'string');
    assert.strictEqual(stdout, expectedStdout);
    assert.strictEqual(stderr, expectedStderr);
  });

  // Test cases that result in buffer encodings.
  [undefined, null, 'buffer', 'invalid'].forEach((encoding) => {
    run({ encoding }, (stdout, stderr) => {
      assert(stdout instanceof Buffer);
      assert(stdout instanceof Buffer);
      assert.strictEqual(stdout.toString(), expectedStdout);
      assert.strictEqual(stderr.toString(), expectedStderr);
    });
  });
}
