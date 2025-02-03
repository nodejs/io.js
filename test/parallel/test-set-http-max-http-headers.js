'use strict';

const common = require('../common');
const assert = require('assert');
const { spawn } = require('child_process');
const path = require('path');
const { suite, test } = require('node:test');
const testName = path.join(__dirname, 'test-http-max-http-headers.js');

test(function(_, cb) {
  console.log('running subtest expecting failure');

  // Validate that the test fails if the max header size is too small.
  const args = ['--expose-internals',
                '--max-http-header-size=1024',
                testName];
  const cp = spawn(process.execPath, args, { stdio: 'inherit' });

  cp.on('close', common.mustCall((code, signal) => {
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
    cb();
  }));
});

test(function(_, cb) {
  console.log('running subtest expecting success');

  const env = Object.assign({}, process.env, {
    NODE_DEBUG: 'http'
  });

  // Validate that the test now passes if the same limit is large enough.
  const args = ['--expose-internals',
                '--max-http-header-size=1024',
                testName,
                '1024'];
  const cp = spawn(process.execPath, args, {
    env,
    stdio: 'inherit'
  });

  cp.on('close', common.mustCall((code, signal) => {
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
    cb();
  }));
});

const skip = process.config.variables.node_without_node_options;
suite('same checks using NODE_OPTIONS if it is supported', { skip }, () => {
  const env = Object.assign({}, process.env, {
    NODE_OPTIONS: '--max-http-header-size=1024'
  });

  test(function(_, cb) {
    console.log('running subtest expecting failure');

    // Validate that the test fails if the max header size is too small.
    const args = ['--expose-internals', testName];
    const cp = spawn(process.execPath, args, { env, stdio: 'inherit' });

    cp.on('close', common.mustCall((code, signal) => {
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
      cb();
    }));
  });

  test(function(_, cb) {
    // Validate that the test now passes if the same limit is large enough.
    const args = ['--expose-internals', testName, '1024'];
    const cp = spawn(process.execPath, args, { env, stdio: 'inherit' });

    cp.on('close', common.mustCall((code, signal) => {
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
      cb();
    }));
  });
});
