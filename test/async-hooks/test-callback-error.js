'use strict';
const common = require('../common');
const assert = require('assert');
const { spawnSync } = require('child_process');
const async_hooks = require('async_hooks');
const initHooks = require('./init-hooks');

const arg = process.argv[2];
switch (arg) {
  case 'test_init_callback':
    initHooks({
      oninit: common.mustCall(() => { throw new Error(arg); }),
    }).enable();
    new async_hooks.AsyncResource(`${arg}_type`);
    return;

  case 'test_callback': {
    initHooks({
      onbefore: common.mustCall(() => { throw new Error(arg); }),
    }).enable();
    const resource = new async_hooks.AsyncResource(`${arg}_type`);
    resource.runInAsyncScope(() => {});
    return;
  }

  case 'test_callback_abort':
    initHooks({
      oninit: common.mustCall(() => { throw new Error(arg); }),
    }).enable();
    new async_hooks.AsyncResource(`${arg}_type`);
    return;
}

// This part should run only for the primary test
assert.ok(!arg);
{
  // console.log should stay until this test's flakiness is solved
  console.log('start case 1');
  console.time('end case 1');
  const child = spawnSync(process.execPath, [__filename, 'test_init_callback']);
  assert.ifError(child.error);
  const test_init_first_line = child.stderr.toString().split(/[\r\n]+/g)[0];
  assert.strictEqual(test_init_first_line, 'Error: test_init_callback');
  assert.strictEqual(child.status, 1);
  console.timeEnd('end case 1');
}

{
  console.log('start case 2');
  console.time('end case 2');
  const child = spawnSync(process.execPath, [__filename, 'test_callback']);
  assert.ifError(child.error);
  const test_callback_first_line = child.stderr.toString().split(/[\r\n]+/g)[0];
  assert.strictEqual(test_callback_first_line, 'Error: test_callback');
  assert.strictEqual(child.status, 1);
  console.timeEnd('end case 2');
}

{
  console.log('start case 3');
  console.time('end case 3');
  let program = process.execPath;
  let args = [
    '--abort-on-uncaught-exception', __filename, 'test_callback_abort' ];
  let options = {};
  if (!common.isWindows) {
    [program, options] = common.escapePOSIXShell`ulimit -c 0 && exec "${program}" ${args[0]} "${args[1]}" ${args[2]}`;
    args = [];
    options.shell = true;
  }

  options.encoding = 'utf8';
  const child = spawnSync(program, args, options);
  if (common.isWindows) {
    assert.strictEqual(child.status, 134);
    assert.strictEqual(child.signal, null);
  } else {
    assert.strictEqual(child.status, null);
    // Most posix systems will show 'SIGABRT', but alpine34 does not
    if (child.signal !== 'SIGABRT') {
      console.log(`primary received signal ${child.signal}\nchild's stderr:`);
      console.log(child.stderr);
      process.exit(1);
    }
    assert.strictEqual(child.signal, 'SIGABRT');
  }
  assert.strictEqual(child.stdout, '');
  const firstLineStderr = child.stderr.split(/[\r\n]+/g)[0].trim();
  assert.strictEqual(firstLineStderr, 'Error: test_callback_abort');
  console.timeEnd('end case 3');
}
