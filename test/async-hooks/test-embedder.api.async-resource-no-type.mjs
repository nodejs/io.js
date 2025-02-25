import { mustCall } from '../common/index.mjs';
import { throws, strictEqual } from 'assert';
import { AsyncResource, executionAsyncId } from 'async_hooks';
import { spawn } from 'child_process';
import { argv, execPath } from 'process';

import initHooks from './init-hooks.mjs';

if (argv[2] === 'child') {
  initHooks().enable();

  class Foo extends AsyncResource {
    constructor(type) {
      super(type, executionAsyncId());
    }
  }

  [null, undefined, 1, Date, {}, []].forEach((i) => {
    throws(() => new Foo(i), {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
  });

} else {
  const args = argv.slice(1).concat('child');
  spawn(execPath, args)
    .on('close', mustCall((code) => {
      // No error because the type was defaulted
      strictEqual(code, 0);
    }));
}
