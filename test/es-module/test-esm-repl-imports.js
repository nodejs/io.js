'use strict';

const { mustCall } = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { spawn } = require('node:child_process');
const { execPath } = require('node:process');
const { describe, it } = require('node:test');


describe('ESM: REPL runs', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it((t, done) => {
    const child = spawn(execPath, [
      '--interactive',
    ], {
      cwd: fixtures.path('es-modules', 'pkgimports'),
    });

    child.stdin.end(
      'try{require("#test");await import("#test")}catch{process.exit(-1)}'
    );

    child.on('exit', mustCall((code) => {
      assert.strictEqual(code, 0);
      done();
    }));
  });
});
