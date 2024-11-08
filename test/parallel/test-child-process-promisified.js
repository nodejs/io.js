'use strict';
const common = require('../common');
const assert = require('assert');
const child_process = require('child_process');
const { promisify } = require('util');

const exec = promisify(child_process.exec);
const execFile = promisify(child_process.execFile);

{
  const promise = exec(...common.escapePOSIXShell`"${process.execPath}" -p 42`);

  assert(promise.child instanceof child_process.ChildProcess);
  promise.then(common.mustCall((obj) => {
    assert.deepStrictEqual(obj, { stdout: '42\n', stderr: '' });
  }));
}

{
  const promise = execFile(process.execPath, ['-p', '42']);

  assert(promise.child instanceof child_process.ChildProcess);
  promise.then(common.mustCall((obj) => {
    assert.deepStrictEqual(obj, { stdout: '42\n', stderr: '' });
  }));
}

{
  const promise = exec('doesntexist');

  assert(promise.child instanceof child_process.ChildProcess);
  promise.catch(common.mustCall((err) => {
    assert(err.message.includes('doesntexist'));
  }));
}

{
  const promise = execFile('doesntexist', ['-p', '42']);

  assert(promise.child instanceof child_process.ChildProcess);
  promise.catch(common.mustCall((err) => {
    assert(err.message.includes('doesntexist'));
  }));
}
const failingCodeWithStdoutErr =
  'console.log(42);console.error(43);process.exit(1)';
{
  exec(...common.escapePOSIXShell`"${process.execPath}" -e "${failingCodeWithStdoutErr}"`)
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 1);
      assert.strictEqual(err.stdout, '42\n');
      assert.strictEqual(err.stderr, '43\n');
    }));
}

{
  execFile(process.execPath, ['-e', failingCodeWithStdoutErr])
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 1);
      assert.strictEqual(err.stdout, '42\n');
      assert.strictEqual(err.stderr, '43\n');
    }));
}
