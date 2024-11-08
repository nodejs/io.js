'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const childProcess = require('child_process');
const fs = require('fs');

const stdoutScript = fixtures.path('echo-close-check.js');
const tmpFile = tmpdir.resolve('stdin.txt');
const string = fixtures.utf8TestText;

tmpdir.refresh();

fs.writeFileSync(tmpFile, string);

childProcess.exec(...common.escapePOSIXShell`"${process.argv0}" "${stdoutScript}" < "${tmpFile}"`, common.mustCall(function(err, stdout, stderr) {
  fs.unlinkSync(tmpFile);

  assert.ifError(err);
  assert.strictEqual(stdout, `hello world\r\n${string}`);
  assert.strictEqual(stderr, '');
}));
