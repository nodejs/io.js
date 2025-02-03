'use strict';

const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const { promises } = fs;
const f = __filename;

// This test ensures that input for lchmod is valid, testing for valid
// inputs for path, mode and callback

if (!common.isMacOS) {
  common.skip('lchmod is only available on macOS');
}

// Check callback
assert.throws(() => fs.lchmod(f), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => fs.lchmod(), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => fs.lchmod(f, {}), { code: 'ERR_INVALID_ARG_TYPE' });

// Check path
[false, 1, {}, [], null, undefined].forEach((i) => {
  assert.throws(
    () => fs.lchmod(i, 0o777, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
  assert.throws(
    () => fs.lchmodSync(i),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
});

// Check mode
[false, null, {}, []].forEach((input) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
  };

  assert.rejects(promises.lchmod(f, input, () => {}), errObj).then(common.mustCall());
  assert.throws(() => fs.lchmodSync(f, input), errObj);
});

assert.throws(() => fs.lchmod(f, '123x', common.mustNotCall()), {
  code: 'ERR_INVALID_ARG_VALUE'
});
assert.throws(() => fs.lchmodSync(f, '123x'), {
  code: 'ERR_INVALID_ARG_VALUE'
});

[-1, 2 ** 32].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "mode" is out of range. It must be >= 0 && <= ' +
             `4294967295. Received ${input}`
  };

  assert.rejects(promises.lchmod(f, input, () => {}), errObj).then(common.mustCall());
  assert.throws(() => fs.lchmodSync(f, input), errObj);
});
