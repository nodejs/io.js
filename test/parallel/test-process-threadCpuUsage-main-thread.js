'use strict';

const { isSunOS } = require('../common');

const { ok, throws, notStrictEqual } = require('assert');

function validateResult(result) {
  notStrictEqual(result, null);

  ok(Number.isFinite(result.user));
  ok(Number.isFinite(result.system));

  ok(result.user >= 0);
  ok(result.system >= 0);
}

// Test that process.threadCpuUsage() works on the main thread
// The if check and the else branch should be removed once SmartOS support is fixed in
// https://github.com/libuv/libuv/issues/4706
if (!isSunOS) {
  const result = process.threadCpuUsage();

  // Validate the result of calling with no previous value argument.
  validateResult(process.threadCpuUsage());

  // Validate the result of calling with a previous value argument.
  validateResult(process.threadCpuUsage(result));

  // Ensure the results are >= the previous.
  let thisUsage;
  let lastUsage = process.threadCpuUsage();
  for (let i = 0; i < 10; i++) {
    thisUsage = process.threadCpuUsage();
    validateResult(thisUsage);
    ok(thisUsage.user >= lastUsage.user);
    ok(thisUsage.system >= lastUsage.system);
    lastUsage = thisUsage;
  }
} else {
  throws(
    () => process.threadCpuUsage(),
    {
      code: 'ERR_OPERATION_FAILED',
      name: 'Error',
      message: 'Operation failed: threadCpuUsage is not available on SunOS'
    }
  );
}

// Test argument validaton
{
  throws(
    () => process.threadCpuUsage(123),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "prevValue" argument must be of type object. Received type number (123)'
    }
  );

  throws(
    () => process.threadCpuUsage([]),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "prevValue" argument must be of type object. Received an instance of Array'
    }
  );

  throws(
    () => process.threadCpuUsage({ user: -123 }),
    {
      code: 'ERR_INVALID_ARG_VALUE',
      name: 'RangeError',
      message: "The property 'prevValue.user' is invalid. Received -123"
    }
  );

  throws(
    () => process.threadCpuUsage({ user: 0, system: 'bar' }),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: "The \"prevValue.system\" property must be of type number. Received type string ('bar')"
    }
  );
}
