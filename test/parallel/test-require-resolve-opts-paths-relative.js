'use strict';

const common = require('../common');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const { isMainThread } = require('worker_threads');

if (!isMainThread)
  common.skip('process.chdir is not available in Workers');

const subdir = fixtures.path('module-require', 'relative', 'subdir');

process.chdir(subdir);

// Parent directory paths (`..`) work as intended
{
  assert(require.resolve('.', { paths: ['../'] }).endsWith('index.js'));
  assert(require.resolve('./index.js', { paths: ['../'] }).endsWith('index.js'));

  // paths: [".."] should resolve like paths: ["../"]
  assert(require.resolve('.', { paths: ['..'] }).endsWith('index.js'));
  assert(require.resolve('./index.js', { paths: ['..'] }).endsWith('index.js'));
}

process.chdir('..');

// Current directory paths (`.`) work as intended
{
  assert(require.resolve('.', { paths: ['.'] }).endsWith('index.js'));
  assert(require.resolve('./index.js', { paths: ['./'] }).endsWith('index.js'));

  // paths: ["."] should resolve like paths: ["../"]
  assert(require.resolve('.', { paths: ['.'] }).endsWith('index.js'));
  assert(require.resolve('./index.js', { paths: ['.'] }).endsWith('index.js'));
}

// Sub directory paths work as intended
{
  // assert.deepStrictEqual(fs.readdirSync('./subdir'), [5]);
  assert(require.resolve('./relative-subdir.js', { paths: ['./subdir'] }).endsWith('relative-subdir.js'));

  // paths: ["subdir"] should resolve like paths: ["./subdir"]
  assert(require.resolve('./relative-subdir.js', { paths: ['subdir'] }).endsWith('relative-subdir.js'));
}
