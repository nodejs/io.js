'use strict';
const common = require('../../common');

// This tests crypto.setEngine().

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

// Engine support in OpenSSL is checked later on.
let hasEngineSupport = true;

assert.throws(() => crypto.setEngine(true), /ERR_INVALID_ARG_TYPE|ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED/);
assert.throws(() => crypto.setEngine('/path/to/engine', 'notANumber'),
              /ERR_INVALID_ARG_TYPE/);

{
  const invalidEngineName = 'xxx';
  assert.throws(() => crypto.setEngine(invalidEngineName),
                /ERR_CRYPTO_ENGINE_UNKNOWN|ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED/);
  assert.throws(() => crypto.setEngine(invalidEngineName,
                                       crypto.constants.ENGINE_METHOD_RSA),
                /ERR_CRYPTO_ENGINE_UNKNOWN|ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED/);
}

try {
  crypto.setEngine('dynamic');
  crypto.setEngine('dynamic');

  crypto.setEngine('dynamic', crypto.constants.ENGINE_METHOD_RSA);
  crypto.setEngine('dynamic', crypto.constants.ENGINE_METHOD_RSA);
} catch (err) {
  assert.strictEqual(err.code, 'ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED');
  hasEngineSupport = false;
}

if (hasEngineSupport) {
  const engine = path.join(__dirname,
                           `/build/${common.buildType}/testsetengine.engine`);

  if (!fs.existsSync(engine))
    common.skip('no engine');

  {
    const engineId = path.parse(engine).name;
    const execDir = path.parse(engine).dir;

    crypto.setEngine(engine);
    // OpenSSL 3.0.1 and 1.1.1m now throw errors if an engine is loaded again
    // with a duplicate absolute path.
    // TODO(richardlau): figure out why this fails on macOS but not Linux.
    // crypto.setEngine(engine);

    // crypto.setEngine(engine, crypto.constants.ENGINE_METHOD_RSA);
    // crypto.setEngine(engine, crypto.constants.ENGINE_METHOD_RSA);

    process.env.OPENSSL_ENGINES = execDir;

    crypto.setEngine(engineId);
    crypto.setEngine(engineId);

    crypto.setEngine(engineId, crypto.constants.ENGINE_METHOD_RSA);
    crypto.setEngine(engineId, crypto.constants.ENGINE_METHOD_RSA);
  }
}
