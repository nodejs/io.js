'use strict';
const common = require('../common');

if (common.isWindows || common.isAIX || common.isIBMi)
  common.skip(`No /dev/stdin on ${process.platform}.`);

const assert = require('assert');
const fs = require('fs/promises');
const childType = ['child-encoding', 'child-non-encoding'];

if (process.argv[2] === childType[0]) {
  fs.readFile('/dev/stdin', 'utf8').then((data) => {
    process.stdout.write(data);
  });
  return;
} else if (process.argv[2] === childType[1]) {
  fs.readFile('/dev/stdin').then((data) => {
    process.stdout.write(data);
  });
  return;
}

const data1 = 'Hello';
const data2 = 'World';
const expected = `${data1}\n${data2}\n`;

const exec = require('child_process').exec;
const f = JSON.stringify(__filename);
const node = JSON.stringify(process.execPath);

function test(child) {
  const cmd = `(echo ${data1}; sleep 0.5; echo ${data2}) | ${node} ${f} ${child}`;
  exec(cmd, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(
      stdout,
      expected,
      `expected to read(${child === childType[0] ? 'with' : 'without'} encoding): '${expected}' but got: '${stdout}'`);
    assert.strictEqual(
      stderr,
      '',
      `expected not to read anything from stderr but got: '${stderr}'`);
  }));
}

test(childType[0]);
test(childType[1]);
