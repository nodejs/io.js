import '../common/index.mjs';

import assert from 'node:assert';
import { execFile } from 'node:child_process';
import { promisify, debuglog } from 'node:util';

// This test verifies that the V8 hash seed is random
// and unique between child processes.

const execFilePromise = promisify(execFile);
const debug = debuglog('test');

const kRepetitions = 3;

const seeds = await Promise.all(Array.from({ length: kRepetitions }, generateSeed));
debug(`Seeds: ${seeds}`);
assert.strictEqual(new Set(seeds).size, seeds.length);
assert.strictEqual(seeds.length, kRepetitions);

async function generateSeed() {
  const output = await execFilePromise(process.execPath, [
    '--expose-internals',
    '--print',
    'require("internal/test/binding").internalBinding("v8").getHashSeed()',
  ]);
  return output.stdout.trim();
}
