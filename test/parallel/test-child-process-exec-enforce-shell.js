'use strict';
const common = require('../common');
const assert = require('assert');
const { exec, execSync } = require('child_process');

const invalidArgTypeError = {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
};

exec('echo should-be-passed-as-argument', { shell: '' }, common.mustSucceed((stdout, stderr) => {
  assert.match(stdout, /should-be-passed-as-argument/);
  assert.ok(!stderr);
}));

{
  const ret = execSync('echo should-be-passed-as-argument', { encoding: 'utf-8', shell: '' });
  assert.match(ret, /should-be-passed-as-argument/);
}

for (const fn of [exec, execSync]) {
  assert.throws(() => fn('should-throw-on-boolean-shell-option', { shell: false }), invalidArgTypeError);
}
