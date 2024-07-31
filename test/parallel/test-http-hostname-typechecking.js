'use strict';

const common = require('../common');
const assert = require('assert');
const http = require('http');

// All of these values should cause http.request() to throw synchronously
// when passed as the value of either options.hostname or options.host
const vals = [{}, [], NaN, Infinity, -Infinity, true, false, 1, 0, new Date()];

vals.forEach((v) => {
  const received = common.invalidArgTypeHelper(v);
  assert.throws(
    () => http.request({ hostname: v }),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "options.hostname" property must be of ' +
               'type string or one of undefined or null.' +
               received
    }
  );

  assert.throws(
    () => http.request({ host: v }),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "options.host" property must be of ' +
               'type string or one of undefined or null.' +
               received
    }
  );
});

// These values are OK and should not throw synchronously.
// Only testing for 'hostname' validation so ignore connection errors.
const dontCare = () => {};
const values = ['', undefined, null];
for (const v of values) {
  http.request({ hostname: v }).on('error', dontCare).end();
  http.request({ host: v }).on('error', dontCare).end();
}
