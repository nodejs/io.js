// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const { internalBinding } = require('internal/test/binding');
const cares = internalBinding('cares_wrap');
cares.getaddrinfo = () => internalBinding('uv').UV_ENOMEM;

// This test ensures that dns.lookup issue a DeprecationWarning
// when invalid options type is given

const dnsPromises = require('dns/promises');

common.expectWarning({
  'internal/test/binding': [
    'These APIs are for internal testing only. Do not use them.',
  ],
});

assert.throws(() => {
  dnsPromises.lookup('127.0.0.1', { hints: '-1' });
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});
assert.throws(() => dnsPromises.lookup('127.0.0.1', { hints: -1 }),
              { code: 'ERR_INVALID_ARG_VALUE' });
assert.throws(() => dnsPromises.lookup('127.0.0.1', { family: '6' }),
              { code: 'ERR_INVALID_ARG_VALUE' });
assert.throws(() => dnsPromises.lookup('127.0.0.1', { all: 'true' }),
              { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => dnsPromises.lookup('127.0.0.1', { verbatim: 'true' }),
              { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => dnsPromises.lookup('127.0.0.1', { order: 'true' }),
              { code: 'ERR_INVALID_ARG_VALUE' });
assert.throws(() => dnsPromises.lookup('127.0.0.1', '6'),
              { code: 'ERR_INVALID_ARG_TYPE' });
