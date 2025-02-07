'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { hasOpenSSL3 } = require('../common/crypto');

if (!hasOpenSSL3)
  common.skip('this test requires OpenSSL 3.x');

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { isMainThread } = require('worker_threads');

if (isMainThread) {
  // TODO(richardlau): Decide if `crypto.setFips` should error if the
  // provider named "fips" is not available.
  crypto.setFips(1);
  crypto.randomBytes(20, common.mustCall((err) => {
    // crypto.randomBytes should either succeed or fail but not hang.
    if (err) {
      assert.match(err.message, /digital envelope routines::unsupported/);
      const expected = /random number generator::unable to fetch drbg/;
      assert(err.opensslErrorStack.some((msg) => expected.test(msg)),
             `did not find ${expected} in ${err.opensslErrorStack}`);
    }
  }));
}

{
  // Startup test. Should not hang.
  const { path } = require('../common/fixtures');
  const { spawnSync } = require('node:child_process');
  const baseConf = path('openssl3-conf', 'base_only.cnf');
  const cp = spawnSync(process.execPath,
                       [ `--openssl-config=${baseConf}`, '-p', '"hello"' ],
                       { encoding: 'utf8' });
  assert(common.nodeProcessAborted(cp.status, cp.signal),
         `process did not abort, code:${cp.status} signal:${cp.signal}`);
}
