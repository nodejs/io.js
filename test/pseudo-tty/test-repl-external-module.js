'use strict';

require('../common');
const fixtures = require('../common/fixtures');
const { execFileSync } = require('child_process');

execFileSync(process.execPath, {
  encoding: 'utf8',
  stdio: 'inherit',
  env: {
    ...process.env,
    NODE_REPL_EXTERNAL_MODULE: fixtures.path('external-repl-module.js'),
  },
});
