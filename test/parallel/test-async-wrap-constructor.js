'use strict';

// This tests that using falsy values in createHook throws an error.

require('../common');
const assert = require('assert');
const async_hooks = require('async_hooks');

const falsyValues = [0, 1, false, true, null, 'hello'];
for (const badArg of falsyValues) {
  const hookNames = ['init', 'before', 'after', 'destroy', 'promiseResolve'];
  for (const hookName of hookNames) {
    assert.throws(() => {
      async_hooks.createHook({ [hookName]: badArg });
    }, {
      code: 'ERR_ASYNC_CALLBACK',
      name: 'TypeError',
      message: `hook.${hookName} must be a function`
    });
  }
}
