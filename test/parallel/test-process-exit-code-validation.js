'use strict';

require('../common');

const invalids = [
  {
    code: '',
    expected: 1,
    pattern: "Received type string \\(''\\)$",
  },
  {
    code: '1 one',
    expected: 1,
    pattern: "Received type string \\('1 one'\\)$",
  },
  {
    code: 'two',
    expected: 1,
    pattern: "Received type string \\('two'\\)$",
  },
  {
    code: {},
    expected: 1,
    pattern: 'Received an instance of Object$',
  },
  {
    code: [],
    expected: 1,
    pattern: 'Received an instance of Array$',
  },
  {
    code: true,
    expected: 1,
    pattern: 'Received type boolean \\(true\\)$',
  },
  {
    code: false,
    expected: 1,
    pattern: 'Received type boolean \\(false\\)$',
  },
  {
    code: 2n,
    expected: 1,
    pattern: 'Received type bigint \\(2n\\)$',
  },
  {
    code: 2.1,
    expected: 1,
    pattern: 'Received 2.1$',
  },
  {
    code: Infinity,
    expected: 1,
    pattern: 'Received Infinity$',
  },
  {
    code: NaN,
    expected: 1,
    pattern: 'Received NaN$',
  },
];
const valids = [
  {
    code: 1,
    expected: 1,
  },
  {
    code: '2',
    expected: 2,
  },
  {
    code: undefined,
    expected: 0,
  },
  {
    code: null,
    expected: 0,
  },
  {
    code: 0,
    expected: 0,
  },
  {
    code: '0',
    expected: 0,
  },
];
const args = [...invalids, ...valids];

if (process.argv[2] === undefined) {
  const { spawnSync } = require('node:child_process');
  const { inspect, debuglog } = require('node:util');
  const { throws, strictEqual } = require('node:assert');

  const debug = debuglog('test');
  const node = process.execPath;
  const test = (index, useProcessExitCode) => {
    const { status: code } = spawnSync(node, [
      __filename,
      index,
      useProcessExitCode,
    ]);
    debug(`actual: ${code}, ${inspect(args[index])} ${!!useProcessExitCode}`);
    strictEqual(
      code,
      args[index].expected,
      `actual: ${code}, ${inspect(args[index])}`
    );
  };

  // Check process.exitCode
  for (const arg of invalids) {
    debug(`invalid code: ${inspect(arg.code)}`);
    throws(() => (process.exitCode = arg.code), new RegExp(arg.pattern));
  }
  for (const arg of valids) {
    debug(`valid code: ${inspect(arg.code)}`);
    process.exitCode = arg.code;
  }

  throws(() => {
    delete process.exitCode;
  }, /Cannot delete property 'exitCode' of #<process>/);
  process.exitCode = 0;

  // Check process.exit([code])
  for (const index of args.keys()) {
    test(index);
    test(index, true);
  }
} else {
  const index = parseInt(process.argv[2]);
  const useProcessExitCode = process.argv[3] !== 'undefined';
  if (Number.isNaN(index)) {
    return process.exit(100);
  }

  if (useProcessExitCode) {
    process.exitCode = args[index].code;
  } else {
    process.exit(args[index].code);
  }
}
