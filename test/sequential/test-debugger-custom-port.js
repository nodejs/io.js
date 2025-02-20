'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const startCLI = require('../common/debugger');
const assert = require('node:assert');
const { test } = require('node:test');

// Skip if the inspector is disabled
common.skipIfInspectorDisabled();

test('should start CLI debugger with custom port and validate output', async (t) => {
  const script = fixtures.path('debugger', 'three-lines.js');
  const cli = startCLI([`--port=${common.PORT}`, script]);

  await t.test('validate debugger prompt and output', async () => {
    await cli.waitForInitialBreak();
    await cli.waitForPrompt();

    assert.match(cli.output, /debug>/, 'prints a prompt');
    assert.match(
      cli.output,
      new RegExp(`< Debugger listening on [^\n]*${common.PORT}`),
      'forwards child output'
    );
  });

  await t.test('ensure CLI exits with code 0', async () => {
    const code = await cli.quit();
    assert.strictEqual(code, 0);
  });
});
