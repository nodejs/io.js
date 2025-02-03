// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
if (module !== require.main) {
  // Signal we've been loaded as a module.
  // The following console.log() is part of the test.
  console.log('Loaded as a module, exiting with status code 42.');
  process.exit(42);
}

const common = require('../common');
const assert = require('assert');
const child = require('child_process');
const path = require('path');
const fixtures = require('../common/fixtures');

if (process.argv.length > 2) {
  console.log(process.argv.slice(2).join(' '));
  process.exit(0);
}

// Assert that nothing is written to stdout.
child.exec(...common.escapePOSIXShell`"${process.execPath}" --eval 42`, common.mustSucceed((stdout, stderr) => {
  assert.strictEqual(stdout, '');
  assert.strictEqual(stderr, '');
}));

// Assert that "42\n" is written to stderr.
child.exec(...common.escapePOSIXShell`"${process.execPath}" --eval "console.error(42)"`,
           common.mustSucceed((stdout, stderr) => {
             assert.strictEqual(stdout, '');
             assert.strictEqual(stderr, '42\n');
           }));

// Assert that the expected output is written to stdout.
['--print', '-p -e', '-pe', '-p'].forEach((s) => {
  const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" ${s}`;

  child.exec(`${cmd} 42`, opts, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout, '42\n');
    assert.strictEqual(stderr, '');
  }));

  child.exec(`${cmd} '[]'`, opts, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout, '[]\n');
    assert.strictEqual(stderr, '');
  }));
});

// Assert that module loading works.
{
  common.spawnPromisified(process.execPath, ['--eval', `require(${JSON.stringify(__filename)})`])
    .then(common.mustCall(({ stdout, stderr, code }) => {
      assert.strictEqual(stderr, '');
      assert.strictEqual(
        stdout, 'Loaded as a module, exiting with status code 42.\n');
      assert.strictEqual(code, 42);
    }));
}

// Check that builtin modules are pre-defined.
child.exec(...common.escapePOSIXShell`"${process.execPath}" --print "os.platform()"`,
           common.mustSucceed((stdout, stderr) => {
             assert.strictEqual(stderr, '');
             assert.strictEqual(stdout.trim(), require('os').platform());
           }));

// Module path resolve bug regression test.
const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" --eval "require('./test/parallel/test-cli-eval.js')"`;
child.exec(cmd,
           { ...opts, cwd: path.resolve(__dirname, '../../') },
           common.mustCall((err, stdout, stderr) => {
             assert.strictEqual(err.code, 42);
             assert.strictEqual(
               stdout, 'Loaded as a module, exiting with status code 42.\n');
             assert.strictEqual(stderr, '');
           }));

// Missing argument should not crash.
child.exec(...common.escapePOSIXShell`"${process.execPath}" -e`, common.mustCall((err, stdout, stderr) => {
  assert.strictEqual(err.code, 9);
  assert.strictEqual(stdout, '');
  assert.strictEqual(stderr.trim(),
                     `${process.execPath}: -e requires an argument`);
}));

// Empty program should do nothing.
child.exec(...common.escapePOSIXShell`"${process.execPath}" -e ""`, common.mustSucceed((stdout, stderr) => {
  assert.strictEqual(stdout, '');
  assert.strictEqual(stderr, '');
}));

// "\\-42" should be interpreted as an escaped expression, not a switch.
child.exec(...common.escapePOSIXShell`"${process.execPath}" -p "\\-42"`, common.mustSucceed((stdout, stderr) => {
  assert.strictEqual(stdout, '-42\n');
  assert.strictEqual(stderr, '');
}));

child.exec(...common.escapePOSIXShell`"${process.execPath}" --use-strict -p process.execArgv`,
           common.mustSucceed((stdout, stderr) => {
             assert.strictEqual(
               stdout, "[ '--use-strict', '-p', 'process.execArgv' ]\n"
             );
             assert.strictEqual(stderr, '');
           }));

// Regression test for https://github.com/nodejs/node/issues/3574.
{
  const emptyFile = fixtures.path('empty.js');

  common.spawnPromisified(process.execPath, ['-e', `require("child_process").fork(${JSON.stringify(emptyFile)})`])
    .then(common.mustCall(({ stdout, stderr, code }) => {
      assert.strictEqual(stdout, '');
      assert.strictEqual(stderr, '');
      assert.strictEqual(code, 0);
    }));

  // Make sure that monkey-patching process.execArgv doesn't cause child_process
  // to incorrectly munge execArgv.
  common.spawnPromisified(process.execPath, [
    '-e',
    'process.execArgv = [\'-e\', \'console.log(42)\', \'thirdArg\'];' +
      `require('child_process').fork(${JSON.stringify(emptyFile)})`,
  ]).then(common.mustCall(({ stdout, stderr, code }) => {
    assert.strictEqual(stdout, '42\n');
    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
  }));
}

// Regression test for https://github.com/nodejs/node/issues/8534.
{
  const script = `
      // console.log() can revive the event loop so we must be careful
      // to write from a 'beforeExit' event listener only once.
      process.once("beforeExit", () => console.log("beforeExit"));
      process.on("exit", () => console.log("exit"));
      console.log("start");
  `;
  const options = { encoding: 'utf8' };
  const proc = child.spawnSync(process.execPath, ['-e', script], options);
  assert.strictEqual(proc.stderr, '');
  assert.strictEqual(proc.stdout, 'start\nbeforeExit\nexit\n');
}

// Regression test for https://github.com/nodejs/node/issues/11948.
{
  const script = `
      process.on('message', (message) => {
        if (message === 'ping') process.send('pong');
        if (message === 'exit') process.disconnect();
      });
  `;
  const proc = child.fork('-e', [script]);
  proc.on('exit', common.mustCall((exitCode, signalCode) => {
    assert.strictEqual(exitCode, 0);
    assert.strictEqual(signalCode, null);
  }));
  proc.on('message', (message) => {
    if (message === 'pong') proc.send('exit');
  });
  proc.send('ping');
}

[ '-arg1',
  '-arg1 arg2 --arg3',
  '--',
  'arg1 -- arg2',
].forEach(function(args) {

  // Ensure that arguments are successfully passed to eval.
  child.exec(
    ...common.escapePOSIXShell`"${process.execPath}" --eval "console.log(process.argv.slice(1).join(' '))" -- ${args}`,
    common.mustCall(function(err, stdout, stderr) {
      assert.strictEqual(stdout, `${args}\n`);
      assert.strictEqual(stderr, '');
      assert.strictEqual(err, null);
    })
  );

  // Ensure that arguments are successfully passed to print.
  child.exec(
    ...common.escapePOSIXShell`"${process.execPath}" --print "process.argv.slice(1).join(' ')" -- ${args}`,
    common.mustCall(function(err, stdout, stderr) {
      assert.strictEqual(stdout, `${args}\n`);
      assert.strictEqual(stderr, '');
      assert.strictEqual(err, null);
    })
  );

  // Ensure that arguments are successfully passed to a script.
  // The first argument after '--' should be interpreted as a script
  // filename.
  child.exec(...common.escapePOSIXShell`"${process.execPath}" -- "${__filename}" ${args}`,
             common.mustCall(function(err, stdout, stderr) {
               assert.strictEqual(stdout, `${args}\n`);
               assert.strictEqual(stderr, '');
               assert.strictEqual(err, null);
             }));
});

// ESModule eval tests


// Assert that "42\n" is written to stdout on module eval.
const execOptions = '--input-type module';
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" ${execOptions} --eval "console.log(42)"`,
  common.mustSucceed((stdout) => {
    assert.strictEqual(stdout, '42\n');
  }));

// Assert that "42\n" is written to stdout with print option.
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" ${execOptions} --print --eval "42"`,
  common.mustCall((err, stdout, stderr) => {
    assert.ok(err);
    assert.strictEqual(stdout, '');
    assert.ok(stderr.includes('--print cannot be used with ESM input'));
  }));

// Assert that error is written to stderr on invalid input.
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" ${execOptions} --eval "!!!!"`,
  common.mustCall((err, stdout, stderr) => {
    assert.ok(err);
    assert.strictEqual(stdout, '');
    assert.ok(stderr.indexOf('SyntaxError: Unexpected end of input') > 0);
  }));

// Assert that require is undefined in ESM support
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" ${execOptions} --eval "console.log(typeof require);"`,
  common.mustSucceed((stdout) => {
    assert.strictEqual(stdout, 'undefined\n');
  }));

// Assert that import.meta is defined in ESM
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" ${execOptions} --eval "console.log(typeof import.meta);"`,
  common.mustSucceed((stdout) => {
    assert.strictEqual(stdout, 'object\n');
  }));

{
// Assert that packages can be imported cwd-relative with --eval
  const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" ${execOptions} --eval`;
  const options = { ...opts, cwd: path.join(__dirname, '../..') };
  child.exec(
    `${cmd} "import './test/fixtures/es-modules/mjs-file.mjs'"`,
    options,
    common.mustSucceed((stdout) => {
      assert.strictEqual(stdout, '.mjs file\n');
    }));


  // Assert that packages can be dynamic imported initial cwd-relative with --eval
  child.exec(
    cmd + ' "process.chdir(\'..\');' +
          'import(\'./test/fixtures/es-modules/mjs-file.mjs\')"',
    options,
    common.mustSucceed((stdout) => {
      assert.strictEqual(stdout, '.mjs file\n');
    }));

  child.exec(
    cmd + ' "process.chdir(\'..\');' +
          'import(\'./test/fixtures/es-modules/mjs-file.mjs\')"',
    options,
    common.mustSucceed((stdout) => {
      assert.strictEqual(stdout, '.mjs file\n');
    }));
}

if (common.hasCrypto) {
  // Assert that calls to crypto utils work without require.
  child.exec(
    ...common.escapePOSIXShell`"${process.execPath}" -e "console.log(crypto.randomBytes(16).toString('hex'))"`,
    common.mustSucceed((stdout) => {
      assert.match(stdout, /[0-9a-f]{32}/i);
    }));
  child.exec(
    ...common.escapePOSIXShell`"${process.execPath}" -p "crypto.randomBytes(16).toString('hex')"`,
    common.mustSucceed((stdout) => {
      assert.match(stdout, /[0-9a-f]{32}/i);
    }));
}
// Assert that overriding crypto works.
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -p "crypto=Symbol('test')"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /Symbol\(test\)/i);
  }));
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -e "crypto = {};console.log('randomBytes', typeof crypto.randomBytes)"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /randomBytes\sundefined/);
  }));
// Assert that overriding crypto with a local variable works.
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -e "const crypto = {};console.log('randomBytes', typeof crypto.randomBytes)"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /randomBytes\sundefined/);
  }));
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -e "let crypto = {};console.log('randomBytes', typeof crypto.randomBytes)"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /randomBytes\sundefined/);
  }));
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -e "var crypto = {};console.log('randomBytes', typeof crypto.randomBytes)"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /randomBytes\sundefined/);
  }));
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -p "const crypto = {randomBytes:1};typeof crypto.randomBytes"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /^number/);
  }));
child.exec(
  ...common.escapePOSIXShell`"${process.execPath}" -p "let crypto = {randomBytes:1};typeof crypto.randomBytes"`,
  common.mustSucceed((stdout) => {
    assert.match(stdout, /^number/);
  }));

// Regression test for https://github.com/nodejs/node/issues/45336
child.execFile(process.execPath,
               ['-p',
                'Object.defineProperty(globalThis, "fs", { configurable: false });' +
                'fs === require("node:fs")'],
               common.mustSucceed((stdout) => {
                 assert.match(stdout, /^true/);
               }));
