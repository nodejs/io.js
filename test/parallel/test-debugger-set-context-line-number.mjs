import { skipIfInspectorDisabled } from '../common/index.mjs';
skipIfInspectorDisabled();

import { path } from '../common/fixtures.mjs';
import startCLI from '../common/debugger.js';

import assert from 'assert';

const script = path('debugger', 'twenty-lines.js');
const cli = startCLI(['--port=0', script]);

function onFatal(error) {
  cli.quit();
  throw error;
}

function getLastLine(output) {
  const splitByLine = output.split(';');
  return splitByLine[splitByLine.length - 2];
}

// Stepping through breakpoints.
try {
  await cli.waitForInitialBreak();
  await cli.waitForPrompt();

  await cli.command('setContextLineNumber("1")');
  assert.ok(cli.output.includes('argument must be of type number. Received type string'));

  await cli.command('setContextLineNumber(0)');
  assert.ok(cli.output.includes('It must be >= 1. Received 0'));

  // Make sure the initial value is 2.
  await cli.stepCommand('n');
  assert.ok(getLastLine(cli.output).includes('4 x = 3'));

  await cli.command('setContextLineNumber(5)');
  await cli.stepCommand('n');
  assert.ok(getLastLine(cli.output).includes('8 x = 7'));

  await cli.command('setContextLineNumber(3)');
  await cli.stepCommand('n');
  assert.ok(getLastLine(cli.output).includes('7 x = 6'));
  await cli.command('list(3)');
  assert.ok(getLastLine(cli.output).includes('7 x = 6'));

  await cli.quit();
} catch (error) {
  onFatal(error);
}
