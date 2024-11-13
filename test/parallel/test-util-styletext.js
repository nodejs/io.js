'use strict';

const common = require('../common');
const assert = require('node:assert');
const util = require('node:util');
const { WriteStream } = require('node:tty');

const styled = '\u001b[31mtest\u001b[39m';
const noChange = 'test';

[
  undefined,
  null,
  false,
  5n,
  5,
  Symbol(),
  () => {},
  {},
].forEach((invalidOption) => {
  assert.throws(() => {
    util.styleText(invalidOption, 'test');
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });
  assert.throws(() => {
    util.styleText('red', invalidOption);
  }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

assert.throws(() => {
  util.styleText('invalid', 'text');
}, {
  code: 'ERR_INVALID_ARG_VALUE',
});

assert.strictEqual(
  util.styleText('red', 'test', { validateStream: false }),
  '\u001b[31mtest\u001b[39m',
);

assert.strictEqual(
  util.styleText(['bold', 'red'], 'test', { validateStream: false }),
  '\u001b[1m\u001b[31mtest\u001b[39m\u001b[22m',
);

assert.strictEqual(
  util.styleText(['bold', 'red'], 'test', { validateStream: false }),
  util.styleText(
    'bold',
    util.styleText('red', 'test', { validateStream: false }),
    { validateStream: false },
  ),
);

assert.throws(() => {
  util.styleText(['invalid'], 'text');
}, {
  code: 'ERR_INVALID_ARG_VALUE',
});

assert.throws(() => {
  util.styleText('red', 'text', { stream: {} });
}, {
  code: 'ERR_INVALID_ARG_TYPE',
});

// does not throw
util.styleText('red', 'text', { stream: {}, validateStream: false });

assert.strictEqual(
  util.styleText('red', 'test', { validateStream: false }),
  styled,
);

const fd = common.getTTYfd();
if (fd !== -1) {
  const writeStream = new WriteStream(fd);

  const originalEnv = process.env;
  [
    { isTTY: true, env: {}, expected: styled },
    { isTTY: false, env: {}, expected: noChange },
    { isTTY: true, env: { NODE_DISABLE_COLORS: '1' }, expected: noChange },
    { isTTY: true, env: { NO_COLOR: '1' }, expected: noChange },
    { isTTY: true, env: { FORCE_COLOR: '1' }, expected: styled },
    { isTTY: true, env: { FORCE_COLOR: '1', NODE_DISABLE_COLORS: '1' }, expected: styled },
    { isTTY: false, env: { FORCE_COLOR: '1', NO_COLOR: '1', NODE_DISABLE_COLORS: '1' }, expected: styled },
    { isTTY: true, env: { FORCE_COLOR: '1', NO_COLOR: '1', NODE_DISABLE_COLORS: '1' }, expected: styled },
  ].forEach((testCase) => {
    writeStream.isTTY = testCase.isTTY;
    process.env = {
      ...process.env,
      ...testCase.env
    };
    const output = util.styleText('red', 'test', { stream: writeStream });
    assert.strictEqual(output, testCase.expected);
    process.env = originalEnv;
  });
} else {
  common.skip('Could not create TTY fd');
}
