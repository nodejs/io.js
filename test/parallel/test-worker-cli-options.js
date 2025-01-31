// Flags: --expose-internals --expose-gc
'use strict';
const common = require('../common');
const { Worker } = require('worker_threads');
const assert = require('assert');

const CODE = `
// If the --expose-internals flag does not pass to worker
// require function will throw an error
require('internal/options');
globalThis.gc();
`;

// Test if the flags is passed to worker threads correctly
// and do not throw an error with the invalid execArgv
// when execArgv is inherited from parent
// See https://github.com/nodejs/node/issues/52825
// See https://github.com/nodejs/node/issues/53011

// Inherited env, execArgv from the parent will be ok
new Worker(CODE, { eval: true });
// Pass process.env explicitly and inherited execArgv from parent will be ok
new Worker(CODE, { eval: true, env: process.env });
// Inherited env from the parent and pass execArgv (Node.js options) explicitly will be ok
new Worker(CODE, { eval: true, execArgv: ['--expose-internals'] });
// Pass process.env and execArgv (Node.js options) explicitly will be ok
new Worker(CODE, { eval: true, env: process.env, execArgv: ['--expose-internals'] });
//  Pass execArgv (V8 options) explicitly will throw an error
assert.throws(() => {
  new Worker(CODE, { eval: true, execArgv: ['--expose-gc'] });
}, /ERR_WORKER_INVALID_EXEC_ARGV/);

// Test ESM eval
new Worker('export {}', { eval: true, execArgv: ['--input-type=module'] });
new Worker('export {}', { eval: true, execArgv: ['--input-type=commonjs'] })
  .once('error', common.expectsError({ name: 'SyntaxError' }));
new Worker('export {}', { eval: true, execArgv: ['--experimental-detect-module'] });
new Worker('export {}', { eval: true, execArgv: ['--no-experimental-detect-module'] })
  .once('error', common.expectsError({ name: 'SyntaxError' }));
