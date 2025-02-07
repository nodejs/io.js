'use strict';

const common = require('../common.js');
const modPath = require.resolve('../fixtures/simple-error-stack.js');
const nodeModulePath = require.resolve('../fixtures/node_modules/error-stack/simple-error-stack.js');
const Module = require('node:module');

const bench = common.createBenchmark(main, {
  method: [
    'without-sourcemap',
    'sourcemap',
    'node-modules-without-sourcemap',
    'node-module-sourcemap'],
  n: [1e5],
});

function runN(n, modPath) {
  delete require.cache[modPath];
  const mod = require(modPath);
  bench.start();
  for (let i = 0; i < n; i++) {
    mod.simpleErrorStack();
  }
  bench.end(n);
}

function main({ n, method }) {
  switch (method) {
    case 'without-sourcemap':
      process.setSourceMapsEnabled(false);
      runN(n, modPath);
      break;
    case 'sourcemap':
      process.setSourceMapsEnabled(true);
      runN(n, modPath);
      break;
    case 'node-modules-without-sourcemap':
      Module.setSourceMapsSupport(true, {
        nodeModules: false,
      });
      runN(n, nodeModulePath);
      break;
    case 'node-modules-sourcemap':
      Module.setSourceMapsSupport(true, {
        nodeModules: true,
      });
      runN(n, nodeModulePath);
      break;
    default:
      throw new Error(`Unexpected method "${method}"`);
  }
}
