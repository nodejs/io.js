'use strict';

// Flags: --expose-internals
// This verifies that
// 1. We do not leak internal modules unless the --require-internals option
//    is on.
// 2. We do not accidentally leak any modules to the public global scope.
// 3. Deprecated modules are properly deprecated.

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Cannot test the existence of --expose-internals from worker');
}

const assert = require('assert');
const fork = require('child_process').fork;

const expectedPublicModules = new Set([
  '_http_agent',
  '_http_client',
  '_http_common',
  '_http_incoming',
  '_http_outgoing',
  '_http_server',
  '_stream_duplex',
  '_stream_passthrough',
  '_stream_readable',
  '_stream_transform',
  '_stream_wrap',
  '_stream_writable',
  '_tls_common',
  '_tls_wrap',
  'assert',
  'async_hooks',
  'buffer',
  'child_process',
  'cluster',
  'console',
  'constants',
  'crypto',
  'dgram',
  'dns',
  'domain',
  'events',
  'fs',
  'http',
  'http2',
  'https',
  'inspector',
  'module',
  'net',
  'os',
  'path',
  'perf_hooks',
  'process',
  'punycode',
  'querystring',
  'readline',
  'repl',
  'stream',
  'string_decoder',
  'sys',
  'timers',
  'tls',
  'trace_events',
  'tty',
  'url',
  'util',
  'v8',
  'vm',
  'worker_threads',
  'zlib',
]);

if (process.argv[2] === 'child') {
  assert(!process.execArgv.includes('--expose-internals'));
  process.once('message', ({ allBuiltins }) => {
    const publicModules = new Set();
    for (const id of allBuiltins) {
      if (id.startsWith('internal/')) {
        assert.throws(() => {
          require(id);
        }, {
          code: 'MODULE_NOT_FOUND',
          message: `Cannot find module '${id}'`
        });
      } else {
        require(id);
        if (!id.startsWith('node:')) {
          require(`node:${id}`);
        }
        publicModules.add(id);
      }
    }
    assert(allBuiltins.length > publicModules.size);
    // Make sure all the public modules are available through
    // require('module').builtinModules
    assert.deepStrictEqual(
      publicModules,
      new Set(require('module').builtinModules)
    );
    assert.deepStrictEqual(publicModules, expectedPublicModules);
  });
} else {
  assert(process.execArgv.includes('--expose-internals'));
  const child = fork(__filename, ['child'], {
    execArgv: []
  });
  const { builtinModules } = require('module');
  // When --expose-internals is on, require('module').builtinModules
  // contains internal modules.
  const message = { allBuiltins: builtinModules };
  child.send(message);
}
