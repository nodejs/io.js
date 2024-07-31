'use strict';

require('../common');
const assert = require('assert');
const ClientRequest = require('http').ClientRequest;

{
  assert.throws(() => {
    new ClientRequest({ insecureHTTPParser: 'wrongValue' });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /insecureHTTPParser/
  }, 'http request should throw when passing invalid insecureHTTPParser');
}
