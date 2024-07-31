import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import tmpdir from '../common/tmpdir.js';
import assert from 'node:assert';
import path from 'node:path';
import fs from 'node:fs/promises';
import { NodeInstance } from '../common/inspector-helper.js';


common.skipIfInspectorDisabled();
tmpdir.refresh();

{
  const child = new NodeInstance(
    ['--test', '--inspect-brk=0'],
    undefined,
    fixtures.path('test-runner/default-behavior/index.test.js')
  );

  let stdout = '';
  let stderr = '';
  child.on('stdout', (line) => stdout += line);
  child.on('stderr', (line) => stderr += line);

  const session = await child.connectInspectorSession();

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
    { method: 'Runtime.enable' },
    { method: 'Runtime.runIfWaitingForDebugger' }]);
  await session.send({ method: 'NodeRuntime.disable' });

  session.disconnect();
  assert.match(stderr,
               /Warning: Using the inspector with --test forces running at a concurrency of 1\. Use the inspectPort option to run with concurrency/);
}


{
  const args = ['--test', '--inspect=0', fixtures.path('test-runner/index.js')];
  const { stderr, stdout, code, signal } = await common.spawnPromisified(process.execPath, args);

  assert.match(stderr,
               /Warning: Using the inspector with --test forces running at a concurrency of 1\. Use the inspectPort option to run with concurrency/);
  assert.match(stdout, /not ok 1 - .+index\.js/);
  assert.strictEqual(code, 1);
  assert.strictEqual(signal, null);
}


{
  // File not found.
  const args = ['--test', '--inspect=0', 'a-random-file-that-does-not-exist.js'];
  const { stderr, stdout, code, signal } = await common.spawnPromisified(process.execPath, args);

  assert.strictEqual(stdout, '');
  assert.match(stderr, /^Could not find/);
  assert.doesNotMatch(stderr, /Warning: Using the inspector with --test forces running at a concurrency of 1\. Use the inspectPort option to run with concurrency/);
  assert.strictEqual(code, 1);
  assert.strictEqual(signal, null);
}


// Outputs coverage when event loop is drained, with no async logic.
{
  const coverageDirectory = tmpdir.resolve('coverage');
  async function getCoveredFiles() {
    const coverageFiles = await fs.readdir(coverageDirectory);
    const files = new Set();
    for (const coverageFile of coverageFiles) {
      const coverage = JSON.parse(await fs.readFile(path.join(coverageDirectory, coverageFile)));
      for (const { url } of coverage.result) {
        if (!url.startsWith('node:')) files.add(url);
      }
    }
    return files;
  }

  const { stderr, code, signal } = await common
          .spawnPromisified(process.execPath,
                            ['--test', fixtures.path('v8-coverage/basic.js')],
                            { env: { ...process.env, NODE_V8_COVERAGE: coverageDirectory } });

  assert.strictEqual(stderr, '');
  assert.strictEqual(code, 0);
  assert.strictEqual(signal, null);
  const files = await getCoveredFiles(coverageDirectory);
  assert.ok(files.has(fixtures.fileURL('v8-coverage/basic.js').href));
}
