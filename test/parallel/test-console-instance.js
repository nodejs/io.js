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
const common = require('../common');
const assert = require('assert');
const Stream = require('stream');
const requiredConsole = require('console');
const Console = requiredConsole.Console;

const out = new Stream();
const err = new Stream();

// Ensure the Console instance doesn't write to the
// process' "stdout" or "stderr" streams.
process.stdout.write = process.stderr.write = common.mustNotCall();

// Make sure that the "Console" function exists.
assert.strictEqual(typeof Console, 'function');

assert.strictEqual(requiredConsole, globalThis.console);
// Make sure the custom instanceof of Console works
assert.ok(globalThis.console instanceof Console);
assert.ok(!({} instanceof Console));

// Make sure that the Console constructor throws
// when not given a writable stream instance.
assert.throws(
  () => { new Console(); },
  {
    code: 'ERR_CONSOLE_WRITABLE_STREAM',
    name: 'TypeError',
    message: /stdout/
  }
);

// Console constructor should throw if stderr exists but is not writable.
assert.throws(
  () => {
    out.write = () => {};
    err.write = undefined;
    new Console(out, err);
  },
  {
    code: 'ERR_CONSOLE_WRITABLE_STREAM',
    name: 'TypeError',
    message: /stderr/
  }
);

out.write = err.write = (d) => {};

{
  const c = new Console(out, err);
  assert.ok(c instanceof Console);

  out.write = err.write = common.mustCall((d) => {
    assert.strictEqual(d, 'test\n');
  }, 2);

  c.log('test');
  c.error('test');

  out.write = common.mustCall((d) => {
    assert.strictEqual(d, '{ foo: 1 }\n');
  });

  c.dir({ foo: 1 });

  // Ensure that the console functions are bound to the console instance.
  let called = 0;
  out.write = common.mustCall((d) => {
    called++;
    assert.strictEqual(d, `${called} ${called - 1} [ 1, 2, 3 ]\n`);
  }, 3);

  [1, 2, 3].forEach(c.log);
}

// Test calling Console without the `new` keyword.
{
  const withoutNew = Console(out, err);
  assert.ok(withoutNew instanceof Console);
}

// Test extending Console
{
  class MyConsole extends Console {
    hello() {}
    // See if the methods on Console.prototype are overridable.
    log() { return 'overridden'; }
  }
  const myConsole = new MyConsole(process.stdout);
  assert.strictEqual(typeof myConsole.hello, 'function');
  assert.ok(myConsole instanceof Console);
  assert.strictEqual(myConsole.log(), 'overridden');

  const log = myConsole.log;
  assert.strictEqual(log(), 'overridden');
}

// Instance that does not ignore the stream errors.
{
  const c2 = new Console(out, err, false);

  out.write = () => { throw new Error('out'); };
  err.write = () => { throw new Error('err'); };

  assert.throws(() => c2.log('foo'), /^Error: out$/);
  assert.throws(() => c2.warn('foo'), /^Error: err$/);
  assert.throws(() => c2.dir('foo'), /^Error: out$/);
}

// Console constructor throws if inspectOptions is not an object.
[null, true, false, 'foo', 5, Symbol()].forEach((inspectOptions) => {
  assert.throws(
    () => {
      new Console({
        stdout: out,
        stderr: err,
        inspectOptions
      });
    },
    {
      message: 'The "options.inspectOptions" property must be of type object.' +
               common.invalidArgTypeHelper(inspectOptions),
      code: 'ERR_INVALID_ARG_TYPE'
    }
  );
});
