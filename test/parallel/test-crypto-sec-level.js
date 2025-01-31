// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');

// OpenSSL has a set of security levels which affect what algorithms
// are available by default. Different OpenSSL veresions have different
// default security levels and we use this value to adjust what a test
// expects based on the security level. You can read more in
// https://docs.openssl.org/1.1.1/man3/SSL_CTX_set_security_level/#default-callback-behaviour
// This test simply validates that we can get some value for the secLevel
// when needed by tests.
const secLevel = require('internal/crypto/util').getOpenSSLSecLevel();
assert.ok(secLevel >= 0 && secLevel <= 5);
