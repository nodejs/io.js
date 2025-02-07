'use strict';

const { mustCall } = require('../common');
const { deepStrictEqual, ok, strictEqual } = require('assert');

const { channel } = require('diagnostics_channel');

const {
  hijackStdout,
  hijackStderr,
  restoreStdout,
  restoreStderr
} = require('../common/hijackstdio');

const stdoutMethods = [
  'log',
  'info',
  'debug',
];

const stderrMethods = [
  'warn',
  'error',
];

const methods = [
  ...stdoutMethods,
  ...stderrMethods,
];

const channels = {
  log: channel('console.log'),
  info: channel('console.info'),
  debug: channel('console.debug'),
  warn: channel('console.warn'),
  error: channel('console.error')
};

process.stdout.isTTY = false;
process.stderr.isTTY = false;

for (const method of methods) {
  let intercepted = false;
  let formatted = false;

  const isStdout = stdoutMethods.includes(method);
  const hijack = isStdout ? hijackStdout : hijackStderr;
  const restore = isStdout ? restoreStdout : restoreStderr;

  const foo = 'string';
  const bar = { key: /value/ };
  const baz = [ 1, 2, 3 ];

  channels[method].subscribe(mustCall((args) => {
    // Should not have been formatted yet.
    intercepted = true;
    ok(!formatted);

    // Should receive expected log message args.
    deepStrictEqual(args, [foo, bar, baz]);

    // Should be able to mutate message args and have it reflected in output.
    bar.added = true;
  }));

  hijack(mustCall((output) => {
    // Should have already been intercepted.
    formatted = true;
    ok(intercepted);

    // Should produce expected formatted output with mutated message args.
    strictEqual(output, 'string { key: /value/, added: true } [ 1, 2, 3 ]\n');
  }));

  console[method](foo, bar, baz);
  restore();
}
