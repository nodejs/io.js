// Flags: --permission --allow-fs-read=*

'use strict';

const common = require('../../common');
const assert = require('assert');

const bindingPath = require.resolve(`./build/${common.buildType}/binding`);

const assertError = (error) => {
  assert(error instanceof Error);
  assert.strictEqual(error.code, 'ERR_DLOPEN_DISABLED');
  assert.strictEqual(
    error.message,
    'Cannot load native addon because loading addons is disabled.',
  );
};

{
  let threw = false;

  try {
    require(bindingPath);
  } catch (error) {
    assertError(error);
    threw = true;
  }

  assert(threw);
}

{
  let threw = false;

  try {
    process.dlopen({ exports: {} }, bindingPath);
  } catch (error) {
    assertError(error);
    threw = true;
  }

  assert(threw);
}
