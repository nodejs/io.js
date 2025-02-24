'use strict';

// Tests to verify that a correctly formatted invalid encoding error
// is thrown. Originally the internal assertEncoding utility was
// reversing the arguments when constructing the ERR_INVALID_ARG_VALUE
// error.

require('../common');
const { opendirSync } = require('node:fs');
const { throws } = require('node:assert');

throws(() => opendirSync('.', { encoding: 'no' }), {
  code: 'ERR_INVALID_ARG_VALUE',
  message: 'The argument \'encoding\' is invalid encoding. Received \'no\'',
});
