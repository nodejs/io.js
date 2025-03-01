'use strict';

const common = require('../../common');
const process = require('process')
const assert = require('assert');
const { fork } = require('child_process');
const binding = require(`./build/${common.buildType}/binding`);

if (process.argv[2] === 'child') {
  binding();
  setTimeout(() => {}, 100);
} else {
  const child = fork(__filename, ['child']);
  child.on('close', (code) => {
    assert.strictEqual(code, 0);
  });
}
