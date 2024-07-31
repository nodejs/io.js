'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const fixtures = require('../common/fixtures');
const startCLI = require('../common/debugger');

const assert = require('assert');

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// Profiles.
{
  const cli = startCLI(['--port=0', fixtures.path('debugger/empty.js')], [], {
    env: {
      ...process.env,
      // When this test is run with NODE_V8_COVERAGE, it clobbers the inspector
      // output, so override to disable coverage for the child process.
      NODE_V8_COVERAGE: undefined,
    }
  });

  function onFatal(error) {
    cli.quit();
    throw error;
  }

  try {
    (async () => {
      await cli.waitForInitialBreak();
      await cli.waitForPrompt();
      await cli.command('exec console.profile()');
      assert.match(cli.output, /undefined/);
      await cli.command('exec console.profileEnd()');
      await delay(250);
      assert.match(cli.output, /undefined/);
      assert.match(cli.output, /Captured new CPU profile\./);
      await cli.quit();
    })()
        .then(common.mustCall());
  } catch (error) {
    return onFatal(error);
  }

}
