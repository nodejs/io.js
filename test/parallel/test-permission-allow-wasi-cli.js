// Flags: --experimental-permission --allow-wasi --allow-fs-read=*
'use strict';

const common = require('../common');
common.skipIfWorker();

const assert = require('assert');
const { WASI } = require('wasi');

// Guarantee the initial state
{
  assert.ok(process.permission.has('wasi'));
}

// When a permission is set by cli, the process shouldn't be able
// to create WASI instance unless --allow-wasi is sent
{
  // doesNotThrow
  new WASI({
    version: 'preview1',
  });
}
