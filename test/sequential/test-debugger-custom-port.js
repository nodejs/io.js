'use strict';
const common = require('../common');
const assert = require('node:assert');
const { test } = require('node:test');

common.skipIfInspectorDisabled();

const fixtures = require('../common/fixtures');
const startCLI = require('../common/debugger');

// Custom port.
const script = fixtures.path('debugger', 'three-lines.js');

const cli = startCLI([`--port=${common.PORT}`, script]);
test('should start the debugger on a custom port and print expected output', async () => {
  try {
    await cli.waitForInitialBreak();
    await cli.waitForPrompt();
    assert.match(cli.output, /debug>/, 'prints a prompt');
    assert.match(
      cli.output,
      new RegExp(`< Debugger listening on [^\n]*${common.PORT}`),
      'forwards child output');
  } finally {
    const code = await cli.quit();
    assert.strictEqual(code, 0);
  }
});
