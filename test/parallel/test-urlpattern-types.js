'use strict';

require('../common');

const { URLPattern } = require('url');
const { throws } = require('assert');

// Verifies that calling URLPattern with no new keyword throws.
throws(() => URLPattern(), {
  code: 'ERR_CONSTRUCT_CALL_REQUIRED',
});

// Verifies that type checks are performed on the arguments.
throws(() => new URLPattern(1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => new URLPattern({}, 1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => new URLPattern({}, '', 1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => new URLPattern({}, { ignoreCase: '' }), {
  code: 'ERR_INVALID_ARG_TYPE',
});

const pattern = new URLPattern();

throws(() => pattern.exec(1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => pattern.exec('', 1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => pattern.test(1), {
  code: 'ERR_INVALID_ARG_TYPE',
});

throws(() => pattern.test('', 1), {
  code: 'ERR_INVALID_ARG_TYPE',
});
