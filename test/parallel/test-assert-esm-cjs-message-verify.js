'use strict';

const { spawnPromisified } = require('../common');
const assert = require('node:assert');
const { describe, it } = require('node:test');

const fileImports = {
  commonjs: 'const assert = require("assert");',
  module: 'import assert from "assert";',
};

describe('ensure the assert.ok throwing similar error messages for esm and cjs files', () => {
  it('should return code 1 for each command', async () => {
    const errorsMessages = [];
    for (const [inputType, header] of Object.entries(fileImports)) {
      const { stderr, code } = await spawnPromisified(process.execPath, [
        '--input-type',
        inputType,
        '--eval',
        `${header}\nassert.ok(0 === 2);\n`,
      ]);
      assert.strictEqual(code, 1);
      // For each error message, filter the lines which will starts with AssertionError
      errorsMessages.push(
        stderr.split('\n').find((s) => s.startsWith('AssertionError'))
      );
    }
    assert.strictEqual(errorsMessages.length, 2);
    assert.deepStrictEqual(errorsMessages[0], errorsMessages[1]);
  });
});
