'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

if (process.argv[2] === 'child') {
  process.emitWarning('foo');
} else {
  function test(newEnv) {
    const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child`;

    cp.exec(cmd, { ...opts, env: { ...opts?.env, ...newEnv } }, common.mustCall((err, stdout, stderr) => {
      assert.strictEqual(err, null);
      assert.strictEqual(stdout, '');

      if (newEnv.NODE_NO_WARNINGS === '1')
        assert.strictEqual(stderr, '');
      else
        assert.match(stderr.trim(), /Warning: foo\n/);
    }));
  }

  test({});
  test(process.env);
  test({ NODE_NO_WARNINGS: undefined });
  test({ NODE_NO_WARNINGS: null });
  test({ NODE_NO_WARNINGS: 'foo' });
  test({ NODE_NO_WARNINGS: true });
  test({ NODE_NO_WARNINGS: false });
  test({ NODE_NO_WARNINGS: {} });
  test({ NODE_NO_WARNINGS: [] });
  test({ NODE_NO_WARNINGS: function() {} });
  test({ NODE_NO_WARNINGS: 0 });
  test({ NODE_NO_WARNINGS: -1 });
  test({ NODE_NO_WARNINGS: '0' });
  test({ NODE_NO_WARNINGS: '01' });
  test({ NODE_NO_WARNINGS: '2' });
  // Don't test the number 1 because it will come through as a string in the
  // child process environment.
  test({ NODE_NO_WARNINGS: '1' });
}
