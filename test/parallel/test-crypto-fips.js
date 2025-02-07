// Flags: --expose-internals
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const spawnSync = require('child_process').spawnSync;
const path = require('path');
const fixtures = require('../common/fixtures');
const { internalBinding } = require('internal/test/binding');
const { testFipsCrypto } = internalBinding('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

const FIPS_ENABLED = 1;
const FIPS_DISABLED = 0;
const FIPS_ERROR_STRING2 =
  'Error [ERR_CRYPTO_FIPS_FORCED]: Cannot set FIPS mode, it was forced with ' +
  '--force-fips at startup.';
const FIPS_UNSUPPORTED_ERROR_STRING = 'fips mode not supported';
const FIPS_ENABLE_ERROR_STRING = 'OpenSSL error when trying to enable FIPS:';

const CNF_FIPS_ON = fixtures.path('openssl_fips_enabled.cnf');
const CNF_FIPS_OFF = fixtures.path('openssl_fips_disabled.cnf');

let num_children_ok = 0;

function sharedOpenSSL() {
  return process.config.variables.node_shared_openssl;
}

function testHelper(stream, args, expectedOutput, cmd, env) {
  const fullArgs = args.concat(['-e', `console.log(${cmd})`]);
  const child = spawnSync(process.execPath, fullArgs, {
    cwd: path.dirname(process.execPath),
    env: env
  });

  console.error(
    `Spawned child [pid:${child.pid}] with cmd '${cmd}' expect %j with args '${
      args}' OPENSSL_CONF=%j`, expectedOutput, env.OPENSSL_CONF);

  function childOk(child) {
    console.error(`Child #${++num_children_ok} [pid:${child.pid}] OK.`);
  }

  function responseHandler(buffer, expectedOutput) {
    const response = buffer.toString();
    assert.notStrictEqual(response.length, 0);
    if (FIPS_ENABLED !== expectedOutput && FIPS_DISABLED !== expectedOutput) {
      // In the case of expected errors just look for a substring.
      assert.ok(response.includes(expectedOutput));
    } else {
      const getFipsValue = Number(response);
      if (!Number.isNaN(getFipsValue))
        // Normal path where we expect either FIPS enabled or disabled.
        assert.strictEqual(getFipsValue, expectedOutput);
    }
    childOk(child);
  }

  responseHandler(child[stream], expectedOutput);
}

// --enable-fips should raise an error if OpenSSL is not FIPS enabled.
testHelper(
  testFipsCrypto() ? 'stdout' : 'stderr',
  ['--enable-fips'],
  testFipsCrypto() ? FIPS_ENABLED : FIPS_ENABLE_ERROR_STRING,
  'process.versions',
  process.env);

// --force-fips should raise an error if OpenSSL is not FIPS enabled.
testHelper(
  testFipsCrypto() ? 'stdout' : 'stderr',
  ['--force-fips'],
  testFipsCrypto() ? FIPS_ENABLED : FIPS_ENABLE_ERROR_STRING,
  'process.versions',
  process.env);

// By default FIPS should be off in both FIPS and non-FIPS builds
// unless Node.js was configured using --shared-openssl in
// which case it may be enabled by the system.
if (!sharedOpenSSL()) {
  testHelper(
    'stdout',
    [],
    FIPS_DISABLED,
    'require("crypto").getFips()',
    { ...process.env, 'OPENSSL_CONF': ' ' });
}

// Toggling fips with setFips should not be allowed from a worker thread
testHelper(
  'stderr',
  [],
  'Calling crypto.setFips() is not supported in workers',
  'new worker_threads.Worker(\'require("crypto").setFips(true);\', { eval: true })',
  process.env);

// This should succeed for both FIPS and non-FIPS builds in combination with
// OpenSSL 1.1.1 or OpenSSL 3.0
const test_result = testFipsCrypto();
assert.ok(test_result === 1 || test_result === 0);

// If Node was configured using --shared-openssl fips support might be
// available depending on how OpenSSL was built. If fips support is
// available the tests that toggle the fips_mode on/off using the config
// file option will succeed and return 1 instead of 0.
//
// Note that this case is different from when calling the fips setter as the
// configuration file is handled by OpenSSL, so it is not possible for us
// to try to call the fips setter, to try to detect this situation, as
// that would throw an error:
// ("Error: Cannot set FIPS mode in a non-FIPS build.").
// Due to this uncertainty the following tests are skipped when configured
// with --shared-openssl.
if (!sharedOpenSSL() && !hasOpenSSL3) {
  // OpenSSL config file should be able to turn on FIPS mode
  testHelper(
    'stdout',
    [`--openssl-config=${CNF_FIPS_ON}`],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_DISABLED,
    'require("crypto").getFips()',
    process.env);

  // OPENSSL_CONF should be able to turn on FIPS mode
  testHelper(
    'stdout',
    [],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_DISABLED,
    'require("crypto").getFips()',
    Object.assign({}, process.env, { 'OPENSSL_CONF': CNF_FIPS_ON }));

  // --openssl-config option should override OPENSSL_CONF
  testHelper(
    'stdout',
    [`--openssl-config=${CNF_FIPS_ON}`],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_DISABLED,
    'require("crypto").getFips()',
    Object.assign({}, process.env, { 'OPENSSL_CONF': CNF_FIPS_OFF }));
}

// OpenSSL 3.x has changed the configuration files so the following tests
// will not work as expected with that version.
// TODO(danbev) Revisit these test once FIPS support is available in
// OpenSSL 3.x.
if (!hasOpenSSL3) {
  testHelper(
    'stdout',
    [`--openssl-config=${CNF_FIPS_OFF}`],
    FIPS_DISABLED,
    'require("crypto").getFips()',
    Object.assign({}, process.env, { 'OPENSSL_CONF': CNF_FIPS_ON }));

  // --enable-fips should take precedence over OpenSSL config file
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--enable-fips', `--openssl-config=${CNF_FIPS_OFF}`],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    process.env);
  // --force-fips should take precedence over OpenSSL config file
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--force-fips', `--openssl-config=${CNF_FIPS_OFF}`],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    process.env);
  // --enable-fips should turn FIPS mode on
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--enable-fips'],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    process.env);

  // --force-fips should turn FIPS mode on
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--force-fips'],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    process.env);

  // OPENSSL_CONF should _not_ make a difference to --enable-fips
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--enable-fips'],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    Object.assign({}, process.env, { 'OPENSSL_CONF': CNF_FIPS_OFF }));

  // Using OPENSSL_CONF should not make a difference to --force-fips
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--force-fips'],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").getFips()',
    Object.assign({}, process.env, { 'OPENSSL_CONF': CNF_FIPS_OFF }));

  // setFipsCrypto should be able to turn FIPS mode on
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    [],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    '(require("crypto").setFips(true),' +
    'require("crypto").getFips())',
    process.env);

  // setFipsCrypto should be able to turn FIPS mode on and off
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    [],
    testFipsCrypto() ? FIPS_DISABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    '(require("crypto").setFips(true),' +
    'require("crypto").setFips(false),' +
    'require("crypto").getFips())',
    process.env);

  // setFipsCrypto takes precedence over OpenSSL config file, FIPS on
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    [`--openssl-config=${CNF_FIPS_OFF}`],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    '(require("crypto").setFips(true),' +
    'require("crypto").getFips())',
    process.env);

  // setFipsCrypto takes precedence over OpenSSL config file, FIPS off
  testHelper(
    'stdout',
    [`--openssl-config=${CNF_FIPS_ON}`],
    FIPS_DISABLED,
    '(require("crypto").setFips(false),' +
    'require("crypto").getFips())',
    process.env);

  // --enable-fips does not prevent use of setFipsCrypto API
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--enable-fips'],
    testFipsCrypto() ? FIPS_DISABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    '(require("crypto").setFips(false),' +
    'require("crypto").getFips())',
    process.env);

  // --force-fips prevents use of setFipsCrypto API
  testHelper(
    'stderr',
    ['--force-fips'],
    testFipsCrypto() ? FIPS_ERROR_STRING2 : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").setFips(false)',
    process.env);

  // --force-fips makes setFipsCrypto enable a no-op (FIPS stays on)
  testHelper(
    testFipsCrypto() ? 'stdout' : 'stderr',
    ['--force-fips'],
    testFipsCrypto() ? FIPS_ENABLED : FIPS_UNSUPPORTED_ERROR_STRING,
    '(require("crypto").setFips(true),' +
    'require("crypto").getFips())',
    process.env);

  // --force-fips and --enable-fips order does not matter
  testHelper(
    'stderr',
    ['--force-fips', '--enable-fips'],
    testFipsCrypto() ? FIPS_ERROR_STRING2 : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").setFips(false)',
    process.env);

  // --enable-fips and --force-fips order does not matter
  testHelper(
    'stderr',
    ['--enable-fips', '--force-fips'],
    testFipsCrypto() ? FIPS_ERROR_STRING2 : FIPS_UNSUPPORTED_ERROR_STRING,
    'require("crypto").setFips(false)',
    process.env);
}
