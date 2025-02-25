import { mustCall, isWindows } from '../common/index.mjs';
import { ok, ifError, strictEqual } from 'assert';
import { spawnSync } from 'child_process';
import { argv, execPath, exit } from 'process';
import { AsyncResource } from 'async_hooks';
import initHooks from './init-hooks.mjs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

const arg = argv[2];
switch (arg) {
  case 'test_init_callback':
    initHooks({
      oninit: mustCall(() => { throw new Error(arg); })
    }).enable();
    new AsyncResource(`${arg}_type`);
    exit();

  case 'test_callback': {
    initHooks({
      onbefore: mustCall(() => { throw new Error(arg); })
    }).enable();
    const resource = new AsyncResource(`${arg}_type`);
    resource.runInAsyncScope(() => {});
    exit();
  }

  case 'test_callback_abort':
    initHooks({
      oninit: mustCall(() => { throw new Error(arg); })
    }).enable();
    new AsyncResource(`${arg}_type`);
    exit();
}

// This part should run only for the primary test
ok(!arg);
{
  // console.log should stay until this test's flakiness is solved
  console.log('start case 1');
  console.time('end case 1');
  const child = spawnSync(execPath, [__filename, 'test_init_callback']);
  ifError(child.error);
  const test_init_first_line = child.stderr.toString().split(/[\r\n]+/g)[0];
  strictEqual(test_init_first_line, 'Error: test_init_callback');
  strictEqual(child.status, 1);
  console.timeEnd('end case 1');
}

{
  console.log('start case 2');
  console.time('end case 2');
  const child = spawnSync(execPath, [__filename, 'test_callback']);
  ifError(child.error);
  const test_callback_first_line = child.stderr.toString().split(/[\r\n]+/g)[0];
  strictEqual(test_callback_first_line, 'Error: test_callback');
  strictEqual(child.status, 1);
  console.timeEnd('end case 2');
}

{
  console.log('start case 3');
  console.time('end case 3');
  let program = execPath;
  let args = [
    '--abort-on-uncaught-exception', __filename, 'test_callback_abort' ];
  const options = { encoding: 'utf8' };
  if (!isWindows) {
    program = `ulimit -c 0 && exec ${program} ${args.join(' ')}`;
    args = [];
    options.shell = true;
  }
  const child = spawnSync(program, args, options);
  if (isWindows) {
    strictEqual(child.status, 134);
    strictEqual(child.signal, null);
  } else {
    strictEqual(child.status, null);
    // Most posix systems will show 'SIGABRT', but alpine34 does not
    if (child.signal !== 'SIGABRT') {
      console.log(`primary received signal ${child.signal}\nchild's stderr:`);
      console.log(child.stderr);
      exit(1);
    }
    strictEqual(child.signal, 'SIGABRT');
  }
  strictEqual(child.stdout, '');
  const firstLineStderr = child.stderr.split(/[\r\n]+/g)[0].trim();
  strictEqual(firstLineStderr, 'Error: test_callback_abort');
  console.timeEnd('end case 3');
}
